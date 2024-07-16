#include "ClimberCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Prototype1Character.h"

namespace MovementClimbingUtils
{
	bool IsDynamicGrabObject(const UPrimitiveComponent* GrabObject)
	{
		return (GrabObject && GrabObject->Mobility == EComponentMobility::Movable && GrabObject->IsSimulatingPhysics());
	}

	bool IsDynamicGrabObject(const FHandsContextData& HandData)
	{
		if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
		{
			return IsDynamicGrabObject(GrabObject);
		}

		return false;
	}

	// TODO: Add Substepping.
	void UpdateGrabbableObjectVelocity(const FHandsContextData& HandData, float DeltaSeconds, FVector& PrevHandObjectLocation, FVector& ArmFixVector, UClimberCharacterMovementComponent* ClimberCMC)
	{
		if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(ClimberCMC->GetCharacterOwner()))
		{
			if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
			{
				const FTransform GrabObjectLocalToWorld = GrabObject->GetComponentTransform();

				FVector ForceDirection = ClimberCharacter->Mesh1P->GetBoneLocation(HandData.ShoulderBoneName) - GrabObject->GetComponentLocation();
				float RopeLength = ForceDirection.Size();
				ForceDirection.Normalize();

				// Calculate stretch ratio (how much the rope is stretched beyond its relaxed length)
				float RopeRelaxedLength = ClimberCharacter->ArmsLengthUnits; // Initial length of the rope when not stretched
				float StretchRatio = (RopeLength - RopeRelaxedLength) / RopeRelaxedLength;



				// Get velocity of Object A (source object)
				FVector ObjectAVelocity = ClimberCMC->Velocity;
				float ObjectAMass = ClimberCMC->Mass;

				// Calculate force magnitude based on velocity and stretch
				float ForceMagnitude = ObjectAVelocity.Size() * ObjectAMass * StretchRatio;



				// Apply force to Object B (attached object)
				FVector ForceToApply = ForceMagnitude * ForceDirection * GrabObject->GetMass() * DeltaSeconds;		
				const FVector ForceToAddToObjectLocal = GrabObjectLocalToWorld.InverseTransformVector(ForceToApply);
				GrabObject->AddForceAtLocationLocal(ForceToAddToObjectLocal, HandData.LocalHandLocation, NAME_None);


				DrawDebugDirectionalArrow(GrabObject->GetWorld(), GrabObjectLocalToWorld.InverseTransformPosition(HandData.LocalHandLocation), GrabObjectLocalToWorld.InverseTransformPosition(HandData.LocalHandLocation) + ForceToApply, 2.f, FColor::Green, false, 0.025f, 0, 1.f);
				
				//
				//const FVector GrabObjectLocation = GrabObject->GetComponentLocation();
				//const FVector GrabObjectVelocity = GrabObjectLocation - PrevHandObjectLocation;
				//PrevHandObjectLocation = GrabObjectLocation;

				//// Calculate Force to add to grabbed object.
				//// We start with player gravity
				//FVector ForceToAddToObject;// = -ClimberCMC->Velocity; //* ClimberCMC->Mass;

				//// -ArmFixVector is the distance we want to move it, DeltaSeconds is the amount of time needed, and we have the initial velocity
				//// So we can calculate the actual force needed to move it towards ArmFixVector
				//// F = m . A
				//// S = u.dt + 1/2 . A . dt2
				//// We now need to calculate the acceleration since we know everything else.
				//// S = -ArmFixVector  // This is a distance, we should convert it to a force.
				//// u = GrabObjectVelocity
				//// dt = DeltaSeconds
				//// So to calculate acceleration, we have:
				//// A = (2.(S-u.dt)) / dt2
				//const FVector Acceleration = (2 * (-ArmFixVector - GrabObjectVelocity * DeltaSeconds)) / (DeltaSeconds * DeltaSeconds);

				//// Then the final Force will be
				//// F = m . (2.(S-u.dt)) / dt2
				//ForceToAddToObject += Acceleration;// *(GrabObject->GetMass() + ClimberCMC->Mass);
				//ForceToAddToObject = ForceToAddToObject * (1.f - FMath::Min(ClimberCMC->ForceDampingAppliedToPhysicsObjectMultiplier * DeltaSeconds, 1.f));

				//if (!ClimberCMC->UseImpulseOnPhysicsObjects)
				//{
				//	//ForceToAddToObject = ForceToAddToObject / DeltaSeconds; // Impulse to Force (if Impulse was multiplied by DeltaSeconds internally)
				//	const FVector ForceToAddToObjectLocal = GrabObjectLocalToWorld.InverseTransformVector(ForceToAddToObject);
				//	GrabObject->AddForceAtLocationLocal(ForceToAddToObjectLocal, HandData.LocalHandLocation, HandData.HitBoneName);
				//}
				//else
				//{
				//	const FVector ImpulseToAddToObject = ForceToAddToObject * DeltaSeconds; // Force to Impulse
				//	GrabObject->AddImpulseAtLocation(ImpulseToAddToObject, GrabObjectLocalToWorld.TransformPosition(HandData.LocalHandLocation), HandData.HitBoneName);
				//}

				//// Debugs
				//const FVector GrabObjectHandWorldLocation = GrabObjectLocalToWorld.TransformPosition(HandData.LocalHandLocation);
				//DrawDebugSphere(GrabObject->GetWorld(), GrabObjectHandWorldLocation, 25.f, 16, FColor::Silver);
				//DrawDebugDirectionalArrow(GrabObject->GetWorld(), GrabObjectHandWorldLocation, GrabObjectHandWorldLocation + ArmFixVector * 10.f, 2.f, FColor::Blue, false, 0.025f, 0, 1.f);
				//DrawDebugDirectionalArrow(GrabObject->GetWorld(), GrabObjectHandWorldLocation, GrabObjectHandWorldLocation + ForceToAddToObject, 1.5f, FColor::Emerald, false, 0.025, 0, 0.5f);

				//const float GrabObjectVelocitySize = GrabObjectVelocity.SizeSquared();
				//if (GrabObjectVelocitySize > 100.f)
				//{
				//	GEngine->AddOnScreenDebugMessage(21, 2.5f, FColor::Green,
				//		FString::Printf(TEXT("Letting go of grabbed object, since it moved! P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
				//			*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize));
				//	UE_LOG(LogTemp, Display, TEXT("Letting go of grabbed object, since it moved! P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
				//		*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize);

				//	/*if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(ClimberCMC->GetCharacterOwner()))
				//	{
				//		ClimberCharacter->ReleaseHand(HandData);
				//	}*/
				//}
				//else
				//{
				//	GEngine->AddOnScreenDebugMessage(20, 1.0f, FColor::Green,
				//		FString::Printf(TEXT("Still grabbing object. P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
				//			*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize));
				//	UE_LOG(LogTemp, Display, TEXT("Still grabbing object. P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
				//		*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize);
				//}
			}
		}
	}

	void UpdateVelocityWithGrabbableObjectVelocity(const FHandsContextData& HandData, FVector& OutAccelerationVector)
	{
		if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
		{
			if (MovementClimbingUtils::IsDynamicGrabObject(GrabObject))
			{
				OutAccelerationVector += GrabObject->ComponentVelocity;
			}
		}
	}
}

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

		const bool bWasJumping = ClimberCharacterOwner->bWasJumping || ClimberCharacterOwner->bPressedJump;
		if (MovementMode == MOVE_Falling)
		{
			if (!bWasJumping && !bWasClimbing)
			{
				ClimberCharacterOwner->StartCoyoteTime();
			}
			else
			{
				ClimberCharacterOwner->StopCoyoteTime();
			}
		}
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
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

		const bool bWasJumping = ClimberCharacterOwner->bWasJumping || ClimberCharacterOwner->bPressedJump;
		if (MovementMode == MOVE_Falling)
		{
			if (bWasJumping)
			{
				ClimberCharacterOwner->StopCoyoteTime();
			}
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

	float remainingTime = DeltaSeconds;
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations))
	{
		Iterations++;
		float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// We may need to impart the velocity of the object that we are holding onto here.
		FVector ClimbingAcceleration = FVector::ZeroVector;
		ClimbingAcceleration += GravityForce * FVector::DownVector; // Should we only apply gravity if we are holding on a stable surface?

		FVector HorizontalHandsControlAcceleration = Acceleration;

		FVector HandSlipAcceleration = FVector::ZeroVector;

		const float MaxDecel = GetMaxBrakingDeceleration();

		// Calculates velocity if not being controlled by root motion.
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			const float MaxSpeed = GetMaxSpeed();

			ClimbingAcceleration += HandMoveDir * MoveIntensityMultiplier;

			GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Green, FString::Printf(TEXT("Applying acceleration. Move dir (%f - %s). Acceleration: (%f - %s)"), HandMoveDir.Length(), *HandMoveDir.ToString(), Acceleration.Length(), *Acceleration.ToString()));

			FVector SnapArmsVector;
			if (ClimberCharacterOwner)
			{
				FVector BodyOffset = (Velocity + HandSlipVelocity) * timeTick;
				ComputeHandAccelerations(0, timeTick, ClimbingAcceleration, HorizontalHandsControlAcceleration, BodyOffset);
				if (ClimberCharacterOwner->LeftHandData.IsGrabbing)
				{
					// Calculate HandSlipAcceleration
					// Hardcoded for left hand for now, for debugging purposes. On the future, each hand should have their own slip end point.
					if (!HandSlipTarget.IsZero())
					{
						HandSlipAcceleration = HandSlipTarget - ClimberCharacterOwner->GetHandLocation(1);
					}
				}
				ComputeHandAccelerations(1, timeTick, ClimbingAcceleration, HorizontalHandsControlAcceleration, BodyOffset);
			}
			//CalcVelocity(DeltaSeconds, WallFriction, true, GetMaxBrakingDeceleration());
			HandMoveDir = FVector::ZeroVector;
			

			//const FVector TotalAcceleration = (HorizontalHandsControlAcceleration + ClimbingAcceleration).GetClampedToMaxSize(GetMaxAcceleration());
			//// Compute Velocity
			//{
			//	// Acceleration = TotalAcceleration for CalcVelocity(), but we restore it after using it.
			//	TGuardValue<FVector> RestoreAcceleration(Acceleration, TotalAcceleration);
			//	CalcVelocity(timeTick, WallFriction, true, MaxDecel);
			//}

			// Apply fluid friction
			Velocity = Velocity * (1.f - FMath::Min(WallFriction * timeTick, 1.f));
			HandSlipVelocity = HandSlipVelocity * (1.f - FMath::Min(WallFriction * timeTick, 1.f));

			// Apply input hand acceleration
			const bool bZeroHandAcceleration = HorizontalHandsControlAcceleration.IsZero();
			if (!bZeroHandAcceleration)
			{
				GEngine->AddOnScreenDebugMessage(30, timeTick, FColor::Green, TEXT("Apply Horizontal Hand Acceleration"));

				// Friction affects our ability to change direction.
				const FVector AccelDir = HorizontalHandsControlAcceleration.GetSafeNormal();
				const float VelSize = Velocity.Size();
				Velocity = Velocity - (Velocity - AccelDir * VelSize) * FMath::Min(timeTick * WallFriction, 1.f);

				const float NewMaxInputSpeed = IsExceedingMaxSpeed(MaxSpeed) ? Velocity.Size() : MaxSpeed;
				Velocity += HorizontalHandsControlAcceleration * timeTick;
				Velocity = Velocity.GetClampedToMaxSize(NewMaxInputSpeed);
			}

			// Apply climbing acceleration
			// Todo: Apply deceleration when HandMoveDir is zero and player is falling towards a overstretched state!
			const bool bZeroClimbingAcceleration = ClimbingAcceleration.IsZero();
			if (!bZeroClimbingAcceleration)
			{
				GEngine->AddOnScreenDebugMessage(31, timeTick, FColor::Green, TEXT("Apply Climbing Acceleration"));

				// Friction affects our ability to change direction.
				const FVector AccelDir = ClimbingAcceleration.GetSafeNormal();
				const float VelSize = Velocity.Size();
				Velocity = Velocity - (Velocity - AccelDir * VelSize) * FMath::Min(timeTick * WallFriction, 1.f);

				const float NewMaxInputSpeed = IsExceedingMaxSpeed(MaxSpeed) ? Velocity.Size() : MaxSpeed;
				Velocity += ClimbingAcceleration * timeTick;
				Velocity = Velocity.GetClampedToMaxSize(NewMaxInputSpeed);
			}

			const bool bZeroHandSlipAcceleration = HandSlipAcceleration.IsZero();
			if (!bZeroHandSlipAcceleration)
			{
				GEngine->AddOnScreenDebugMessage(32, timeTick, FColor::Green, TEXT("Apply Hand Slip Acceleration"));

				HandSlipVelocity += HandSlipAcceleration * timeTick;
			}

			{
				TGuardValue<FVector> RestoreVelocity(Velocity, HandSlipVelocity);
				const float NewMaxInputSlipSpeed = IsExceedingMaxSpeed(MaxSlipSpeed) ? HandSlipVelocity.Size() : MaxSlipSpeed;
				HandSlipVelocity = HandSlipVelocity.GetClampedToMaxSize(NewMaxInputSlipSpeed);
			}

			// Add HandSlipVelocity to Velocity
			const float NewMaxInputSpeed = IsExceedingMaxSpeed(MaxSpeed) ? Velocity.Size() : MaxSpeed;
			Velocity += HandSlipVelocity;
			Velocity = Velocity.GetClampedToMaxSize(NewMaxInputSpeed);

			// Debug
			//const FVector ActorLocation = UpdatedComponent->GetComponentLocation();
			//DrawDebugDirectionalArrow(GetWorld(), ActorLocation, ActorLocation + Acceleration, 1.0f, FColor::Emerald, false, 0.25f, 0, 0.5f);
		}

		ApplyRootMotionToVelocity(timeTick);

		bJustTeleported = false;

		////////////////////////////////////////////////////////////////////////////////////////
		// Again only hardcoded for left hand for now, add this to each hand later.
		FVector OldHandLocation = ClimberCharacterOwner->GetMutableHandData(1).GetHandLocation();
		const FVector HandDelta = HandSlipVelocity * timeTick;
		const FVector NewHandLocation = ClimberCharacterOwner->MoveHandGrabLocation(ClimberCharacterOwner->GetMutableHandData(1), HandDelta);
		if (HandSlipTarget.Equals(NewHandLocation))
		{
			HandSlipTarget = FVector::ZeroVector;
		}

		HandSlipVelocity = (NewHandLocation - OldHandLocation) / timeTick;
		////////////////////////////////////////////////////////////////////////////////////////

		FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FVector Delta = Velocity * timeTick;

		// The actual movement.
		// We can try to move the ClimbingCollider before, and check for collisions then.
		// Just set Sweep to false
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

		// If Hit.Time >= 1.f, didn't hit anything.
		if (Hit.Time < 1.f)
		{
			// Handles blocking/physics interaction.
			HandleImpact(Hit, timeTick, Delta);
			// Slides along collision. Specially important for climbing to feel good.
			SlideAlongSurface(Delta, (1.f - Hit.Time), Hit.Normal, Hit, true);
		}

		if (ClimberCharacterOwner)
		{
			if (!ClimberCharacterOwner->IsGrabbing())
			{
				SetMovementMode(EMovementMode::MOVE_Falling);
				//StartNewPhysics(timeTick, Iterations);
				//return;
			}
		}

		//// Velocity based on distance traveled.
		//if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		//{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;
		//}
	}
}

void UClimberCharacterMovementComponent::ComputeHandAccelerations(const int HandIndex, float DeltaSeconds, FVector& ClimbingAcceleration, FVector& HorizontalHandsControlAcceleration, const FVector& BodyOffset)
{
	FHandsContextData& HandData = ClimberCharacterOwner->GetMutableHandData(HandIndex);
	if (!HandData.IsGrabbing)
	{
		return;
	}

	// Calculate HandControlAcceleration
	HorizontalHandsControlAcceleration += GetHorizontalHandAcceleration(HorizontalHandsControlAcceleration, HandData);

	FVector RootDeltaFixHand = FVector::ZeroVector;
	FVector& PrevHandObjectLocation = (HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;
	bool IsArmOutstretched;
	FVector ArmVector = ClimberCharacterOwner->CalculateArmConstraint(HandData, DeltaSeconds, BodyOffset, IsArmOutstretched, RootDeltaFixHand);
	if (MovementClimbingUtils::IsDynamicGrabObject(HandData))
	{
		MovementClimbingUtils::UpdateGrabbableObjectVelocity(HandData, DeltaSeconds, PrevHandObjectLocation, RootDeltaFixHand, this);
	}
	else
	{
		//MovementClimbingUtils::UpdateVelocityWithGrabbableObjectVelocity(HandData, Acc);	
	}

	// Snapping Root back to a acceptable shoulder distance from the hand.
	// In A Difficult Game About Climbing, when the arms get overstretched when going down, the grip point is moved, as if trying to grasp. (Going up is almost impossible because of gravity)
	const bool bZeroRootDeltaFixHand = RootDeltaFixHand.IsZero();
	if (!bZeroRootDeltaFixHand)
	{
		const FVector AccelWithoutArmStretch = ClimbingAcceleration;
		ClimbingAcceleration += RootDeltaFixHand * ArmStretchIntensityMultiplier;

		GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Blue, FString::Printf(TEXT("Applying arm stretch (%f - %s). Acc before: (%f - %s) Acc after: (%f - %s)"),
			RootDeltaFixHand.Length(), *RootDeltaFixHand.ToString(), AccelWithoutArmStretch.Length(), *AccelWithoutArmStretch.ToString(), ClimbingAcceleration.Length(), *ClimbingAcceleration.ToString()));
	}
}
FVector UClimberCharacterMovementComponent::GetHorizontalHandAcceleration(const FVector& InitialAcceleration, const FHandsContextData& HandData)
{
	// No acceleration in X and Z 
	// Only Y which is relative to Hand, which is Forward and Backward a.k.a: only W/S works for controlling the arms horizontally
	const FVector HandRelativeAcceleration = ClimberCharacterOwner->RotateToHand(HandData, InitialAcceleration);
	FVector HandAcceleration = ClimberCharacterOwner->RotateToWorld(HandData, FVector(0.f, HandRelativeAcceleration.Y, 0.f));

	// bound acceleration, falling object has minimal ability to impact acceleration
	if (HandRelativeAcceleration.SizeSquared2D() > 0.f)
	{
		HandAcceleration = HandAcceleration.GetClampedToMaxSize(GetMaxAcceleration());
	}

	return HandAcceleration;
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

void UClimberCharacterMovementComponent::SetHandGrabbing(const FHandsContextData& HandData)
{
	FVector& PrevHandObjectLocation = (HandData.HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;

	if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
	{
		PrevHandObjectLocation = GrabObject->GetComponentLocation();

		if (MovementClimbingUtils::IsDynamicGrabObject(GrabObject))
		{
			GrabObject->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Ignore);
		}
	}
}

void UClimberCharacterMovementComponent::ReleaseHand(const FHandsContextData& HandData)
{
	FVector& PrevHandObjectLocation = (HandData.HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;

	PrevHandObjectLocation = FVector::ZeroVector;

	if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
	{
		if (MovementClimbingUtils::IsDynamicGrabObject(GrabObject))
		{
			GrabObject->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Block);
		}
	}
}
