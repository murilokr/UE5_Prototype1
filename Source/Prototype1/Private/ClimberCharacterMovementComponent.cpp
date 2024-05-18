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
		//PhysClimbing(DeltaSeconds, Iterations);
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

	// Calculates velocity if not being controlled by root motion.
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction;

		Acceleration += HandMoveDir;

		// Important!! - Updates velocity and acceleration with given friction and deceleration.
		CalcVelocity(DeltaSeconds, Friction, true, GetMaxBrakingDeceleration());
	}

	ApplyRootMotionToVelocity(DeltaSeconds);

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Delta = Velocity * DeltaSeconds;

	if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(CharacterOwner))
	{
		bool IsArmOutstretched;
		ClimberCharacter->GetArmVector(ClimberCharacter->LeftHandData, Delta, IsArmOutstretched);
	}

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

	// Velocity based on distance traveled.
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaSeconds;
	}
}

bool UClimberCharacterMovementComponent::IsClimbing() const
{
	return MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == ECustomMovementMode::CMOVE_Climbing;
}
