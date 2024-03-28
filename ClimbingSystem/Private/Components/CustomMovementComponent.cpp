// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "Components/CapsuleComponent.h"
#include "ClimbingSystem/DebugHelper.h"
#include "Kismet/KismetMathLibrary.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "MotionWarpingComponent.h"

void UCustomMovementComponent::BeginPlay() {
	Super::BeginPlay();
	owningPlayerAnimInstance = CharacterOwner->GetMesh()->GetAnimInstance();
	if(owningPlayerAnimInstance) {
		owningPlayerAnimInstance->OnMontageEnded.AddDynamic(this, &UCustomMovementComponent::onClimbMontageEnded);
		owningPlayerAnimInstance->OnMontageBlendingOut.AddDynamic(this, &UCustomMovementComponent::onClimbMontageEnded);
	}

	playerChar = Cast<AClimbingSystemCharacter>(CharacterOwner);
}

void UCustomMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UCustomMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) {
	if(IsClimbing()) {
		bOrientRotationToMovement = false;
		CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(48.f);

		OnEnterClimbStateDelegate.ExecuteIfBound();
	}

	if(PreviousMovementMode == MOVE_Custom && PreviousCustomMode == ECustomMovementMode::MOVE_Climb) {
		bOrientRotationToMovement = true;
		CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.f);

		const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
		const FRotator CleanStandRotation = FRotator(0.f, DirtyRotation.Yaw, 0.f);
		UpdatedComponent->SetRelativeRotation(CleanStandRotation);
		StopMovementImmediately();

		OnExitClimbStateDelegate.ExecuteIfBound();
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void UCustomMovementComponent::PhysCustom(float deltaTime, int32 Iterations) {
	if(IsClimbing()) {
		PhysClimb(deltaTime, Iterations);
	}

	Super::PhysCustom(deltaTime, Iterations);
}

float UCustomMovementComponent::GetMaxSpeed() const {
	if(IsClimbing()) {
		return MaxClimbSpeed;
	}

	return Super::GetMaxSpeed();
}

float UCustomMovementComponent::GetMaxAcceleration() const {
	if(IsClimbing()) {
		return MaxClimbAcceleration;
	}

	return Super::GetMaxAcceleration();
}

FVector UCustomMovementComponent::ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity, const FVector& CurrentVelocity) const {
	if(IsFalling() && owningPlayerAnimInstance && owningPlayerAnimInstance->IsAnyMontagePlaying()) {
		return RootMotionVelocity;
	}

	return Super::ConstrainAnimRootMotionVelocity(RootMotionVelocity, CurrentVelocity);
}

#pragma region ClimbTraces
TArray<FHitResult> UCustomMovementComponent::DoCapsuleTraceMultiByObject(const FVector& start, const FVector& end, bool bShowDebugShape, bool bDrawPersistentShapes) {
	TArray<FHitResult> outCapsuleTraceHitResults;

	auto debugTraceType = EDrawDebugTrace::None;
	if(bShowDebugShape) {
		debugTraceType = bDrawPersistentShapes ? EDrawDebugTrace::Persistent : EDrawDebugTrace::ForOneFrame;
	}
	UKismetSystemLibrary::CapsuleTraceMultiForObjects(
		this,
		start,
		end,
		ClimbCapsuleTraceRadius,
		ClimbCapsuleTraceHalfHeight,
		ClimableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		debugTraceType,
		outCapsuleTraceHitResults,
		false
	);

	return outCapsuleTraceHitResults;
}

FHitResult UCustomMovementComponent::DoLineTraceSingleByObject(const FVector& start, const FVector& end, bool bShowDebugShape, bool bDrawPersistentShapes, FColor color) {
	FHitResult outHit;

	auto debugTraceType = EDrawDebugTrace::None;
	if(bShowDebugShape) {
		debugTraceType = bDrawPersistentShapes ? EDrawDebugTrace::Persistent : EDrawDebugTrace::ForOneFrame;
	}
	UKismetSystemLibrary::LineTraceSingleForObjects(
		this,
		start,
		end,
		ClimableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		debugTraceType,
		outHit,
		false,
		color
	);
	return outHit;
}
#pragma endregion

#pragma region ClimbCore

void UCustomMovementComponent::ToggleClimbing(bool bEnableClimb) {
	if(bEnableClimb) {
		if(CanStartClimbing()) { 
			// enter climb state
			//Debug::Print(TEXT("Can start climbing"));
			playClimbMontage(IdleToClimbMontage);
		} else if(CanClimbDown()) {
			playClimbMontage(ClimbDownLedgeMontage);
		} else {
			TryStartVaulting();
		}
	}
	
	if(!bEnableClimb) {
		//Debug::Print(TEXT("cannot start climbing"));
		//stop climbing
		stopClimbing();
	}
}

bool UCustomMovementComponent::CanStartClimbing() {
	if(IsFalling()) { return false; }
	if(!TraceClimbableSurfaces()) { return false; }
	if(!TraceFromEyeHeight(100.f).bBlockingHit) { return false; }

	return true;
}

void UCustomMovementComponent::startClimbing() {
	SetMovementMode(MOVE_Custom, ECustomMovementMode::MOVE_Climb);
}

void UCustomMovementComponent::stopClimbing() {
	SetMovementMode(MOVE_Falling);
}

void UCustomMovementComponent::PhysClimb(float deltaTime, int32 Iterations) {
	if(deltaTime < MIN_TICK_TIME) {
		return;
	}
	
	TraceClimbableSurfaces();
	processClimbableSurfaceInfo();

	if(CheckShouldStopClimbing() || CheckHasReachedFloor()) {
		stopClimbing();
	}

	RestorePreAdditiveRootMotionVelocity();

	if(!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity()) {
		CalcVelocity(deltaTime, 0.f, true, MaxBreakClimbDeceleration);
	}

	ApplyRootMotionToVelocity(deltaTime);

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted, GetClimbRotation(deltaTime), true, Hit);

	if(Hit.Time < 1.f) {
		// adjust and try again
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	if(!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity()) {
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}

	snapMovementToSurface(deltaTime);

	if(IsClimbing() && LedgeDetected() && getUnrotatedClimbVelocity().Z > 10.f) {
		playClimbMontage(ClimbToTopMontage);
	}
}

void UCustomMovementComponent::processClimbableSurfaceInfo() {
	currentClimbableSurfaceLocation = FVector::ZeroVector;
	currentClimbableSurfaceNormal = FVector::ZeroVector;

	if(climableSurfacesTracedResults.IsEmpty()) { return; }

	for(const auto& hitResult : climableSurfacesTracedResults) {
		currentClimbableSurfaceLocation += hitResult.ImpactPoint;
		currentClimbableSurfaceNormal += hitResult.ImpactNormal;
	}

	currentClimbableSurfaceLocation /= climableSurfacesTracedResults.Num();
	currentClimbableSurfaceNormal = currentClimbableSurfaceNormal.GetSafeNormal();
}

bool UCustomMovementComponent::CheckShouldStopClimbing() {
	if(climableSurfacesTracedResults.IsEmpty()) { return true; }
	auto dotResult = FVector::DotProduct(currentClimbableSurfaceNormal, FVector::UpVector);
	auto degreeDiff = FMath::RadiansToDegrees(FMath::Acos(dotResult));

	if(degreeDiff <= 60.f) {
		return true;
	}

	//Debug::Print(TEXT("Degree Diff: ") + FString::SanitizeFloat(degreeDiff), FColor::Cyan, 1);
	return false;
}

bool UCustomMovementComponent::CheckHasReachedFloor() {
	auto downVector = -UpdatedComponent->GetUpVector();
	auto startOffset = downVector * 50.f;
	auto start = UpdatedComponent->GetComponentLocation() + startOffset;
	auto end = start + downVector;

	auto possibleFloorHits = DoCapsuleTraceMultiByObject(start, end);

	if(possibleFloorHits.IsEmpty()) { return false; }

	for(const auto& floorHit : possibleFloorHits) {
		if(FVector::Parallel(-floorHit.ImpactNormal, FVector::UpVector) &&
		   getUnrotatedClimbVelocity().Z < -10.f) {
			return true;
		}
	}
	return false;
}

bool UCustomMovementComponent::LedgeDetected() {
	auto hitResult = TraceFromEyeHeight(100.f, 50.f);
	if(!hitResult.bBlockingHit) { 
		auto offset = -UpdatedComponent->GetUpVector() * 100.f;
		auto startTrace = hitResult.TraceEnd;
		auto endTrace = startTrace + offset;

		return DoLineTraceSingleByObject(startTrace, endTrace).bBlockingHit;
	}
	return false;
}

bool UCustomMovementComponent::CanClimbDown() {
	if(IsFalling()) { return false; }

	//float HalfHeight, Radius;
	//CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(Radius, HalfHeight);

	//auto hit = TraceFromEyeHeight(50.f, 0.f);

	//auto start = hit.TraceEnd;
	//auto offsetToFeet = -UpdatedComponent->GetUpVector() * (HalfHeight - 15.f) * 2;
	//auto end = start + offsetToFeet;

	//auto heightStartHit = DoLineTraceSingleByObject(start, end, true);

	//auto climbDownStart = heightStartHit.TraceEnd;
	//auto climbDownOffset = -UpdatedComponent->GetUpVector() * HalfHeight;
	//auto climbDownEnd = climbDownStart + climbDownOffset;
	//
	//auto hitResult = DoLineTraceSingleByObject(climbDownStart, climbDownEnd, true);

	//if(!hitResult.bBlockingHit) {
	//	Debug::Print(TEXT("we can climb down"));
	//}
	//return false;

	auto componentLocation = UpdatedComponent->GetComponentLocation();
	auto componentForward = UpdatedComponent->GetForwardVector();
	auto downVec = -UpdatedComponent->GetUpVector();

	auto walkableSurfaceStart = componentLocation + componentForward * ClimbDownWalkableSurfaceForwardTraceOffset;
	auto walkableSurfaceEnd = walkableSurfaceStart + downVec * 100.f;

	auto walkableSurfaceHit = DoLineTraceSingleByObject(walkableSurfaceStart, walkableSurfaceEnd);


	auto ledgeStart = walkableSurfaceHit.TraceStart + componentForward * ClimbDownLedgeForwardTraceOffset;
	auto ledgeEnd = ledgeStart + downVec * 200.f;

	auto ledgeHit = DoLineTraceSingleByObject(ledgeStart, ledgeEnd);

	if(walkableSurfaceHit.bBlockingHit && !ledgeHit.bBlockingHit) {
		return true;
	}
	return false;
}

void UCustomMovementComponent::TryStartVaulting() {
	FVector vaultStartPos;
	FVector vaultLandPos;
	if(CanStartVaulting(vaultStartPos, vaultLandPos)) {
		SetMotionWarpTarget(FName("VaultStart"), vaultStartPos);
		SetMotionWarpTarget(FName("VaultEnd"), vaultLandPos);

		startClimbing();
		playClimbMontage(VaultMontage);
	}
}

bool UCustomMovementComponent::CanStartVaulting(FVector& outVaultStartPosition, FVector& outVaultLandPosition) {
	if(IsFalling()) { return false; }

	outVaultStartPosition = FVector::ZeroVector;
	outVaultLandPosition = FVector::ZeroVector;

	auto componentLocation = UpdatedComponent->GetComponentLocation();
	auto componentForward = UpdatedComponent->GetForwardVector();
	auto upVector = UpdatedComponent->GetUpVector();
	auto downVector = -UpdatedComponent->GetUpVector();
	auto forwardAmount = 150.f;

	for(auto i = 0; i < 5; ++i) {
		auto newForward = forwardAmount;
		if(i != 0) { newForward -= 20; }
		auto start = componentLocation + upVector * 100.f + componentForward * newForward * (i + 1);
		auto end = start + downVector * 100.f * (i + 1);

		auto hit = DoLineTraceSingleByObject(start, end);

		if(i == 0 && hit.bBlockingHit) {
			outVaultStartPosition = hit.ImpactPoint;
		}

		if(i == 4 && hit.bBlockingHit) {
			outVaultLandPosition = hit.ImpactPoint;
		}
	}

	if(outVaultStartPosition != FVector::ZeroVector &&
	   outVaultLandPosition != FVector::ZeroVector) {
		return true;
	}

	return false;
}

FQuat UCustomMovementComponent::GetClimbRotation(float deltaTime) {
	auto currentQuat = UpdatedComponent->GetComponentQuat();
	if(HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity()) {
		return currentQuat;
	}

	auto targetQuat = FRotationMatrix::MakeFromX(-currentClimbableSurfaceNormal).ToQuat();
	return FMath::QInterpTo(currentQuat, targetQuat, deltaTime, 5.f);
}

void UCustomMovementComponent::snapMovementToSurface(float deltaTime) {
	auto componentForward = UpdatedComponent->GetForwardVector();
	auto componentLocation = UpdatedComponent->GetComponentLocation();
	auto projectedCharToSurface = (currentClimbableSurfaceLocation - componentLocation).ProjectOnTo(componentForward);

	auto snapVector = -currentClimbableSurfaceNormal * projectedCharToSurface.Length();
	UpdatedComponent->MoveComponent(snapVector * deltaTime * MaxClimbSpeed, UpdatedComponent->GetComponentQuat(), true);
}

bool UCustomMovementComponent::IsClimbing() const {
	return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_Climb;
}

bool UCustomMovementComponent::TraceClimbableSurfaces() {
	auto startOffset = UpdatedComponent->GetForwardVector() * 30.f;
	auto start = UpdatedComponent->GetComponentLocation() + startOffset;

	auto end = start + UpdatedComponent->GetForwardVector();
	climableSurfacesTracedResults = DoCapsuleTraceMultiByObject(start, end);

	return !climableSurfacesTracedResults.IsEmpty();
}

FHitResult UCustomMovementComponent::TraceFromEyeHeight(float TraceDistance, float TraceStartOffset, bool bShowDebugShape, bool bDrawPersistentShapes) {
	auto componentLocation = UpdatedComponent->GetComponentLocation();
	auto eyeHeightOffset = UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartOffset);
	auto start = componentLocation + eyeHeightOffset;
	auto end = start + UpdatedComponent->GetForwardVector() * TraceDistance;

	return DoLineTraceSingleByObject(start, end, bShowDebugShape, bDrawPersistentShapes);
}

void UCustomMovementComponent::playClimbMontage(UAnimMontage* montageToPlay) {
	if(!montageToPlay) { return; }
	if(!owningPlayerAnimInstance) return;
	if(owningPlayerAnimInstance->IsAnyMontagePlaying()) { return; }

	owningPlayerAnimInstance->Montage_Play(montageToPlay);
}

void UCustomMovementComponent::onClimbMontageEnded(UAnimMontage* montage, bool interrupted) {
	if(montage == IdleToClimbMontage || montage == ClimbDownLedgeMontage) {
		startClimbing();
		StopMovementImmediately();
	}
	
	if(montage == ClimbToTopMontage || montage == VaultMontage) {
		SetMovementMode(MOVE_Walking);
	}
}

void UCustomMovementComponent::SetMotionWarpTarget(const FName& inWarpTargetName, const FVector& inTargetPos) {

	if(!playerChar) { return; }
	playerChar->GetMotionWarpingComponent()->AddOrUpdateWarpTargetFromLocation(inWarpTargetName, inTargetPos);
}

FVector UCustomMovementComponent::getUnrotatedClimbVelocity() const {
	return UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(), Velocity);
}

void UCustomMovementComponent::RequestHopping() {
	auto unrotatedLastInputVector = 
	UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(), GetLastInputVector());

	auto result = FVector::DotProduct(unrotatedLastInputVector.GetSafeNormal(), FVector::UpVector);

	if(result >= 0.9f) {
		HandleHopUp();
		return;
	}

	if(result <= -0.9) {
		HandleHopDown();
		return;
	}
}

void UCustomMovementComponent::HandleHopUp() {
	FVector hopUpTargetPoint;
	if(CheckCanHopUp(hopUpTargetPoint)) {
		SetMotionWarpTarget(FName("HopUp"), hopUpTargetPoint);
		playClimbMontage(HopUpMontage);
	}
}

void UCustomMovementComponent::HandleHopDown() {
	FVector hopDownTargetPoint;
	if(CheckCanHopDown(hopDownTargetPoint)) {
		SetMotionWarpTarget(FName("HopDown"), hopDownTargetPoint);
		playClimbMontage(HopDownMontage);
	}
}

bool UCustomMovementComponent::CheckCanHopUp(FVector& inTargetPos) {
	auto hit = TraceFromEyeHeight(100.f, -10.f);
	auto ledgeHit = TraceFromEyeHeight(100.f, 150.f);
	
	if(hit.bBlockingHit && ledgeHit.bBlockingHit) {
		inTargetPos = hit.ImpactPoint;
		return true;
	 }

	 return false;
}

bool UCustomMovementComponent::CheckCanHopDown(FVector& inTargetPos) {
	auto hit = TraceFromEyeHeight(100.f, -300.f, true, true);

	if(hit.bBlockingHit) {
		inTargetPos = hit.ImpactPoint;
		return true;
	}

	return false;
}

#pragma endregion
