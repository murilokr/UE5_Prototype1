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
		if (UPrimitiveComponent* GrabObject = HandData.HitResult.Component.Get())
		{
			return IsDynamicGrabObject(GrabObject);
		}

		return false;
	}

	void UpdateGrabbableObjectVelocity(const FHandsContextData& HandData, float DeltaSeconds, FVector& PrevHandObjectLocation, FVector& ArmFixVector, UClimberCharacterMovementComponent* ClimberCMC)
	{
		if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(ClimberCMC->GetCharacterOwner()))
		{
			UPrimitiveComponent* GrabObject = HandData.HitResult.Component.Get();

			const FVector GrabObjectLocation = GrabObject->GetComponentLocation();
			const FVector GrabObjectVelocity = GrabObjectLocation - PrevHandObjectLocation;
			PrevHandObjectLocation = GrabObjectLocation;
			const float GrabObjectVelocitySize = GrabObjectVelocity.SizeSquared();

			// Calculate Force to add to grabbed object.
			// We start with player gravity
			FVector ForceToAddToObject = ClimberCMC->Mass * FVector::DownVector;

			// -ArmFixVector is the distance we want to move it, DeltaSeconds is the amount of time needed, and we have the initial velocity
			// So we can calculate the actual force needed to move it towards ArmFixVector
			// F = m . A
			// S = u.dt + 1/2 . A . dt2
			// We now need to calculate the acceleration since we know everything else.
			// S = -ArmFixVector  // This is a distance, we should convert it to a force.
			// u = GrabObjectVelocity
			// dt = DeltaSeconds
			// So to calculate acceleration, we have:
			// -----> A = (2.(S-u.dt)) / dt2 <-----
			const FVector Acceleration = (2 * (-ArmFixVector - GrabObjectVelocity * DeltaSeconds)) / (DeltaSeconds * DeltaSeconds);

			// Then the final Force will be
			// F = m . (2.(S-u.dt)) / dt2
			ForceToAddToObject += Acceleration * ClimberCMC->Mass;//GrabObject->GetMass();
			ForceToAddToObject *= ClimberCMC->ForceAppliedToPhysicsObjectMultiplier; /// DeltaSeconds;

			// We still have a problem regarding the collision of our player and the object, we should do something.

			const FTransform GrabObjectLocalToWorld = GrabObject->GetSocketTransform(HandData.HitResult.BoneName);
			const FVector GrabObjectHandWorldLocation = GrabObjectLocalToWorld.TransformPosition(HandData.LocalHandLocation);
			DrawDebugSphere(GrabObject->GetWorld(), GrabObjectHandWorldLocation, 25.f, 16, FColor::Silver);

			const FVector ForceToAddToObjectLocal = GrabObjectLocalToWorld.InverseTransformVectorNoScale(ForceToAddToObject);

			// Calculate velocity before applying force.
			//GrabObject->AddVelocityChangeImpulseAtLocation(ForceToAddToObject, GrabObjectHandWorldLocation, HandData.HitResult.BoneName);
			GrabObject->AddForceAtLocationLocal(ForceToAddToObjectLocal, HandData.LocalHandLocation, HandData.HitResult.BoneName);
			//GrabObject->AddForce(ForceToAddToObject, HandData.HitResult.BoneName, false);

			if (GrabObjectVelocitySize > 100.f)
			{
				GEngine->AddOnScreenDebugMessage(21, 2.5f, FColor::Green,
					FString::Printf(TEXT("Letting go of grabbed object, since it moved! P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
						*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize));
				UE_LOG(LogTemp, Display, TEXT("Letting go of grabbed object, since it moved! P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
					*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize);

				/*if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(ClimberCMC->GetCharacterOwner()))
				{
					ClimberCharacter->ReleaseHand(HandData);
				}*/
			}
			else
			{
				GEngine->AddOnScreenDebugMessage(20, 1.0f, FColor::Green,
					FString::Printf(TEXT("Still grabbing object. P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
						*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize));
				UE_LOG(LogTemp, Display, TEXT("Still grabbing object. P.L: [%s] - C.L: [%s] = V [%s] (%f)"),
					*PrevHandObjectLocation.ToString(), *GrabObjectLocation.ToString(), *GrabObjectVelocity.ToString(), GrabObjectVelocitySize);
			}
		}
	}

	void UpdateVelocityWithGrabbableObjectVelocity(const FHandsContextData& HandData, FVector& OutAccelerationVector)
	{
		if (UPrimitiveComponent* GrabObject = HandData.HitResult.Component.Get())
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

	// We need to impart the velocity of the object that we are holding onto here.
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
				const FHandsContextData& LeftHandData = ClimberCharacterOwner->LeftHandData;
				ClimberCharacterOwner->GetArmVector(LeftHandData, BodyOffset, IsArmOutstretched, RootDeltaFixLeftHand);
				if (MovementClimbingUtils::IsDynamicGrabObject(LeftHandData))
				{
					MovementClimbingUtils::UpdateGrabbableObjectVelocity(LeftHandData, DeltaSeconds, PrevLeftHandObjectLocation, RootDeltaFixLeftHand, this);
				}
				else
				{
					//MovementClimbingUtils::UpdateVelocityWithGrabbableObjectVelocity(ClimberCharacterOwner->LeftHandData, Acc);	
				}
			}
			if (ClimberCharacterOwner->RightHandData.IsGrabbing)
			{
				const FHandsContextData& RightHandData = ClimberCharacterOwner->RightHandData;
				ClimberCharacterOwner->GetArmVector(RightHandData, BodyOffset, IsArmOutstretched, RootDeltaFixRightHand);
				if (MovementClimbingUtils::IsDynamicGrabObject(RightHandData))
				{
					MovementClimbingUtils::UpdateGrabbableObjectVelocity(RightHandData, DeltaSeconds, PrevRightHandObjectLocation, RootDeltaFixRightHand, this);
				}
				else
				{
					//MovementClimbingUtils::UpdateVelocityWithGrabbableObjectVelocity(ClimberCharacterOwner->LeftHandData, Acc);	
				}
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

void UClimberCharacterMovementComponent::SetHandGrabbing(const FHandsContextData& HandData)
{
	FVector& PrevHandObjectLocation = (HandData.HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;

	UPrimitiveComponent* GrabObject = HandData.HitResult.Component.Get();
	PrevHandObjectLocation = GrabObject->GetComponentLocation();
}

void UClimberCharacterMovementComponent::ReleaseHand(const FHandsContextData& HandData)
{
	FVector& PrevHandObjectLocation = (HandData.HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;

	PrevHandObjectLocation = FVector::ZeroVector;
}
