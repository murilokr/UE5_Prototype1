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
			ClimberCharacterOwner->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ClimberCharacterOwner->ClimbingCollider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}

		const bool bWasClimbing = PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Climbing;
		if (bWasClimbing)
		{
			ClimberCharacterOwner->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ClimberCharacterOwner->ClimbingCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			//StopMovementImmediately(); //Hmm
		}
	}
}

void UClimberCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	if (ClimberCharacterOwner)
	{
		if (ClimberCharacterOwner->IsGrabbing())
		{
			HandMoveDir = FVector::ZeroVector;
			SetMovementMode(EMovementMode::MOVE_Custom, ECustomMovementMode::CMOVE_Climbing);
		}
	}

	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
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

	Velocity += GravityForce * FVector::DownVector * DeltaSeconds;//-GetGravityDirection() * GetGravityZ();

	// Calculates velocity if not being controlled by root motion.
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Acceleration = HandMoveDir * MoveIntensityMultiplier;// *DeltaSeconds;
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

			// Snapping Root back to a acceptable shoulder distance from the hand.
			// In A Difficult Game About Climbing, when the arms get overstretched when going down, the grip point is moved, as if trying to grasp. (Going up is almost impossible because of gravity)
			SnapArmsVector = RootDeltaFixLeftHand + RootDeltaFixRightHand;

			if (!SnapArmsVector.IsZero())
			{
				const FVector AccelWithoutArmStretch = Acceleration;
				Acceleration += (SnapArmsVector * ArmStretchIntensityMultiplier);// / DeltaSeconds; //Snapping arms to their limit is a force. Since we want it to zero out with gravity when we have our arm stretched up high, (GravityForce + ArmStretchForce = 0)
				GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Blue, FString::Printf(TEXT("Applying arm stretch (%f - %s). Acc before: (%f - %s) Acc after: (%f - %s)"), 
					SnapArmsVector.Length(), *SnapArmsVector.ToString(), AccelWithoutArmStretch.Length(), *AccelWithoutArmStretch.ToString(), Acceleration.Length(), *Acceleration.ToString()));
				
				//bIsArmsStretchVelocityApplied = true;

				//if (!VelocityWithoutArmStretch.IsZero() && (Velocity | VelocityWithoutArmStretch) <= 0.0f)
				//{
				//	GEngine->AddOnScreenDebugMessage(2, 1.0f, FColor::Red, TEXT("Stopping velocity, since arm stretch made us change directions"));
				//    Velocity = FVector::ZeroVector;
				//	//StopMovementImmediately();
				//}
			}
			/*else if (bIsArmsStretchVelocityApplied)
			{
				bIsArmsStretchVelocityApplied = false;
				StopMovementImmediately();
			}*/
		}

		HandMoveDir = FVector::ZeroVector;
		CalcVelocity(DeltaSeconds, WallFriction, true, GetMaxBrakingDeceleration());
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
