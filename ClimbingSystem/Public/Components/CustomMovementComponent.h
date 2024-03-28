// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CustomMovementComponent.generated.h"

DECLARE_DELEGATE(FOnEnterClimbState)
DECLARE_DELEGATE(FOnExitClimbState)

class UAnimMontage;
class UAnimInstance;
class AClimbingSystemCharacter;

UENUM(BlueprintType)
namespace ECustomMovementMode {
	enum Type {
		MOVE_Climb UMETA(DisplayName = "Climb Mode")
	};
}
/**
 * 
 */
UCLASS()
class CLIMBINGSYSTEM_API UCustomMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	FOnEnterClimbState OnEnterClimbStateDelegate;
	FOnExitClimbState OnExitClimbStateDelegate;
	
protected:
#pragma region OverridenFunctions
	void BeginPlay() override;
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	void PhysCustom(float deltaTime, int32 Iterations) override;
	float GetMaxSpeed() const override;
	float GetMaxAcceleration() const override;
	FVector ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity, const FVector& CurrentVelocity) const override;
#pragma endregion

private:
#pragma region ClimbTraces
TArray<FHitResult> DoCapsuleTraceMultiByObject(const FVector& start, const FVector& end, bool bShowDebugShape = false, bool bDrawPersistentShapes = false);
FHitResult DoLineTraceSingleByObject(const FVector& start, const FVector& end, bool bShowDebugShape = false, bool bDrawPersistentShapes = false, FColor color = FColor::Red);
#pragma endregion

#pragma region ClimbCore
	
	bool TraceClimbableSurfaces();
	FHitResult TraceFromEyeHeight(float TraceDistance, float TraceStartOffset = 0.f, bool bShowDebugShape = false, bool bDrawPersistentShapes = false);
	bool CanStartClimbing();

	void startClimbing();
	void stopClimbing();
	void PhysClimb(float deltaTime, int32 Iterations);
	void processClimbableSurfaceInfo();

	bool CheckShouldStopClimbing();
	bool CheckHasReachedFloor();

	bool LedgeDetected();
	bool CanClimbDown();

	void TryStartVaulting();
	bool CanStartVaulting(FVector& outVaultStartPosition, FVector& outVaultLandPosition);

	FQuat GetClimbRotation(float deltaTime);
	void snapMovementToSurface(float deltaTime);

	void playClimbMontage(UAnimMontage* montageToPlay);

	UFUNCTION()
	void onClimbMontageEnded(UAnimMontage* montage, bool interrupted);
	void SetMotionWarpTarget(const FName& inWarpTargetName, const FVector& inTargetPos);

	void HandleHopUp();
	void HandleHopDown();
	bool CheckCanHopUp(FVector& inTargetPos);
	bool CheckCanHopDown(FVector& inTargetPos);

#pragma endregion

#pragma region ClimbCoreVariables
	TArray<FHitResult> climableSurfacesTracedResults;
	FVector currentClimbableSurfaceLocation;
	FVector currentClimbableSurfaceNormal;
	
	UPROPERTY()
	UAnimInstance* owningPlayerAnimInstance;

	UPROPERTY()
	AClimbingSystemCharacter* playerChar;

#pragma endregion

#pragma region ClimbVariables
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	TArray<TEnumAsByte<EObjectTypeQuery>> ClimableSurfaceTraceTypes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	float ClimbCapsuleTraceRadius = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	float ClimbCapsuleTraceHalfHeight = 72.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	float MaxBreakClimbDeceleration = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	float MaxClimbSpeed = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	float MaxClimbAcceleration = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	float ClimbDownWalkableSurfaceForwardTraceOffset = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	float ClimbDownLedgeForwardTraceOffset = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	UAnimMontage* IdleToClimbMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	UAnimMontage* ClimbToTopMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	UAnimMontage* ClimbDownLedgeMontage;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	UAnimMontage* VaultMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	UAnimMontage* HopUpMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Climbing", meta = (AllowPrivateAccess = "true"))
	UAnimMontage* HopDownMontage;
#pragma endregion

public:
	void ToggleClimbing(bool bEnableClimb);
	void RequestHopping();
	bool IsClimbing() const;
	FORCEINLINE FVector GetClimbableSurfaceNormal() const { return currentClimbableSurfaceNormal; }
	FVector getUnrotatedClimbVelocity() const;
};
