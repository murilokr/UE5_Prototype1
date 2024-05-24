#include "ClimberCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Prototype1Character.h"

void UClimberCharacterMovementComponent::InitializeComponent()
{
	ClimberCharacterOwner = Cast<APrototype1Character>(GetOwner());
}

void UClimberCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (ClimberCharacterOwner)
	{
		if (IsClimbing())
		{
			//ClimberCharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(45.0f);
		}

		const bool bWasClimbing = PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Climbing;
		if (bWasClimbing)
		{
			//ClimberCharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.0f);
		}
	}
}

void UClimberCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	if (ClimberCharacterOwner)
	{
		if (ClimberCharacterOwner->IsGrabbing())
		{
			HandMoveDir = FVector::ZeroVector;
			SetMovementMode(EMovementMode::MOVE_Custom, ECustomMovementMode::CMOVE_Climbing);
		}
	}
}

void UClimberCharacterMovementComponent::PhysCustom(float DeltaSeconds, int32 Iterations)
{
	if (CustomMovementMode == ECustomMovementMode::CMOVE_Climbing)
	{
		PhysClimbing(DeltaSeconds, Iterations);
	}

	Super::PhysCustom(DeltaSeconds, Iterations);
}

void UClimberCharacterMovementComponent::PhysClimbing(float DeltaSeconds, int32 Iterations)
{
	if (DeltaSeconds < MIN_TICK_TIME)
	{
		return;
	}

	RestorePreAdditiveRootMotionVelocity();

	if (ClimberCharacterOwner)
	{
		if (!ClimberCharacterOwner->IsGrabbing())
		{
			SetMovementMode(EMovementMode::MOVE_Falling);
			StartNewPhysics(DeltaSeconds, Iterations);
			return;
		}
	}

	FVector Acc = GravityForce * FVector::DownVector;

	// Calculates velocity if not being controlled by root motion.
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Acc += HandMoveDir * MoveIntensityMultiplier;
		
		GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Green, FString::Printf(TEXT("Applying acceleration. Move dir (%f - %s). Acceleration: (%f - %s)"), HandMoveDir.Length(), *HandMoveDir.ToString(), Acceleration.Length(), *Acceleration.ToString()));

		FVector SnapArmsVector;
		if (ClimberCharacterOwner)
		{
			bool IsArmOutstretched;
			FVector RootDeltaFixLeftHand = FVector::ZeroVector;
			FVector RootDeltaFixRightHand = FVector::ZeroVector;

			const FVector BodyOffset = Velocity * DeltaSeconds;

			if (ClimberCharacterOwner->LeftHandData.IsGrabbing)
			{
				ClimberCharacterOwner->GetArmVector(ClimberCharacterOwner->LeftHandData, BodyOffset, IsArmOutstretched, RootDeltaFixLeftHand);
			}
			if (ClimberCharacterOwner->RightHandData.IsGrabbing)
			{
				ClimberCharacterOwner->GetArmVector(ClimberCharacterOwner->RightHandData, BodyOffset, IsArmOutstretched, RootDeltaFixRightHand);
			}

			//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + ProjectedArmVector, 1.0f, (ArmOverstretched) ? FColor::Magenta : FColor::Emerald, false, -1.0f, 0, 1.0f);
			//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + ArmVector, 1.0f, (ArmOverstretched) ? FColor::Magenta : FColor::Emerald, false, -1.0f, 0, 0.5f);


			// Snapping Root back to a acceptable shoulder distance from the hand.
			// In A Difficult Game About Climbing, when the arms get overstretched when going down, the grip point is moved, as if trying to grasp. (Going up is almost impossible because of gravity)
			SnapArmsVector = RootDeltaFixLeftHand + RootDeltaFixRightHand;

			if (!SnapArmsVector.IsZero())
			{
				const FVector AccelWithoutArmStretch = Acceleration;
				Acc += SnapArmsVector * ArmStretchIntensityMultiplier;
				GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Blue, FString::Printf(TEXT("Applying arm stretch (%f - %s). Acc before: (%f - %s) Acc after: (%f - %s)"), 
					SnapArmsVector.Length(), *SnapArmsVector.ToString(), AccelWithoutArmStretch.Length(), *AccelWithoutArmStretch.ToString(), Acceleration.Length(), *Acceleration.ToString()));
			}
		}

		HandMoveDir = FVector::ZeroVector;
		//CalcVelocity(DeltaSeconds, WallFriction, true, GetMaxBrakingDeceleration());
		Velocity += Acc * DeltaSeconds; //* DampingScale;

		// Apply friction
		Velocity = Velocity * (1.f - FMath::Min(WallFriction * DeltaSeconds, 1.f));

		const FVector ActorLocation = UpdatedComponent->GetComponentLocation();
		DrawDebugDirectionalArrow(GetWorld(), ActorLocation, ActorLocation + Velocity, 1.0f, FColor::Emerald, false, 0.25f, 0, 0.5f);
	}

	ApplyRootMotionToVelocity(DeltaSeconds);

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Delta = Velocity * DeltaSeconds;

	// The actual movement.
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

	// If Hit.Time >= 1.f, didn't hit anything.
	if (Hit.Time < 1.f)
	{
		// Handles blocking/physics interaction.
		HandleImpact(Hit, DeltaSeconds, Delta);
		// Slides along collision. Specially important for climbing to feel good.
		SlideAlongSurface(Delta, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	if (ClimberCharacterOwner)
	{
		if (!ClimberCharacterOwner->IsGrabbing())
		{
			SetMovementMode(EMovementMode::MOVE_Falling);
			//StartNewPhysics(DeltaSeconds, Iterations);
			//return;
		}
	}

	//// Velocity based on distance traveled.
	//if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	//{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaSeconds;
	//}
}

bool UClimberCharacterMovementComponent::IsClimbing() const
{
	return MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == ECustomMovementMode::CMOVE_Climbing;
}

float UClimberCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
	if (MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == ECustomMovementMode::CMOVE_Climbing)
	{
		return BrakingDecelerationClimbing;
	}

	return Super::GetMaxBrakingDeceleration();
}
