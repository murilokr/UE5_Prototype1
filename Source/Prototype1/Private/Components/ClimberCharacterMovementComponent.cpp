#include "Components/ClimberCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Characters/Prototype1Character.h"
#include "Kismet/KismetMathLibrary.h"

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

	// DeltaTime is already substepped.
	void UpdateGrabbableObjectVelocity(const FHandsContextData& HandData, float DeltaTime, const FVector& PlayerAcceleration, FVector& PrevHandObjectLocation, FVector& PrevHandObjectVelocity, FVector& ArmFixVector, UClimberCharacterMovementComponent* ClimberCMC)
	{
		if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(ClimberCMC->GetCharacterOwner()))
		{
			if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
			{
				const FTransform GrabObjectLocalToWorld = GrabObject->GetComponentTransform();

				//FVector ForceDirection = ClimberCharacter->Mesh1P->GetBoneLocation(HandData.UpperArmBoneName) - GrabObject->GetComponentLocation();
				//float RopeLength = ForceDirection.Size();
				//ForceDirection.Normalize();

				//// Calculate stretch ratio (how much the rope is stretched beyond its relaxed length)
				//float RopeRelaxedLength = ClimberCharacter->ArmsLengthUnits; // Initial length of the rope when not stretched
				//float StretchRatio = (RopeLength - RopeRelaxedLength) / RopeRelaxedLength;



				//// Get velocity of Object A (source object)
				//FVector ObjectAVelocity = ClimberCMC->Velocity;
				//float ObjectAMass = ClimberCMC->Mass;

				//// Calculate force magnitude based on velocity and stretch
				//float ForceMagnitude = ObjectAVelocity.Size() * ObjectAMass * StretchRatio;



				//// Apply force to Object B (attached object)
				//FVector ForceToApply = ForceMagnitude * ForceDirection * GrabObject->GetMass() * DeltaTime;		
				//const FVector ForceToAddToObjectLocal = GrabObjectLocalToWorld.InverseTransformVector(ForceToApply);
				//GrabObject->AddForceAtLocationLocal(ForceToAddToObjectLocal, HandData.HandSurfaceLocalLocation, NAME_None);


				//DrawDebugDirectionalArrow(GrabObject->GetWorld(), GrabObjectLocalToWorld.InverseTransformPosition(HandData.HandSurfaceLocalLocation), GrabObjectLocalToWorld.InverseTransformPosition(HandData.HandSurfaceLocalLocation) + ForceToApply, 2.f, FColor::Green, false, 0.025f, 0, 1.f);
				
				GEngine->AddOnScreenDebugMessage(102, DeltaTime, FColor::Yellow, TEXT("UPDATE GRABBABLE OBJECT VELOCITY"));
				const FVector GrabObjectLocation = GrabObject->GetComponentLocation();
				FVector GrabObjectVelocity = GrabObjectLocation - PrevHandObjectLocation; //TODO: THIS ISNT ACTUALLY SPEED! WE NEED TO DIVIDE BY DeltaTime!!!!!!!!!!
				GrabObjectVelocity = PrevHandObjectVelocity;
				PrevHandObjectLocation = GrabObjectLocation;

				// Calculate Force to add to grabbed object.
				// We start with player gravity
				FVector ForceToAddToObject;// = -ClimberCMC->Velocity; //* ClimberCMC->Mass;
				ForceToAddToObject += PlayerAcceleration;// * ClimberCMC->Mass;

				// -ArmFixVector is the distance we want to move it, DeltaTime is the amount of time needed, and we have the initial velocity
				// So we can calculate the actual force needed to move it towards ArmFixVector
				// F = m . A
				// S = u.dt + 1/2 . A . dt2
				// We now need to calculate the acceleration since we know everything else.
				// S = -ArmFixVector  // This is a distance, we should convert it to a force.
				// u = GrabObjectVelocity
				// dt = DeltaTime
				// So to calculate acceleration, we have:
				// A = (2.(S-u.dt)) / dt2
				const FVector Acceleration = (2 * (-ArmFixVector - GrabObjectVelocity * DeltaTime)) / (DeltaTime * DeltaTime);

				// Then the final Force will be
				// F = m . (2.(S-u.dt)) / dt2
				ForceToAddToObject += Acceleration;// *(GrabObject->GetMass() + ClimberCMC->Mass);
				ForceToAddToObject *= GrabObject->GetMass();
				ForceToAddToObject = ForceToAddToObject * (1.f - FMath::Min(ClimberCMC->ForceDampingAppliedToPhysicsObjectMultiplier * DeltaTime, 1.f));

				if (!ClimberCMC->UseImpulseOnPhysicsObjects)
				{
					//ForceToAddToObject = ForceToAddToObject / DeltaTime; // Impulse to Force (if Impulse was multiplied by DeltaTime internally)
					const FVector ForceToAddToObjectLocal = GrabObjectLocalToWorld.InverseTransformVector(ForceToAddToObject);
					GrabObject->AddForceAtLocationLocal(ForceToAddToObjectLocal, HandData.HandSurfaceLocalLocation, HandData.HitBoneName);
				}
				else
				{
					const FVector ImpulseToAddToObject = ForceToAddToObject * DeltaTime; // Force to Impulse
					GrabObject->AddImpulseAtLocation(ImpulseToAddToObject, GrabObjectLocalToWorld.TransformPosition(HandData.HandSurfaceLocalLocation), HandData.HitBoneName);
				}

				// Debugs
				const FVector GrabObjectHandWorldLocation = GrabObjectLocalToWorld.TransformPosition(HandData.HandSurfaceLocalLocation);
				DrawDebugSphere(GrabObject->GetWorld(), GrabObjectHandWorldLocation, 25.f, 16, FColor::Silver);
				DrawDebugDirectionalArrow(GrabObject->GetWorld(), GrabObjectHandWorldLocation, GrabObjectHandWorldLocation + ArmFixVector * 10.f, 2.f, FColor::Blue, false, 0.025f, 0, 1.f);
				DrawDebugDirectionalArrow(GrabObject->GetWorld(), GrabObjectHandWorldLocation, GrabObjectHandWorldLocation + ForceToAddToObject, 1.5f, FColor::Emerald, false, 0.025, 0, 0.5f);

				const float GrabObjectVelocitySize = GrabObjectVelocity.SizeSquared();
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

				PrevHandObjectVelocity = (GrabObject->GetComponentLocation() - PrevHandObjectLocation) / DeltaTime;
			}
		}
	}

	FVector GetGrabbableObjectVelocity(const FHandsContextData& HandData)
	{
		if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
		{
			if (MovementClimbingUtils::IsDynamicGrabObject(GrabObject))
			{
				return GrabObject->ComponentVelocity;
			}
		}

		return FVector::ZeroVector;
	}

	void UpdateVelocityWithGrabbableObjectVelocity(const FHandsContextData& HandData, FVector& OutAccelerationVector)
	{
		OutAccelerationVector += GetGrabbableObjectVelocity(HandData);
	}
}

void UClimberCharacterMovementComponent::InitializeComponent()
{
	ClimberCharacterOwner = Cast<APrototype1Character>(GetOwner());
}

void UClimberCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsClimbing())
	{
		return;
	}
	
	if (HelperSpringIntensityFalloffTimer > 0.0f)
	{
		HelperSpringIntensityFalloffTimer = FMath::Max(0.0f, HelperSpringIntensityFalloffTimer - DeltaTime);

		const float FalloffTime = 1.0f - FMath::SmoothStep(0.0f, HelperSpringIntensityFalloffDuration, HelperSpringIntensityFalloffTimer);
		HelperSpringIntensity = HelperSpringIntensityFalloffCurve->GetFloatValue(FalloffTime) * HelperSpringMaxIntensity;

		const float dThickness = FMath::Lerp(0.75f, 0.2f, FalloffTime);
		const FLinearColor dColor = FLinearColor::LerpUsingHSV(FLinearColor::Blue, FColor::Red, FalloffTime);
		const FVector SpringDirection = HelperSpringAnchor - UpdatedComponent->GetComponentLocation();
		DrawDebugDirectionalArrow(GetWorld(), UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentLocation() + SpringDirection, 1.0f, dColor.ToFColor(false), false, 0.025f, 10, dThickness);
		//GEngine->AddOnScreenDebugMessage(44, DeltaTime, FColor::Yellow, FString::Printf(TEXT("HelperSpring - Intensity: %f - Timer: %f - dThickness: %f"), HelperSpringIntensity, HelperSpringIntensityFalloffTimer, dThickness));
	}
	else // for debug
	{
		const float FalloffTime = 1.0f - FMath::SmoothStep(0.0f, HelperSpringIntensityFalloffDuration, HelperSpringIntensityFalloffTimer);
		
		const float dThickness = FMath::Lerp(0.75f, 0.2f, FalloffTime);
		const FLinearColor dColor = FLinearColor::LerpUsingHSV(FColor::Blue, FColor::Red, FalloffTime);
		const FVector SpringDirection = HelperSpringAnchor - UpdatedComponent->GetComponentLocation();
		DrawDebugDirectionalArrow(GetWorld(), UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentLocation() + SpringDirection, 1.0f, dColor.ToFColor(false), false, 0.025f, 10, dThickness);
		//GEngine->AddOnScreenDebugMessage(44, 5.0f, FColor::Red, FString::Printf(TEXT("HelperSpringIntensity: %f"), HelperSpringIntensity));
	}
}

void UClimberCharacterMovementComponent::OnStartClimbing()
{
	//ClimberCharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(45.0f);
}

void UClimberCharacterMovementComponent::OnEndClimbing()
{
	//ClimberCharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.0f);
}

void UClimberCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (ClimberCharacterOwner)
	{
		if (IsClimbing())
		{
			OnStartClimbing();
		}

		const bool bWasClimbing = PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Climbing;
		if (bWasClimbing)
		{
			OnEndClimbing();
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
		else if (PreviousMovementMode == MOVE_Falling)
		{
			ClimberCharacterOwner->StopFallingToDeathTime();
		}
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void UClimberCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	if (ClimberCharacterOwner)
	{
		if (ClimberCharacterOwner->IsClimbing())
		{
			HandMoveDir = FVector::ZeroVector;
			SetMovementMode(EMovementMode::MOVE_Custom, ECustomMovementMode::CMOVE_Climbing);
		}

		const bool bWasJumping = ClimberCharacterOwner->bWasJumping || ClimberCharacterOwner->bPressedJump;
		if (MovementMode == MOVE_Falling)
		{
			GEngine->AddOnScreenDebugMessage(26, 3.5f, FColor::Yellow, FString::Printf(TEXT("Falling velocity: %f"), Velocity.Z));
			if (!ClimberCharacterOwner->IsAlmostFallingToDeath() && Velocity.Z < ClimberCharacterOwner->FallToDeathMinSpeed)
			{
				ClimberCharacterOwner->StartFallingToDeathTime();
			}
			
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

void UClimberCharacterMovementComponent::ForcePullOrPushHorizontalMovementTowardsGrabLocation(FVector& HorizontalHandsControlAcceleration)
{
	bool IsInFrontOfHands = false;
	
	const FVector CharacterForward = ClimberCharacterOwner->GetActorForwardVector();

	// We are now going to check if we are going in front of the hands, and if we are, stop forcing "forward" horizontal movement.
	float RightHandMeshDir = 1.f;
	if (ClimberCharacterOwner->IsHandClimbing(0))
	{
		const FVector MeshToRightHand = (ClimberCharacterOwner->GetHandLocation(0) - ClimberCharacterOwner->GetActorLocation()).GetSafeNormal();
		RightHandMeshDir = FVector::DotProduct(CharacterForward, MeshToRightHand);
		if (FMath::IsNearlyZero(RightHandMeshDir, 0.05f))
		{
			GEngine->AddOnScreenDebugMessage(43, 0.16, FColor::Red, FString::Printf(TEXT("RightHandMeshDir is nearly zero (perpendicular): %f - So we are not forcing horizontal move input"), RightHandMeshDir));
			return;
		}
		
		IsInFrontOfHands |= RightHandMeshDir <= 0;
	}
	
	float LeftHandMeshDir = 1.f;
	if (ClimberCharacterOwner->IsHandClimbing(1))
	{
		const FVector MeshToLeftHand = (ClimberCharacterOwner->GetHandLocation(1) - ClimberCharacterOwner->GetActorLocation()).GetSafeNormal();
		LeftHandMeshDir = FVector::DotProduct(CharacterForward, MeshToLeftHand);
		if (FMath::IsNearlyZero(LeftHandMeshDir, 0.05f))
		{
			GEngine->AddOnScreenDebugMessage(43, 0.16, FColor::Red, FString::Printf(TEXT("LeftHandMeshDir is nearly zero (perpendicular): %f - So we are not forcing horizontal move input"), LeftHandMeshDir));
			return;
		}
		
		IsInFrontOfHands |= LeftHandMeshDir <= 0;
	}
	
	GEngine->AddOnScreenDebugMessage(43, 0.16, FColor::Blue, FString::Printf(TEXT("RightHandMeshDir: %f - LeftHandMeshDir: %f - IsInFrontOfHands: %hhd"), RightHandMeshDir, LeftHandMeshDir, IsInFrontOfHands));

	const FVector ForceInputDirection = IsInFrontOfHands ? -CharacterForward : CharacterForward;
	const double IsPlayerInputOpposite = FVector::DotProduct(HorizontalHandsControlAcceleration, ForceInputDirection);
	if (IsPlayerInputOpposite > 0.85f || HorizontalHandsControlAcceleration.IsNearlyZero()) // There's no input being applied to the character
	{
		GEngine->AddOnScreenDebugMessage(136, 0.2f, FColor::Green, (IsInFrontOfHands) ? TEXT("Force pushing against wall") : TEXT("Force pulling towards wall"));
		HorizontalHandsControlAcceleration = ScaleInputAcceleration(ForceInputDirection);
	}
}

float UClimberCharacterMovementComponent::GetCoyoteGravityForce() const
{
	const float CoyoteStep = (1.0f - FMath::SmoothStep(0.0f, HelperSpringIntensityFalloffDuration,HelperSpringIntensityFalloffTimer));
	return FMath::Lerp(MinCoyoteGravityForce, GravityForce, CoyoteStep);
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
		if (!ClimberCharacterOwner->IsClimbing())
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
		FVector ClimbingAcceleration = GetCoyoteGravityForce() * FVector::DownVector; // Should we only apply gravity if we are holding on a stable surface?
		// GEngine->AddOnScreenDebugMessage(44, 5.0f, FColor::Yellow, FString::Printf(TEXT("GravityCoyoteForce: %f"), GetCoyoteGravityForce()));

		FVector HorizontalHandsControlAcceleration = Acceleration.GetSafeNormal();
		if (bForceCharacterPullingWhenNotMoving)
		{
			ForcePullOrPushHorizontalMovementTowardsGrabLocation(HorizontalHandsControlAcceleration);
		}

		

		// Calculates velocity if not being controlled by root motion.
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			if (ClimberCharacterOwner)
			{
				GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Green, FString::Printf(TEXT("Applying acceleration. Move dir (%f - %s). Acceleration: (%f - %s)"), HandMoveDir.Length(), *HandMoveDir.ToString(), Acceleration.Length(), *Acceleration.ToString()));

				// If HandMoveDir is nearly zero, we are going to add a temporary virtual arm spring, to try and keep the player in the same position for a few frames.
				// It actually needs to be processed over a few frames, as we need to keep updating this virtual arm spring target. When should it be active?
				// Should it be one "extra" arm that does the load for the two arms? Or as an extra spring for each arm, but with lower force?
				// The force of the spring should deteriorate over the period of this virtual arm spring existence, so that over time the force get lower until it releases. 
				
				ClimbingAcceleration += HandMoveDir * MoveIntensityMultiplier;
				HandMoveDir = FVector::ZeroVector;
			
				FVector BodyOffset = Velocity;
				BodyOffset += MovementClimbingUtils::GetGrabbableObjectVelocity(ClimberCharacterOwner->LeftHandData);
				BodyOffset += MovementClimbingUtils::GetGrabbableObjectVelocity(ClimberCharacterOwner->RightHandData);
				//BodyOffset += ClimbingAcceleration * timeTick;
				BodyOffset = BodyOffset * timeTick;

				ComputeHandAccelerations(0, timeTick, ClimbingAcceleration, HorizontalHandsControlAcceleration, BodyOffset);
				ComputeHandAccelerations(1, timeTick, ClimbingAcceleration, HorizontalHandsControlAcceleration, BodyOffset);

				// Force Full HandControlAcceleration.
				if (HorizontalHandsControlAcceleration.SizeSquared() > UE_SMALL_NUMBER)
				{
					HorizontalHandsControlAcceleration = HorizontalHandsControlAcceleration.GetSafeNormal() * HandsControlAcceleration;
				}
			}

			FVector HelperSpringAcceleration = FVector::ZeroVector;
			{
				// Applying ArmSpring force to the acceleration. This is used to bring the arm back from a slightly stretched state to a relaxed state.
				const FVector HelperSpringForceDirection = (HelperSpringAnchor - UpdatedComponent->GetComponentLocation()).GetSafeNormal();
				HelperSpringAcceleration += (HelperSpringForceDirection * HelperSpringIntensity) / Mass;
			}

			// Compute Velocity by applying ClimbingAcceleration and HorizontalHandsControlAcceleration.
			{
				AnalogInputModifier = FMath::Clamp<FVector::FReal>(ClimbingAcceleration.Size() / GetMaxAcceleration(), 0.f, 1.f);
				FVector FinalAcceleration = ClimbingAcceleration + HorizontalHandsControlAcceleration + HelperSpringAcceleration;

				const float MaxDecel = GetMaxBrakingDeceleration();

				// Acceleration = FinalAcceleration when inside CalcVelocity, and returns to it's original state afterwards.
				TGuardValue<FVector> RestoreAcceleration(Acceleration, FinalAcceleration);
				CalcVelocity(timeTick, WallFriction, true, MaxDecel);
			}

			

			// Add HandSlipVelocity to Velocity
			const float MaxSpeed = GetMaxSpeed();
			const float NewMaxInputSpeed = IsExceedingMaxSpeed(MaxSpeed) ? Velocity.Size() : MaxSpeed;
			//Velocity += HandSlipVelocity;
			Velocity = Velocity.GetClampedToMaxSize(NewMaxInputSpeed);

			// Debug
			//const FVector ActorLocation = UpdatedComponent->GetComponentLocation();
			//DrawDebugDirectionalArrow(GetWorld(), ActorLocation, ActorLocation + Acceleration, 1.0f, FColor::Emerald, false, 0.25f, 0, 0.5f);
		}

		ApplyRootMotionToVelocity(timeTick);

		bJustTeleported = false;

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
			if (!ClimberCharacterOwner->IsClimbing())
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

void UClimberCharacterMovementComponent::ComputeHandAccelerations(const int HandIndex, float DeltaTime, FVector& ClimbingAcceleration, FVector& HorizontalHandsControlAcceleration, const FVector& BodyOffset)
{
	FHandsContextData& HandData = ClimberCharacterOwner->GetMutableHandData(HandIndex);
	FHandsRuntimeMovementData& HandMovementData = (HandIndex == 0) ? RightHandRuntimeData : LeftHandRuntimeData;
	if (!HandData.IsInteractClimbing())
	{
		return;
	}

	// Calculate HandControlAcceleration
	const FVector HorizontalHandControlAcceleration = GetHorizontalHandAcceleration(HorizontalHandsControlAcceleration, HandData);
	HorizontalHandsControlAcceleration += HorizontalHandControlAcceleration;

	FVector RootDeltaFixHand = FVector::ZeroVector;
	FVector ArmSpringForce = FVector::ZeroVector;
	bool IsArmOutstretched;
	FVector ArmVector = ClimberCharacterOwner->CalculateArmConstraint(HandData, DeltaTime, BodyOffset, IsArmOutstretched, RootDeltaFixHand, ArmSpringForce);

	FVector HandAcceleration = FVector::ZeroVector;
	
	// Relaxed Arm Spring
	{
		const FVector AccelWithoutArmSpring = ClimbingAcceleration;
		
		// Applying ArmSpring force to the acceleration. This is used to bring the arm back from a slightly stretched state to a relaxed state.
		const FVector ArmSpringAcceleration = (ArmSpringForce * ArmSpringForceIntensity * GetCoyoteGravityForce()) / Mass;
		//ClimbingAcceleration += ((ArmSpringForce * ArmSpringForceIntensity) + -Velocity * ArmSpringDampening) / Mass;
		HandAcceleration += ArmSpringAcceleration;

		GEngine->AddOnScreenDebugMessage(2 + HandIndex, 1.0f, FColor::Cyan, FString::Printf(TEXT("Applying (%i) arm spring (%f - %s). ArmSpringForce (%f - %s). Acc before: (%f - %s) Acc after: (%f - %s)"),
			HandIndex, ArmSpringAcceleration.Length(), *ArmSpringAcceleration.ToString(), ArmSpringForce.Length(), *ArmSpringForce.ToString(), AccelWithoutArmSpring.Length(), *AccelWithoutArmSpring.ToString(), (ClimbingAcceleration+HandAcceleration).Length(), *(ClimbingAcceleration+HandAcceleration).ToString()));
	}
	
	// Snapping Root back to a acceptable shoulder distance from the hand.
	// In A Difficult Game About Climbing, when the arms get overstretched when going down, the grip point is moved, as if trying to grasp. (Going up is almost impossible because of gravity)
	if (!RootDeltaFixHand.IsZero())
	{
		const FVector AccelWithoutArmStretch = ClimbingAcceleration;
		
		// Applying Force per Unit Acceleration
		// No mass included, so this is used to "snap" the arm back into place once it's fully overstretched, this prevents the arm from going way further than intended
		HandAcceleration += RootDeltaFixHand * ArmStretchIntensityMultiplier;

		GEngine->AddOnScreenDebugMessage(HandIndex, 1.0f, FColor::Blue, FString::Printf(TEXT("Applying (%i) arm stretch (%f - %s). Acc before: (%f - %s) Acc after: (%f - %s)"),
			HandIndex, RootDeltaFixHand.Length(), *RootDeltaFixHand.ToString(), AccelWithoutArmStretch.Length(), *AccelWithoutArmStretch.ToString(), (ClimbingAcceleration+HandAcceleration).Length(), *(ClimbingAcceleration+HandAcceleration).ToString()));
	}

	// GEngine->AddOnScreenDebugMessage(37, DeltaSeconds, FColor::Green, FString::Printf(TEXT("HandAcceleration (single hand) Before: %f"), HandAcceleration.Length()));
	// // Should this clamp be separate from Climbing and Horizontal?? (TODO: THIS MIGHT CAUSE A BUG BUG BUG!!!! (All these bugs are tags for me to find in the future in case it really does occur))
	// HandAcceleration = HandAcceleration.GetClampedToMaxSize(GetMaxAcceleration());
	// GEngine->AddOnScreenDebugMessage(38, DeltaSeconds, FColor::Green, FString::Printf(TEXT("HandAcceleration (single hand) After: %f"), HandAcceleration.Length()));

	ClimbingAcceleration += HandAcceleration;

	// TODO: MIGHT GET REMOVED! 
	// Now we update any object that we are holding.
	if (MovementClimbingUtils::IsDynamicGrabObject(HandData))
	{
		FVector& PrevHandObjectLocation = (HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;
		FVector& PrevHandObjectVelocity = (HandIndex == 0) ? PrevRightHandObjectVelocity : PrevLeftHandObjectVelocity;
		// HorizontalHandControlAcceleration should be inverted in this case
		const FVector PlayerAcceleration = GravityForce * FVector::DownVector + HandAcceleration - HorizontalHandControlAcceleration;
		MovementClimbingUtils::UpdateGrabbableObjectVelocity(HandData, DeltaTime, PlayerAcceleration, PrevHandObjectLocation, PrevHandObjectVelocity, RootDeltaFixHand, this);
	}


	// HandSlip Calculation.
	// 
	// Calculate HandSlipAcceleration
	TGuardValue<float> RestoreMaxAccelerationAfter(MaxAcceleration, HandSlipMaxAcceleration);
	FVector HandSlipAcceleration = GetMaxAcceleration() * ConsumeSlipHandInputVector(HandData).GetClampedToMaxSize(1.0f);

	if (!HandMovementData.HandSlipTarget.IsZero())
	{
		// Actually override the HandSlipAcceleration to track the HandSlipTarget.
		const FVector HandSlipTargetDir = HandMovementData.HandSlipTarget - ClimberCharacterOwner->GetHandLocation(HandData);
		HandSlipAcceleration = GetMaxAcceleration() * HandSlipTargetDir.GetClampedToMaxSize(1.0f);
	}

	// Apply friction
	HandMovementData.HandSlipVelocity = HandMovementData.HandSlipVelocity * (1.f - FMath::Min(WallFriction * DeltaTime, 1.f));

	const bool bZeroHandSlipAcceleration = HandSlipAcceleration.IsZero();
	if (!bZeroHandSlipAcceleration)
	{
		GEngine->AddOnScreenDebugMessage(32, DeltaTime, FColor::Green, TEXT("Apply Hand Slip Acceleration"));

		HandMovementData.HandSlipVelocity += HandSlipAcceleration * DeltaTime;
	}

	{
		TGuardValue<FVector> RestoreVelocity(Velocity, HandMovementData.HandSlipVelocity);
		const float NewMaxInputSlipSpeed = IsExceedingMaxSpeed(MaxSlipSpeed) ? HandMovementData.HandSlipVelocity.Size() : MaxSlipSpeed;
		HandMovementData.HandSlipVelocity = HandMovementData.HandSlipVelocity.GetClampedToMaxSize(NewMaxInputSlipSpeed);
	}

	// Now actually and physically move the hand!
	FVector OldHandLocation = HandData.GetHandLocation();
	const FVector HandDelta = HandMovementData.HandSlipVelocity * DeltaTime;
	const FVector NewHandLocation = ClimberCharacterOwner->MoveHandGrabLocation(HandData, HandDelta);
	if (HandMovementData.HandSlipTarget.Equals(NewHandLocation))
	{
		HandMovementData.HandSlipTarget = FVector::ZeroVector;
	}

	HandMovementData.HandSlipVelocity = (NewHandLocation - OldHandLocation) / DeltaTime;	
}

FVector UClimberCharacterMovementComponent::GetHorizontalHandAcceleration(const FVector& InitialAcceleration, const FHandsContextData& HandData)
{
	const FRotator AccelRotator = FRotationMatrix::MakeFromX(Acceleration).Rotator();
	//DrawDebugCoordinateSystem(GetWorld(), HandData.GetHandLocation() + FVector::UpVector * 10.f, AccelRotator, 15.0f, false, 0.15f, 0, 1.0f);

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

void UClimberCharacterMovementComponent::SetHandClimbing(const FHandsContextData& HandData)
{
	UpdateHelperSpring(HelperSpringIntensityIdleFalloffDuration);
	
	FVector& PrevHandObjectLocation = (HandData.HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;
	FVector& PrevHandObjectVelocity = (HandData.HandIndex == 0) ? PrevRightHandObjectVelocity : PrevLeftHandObjectVelocity;

	if (UPrimitiveComponent* GrabObject = HandData.HitComponent)
	{
		PrevHandObjectLocation = GrabObject->GetComponentLocation();
		PrevHandObjectVelocity = FVector::ZeroVector;
	}
}

void UClimberCharacterMovementComponent::ReleaseHand(const FHandsContextData& HandData)
{
	FVector& PrevHandObjectLocation = (HandData.HandIndex == 0) ? PrevRightHandObjectLocation : PrevLeftHandObjectLocation;
	FVector& PrevHandObjectVelocity = (HandData.HandIndex == 0) ? PrevRightHandObjectVelocity : PrevLeftHandObjectVelocity;

	PrevHandObjectLocation = FVector::ZeroVector;
	PrevHandObjectVelocity = FVector::ZeroVector;

}

void UClimberCharacterMovementComponent::UpdateHelperSpring(float SpringIntensityFalloffCustomDuration)
{
	HelperSpringAnchor = UpdatedComponent->GetComponentLocation();
	HelperSpringIntensity = HelperSpringMaxIntensity;
	
	HelperSpringIntensityFalloffDuration = SpringIntensityFalloffCustomDuration;
	if (HelperSpringIntensityFalloffDuration == -1)
	{
		// Default falloff duration is the move falloff duration.
		HelperSpringIntensityFalloffDuration = HelperSpringIntensityMoveFalloffDuration;
	}
	
	HelperSpringIntensityFalloffTimer = HelperSpringIntensityFalloffDuration;
}

FHandsRuntimeMovementData& UClimberCharacterMovementComponent::GetMutableHandMovementData(int HandIndex)
{
	FHandsRuntimeMovementData& HandMovementData = (HandIndex == 0) ? RightHandRuntimeData : LeftHandRuntimeData;
	return HandMovementData;
}

FVector UClimberCharacterMovementComponent::ConsumeSlipHandInputVector(const FHandsContextData& HandData)
{
	FHandsRuntimeMovementData& HandMovementData = GetMutableHandMovementData(HandData.HandIndex);

	HandMovementData.LastHandSlipAccelerationInput = HandMovementData.HandSlipAccelerationInput;
	HandMovementData.HandSlipAccelerationInput = FVector::ZeroVector;
	return HandMovementData.LastHandSlipAccelerationInput;
}

void UClimberCharacterMovementComponent::AddHandSlipAccelerationInput(const FHandsContextData& HandData, const FVector& InputVector)
{
	FHandsRuntimeMovementData& HandMovementData = GetMutableHandMovementData(HandData.HandIndex);
	HandMovementData.HandSlipAccelerationInput += InputVector;
}

void UClimberCharacterMovementComponent::AddHandSlipTarget(const FHandsContextData& HandData, const FVector& SlipTarget)
{
	if (ClimberCharacterOwner)
	{
		FHandsRuntimeMovementData& HandMovementData = GetMutableHandMovementData(HandData.HandIndex);
		const FVector ValidatedSlipTarget = ClimberCharacterOwner->ValidateHandSlipTarget(HandData, SlipTarget);
		HandMovementData.HandSlipTarget = ValidatedSlipTarget;
	}
}

void UClimberCharacterMovementComponent::SetHandSlipVelocity(const FHandsContextData& HandData, const FVector& SlipVelocity, bool bOverrideVelocity)
{
	FHandsRuntimeMovementData& HandMovementData = GetMutableHandMovementData(HandData.HandIndex);

	if (bOverrideVelocity)
	{
		HandMovementData.HandSlipVelocity = SlipVelocity;
	}
	else
	{
		// Add to the current HandSlipVelocity
		HandMovementData.HandSlipVelocity += SlipVelocity;
	}
}
