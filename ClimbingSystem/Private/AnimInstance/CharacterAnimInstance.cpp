// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/CharacterAnimInstance.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "Components/CustomMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

void UCharacterAnimInstance::NativeInitializeAnimation() {
	Super::NativeInitializeAnimation();

	climbingSystemChar = Cast<AClimbingSystemCharacter>(TryGetPawnOwner());

	if(climbingSystemChar) {
		customMovementComp = climbingSystemChar->GetCustomMovementComponent();
	}
}

void UCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds) {
	Super::NativeUpdateAnimation(DeltaSeconds);

	if(!climbingSystemChar || !customMovementComp) { return; }

	GetGroundSpeed();
	GetAirSpeed();
	GetIsFalling();
	GetShouldMove();
	GetIsClimbing();
	GetClimbVelocity();
}

void UCharacterAnimInstance::GetGroundSpeed() {
	groundSpeed = UKismetMathLibrary::VSizeXY(climbingSystemChar->GetVelocity());
}

void UCharacterAnimInstance::GetAirSpeed() {
	airSpeed = climbingSystemChar->GetVelocity().Z;
}

void UCharacterAnimInstance::GetShouldMove() {
	bShouldMove = customMovementComp->GetCurrentAcceleration().Size() > 0 &&
					groundSpeed > 5.f &&
					!bIsFalling;
}

void UCharacterAnimInstance::GetIsFalling() {
	bIsFalling = customMovementComp->IsFalling();
}

void UCharacterAnimInstance::GetIsClimbing() {
	bIsClimbing = customMovementComp->IsClimbing();
}

void UCharacterAnimInstance::GetClimbVelocity() {
	climbVelocity = customMovementComp->getUnrotatedClimbVelocity();
}
