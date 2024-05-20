#include "ClimberCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Prototype1Character.h"

void UClimberCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(CharacterOwner))
	{
		if (IsClimbing())
		{
			ClimberCharacter->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ClimberCharacter->ClimbingCollider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}

		const bool bWasClimbing = PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Climbing;
		if (bWasClimbing)
		{
			ClimberCharacter->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ClimberCharacter->ClimbingCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			//StopMovementImmediately(); //Hmm
		}
	}
}

void UClimberCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{

	if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(CharacterOwner))
	{
		if (ClimberCharacter->IsGrabbing())
		{
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

	const FVector Gravity = -GetGravityDirection() * GetGravityZ();
	Velocity = NewFallVelocity(/*Velocity*/FVector::ZeroVector, Gravity, DeltaSeconds);

	Velocity += HandMoveDir * MoveIntensityMultiplier;// *DeltaSeconds;
	HandMoveDir = FVector::ZeroVector;

	FVector SnapArmsVector;
	if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(CharacterOwner))
	{
		bool IsArmOutstretched;
		FVector RootDeltaFixLeftHand = FVector::ZeroVector;
		FVector RootDeltaFixRightHand = FVector::ZeroVector;

		const FVector BodyOffset = Velocity * DeltaSeconds;
		
		if (ClimberCharacter->LeftHandData.IsGrabbing)
		{
			ClimberCharacter->GetArmVector(ClimberCharacter->LeftHandData, BodyOffset, IsArmOutstretched, RootDeltaFixLeftHand);
		}
		if (ClimberCharacter->RightHandData.IsGrabbing)
		{
			ClimberCharacter->GetArmVector(ClimberCharacter->RightHandData, BodyOffset, IsArmOutstretched, RootDeltaFixRightHand);
		}

		// Snapping Root back to a acceptable shoulder distance from the hand.
		// In A Difficult Game About Climbing, when the arms get overstretched when going down, the grip point is moved, as if trying to grasp. (Going up is almost impossible because of gravity)
		SnapArmsVector = RootDeltaFixLeftHand + RootDeltaFixRightHand;
	}
	
	if (!SnapArmsVector.IsZero())
	{
		const FVector VelocityWithoutArmStretch = Velocity;
		Velocity += SnapArmsVector * ArmStretchIntensityMultiplier; //Snapping arms to their limit is a force. Since we want it to zero out with gravity when we have our arm stretched up high, (GravityForce + ArmStretchForce = 0)
		GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Blue, TEXT("Applying arm stretch"));
		bIsArmsStretchVelocityApplied = true;
		
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

	ApplyVelocityBraking(DeltaSeconds, GetPhysicsVolume()->FluidFriction, GetMaxBrakingDeceleration());


	// Calculates velocity if not being controlled by root motion.
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		const float Friction = GetPhysicsVolume()->FluidFriction;

		//Velocity = FVector::ZeroVector;
		//CalcVelocity(DeltaSeconds, Friction, true, GetMaxBrakingDeceleration());
	}

	

	ApplyRootMotionToVelocity(DeltaSeconds);

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Delta = Velocity * DeltaSeconds;

	// The actual movement.
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(CharacterOwner))
	{
		if (!ClimberCharacter->IsGrabbing())
		{
			SetMovementMode(EMovementMode::MOVE_Falling);
			StartNewPhysics(DeltaSeconds, Iterations);
			return;
		}
	}

	// If Hit.Time >= 1.f, didn't hit anything.
	if (Hit.Time < 1.f)
	{
		/* Ignore - We are not interested in Stepping Up. */
		const FVector GravDir = FVector(0.f, 0.f, -1.f);
		const FVector VelDir = Velocity.GetSafeNormal();
		const float UpDown = GravDir | VelDir;

		bool bSteppedUp = false;
		if ((FMath::Abs(Hit.ImpactNormal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit))
		{
			float stepZ = UpdatedComponent->GetComponentLocation().Z;
			bSteppedUp = StepUp(GravDir, Delta * (1.f - Hit.Time), Hit);
			if (bSteppedUp)
			{
				OldLocation.Z = UpdatedComponent->GetComponentLocation().Z + (OldLocation.Z - stepZ);
			}
		}
		/* Ignore */


		if (!bSteppedUp)
		{
			// Handles blocking/physics interaction.
			HandleImpact(Hit, DeltaSeconds, Delta);

			// Slides along collision. Specially important for climbing to feel good.
			SlideAlongSurface(Delta, (1.f - Hit.Time), Hit.Normal, Hit, true);
		}
	}

	//// Velocity based on distance traveled.
	//if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	//{
	//	Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaSeconds;
	//}
}

bool UClimberCharacterMovementComponent::IsClimbing() const
{
	return MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == ECustomMovementMode::CMOVE_Climbing;
}
