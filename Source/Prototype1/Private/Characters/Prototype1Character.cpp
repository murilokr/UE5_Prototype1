// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/Prototype1Character.h"
#include "Prototype1Projectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/ClimberCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InteractableActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include <Kismet/KismetSystemLibrary.h>
#include <Kismet/KismetMathLibrary.h>

#include "Managers/ClimberCameraManager.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include <Misc/Debug/MDebugHelper.h>

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

#define M_SAFE_WORLD_PERPENDICULAR_DOT 0.2

namespace MUtils
{
	template<typename TShapeElem>
	bool GetPrimitiveRotation(FKShapeElem* Primitive, FQuat& OutRotation)
	{
		OutRotation = FQuat();

		if (TShapeElem* ShapeElem = static_cast<TShapeElem*>(Primitive))
		{
			OutRotation = ShapeElem->Rotation.Quaternion();
			return true;
		}

		return false;
	}
};

//////////////////////////////////////////////////////////////////////////
// MFMath

namespace MFMath // Murilo's FMath
{
	// If a Vector is less than a unit vector, and we want to multiply it by a scalar, but making sure it won't exceed it's unit length.
	FVector SafeMultiplyUnderUnitVector(const FVector &Vector, const float Scalar)
	{
		FVector Result = Vector * Scalar;
		if (Result.Size() > 1)
		{
			Result = Result.GetSafeNormal();
		}
		return Result;
	}

	bool SweepConeTrace(const UWorld* InWorld, FHitResult& HitResult, FVector Origin, FVector Direction, FVector UpVector, float ConeAngle, float SweepLength, int32 NumSteps, const FCollisionQueryParams& CollisionParams)
	{
		// Convert ConeAngle from degrees to radians
		float HalfConeAngleRad = FMath::DegreesToRadians(ConeAngle / 2.0f);

		DrawDebugCone(InWorld, Origin, Direction, SweepLength, HalfConeAngleRad, HalfConeAngleRad, 32, FColor::Purple, false, 0.02f, 0, 0.05f);

		// Perform the trace in multiple steps along the cone path
		float PrevDistanceAlongCone = 0;
		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			// Calculate the distance along the cone for this step			
			const float DistanceAlongCone = (SweepLength / NumSteps) * (Step + 1);

			// Calculate the radius of the cone at this distance
			const float ConeRadiusAtDistance = DistanceAlongCone * FMath::Tan(HalfConeAngleRad);

			// Calculate the startpoint of the sphere trace by using the previous distance;
			const FVector FixedOrigin = Origin + Direction * PrevDistanceAlongCone;

			// Calculate the endpoint of the sphere trace
			FVector End = Origin + Direction * DistanceAlongCone;

			// Perform the sphere sweep (this will create a sphere with radius `ConeRadiusAtDistance` at the given point along the cone)
			bool bHit = InWorld->SweepSingleByChannel(
				HitResult,
				FixedOrigin,
				End,
				FQuat::Identity,
				ECC_Visibility, // Collision channel
				FCollisionShape::MakeSphere(ConeRadiusAtDistance), // Sphere with dynamic radius
				CollisionParams
			);

			// If a hit occurs, process it
			if (bHit)
			{			
				// Visualize the hit in the world (green if it's within the cone, red if outside)
				FVector HitLocation = HitResult.ImpactPoint;
				DrawDebugSphere(InWorld, HitLocation, 10.0f, 32, FColor::Yellow, false, 0.02f, 0, 0.25f);

				// Calculate the angle between the hit point and the center direction of the cone
				FVector HitDirection = (HitLocation - Origin).GetSafeNormal();
				float DotProduct = FVector::DotProduct(Direction.GetSafeNormal(), HitDirection);

				// Calculate the angle from the dot product (in radians)
				float Angle = FMath::Acos(DotProduct);
				float AngleInDegrees = FMath::RadiansToDegrees(Angle);

				// If the angle is within the cone, we accept the hit
				if (AngleInDegrees <= ConeAngle / 2.0f)
				{					
					DrawDebugSphere(InWorld, FixedOrigin, ConeRadiusAtDistance * 0.75f, 8, FColor::Green, false, 0.02f, 0, ((Step + 1) / NumSteps) * 0.5f);
					DrawDebugSphere(InWorld, End, ConeRadiusAtDistance, 16, FColor::Green, false, 0.02f, 0, (Step + 1) / NumSteps);
#ifdef M_DEBUG_ENABLED
					if (MDebugHelper::ShouldDrawTraceDebug())
					{
						// Debug: If it's inside the cone, draw a green debug line
						DrawDebugLine(InWorld, Origin, HitLocation, FColor::Green, false, 0.02f, 0, 1.0f);
					}
#endif
					return true;
				}
				else
				{
					GEngine->AddOnScreenDebugMessage(34, 0.02f, FColor::Blue, FString::Printf(TEXT("Trace not accepted. Angle: %f"), AngleInDegrees));
					DrawDebugSphere(InWorld, FixedOrigin, ConeRadiusAtDistance * 0.75f, 8, FColor::Blue, false, 0.02f, 0, ((Step + 1) / NumSteps) * 0.5f);
					DrawDebugSphere(InWorld, End, ConeRadiusAtDistance, 16, FColor::Blue, false, 0.02f, 0, (Step + 1) / NumSteps);
				}
			}
			else
			{
				DrawDebugSphere(InWorld, FixedOrigin, ConeRadiusAtDistance * 0.75f, 8, FColor::Red, false, 0.02f, 0, ((Step + 1) / NumSteps) * 0.5f);
				DrawDebugSphere(InWorld, End, ConeRadiusAtDistance, 16, FColor::Red, false, 0.02f, 0, (Step + 1) / NumSteps);
			}

			PrevDistanceAlongCone = DistanceAlongCone;
		}

		return false;



		//// Normalize the direction vector to ensure it's a unit vector
		//FVector ForwardDirection = Direction.GetSafeNormal();

		//// Perform the sphere trace for 1 unit distance (since it's a unit length trace)
		//FVector End = Origin + ForwardDirection;

		//// Perform the sphere sweep (this will create a sphere with a radius that approximates the cone's spread)
		//bool bHit = InWorld->SweepSingleByChannel(
		//	HitResult,
		//	Origin,
		//	End,
		//	FQuat::Identity,
		//	ECC_Visibility, // Collision channel
		//	FCollisionShape::MakeSphere(SweepLength), // The radius of the sphere (this is how wide the cone is)
		//	CollisionParams
		//);

		//float HalfConeAngleRad = FMath::DegreesToRadians(ConeAngle / 2.0f);
		//float ConeRadiusAtDistance = SweepLength * FMath::Tan(HalfConeAngleRad);
		//DrawDebugCone(InWorld, Origin, Direction, SweepLength, HalfConeAngleRad, HalfConeAngleRad, 32, FColor::Orange, false, 0.02f, 0, 0.25f);
		//DrawDebugSphere(InWorld, Origin, SweepLength, 32, (bHit) ? FColor::Green : FColor::Red, false, 0.02f, 0, 0.125f);

		//// If a hit occurs, process it
		//if (bHit)
		//{
		//	// Visualize the hit in the world (green if it's within the cone, red if outside)
		//	FVector HitLocation = HitResult.ImpactPoint;

		//	// Calculate the vector from the Origin to the hit location
		//	FVector HitDirection = (HitLocation - Origin).GetSafeNormal();

		//	// Calculate the dot product between the forward direction of the cone and the direction to the hit
		//	float DotProduct = FVector::DotProduct(ForwardDirection, HitDirection);

		//	// Calculate the angle from the dot product (in radians)
		//	float AngleRad = FMath::Acos(DotProduct);

		//	// If the angle is within the cone, we accept the hit
		//	if (AngleRad <= HalfConeAngleRad)
		//	{
		//		// Debug: If it's inside the cone, draw a green debug line
		//		DrawDebugLine(InWorld, Origin, HitLocation, FColor::Green, false, 1.0f, 0, 1.0f);
		//		return true;
		//	}
		//	else
		//	{
		//		// Otherwise, it's outside the cone, so draw a red debug line
		//		DrawDebugLine(InWorld, Origin, HitLocation, FColor::Red, false, 1.0f, 0, 1.0f);
		//	}
		//}
		//else
		//{
		//	// Debug: If no hit occurs, draw a red line to show the cone path
		//	DrawDebugLine(InWorld, Origin, End, FColor::Red, false, 1.0f, 0, 1.0f);
		//}

		//return false;
	}
};


//////////////////////////////////////////////////////////////////////////
// FHandsContextData

FVector FHandsContextData::GetHandLocation() const
{
	if (!HitActor)
	{
		return FVector();
	}

	FTransform HitBoneLocalToWorldTransform = HitActor->GetActorTransform();
	return HitBoneLocalToWorldTransform.TransformPosition(HandSurfaceLocalLocation);
}

FVector FHandsContextData::GetHandNormal() const
{
	if (!HitActor)
	{
		return FVector();
	}

	FTransform HitBoneLocalToWorldTransform = HitActor->GetActorTransform();
	return HitBoneLocalToWorldTransform.TransformVector(HandSurfaceLocalNormal);
}

FRotator FHandsContextData::GetHandRotation(bool bShouldFlip, const FVector RelativeRight, const FVector RelativeUp) const
{
	FVector HandNormal = GetHandNormal();
	const FRotator GrabRot = GetGrabRotation(RelativeRight, RelativeUp);
	FVector FixedYAxis = GrabRot.RotateVector(-FVector::YAxisVector);

	// Invert the normal.
	if (bShouldFlip)
	{
		HandNormal = -HandNormal;
	}

	const FRotator HandRotation = FRotationMatrix::MakeFromYZ(HandNormal, FixedYAxis).Rotator();//FRotationMatrix::MakeFromY(HandNormal).Rotator();

	// Debug
	//DrawDebugCoordinateSystem(GEngine->GetWorld(), GetHandLocation(), HandRotation, 10.0f, false, -1.0f, 0, 1.0f);

	// Hand bones are oriented towards Y, which is why we don't get OrientationVector.
	return HandRotation;
}

// Perhaps get CapsuleComponent()->GetUpVector instead of camera?
FRotator FHandsContextData::GetGrabRotation(const FVector RelativeRight, const FVector RelativeUp) const
{
	const FVector HandLocation = GetHandLocation();
	const FVector HandNormal = GetHandNormal();

	const float RelativeRightNormalDot = RelativeRight | HandNormal;
	if (FMath::Abs(RelativeRightNormalDot) <= M_SAFE_WORLD_PERPENDICULAR_DOT)
	{
		// Micro-optimization, we are manually doing FVector::VectorPlaneProject using the dot calculated above.
		const FVector HandRelativeRight = RelativeRight - (HandNormal * RelativeRightNormalDot);
		return FRotationMatrix::MakeFromXY(HandNormal, HandRelativeRight).Rotator();
	}

	// Relative Right vector is parallel to the normal, so we'll instead try to calculate using the world up, and if it fails, we'll use relativeup instead.
	
	FVector HandRelativeUp = -FVector::UpVector;
	const float RelativeUpNormalDot = HandRelativeUp | HandNormal;
	if (FMath::Abs(RelativeUpNormalDot) <= M_SAFE_WORLD_PERPENDICULAR_DOT)
	{
		// FVector::UpVector Dot HandNormal should be greater than zero to use, otherwise we project using RelativeUp instead.

		// Micro-optimization, we are manually doing FVector::VectorPlaneProject using the dot calculated above.
		HandRelativeUp = (HandRelativeUp - (HandNormal * RelativeUpNormalDot)).GetSafeNormal();
	}
	else
	{
		HandRelativeUp = FVector::VectorPlaneProject(RelativeUp, HandNormal).GetSafeNormal();
	}

	
	return FRotationMatrix::MakeFromXZ(HandNormal, HandRelativeUp).Rotator();

	/** GrabRot relative to HandLocation */
	//DrawDebugCoordinateSystem(GetWorld(), HandLocation, GrabRot, 10.0f, false, 0.15f, 0, 1.0f);

	/** HandLocation */
	//DrawDebugSphere(GetWorld(), HandLocation, 50.0f, 6, FLinearColor(1.0f, 0.39f, 0.87f, 1.0f).ToFColorSRGB(), false, 0.15f, 0, 0.0f);

	/** HandNormal pointing outwards from HandLocation */
	//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + (HandNormal * 12.0f), 1.0f, FLinearColor(0.18f, 0.57f, 1.0f, 1.0f).ToFColorSRGB(), false, 0.15f, 0, 1.0f);

	/** Arrow pointing from HandLocation to MoveDir */
	//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + MoveDir * 2.f, 1.0f, FLinearColor(0.46f, 1.0f, 0.15f, 1.0f).ToFColorSRGB(), false, 0.15f, 0, 1.0f);
	//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + MouseInput * 7.5f, 1.0f, FLinearColor(0.15f, 0.46f, 1.0f, 1.0f).ToFColorSRGB(), false, 0.15f, 0, 0.5f);

	//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + ProjectedArmVector, 1.0f, (ArmOverstretched) ? FColor::Magenta : FColor::Emerald, false, -1.0f, 0, 1.0f);

	//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + ArmVector, 1.0f, (ArmOverstretched) ? FColor::Magenta : FColor::Emerald, false, -1.0f, 0, 0.5f);
	// End of Debugs
}

void FHandsContextData::StoreHit(const FHitResult& HitResult, const FVector& OverrideHitLocation, const FVector& OverrideHitNormal)
{
	CurrentFrameTracedHitResult = HitResult;

	const bool bUseOverrideHitLocation = OverrideHitLocation.IsZero();
	const bool bUseOverrideHitNormal = OverrideHitNormal.IsZero();

	const FVector GrabLocation = (bUseOverrideHitLocation) ? HitResult.ImpactPoint : OverrideHitLocation;//Location;
	const FVector GrabNormal = (bUseOverrideHitNormal) ? HitResult.Normal : OverrideHitNormal; // Using Normal instead of ImpactNormal (This is due to the ConeTrace)

	HitActor = HitResult.GetActor();
	HitComponent = HitResult.GetComponent();
	HitBoneName = HitResult.BoneName;

	FTransform HitBoneWorldToLocalTransform = HitResult.GetActor()->GetActorTransform(); //HitResult.Component->GetSocketTransform(HitResult.BoneName).Inverse();
	HandSurfaceLocalLocation = HitBoneWorldToLocalTransform.InverseTransformPosition(GrabLocation);
	HandSurfaceLocalNormal = HitBoneWorldToLocalTransform.InverseTransformVector(GrabNormal);
}

FQuat FHandsContextData::GetCollisionPrimitiveRotation() const
{
	FQuat PrimitiveRotation = FQuat();
	if (bool bValid = MUtils::GetPrimitiveRotation<FKSphylElem>(HandCollisionPrimitive, PrimitiveRotation))
	{
		return PrimitiveRotation;
	}
	
	if (bool bValid = MUtils::GetPrimitiveRotation<FKBoxElem>(HandCollisionPrimitive, PrimitiveRotation))
	{
		return PrimitiveRotation;
	}

	return PrimitiveRotation;
}

void FHandsContextData::ResetHandState()
{
	InteractionType = EInteractType::INT_None;
	HitActor = 0;
	HitComponent = 0;
	HitBoneName = FName();
	CurrentFrameTracedHitResult = FHitResult();
	IsOverstretched = false;
	IsExertingForce = false;
}

bool FHandsContextData::IsInteractClimbing() const
{
	return InteractionType == EInteractType::INT_Climbable;
}

bool FHandsContextData::IsInteractGrabbing() const
{
	return InteractionType == EInteractType::INT_Grabbable;
}


//////////////////////////////////////////////////////////////////////////
// APrototype1Character

APrototype1Character::APrototype1Character(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UClimberCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	ClimberMovementComponent = Cast<UClimberCharacterMovementComponent>(GetCharacterMovement());

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(40.0f, 96.0f);

	MeshPivot = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshPivot"));
	MeshPivot->SetupAttachment(GetCapsuleComponent());
	MeshPivot->SetRelativeLocation(FVector(-10.f, 0.f, 59.414395f));
	MeshPivot->SetRelativeScale3D(FVector(1.f, 1.f, 1.f));

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	//Mesh1P->SetupAttachment(MeshPivot); // Default (Non Full Body)
	Mesh1P->SetupAttachment(GetCapsuleComponent()); // Full Body Setup
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	//Mesh1P->SetRelativeLocation(FVector(10.f, 0.f, -180.82879f)); // Default (Non Full Body)
	//Mesh1P->SetRelativeRotation(FRotator(0.9f, -19.19f, 5.2f));
	//Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));
	Mesh1P->SetRelativeLocation(FVector(0.f, 0.f, -121.414395f)); // Use this if not attaching to MeshPivot. (Full Body)

	// Create a CameraComponent
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	// If we are using a True FPS Pawn, we want to setup a attachment bone -- might be TEXT("head").
	FirstPersonCameraComponent->SetupAttachment(Mesh1P, TEXT("VB head_root")); //root_head
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 180.82879f)); // Position the camera

	//FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
	//FirstPersonCameraComponent->SetRelativeLocation(FVector((40.881380f, 0.f, 60.f)); // my overriden values.
	FirstPersonCameraComponent->bUsePawnControlRotation = false;

	PhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandle"));

	// Setting up local clavicles. 
	// TODO: Maybe in the future have a local directional vector from the camera to the clavicles, this way I'd avoid an extra GetComponentTransform().GetLocation()
	LocalClavicle_L = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LocalClavicle_L"));
	LocalClavicle_L->SetupAttachment(GetRootComponent());
	LocalClavicle_R = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LocalClavicle_R"));
	LocalClavicle_R->SetupAttachment(GetRootComponent());

	LocalUpperArm_L = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LocalUpperArm_L"));
	LocalUpperArm_L->SetupAttachment(GetRootComponent());
	LocalUpperArm_R = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LocalUpperArm_R"));
	LocalUpperArm_R->SetupAttachment(GetRootComponent());

	ElbowJointTarget_L = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ElbowJointTarget_L"));
	ElbowJointTarget_L->SetupAttachment(GetRootComponent());
	ElbowJointTarget_L->SetRelativeLocation(FVector(-300.f, -1000.f, 0.f));
	ElbowJointTarget_L->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
	ElbowJointTarget_R = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ElbowJointTarget_R"));
	ElbowJointTarget_R->SetupAttachment(GetRootComponent());
	ElbowJointTarget_R->SetRelativeLocation(FVector(-300.f, 1000.f, 0.f));
	ElbowJointTarget_R->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
}

void APrototype1Character::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	// Workaround to get extra colliders to work with CMC. 
	// We need to set CapsuleCollider "Simulate Physics" to true in the asset, and then set it to false on BeginPlay.
	// Don't know why it works, but it does. 
	// The other possible solution would be to override the SafeMoveUpdatedComponent for our custom CMC, to consider
	// the extra colliders instead of only the default CapsuleCollider.
	GetCapsuleComponent()->SetSimulatePhysics(false);

	// Maybe have an initializer for a few gameplay states.
	bIsAlive = true;

	Mesh1PPhysicsAsset = Mesh1P->GetPhysicsAsset();

	RightHandData.HandIndex = 0;
	LeftHandData.HandIndex = 1;

	LeftHandData.LocalHandIdleLocation = LeftHandIdlePositionLocal;
	LeftHandData.LocalHandIdleRotation = LeftHandIdleRotationLocal;
	
	RightHandData.LocalHandIdleLocation = RightHandIdlePositionLocal;
	RightHandData.LocalHandIdleRotation = RightHandIdleRotationLocal;

	SetupHandRuntimeContextData(RightHandData);
	SetupHandRuntimeContextData(LeftHandData);

	SetupArm(0, ASETUP_Idle);
	SetupArm(1, ASETUP_Idle);

	// Since we start on the ground, default movement mode will be Walking.
	ClimberMovementComponent->SetMovementMode(EMovementMode::MOVE_Walking);
	
	/** Caching some "heavy" calculations that will be used everyframe */
	ArmsLengthUnitsSquared = FMath::Square(ArmsLengthUnits);

	// Deprecated, but we will still calculate this. (This will actually be the half-length
	HandPhysicalHeight = (HandPhysicalLength + (HandPhysicalRadius * 2)) / 2;


	const FVector RightClavicleBoneLocation = Mesh1P->GetBoneLocation(RightHandData.ClavicleBoneName);
	const FVector RightUpperArmBoneLocation = Mesh1P->GetBoneLocation(RightHandData.UpperArmBoneName);
	const FVector LeftClavicleBoneLocation = Mesh1P->GetBoneLocation(LeftHandData.ClavicleBoneName);
	const FVector LeftUpperArmBoneLocation = Mesh1P->GetBoneLocation(LeftHandData.UpperArmBoneName);

	// Get UpperArm-Clavicle Length (Taking reference from right arm, since left/right should be the same, under a certain error margin).
	ClavicleShoulderLength = (RightUpperArmBoneLocation - RightClavicleBoneLocation).Length() * ClavicleShoulderLengthMultiplier;

	// Setting up local clavicles.	
	LocalClavicle_L->SetWorldLocation(LeftClavicleBoneLocation);
	LocalClavicle_R->SetWorldLocation(RightClavicleBoneLocation);

	// Setting up local upper arms
	LocalUpperArm_L->SetWorldLocation(LeftUpperArmBoneLocation);
	LocalUpperArm_R->SetWorldLocation(RightUpperArmBoneLocation);
}

//////////////////////////////////////////////////////////////////////////// Input

void APrototype1Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping (fly up)
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &APrototype1Character::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Ducking (fly down)
		EnhancedInputComponent->BindAction(DuckAction, ETriggerEvent::Started, this, &APrototype1Character::Duck);
#if WITH_EDITOR
		// Used for Flying Down in Debug God mode.
		EnhancedInputComponent->BindAction(DuckAction, ETriggerEvent::Triggered, this, &APrototype1Character::FlyDown);
#endif

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APrototype1Character::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APrototype1Character::Look);
		EnhancedInputComponent->BindAction(FreeLookAction, ETriggerEvent::Started, this, &APrototype1Character::BeginFreeLook);
		EnhancedInputComponent->BindAction(FreeLookAction, ETriggerEvent::Completed, this, &APrototype1Character::EndFreeLook);

		// Grabbing
		EnhancedInputComponent->BindAction(GrabActionL, ETriggerEvent::Started, this, &APrototype1Character::InteractL);
		EnhancedInputComponent->BindAction(GrabActionL, ETriggerEvent::Completed, this, &APrototype1Character::StopInteractL);
		EnhancedInputComponent->BindAction(GrabActionR, ETriggerEvent::Started, this, &APrototype1Character::InteractR);
		EnhancedInputComponent->BindAction(GrabActionR, ETriggerEvent::Completed, this, &APrototype1Character::StopInteractR);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void APrototype1Character::SetupHandRuntimeContextData(FHandsContextData& HandData) const
{
	if (!IsValid(Mesh1PPhysicsAsset))
	{
		UE_LOG(LogTemp, Error, TEXT("[APrototype1Character::SetupHandsRuntimeContextData] No Mesh1PPhysicsAsset present"));
		return;
	}

	int32 HandBodyIndex = Mesh1PPhysicsAsset->FindBodyIndex(HandData.HandBoneName);
	check(Mesh1PPhysicsAsset->SkeletalBodySetups.IsValidIndex(HandBodyIndex));
	FKAggregateGeom* AggGeom = &Mesh1PPhysicsAsset->SkeletalBodySetups[HandBodyIndex]->AggGeom;

	// Check for Capsule Shape Element.
	FKShapeElem* Elem = AggGeom->GetElement(EAggCollisionShape::Sphyl, 0);
	if (FKSphylElem* SphylElem = static_cast<FKSphylElem*>(Elem))
	{
		HandData.HandCollisionPrimitive = SphylElem;
		HandData.HandCollisionShape = FCollisionShape::MakeCapsule(SphylElem->Radius, (SphylElem->Length + SphylElem->Radius * 2) / 2.f);
	}
	else
	{
		// Check for Box Shape Element
		Elem = AggGeom->GetElement(EAggCollisionShape::Box, 0);
		if (FKBoxElem* BoxElem = static_cast<FKBoxElem*>(Elem))
		{
			HandData.HandCollisionPrimitive = BoxElem;
			HandData.HandCollisionShape = FCollisionShape::MakeBox(FVector(BoxElem->X, BoxElem->Y, BoxElem->Z));
		}
	}
}

void APrototype1Character::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Handling Input Buffers
	ProcessInputBuffers(DeltaSeconds);

	TraceForHand(RightHandData);
	TraceForHand(LeftHandData);

	if (IsGrabbing() && !IsFreeLooking)
	{
		MoveGrabbedObject(LeftHandData);
		MoveGrabbedObject(RightHandData);

		// if (IA_R) // Penumbra "Interact" mode.
		//	add_mouse_dir_to_physicshandle_target_location
		//	return;
	}

	InterpArms(0, DeltaSeconds);
	InterpArms(1, DeltaSeconds);

	// Need to convert all these timers into a class, or struct.
	if (IsLookingBack())
	{
		LookBackTimer -= DeltaSeconds;
		if (LookBackTimer <= 0.f)
		{
			ResetLook();
		}
	}
	
	if (FallToDeathTimer > 0.f)
	{
		GEngine->AddOnScreenDebugMessage(27, 3.5f, FColor::Red, FString::Printf(TEXT("Falling to death in: %fs"), FallToDeathTimer));
		FallToDeathTimer -= DeltaSeconds;
		if (FallToDeathTimer <= 0.f)
		{
			OnFallDeath();
		}
	}
	
	if (CoyoteTimer > 0.f)
	{
		CoyoteTimer -= DeltaSeconds;
		CoyoteTimer = FMath::Max(CoyoteTimer, 0.f);
	}
}

// TODO: Make W/S move forward and backwards a little in relation to the arms when grabbed onto something.
void APrototype1Character::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

#ifdef M_DEBUG_ENABLED
	if (ClimberMovementComponent->MovementMode == MOVE_Flying)
	{
		AddMovementInput(FirstPersonCameraComponent->GetForwardVector(), MovementVector.Y);
		AddMovementInput(FirstPersonCameraComponent->GetRightVector(), MovementVector.X);
		return;
	}
#endif

	if (Controller != nullptr)
	{
		// add movement 
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

void APrototype1Character::Jump()
{
	// Since this comes from an EnhancedInput of Triggered,
	// we can only call the actual jump function only once (the first time you press the jump key) before letting the key go
	if (!bPressedJump)
	{
		Super::Jump();
	}

#ifdef M_DEBUG_ENABLED
	if (ClimberMovementComponent->MovementMode == MOVE_Flying)
	{
		AddMovementInput(FVector::UpVector, 1.0f);
	}
#endif
}

void APrototype1Character::Duck()
{
	if (!ClimberMovementComponent->IsClimbing())
	{
		if (bIsCrouched)
		{
			UnCrouch();
		}
		else
		{
			Crouch();
		}
	}
}

#if WITH_EDITOR
void APrototype1Character::FlyDown()
{
	if (ClimberMovementComponent->MovementMode == MOVE_Flying)
	{
		AddMovementInput(FVector::UpVector, -1.0f);
		return;
	}
	else if (ClimberMovementComponent->IsClimbing())
	{
		const FHandsContextData HandData = (LeftHandData.IsInteractClimbing()) ? LeftHandData : RightHandData;

		// Perhaps get CapsuleComponent()->GetUpVector instead of camera?
		const FRotator GrabRot = HandData.GetGrabRotation(GetCapsuleComponent()->GetRightVector(), -FirstPersonCameraComponent->GetUpVector());

		// MoveDir is negated from MouseInput, because Mouse movement is set to INVERTED. Might want to add a check here if I plan on adding mouse settings later.
		const FVector MouseInput = GrabRot.RotateVector(FVector(0, 0, -1.0f));
		const FVector MoveDir = -MouseInput;

		ClimberMovementComponent->AddHandSlipAccelerationInput(HandData, MoveDir);
		return;
	}
}
#endif

void APrototype1Character::ReleaseHand(int HandIndex)
{
	StopInteracting(HandIndex);
}

void APrototype1Character::ReleaseHand(const FHandsContextData& HandData)
{
	StopInteracting(HandData.HandIndex);
}

void APrototype1Character::StartFallingToDeathTime()
{
	FallToDeathTimer = FallToDeathTimeDuration;
	GEngine->AddOnScreenDebugMessage(23, 3.5f, FColor::Red, TEXT("Start Falling to Death Time"));
}

void APrototype1Character::StopFallingToDeathTime()
{
	FallToDeathTimer = 0.f;
	GEngine->AddOnScreenDebugMessage(23, 3.5f, FColor::Green, TEXT("Stop Falling to Death Time"));
}

void APrototype1Character::StartCoyoteTime()
{
	CoyoteTimer = CoyoteTimeDuration;
	//GEngine->AddOnScreenDebugMessage(23, 3.5f, FColor::Yellow, TEXT("Start Coyote Time"));
}

void APrototype1Character::StopCoyoteTime()
{
	CoyoteTimer = 0.f;
	//GEngine->AddOnScreenDebugMessage(23, 3.5f, FColor::Red, TEXT("Stopping Coyote Time"));
}

void APrototype1Character::BeginFreeLook(const FInputActionValue& Value)
{
	if (IsFreeLooking || IsLookingBack())
	{
		return;
	}

	IsFreeLooking = true;
	FreeLookControlRotation = GetControlRotation();
	bUseControllerRotationYaw = false;

	// If we are using a True FPS Pawn, we "almost" always want this to be true.
	FirstPersonCameraComponent->bUsePawnControlRotation = false;
}

void APrototype1Character::EndFreeLook(const FInputActionValue& Value)
{
	if (!IsFreeLooking || IsLookingBack())
	{
		return;
	}

	LookBackTimer = LookBackTime;
}

void APrototype1Character::ResetLook()
{
	LookBackTimer = 0.f;

	// Reset camera to ControlRotation.
	if (AController* MyController = GetController())
	{
		MyController->SetControlRotation(FreeLookControlRotation);
	}

	bUseControllerRotationYaw = true;

	FirstPersonCameraComponent->SetRelativeRotation(DefaultCameraRotation);

	// If we are using a True FPS Pawn, we "almost" always want this to be true.
	//if (!IsUsingFullBody)
	{
		FirstPersonCameraComponent->bUsePawnControlRotation = false;
	}

	IsFreeLooking = false;
}

bool APrototype1Character::IsLookingBack() const
{
	return LookBackTimer > 0.f;
}

float APrototype1Character::GetLookBackBlend() const
{
	return 1.f - FMath::Clamp(LookBackTimer / LookBackTime, 0.f, 1.f);
}


void APrototype1Character::Look(const FInputActionValue& Value)
{
	// Doesn't process any camera inputs for death.
	if (!bIsAlive)
	{
		return;
	}
	
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// If we are grabbing something, send mouse input to hands instead.
	// But if we are looking around, ignore sending data to hands.
	if (IsClimbing() && !IsFreeLooking)
	{
		MoveHand(LeftHandData, LookAxisVector);
		MoveHand(RightHandData, LookAxisVector);

		ClimberMovementComponent->UpdateHelperSpring();

		return;
	}

	if (Controller != nullptr)
	{
		FRotator Delta = UKismetMathLibrary::NormalizedDeltaRotator(GetActorRotation(), GetControlRotation());

		// add yaw and pitch input to controller
		if (CanUseYaw(Delta, LookAxisVector.X))
		{
			AddControllerYawInput(LookAxisVector.X);
		}
		if (CanUsePitch(Delta, LookAxisVector.Y))
		{
			AddControllerPitchInput(LookAxisVector.Y);
		}
	}
}

void APrototype1Character::OnFallDeath_Implementation()
{
	FallToDeathTimer = 0.f;
	bIsAlive = false;
	
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (AClimberCameraManager* ClimberCameraManager = Cast<AClimberCameraManager>(PlayerController->PlayerCameraManager))
		{
			ClimberCameraManager->StartDeathCam();
		}
	}
}



void APrototype1Character::MoveHand(FHandsContextData& HandData, FVector2D LookAxisVector)
{
	if (!HandData.IsInteractClimbing())
	{
		return;
	}
	
	// Perhaps get CapsuleComponent()->GetUpVector instead of camera?
	const FRotator GrabRot = HandData.GetGrabRotation(GetCapsuleComponent()->GetRightVector(), -FirstPersonCameraComponent->GetUpVector());

	// MoveDir is negated from MouseInput, because Mouse movement is set to INVERTED. Might want to add a check here if I plan on adding mouse settings later.
	const FVector MouseInput = GrabRot.RotateVector(FVector(0, LookAxisVector.X, LookAxisVector.Y));
	const FVector MoveDir = -MouseInput;
	
	// Debugs
	GEngine->AddOnScreenDebugMessage(0, 2.5f, FColor::Yellow, FString::Printf(TEXT("MouseInput: (w/ sensitivity: %f - w/o sensitivity: %f) - %s"), (MouseInput * MouseClimbingSensitivity).Length(), MouseInput.Length(), *MouseInput.ToString()));
	GEngine->AddOnScreenDebugMessage(54, 2.5f, FColor::Blue, FString::Printf(TEXT("MoveDir: (w/ sensitivity: %f - w/o sensitivity: %f) - %s"), (MoveDir * MouseClimbingSensitivity).Length(), MoveDir.Length(), *MoveDir.ToString()));

	// HandMoveDir should not exceed a unit vector, we only want the mouse sensitivity to help with the movement, but we can't exceed 1
	// otherwise we would be applying more force than designed in our CMC
	///ClimberMovementComponent->HandMoveDir += MFMath::SafeMultiplyUnderUnitVector(MoveDir, MouseClimbingSensitivity);
	ClimberMovementComponent->HandMoveDir += MoveDir * MouseClimbingSensitivity;

	HandData.IsExertingForce = HandData.IsExertingForce || ClimberMovementComponent->HandMoveDir.Length() >= UE_KINDA_SMALL_NUMBER;
}

void APrototype1Character::MoveGrabbedObject(FHandsContextData& HandData)
{
	if (!HandData.IsInteractGrabbing() || !PhysicsHandle->GrabbedComponent)
	{
		return;
	}

	const FVector WorldHandLocation = FirstPersonCameraComponent->GetComponentTransform().TransformPosition(HandData.HandObjectLocalLocation);
	PhysicsHandle->SetTargetLocation(WorldHandLocation);
}

void APrototype1Character::TraceForHand(FHandsContextData& HandData)
{
	if (HandData.IsInteracting())
	{
		HandData.CurrentFrameTracedHitResult = FHitResult(-1.0f);
		return;
	}

	// Init
	HandData.CanInteract = false;

	// Calculating VerticalExtension that goes from 1 to 2. This is to increase a bit on the trace distance if looking upwards.
	const float TraceVerticalExtension = 1.0f; // FMath::Max(1 + (FVector::UpVector | FirstPersonCameraComponent->GetForwardVector()), 1.f);	

	const FVector ClavicleBoneLocation = (HandData.HandIndex == 0) ? LocalClavicle_R->GetComponentLocation() : LocalClavicle_L->GetComponentLocation();
	const FVector TraceStart = ClavicleBoneLocation + FirstPersonCameraComponent->GetForwardVector();
	const FVector TraceEnd = ClavicleBoneLocation + FirstPersonCameraComponent->GetForwardVector() * TraceVerticalExtension * (ArmsLengthUnits);// + ClavicleShoulderLength);
	const FVector TraceDir = TraceEnd - TraceStart;
	const float TraceLength = TraceDir.SquaredLength();

	// You can use FCollisionQueryParams to further configure the query
	// Here we add ourselves to the ignored list so we won't block the trace
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult HitResult;
	GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECollisionChannel::ECC_PhysicsBody, QueryParams);	

	HandData.CurrentFrameTracedHitResult = HitResult;

#ifdef M_DEBUG_ENABLED
	if (MDebugHelper::ShouldDrawTraceDebug())
	{
		GEngine->AddOnScreenDebugMessage(5, 2.5f, FColor::Yellow, FString::Printf(TEXT("Trace Vertical Extension: %f"), TraceVerticalExtension));
		DrawDebugLine(GetWorld(), TraceStart, TraceEnd, (HitResult.bBlockingHit) ? FColor::Green : FColor::Red, false, 0.02f, 0, 1.0f);
	}
#endif

	if (HitResult.bBlockingHit)
	{
		// If InteractableActorComponent is present, but InteractType is None, then we discard this trace. If not present, then by default we accept this trace.
		HandData.CanInteract = UInteractableActorComponent::IsActorInteractable(HitResult.GetActor(), true);

		if (HandData.CanInteract)
		{
			// Hand is ready to grab.			

			// Now we check for the input buffer in case we have a pending input on this hand
			float& HandInputBuffer = (HandData.HandIndex == 0) ? RightHandGrabInputBuffer : LeftHandGrabInputBuffer;
			if (HandInputBuffer > 0.f)
			{

#ifdef M_DEBUG_ENABLED
				if (MDebugHelper::ShouldDrawTraceDebug())
				{
					GEngine->AddOnScreenDebugMessage(410, 5.f, FColor::Yellow, TEXT("Triggering Grab from Input Buffer as we have a valid trace!"));
				}
#endif

				Interact(HandData.HandIndex);
				HandInputBuffer = 0.f;
				return;
			}
		}
	}
	

	// Hand is not ready to grab.
	const FQuat HandRotation = FQuat::Identity;// GetHandRotation(HandData).Quaternion();

	FVector SweepTraceStart = TraceEnd;
	FVector SweepTraceStartFixed = SweepTraceStart;		

	const FVector SweepTraceEnd = ClavicleBoneLocation + ClimberMovementComponent->UpdatedComponent->GetForwardVector() * TraceVerticalExtension * (ArmsLengthUnits);// +ClavicleShoulderLength);
	FVector SweepTraceEndFixed = SweepTraceEnd;
	FVector SweepPlaneNormal = -ClimberMovementComponent->UpdatedComponent->GetForwardVector();
	if (GetWorld()->LineTraceSingleByChannel(HitResult, ClavicleBoneLocation, SweepTraceEnd, ECollisionChannel::ECC_PhysicsBody, QueryParams))
	{			
		SweepTraceEndFixed = HitResult.Location;
		SweepPlaneNormal = HitResult.Normal;
	}					

	const FVector SweepDir = SweepTraceStartFixed - SweepTraceEndFixed;
	if (SweepDir.Length() > 50.0f)
	{
		// We are tracing too far from the cursor, early out to give a better game feel.
		return;
	}

	const FVector SweepDirProjected = FVector::VectorPlaneProject(SweepDir, SweepPlaneNormal);
	SweepTraceStartFixed = SweepDirProjected + SweepTraceEndFixed + SweepPlaneNormal * HandData.HandCollisionShape.GetCapsuleRadius() * 2.f; // + Some offset in the normal direction.

	const bool ShouldMirrorSweepStart = (SweepDir | SweepPlaneNormal) < 0.f;
	if (ShouldMirrorSweepStart)
	{
		const FVector ProjDir = SweepTraceStartFixed - SweepTraceStart;
		SweepTraceStart = SweepTraceStartFixed + ProjDir;			
	}		

#ifdef M_DEBUG_ENABLED
	if (MDebugHelper::ShouldDrawTraceDebug())
	{
		DrawDebugCapsule(GetWorld(), SweepTraceStart, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Red, false, 0.02f, 0, 0.5f);
		DrawDebugCapsule(GetWorld(), SweepTraceEnd, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Red, false, 0.02f, 0, 0.5f);
		DrawDebugCapsule(GetWorld(), SweepTraceEndFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Yellow, false, 0.02f, 0, 0.75f);
		//UKismetSystemLibrary::DrawDebugPlane(GetWorld(), FPlane(SweepTraceEndFixed, SweepPlaneNormal), SweepTraceEndFixed, 100.0f, FLinearColor::White, 0.02f);
		DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceEndFixed + SweepDir, 1.0f, FColor::Yellow, false, 0.02f, 0, 0.5f);
		DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceEndFixed + SweepDirProjected, 1.0f, FColor::Green, false, 0.02f, 0, 0.5f);
	}
#endif


	for (int BinarySweepCount = 0; BinarySweepCount < 3; BinarySweepCount++)
	{
		if (GetWorld()->SweepSingleByChannel(HitResult, SweepTraceStartFixed, SweepTraceEndFixed, HandRotation, ECollisionChannel::ECC_PhysicsBody, HandData.HandCollisionShape, QueryParams))
		{
			if (UInteractableActorComponent::IsActorInteractable(HitResult.GetActor(), true))
			{
				const float AngleDiff = FMath::RadiansToDegrees(FMath::Acos(HitResult.ImpactNormal | SweepPlaneNormal));
				if (AngleDiff <= 35.0f)
				{
#ifdef M_DEBUG_ENABLED
					if (MDebugHelper::ShouldDrawTraceDebug())
					{
						DrawDebugCapsule(GetWorld(), SweepTraceStartFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Blue, false, 0.02f, 0, 1.0f);
						DrawDebugDirectionalArrow(GetWorld(), SweepTraceStartFixed, SweepTraceStartFixed + SweepPlaneNormal * 10.0f, 1.0f, FColor::Orange, false, 0.02f, 0, 0.75f);
						GEngine->AddOnScreenDebugMessage(37, 0.02f, FColor::Purple, FString::Printf(TEXT("Angle Diff of Hand Sweep Target ImpactNormal and Plane Normal: %f"), AngleDiff));
						DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceStartFixed, 1.0f, FColor::Green, false, 0.02f, 0, 1.0f);
						DrawDebugCapsule(GetWorld(), HitResult.Location, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Green, false, 0.02f, 0, 0.5f);
						DrawDebugDirectionalArrow(GetWorld(), HitResult.Location, HitResult.Location + HitResult.Normal * 10.0f, 1.0f, FColor::Green, false, 0.02f, 0, 0.75f);
						DrawDebugCapsule(GetWorld(), HitResult.ImpactPoint, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Purple, false, 0.02f, 0, 1.0f);
						DrawDebugDirectionalArrow(GetWorld(), HitResult.ImpactPoint, HitResult.ImpactPoint + HitResult.ImpactNormal * 10.0f, 1.0f, FColor::Purple, false, 0.02f, 0, 0.75f);
					}
#endif

					// Hand is ready to grab.

					HandData.CurrentFrameTracedHitResult = HitResult;
					HandData.CanInteract = true;

					// Now we check for the input buffer in case we have a pending input on this hand
					float& HandInputBuffer = (HandData.HandIndex == 0) ? RightHandGrabInputBuffer : LeftHandGrabInputBuffer;
					if (HandInputBuffer > 0.f)
					{
						GEngine->AddOnScreenDebugMessage(420, 5.f, FColor::Yellow, TEXT("Triggering Grab from Input Buffer as we have a valid secondary trace!"));
						Interact(HandData.HandIndex);
						HandInputBuffer = 0.f;
					}
					return;
				}
#ifdef M_DEBUG_ENABLED
				else if (MDebugHelper::ShouldDrawTraceDebug())
				{
					DrawDebugCapsule(GetWorld(), SweepTraceStartFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Red, false, 0.02f, 0, 0.25f);
					DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceStartFixed, 1.0f, FColor::Red, false, 0.02f, 0, 0.25f);
				}
#endif
			}
			else
			{
				UE_LOG(LogTemp, VeryVerbose, TEXT("Found actor %s but it's UInteractableActorComponent is set to no interaction. Skipping.."), *HitResult.GetActor()->GetName());
			}
		}
#ifdef M_DEBUG_ENABLED
		else if (MDebugHelper::ShouldDrawTraceDebug())
		{
			DrawDebugCapsule(GetWorld(), SweepTraceStartFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Magenta, false, 0.02f, 0, 0.25f);
			DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceStartFixed, 1.0f, FColor::Yellow, false, 0.02f, 0, 0.25f);
		}
#endif

		SweepTraceStartFixed = UKismetMathLibrary::VLerp(SweepTraceStartFixed, SweepTraceStart, 0.5f);	// Binary lerp.	
	}


//		const FVector SweepTraceStart = ClavicleBoneLocation;
//		const float SweepTraceLength = TraceVerticalExtension * (ArmsLengthUnits);// +ClavicleShoulderLength);
//
//		if (MFMath::SweepConeTrace(GetWorld(), HitResult, SweepTraceStart, FirstPersonCameraComponent->GetForwardVector(), FirstPersonCameraComponent->GetUpVector(), ArmConeTraceAngle, SweepTraceLength, ArmConeTraceSteps, QueryParams))
//		{
//			// Hand is ready to grab.
//
//			HandData.CurrentFrameTracedHitResult = HitResult;
//			HandData.CanInteract = true;
//
//#ifdef M_DEBUG_ENABLED
//			if (MDebugHelper::ShouldDrawTraceDebug())
//			{
//				DrawDebugDirectionalArrow(GetWorld(), HitResult.ImpactPoint, HitResult.ImpactPoint + HitResult.Normal * 10.0f, 1.0f, FColor::Purple, false, 0.02f, 0, 0.75f);
//				DrawDebugSphere(GetWorld(), HitResult.ImpactPoint, HandData.HandCollisionShape.GetCapsuleRadius(), 32, FColor::Purple, false, 0.02f, 0, 0.0f);
//			}
//#endif
//
//			// Now we check for the input buffer in case we have a pending input on this hand
//			float& HandInputBuffer = (HandData.HandIndex == 0) ? RightHandGrabInputBuffer : LeftHandGrabInputBuffer;
//			if (HandInputBuffer > 0.f)
//			{
//				GEngine->AddOnScreenDebugMessage(40, 5.f, FColor::Yellow, TEXT("Triggering Grab from Input Buffer as we have a valid trace!"));
//				Interact(HandData.HandIndex);
//				HandInputBuffer = 0.f;
//			}
//		}
}

void APrototype1Character::Interact(int HandIndex)
{
	FHandsContextData& HandData = (HandIndex == 0) ? RightHandData : LeftHandData;
	if (HandData.IsInteracting() || !HandData.CanInteract)
	{
		return;
	}

	const FHitResult HitResult = HandData.CurrentFrameTracedHitResult;
	if (HitResult.Time == -1.0f || !HitResult.bBlockingHit)
	{
		// Nothing was hit in this frame. Let's add this input to the input buffer so we can check the same input over the next few frames.
		StoreGrabInputBuffer(HandData);
		return;
	}
	
	HandData.StoreHit(HandData.CurrentFrameTracedHitResult);
	const FVector GrabLocation = HitResult.ImpactPoint;//Location;

	UInteractableActorComponent* InteractableActorComponent = UInteractableActorComponent::GetComponentFromActor(HandData.HitActor);
	ensureMsgf(InteractableActorComponent, TEXT("Traced actor %s must have UInteractableActorComponent, by default treat them as interactable, but I should author them using the editor tool \"Add InteractableActorComponent to StaticMeshes\""), *HandData.HitActor->GetName());

	bool bIsObjectMovable = HandData.HitComponent && HandData.HitComponent->Mobility == EComponentMobility::Movable && HandData.HitComponent->IsSimulatingPhysics(); // Default
	HandData.InteractionType = (bIsObjectMovable) ? EInteractType::INT_Grabbable : EInteractType::INT_Climbable; // Default
	if (InteractableActorComponent)
	{
		bIsObjectMovable = InteractableActorComponent->IsGrabbable();
		HandData.InteractionType = InteractableActorComponent->InteractType;
	}
	// TODO: Improve this default behavior for not having UInteractableActorComponent set.

	if (bIsObjectMovable)
	{
		PhysicsHandle->GrabComponentAtLocation(HandData.HitComponent, HandData.HitBoneName, GrabLocation);
		HandData.HandObjectLocalLocation = FirstPersonCameraComponent->GetComponentTransform().InverseTransformPosition(GrabLocation);
		HandData.HitComponent->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Ignore);
	}



	const FRotator HandRotation = GetHandRotation(HandData);
	if (HandIndex == 0)
	{
		HandData.WorldToHandTransform = FQuat::FindBetweenNormals(FVector::RightVector, -HandRotation.RotateVector(FVector::RightVector));
	}
	else
	{
		HandData.WorldToHandTransform = FQuat::FindBetweenNormals(FVector::RightVector, HandRotation.RotateVector(FVector::RightVector));
	}

	HandData.HandToWorldTransform = HandData.WorldToHandTransform.Inverse();
	DrawDebugCoordinateSystem(GEngine->GetWorld(), GrabLocation, HandData.WorldToHandTransform.Rotator(), 10.0f, true, 35.0f, 0, 1.0f);
	DrawDebugCoordinateSystem(GEngine->GetWorld(), GrabLocation, HandData.HandToWorldTransform.Rotator(), 10.0f, true, 35.0f, 0, 1.0f);

	SetupArm(HandIndex, ASETUP_Climbing);

	if (HandData.IsInteractClimbing())
	{
		ClimberMovementComponent->SetHandClimbing(HandData);
	}

	OnStartGrab(HandData, HandIndex);
}

void APrototype1Character::StopInteracting(int HandIndex)
{
	FHandsContextData& HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	if (!HandData.IsInteracting())
	{
		// We weren't interacting with anything.
		return;
	}

	// Release grabbed object, if it exists for this hand.
	if (PhysicsHandle->GrabbedComponent)
	{
		PhysicsHandle->GrabbedComponent->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Block);
		PhysicsHandle->ReleaseComponent();
	}

	SetupArm(HandIndex, ASETUP_Idle);

	if (HandData.IsInteractClimbing())
	{
		ClimberMovementComponent->ReleaseHand(HandData);
	}

	HandData.ResetHandState();

	OnEndGrab(HandData, HandIndex);
}

void APrototype1Character::SetupArm(const int HandIndex, const EArmSetupType& ArmSetupType)
{
	float& TargetArmLerpTime = (HandIndex == 0) ? RightArmLerpTime : LeftArmLerpTime;
	FArmSetup& TargetArmSetup = (HandIndex == 0) ? CurrentRightArmSetup : CurrentLeftArmSetup;

	for (const FArmSetup& ArmSetup : ArmsSetup)
	{
		if (ArmSetup.SetupType == ArmSetupType)
		{
			// We can get the desired location from the setup.
			TargetArmSetup = ArmSetup;

			TargetArmLerpTime = ArmSetup.LerpInDuration;
		}
	}
}

void APrototype1Character::InterpArms(const int HandIndex, float DeltaSeconds)
{
	float& TargetArmLerpTime = (HandIndex == 0) ? RightArmLerpTime : LeftArmLerpTime;

	if (TargetArmLerpTime > 0.f)
	{		
		TargetArmLerpTime = FMath::Max(TargetArmLerpTime - DeltaSeconds, 0.f);

		FArmSetup TargetArmSetup = (HandIndex == 0) ? CurrentRightArmSetup : CurrentLeftArmSetup;
		for (const FArmSetup& ArmSetup : ArmsSetup)
		{
			if (ArmSetup.SetupType == TargetArmSetup.SetupType)
			{
				const float TimeNormalized = 1.f - (TargetArmLerpTime / ArmSetup.LerpInDuration);

				UStaticMeshComponent* ElbowComponent = (HandIndex == 0) ? ElbowJointTarget_R : ElbowJointTarget_L;
				const FVector TargetElbowLocation = (HandIndex == 0) ? ArmSetup.RightElbowRelativeLocation : ArmSetup.LeftElbowRelativeLocation;
				const FVector ElbowLocation = FMath::Lerp(ElbowComponent->GetRelativeLocation(), TargetElbowLocation, TimeNormalized);
				ElbowComponent->SetRelativeLocation(ElbowLocation);
			}
		}
	}
}

void APrototype1Character::StoreGrabInputBuffer(const FHandsContextData& HandData)
{
	float& HandGrabInputBuffer = (HandData.HandIndex == 0) ? RightHandGrabInputBuffer : LeftHandGrabInputBuffer;
	HandGrabInputBuffer = GrabInputBufferDuration;
}

void APrototype1Character::ProcessInputBuffers(float DeltaSeconds)
{
	auto ProcessBuffer = [this](float& GrabInputBuffer, float DeltaSeconds)
	{
		GrabInputBuffer -= DeltaSeconds;
		if (GrabInputBuffer <= 0.f)
		{
			GrabInputBuffer = 0.f;
		}
	};

	ProcessBuffer(RightHandGrabInputBuffer, DeltaSeconds);
	ProcessBuffer(LeftHandGrabInputBuffer, DeltaSeconds);
}

FHandsContextData& APrototype1Character::GetMutableHandData(int HandIndex)
{
	FHandsContextData& HandData = (HandIndex == 0) ? RightHandData : LeftHandData;
	return HandData;
}

const FHandsContextData& APrototype1Character::GetHandData(int HandIndex) const
{
	const FHandsContextData& HandData = (HandIndex == 0) ? RightHandData : LeftHandData;
	return HandData;
}

FVector APrototype1Character::CalculateArmConstraint(int HandIndex, float DeltaSeconds, const FVector& BodyOffset, FVector& RootDeltaFix, FVector& ArmSpringForce)
{
	return CalculateArmConstraint(GetMutableHandData(HandIndex), DeltaSeconds, BodyOffset, RootDeltaFix, ArmSpringForce);
}

FVector APrototype1Character::CalculateArmConstraint(FHandsContextData& HandData, float DeltaSeconds, const FVector& BodyOffset, FVector& RootDeltaFix, FVector& ArmSpringForce)
{
	RootDeltaFix = FVector::ZeroVector;
	ArmSpringForce = FVector::ZeroVector;

	FVector HandLocation = GetSafeHandLocation(HandData);
	const FVector HandNormal = HandData.GetHandNormal();
	const FVector HandRelativeUp = FVector::VectorPlaneProject(FirstPersonCameraComponent->GetUpVector(), HandNormal).GetSafeNormal();

	const FVector UpperArmPhysBoneLocation = (HandData.HandIndex == 0) ? LocalUpperArm_R->GetComponentLocation() : LocalUpperArm_L->GetComponentLocation();
	const FVector UpperArmOffset = UpperArmPhysBoneLocation + BodyOffset;//Mesh1P->GetBoneLocation(HandData.UpperArmBoneName) + BodyOffset;

	// Variable used for debugging only. (Add a pragma flag to dynamically remove this)
	const FVector RootLocation = ClimberMovementComponent->UpdatedComponent->GetComponentLocation() + BodyOffset;

	FVector OutArmVector = UpperArmOffset - HandLocation;
	const FVector ArmVectorProjected = FVector::VectorPlaneProject(OutArmVector, HandNormal).GetSafeNormal();

	// Check if we need to apply Stretch Multiplier
	const float Angle = FMath::RadiansToDegrees(FMath::Asin(ArmVectorProjected | HandRelativeUp));
	const float AngleNormalized = (Angle - ArmStretchMultiplierMinAngle) / (ArmStretchMultiplierMaxAngle - ArmStretchMultiplierMinAngle);
	const float StretchMultiplier = FMath::Max(1 + (ArmStretchMultiplierCurve->GetFloatValue(AngleNormalized) * (ArmStretchMultiplier - 1)), 1.0f);

	// Check if arm is stretched with added multiplier.
	const float ArmMaxStretchedLength = FMath::Square(ArmsLengthUnits * StretchMultiplier);
	const float ArmMinRelaxedLength = ArmMaxStretchedLength * ArmMinRelaxedT;
	const float ArmMaxRelaxedLength = ArmMaxStretchedLength * ArmRelaxedT;
	float ArmCurrentLength = OutArmVector.SizeSquared();

	const float StretchRatio = FMath::Max((ArmCurrentLength - ArmMaxStretchedLength) / ArmMaxStretchedLength, 0.0f);
	HandData.IsOverstretched = ArmCurrentLength >= ArmMaxStretchedLength;

	GEngine->AddOnScreenDebugMessage(6, 0.1f, FColor::Emerald, FString::Printf(TEXT("ArmCurrentLength: %f - ArmMaxRelaxedLength: %f - ArmMaxStretchedLength: %f - Stretch Ratio: %f"), ArmCurrentLength, ArmMaxRelaxedLength, ArmMaxStretchedLength, StretchRatio));
	//GEngine->AddOnScreenDebugMessage(6, 0.1f, FColor::Emerald, FString::Printf(TEXT("Angle: %f (n: %f) - Stretch Multiplier: %f - Initial Arms Length: %f - Final Arms Length: %f"), 
	//Angle, AngleNormalized, StretchMultiplier, ArmsLengthUnits, ArmsLengthUnits * StretchMultiplier));
	
	// Apply relaxed->overstretched spring.
	const float SpringReadiness = FMath::SmoothStep(ArmMinRelaxedLength, ArmMaxRelaxedLength, ArmCurrentLength);
	GEngine->AddOnScreenDebugMessage(7, 0.1f, FColor::Silver, FString::Printf(TEXT("SpringReadiness: %f"), SpringReadiness));
	if (SpringReadiness > 0.0f)
	{
		GEngine->AddOnScreenDebugMessage(87 + HandData.HandIndex, 0.1f, FColor::Blue, FString::Printf(TEXT("Applying %s arm spring to relaxed state"), *HandData.HandBoneName.ToString()));
		
		const FVector RelaxedArmVector = OutArmVector.GetSafeNormal() * ArmsLengthUnits * StretchMultiplier * ArmRelaxedT;
		const FVector SpringForce = HandLocation + RelaxedArmVector - UpperArmOffset;
		const FVector SpringForceProjected = FVector::VectorPlaneProject(SpringForce, HandNormal);
		ArmSpringForce += SpringForce;

		DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + RelaxedArmVector, 0.7f, FColor::Purple, false, 0.25f, 10, 0.75f);
		DrawDebugDirectionalArrow(GetWorld(), UpperArmOffset, UpperArmOffset + SpringForce, 1.0f, FColor::Blue, false, 0.25f, 20, 1.0f);
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(87 + HandData.HandIndex, 0.1f, FColor::Green, FString::Printf(TEXT("Not applying %s arm spring to relaxed state"), *HandData.HandBoneName.ToString()));
	}

	// If Arm is Overstretched (limb limit), then we'll want to move the root so as to get the shoulder in the correct position such as ArmVector.Length() == ArmsLengthUnitsSquared
	if (HandData.IsOverstretched)
	{
		GEngine->AddOnScreenDebugMessage(77, 0.1f, FColor::Red, TEXT("Applying arm limit snap."));
		
		// Stretched visualization.
		DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation - UpperArmOffset, 1.0f, FColor::Red, false, 0.25f, 1, 1.0f);
		
		const FVector FixedArmVector = OutArmVector.GetSafeNormal() * ArmsLengthUnits * StretchMultiplier;
		DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + FixedArmVector, 1.0f, FColor::Green, false, 0.25f, 5, 0.5f);

		TryToSlipHand(HandData, FixedArmVector, StretchRatio, DeltaSeconds);
		// TODO: Need to find a way to have a hand grip strength to drive how much we slip vs how much we compensate by the overstretching.
		
		// IMPORTANT: ShoulderRootDir is unused. See IMPORTANT notes below.
		//const FVector ShoulderRootDir = RootLocation - UpperArmOffset;
		//DrawDebugDirectionalArrow(GetWorld(), UpperArmOffset, RootLocation, 1.0f, FColor::Yellow, false, 0.25f, 0, 0.5f);

		// ArmDiff is where the shoulder SHOULD be to fix overstretching.
		// IMPORTANT: ArmDiff already is RootDeltaFix. So we are commenting this out.
		//const FVector ArmDiff = HandLocation + FixedArmVector - UpperArmOffset;
		//DrawDebugDirectionalArrow(GetWorld(), UpperArmOffset, UpperArmOffset + ArmDiff, 1.0f, FColor::Purple, false, 0.25f, 0, 1.0f);

		// RootDelta is where the root should be to fix the shoulder, so this is our final fix vector
		// IMPORTANT: Commented code is how the equation was prior. But due to vector math I managed to reduce it to the following one.
		//RootDeltaFix = (UpperArmOffset + ArmDiff + ShoulderRootDir) - RootLocation;
		RootDeltaFix = HandLocation + FixedArmVector - UpperArmOffset;
		DrawDebugDirectionalArrow(GetWorld(), RootLocation, RootLocation + RootDeltaFix, 1.0f, FColor::Red, false, 0.25f, 0, 1.0f);
	}

	HandData.IsExertingForce = HandData.IsOverstretched || SpringReadiness > 0.0f;

	return OutArmVector;
}

bool APrototype1Character::TryToSlipHand(FHandsContextData& HandData, const FVector& ArmVector, float SlipRatio, float DeltaSeconds)
{
	const FVector HandLocation = HandData.GetHandLocation();
	const FQuat HandRotation = GetHandRotation(HandData).Quaternion();
	const FVector HandNormal = HandData.GetHandNormal();
	const FVector HandRelativeUp = FVector::VectorPlaneProject(FirstPersonCameraComponent->GetUpVector(), HandNormal).GetSafeNormal();
	const FVector ProjectedArmVector = FVector::VectorPlaneProject(ArmVector, HandNormal).GetSafeNormal();

	DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + ProjectedArmVector, 1.0f, FColor::Yellow, false, 0.25f, 0, 0.5f);

	const bool bShouldSlip = SlipRatio >= MinStretchRatioToSlip;
	if (!bShouldSlip)
	{
		return false;
	}

	const FVector HandSlipVector = -HandRelativeUp * (4 + SlipRatio);
	ClimberMovementComponent->SetHandSlipVelocity(HandData, HandSlipVector);
	return true;
	//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + HandSlipVector, 1.0f, FColor::Red, false, 1.25f, 0, 0.5f);
	//
	//GEngine->AddOnScreenDebugMessage(16, 0.5f, FColor::Yellow, TEXT("SLIPPING!"));
	//GEngine->AddOnScreenDebugMessage(17, 0.5f, FColor::Emerald, FString::Printf(TEXT("Slip Vector Length: %f - Arm Vector Length: %f (%f)"),
	//	HandSlipVector.Size(), ProjectedArmVector.Size(), ArmVector.Size()));

	//DrawDebugCapsule(GetWorld(), HandLocation, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Yellow, false, 1.25f, 0, 1.0f);

	//FCollisionQueryParams QueryParams;
	//QueryParams.AddIgnoredActor(this);

	////// We need to add a trace here, to make sure that when we slip to the vector direction, we don't end up where there isn't anything to hold on to.
	//TArray<FHitResult> HitResults;
	//GetWorld()->SweepMultiByChannel(HitResults, HandLocation + HandSlipVector, HandLocation, HandRotation, ECollisionChannel::ECC_PhysicsBody, HandData.HandCollisionShape, QueryParams);

	//FHitResult BestHitResult;
	//bool bHadValidBlockingHit = false;
	//for (const FHitResult& HR : HitResults)
	//{
	//	if (HR.bBlockingHit)
	//	{
	//		if (HR.GetActor() == HandData.HitActor)
	//		{
	//			BestHitResult = HR;
	//			bHadValidBlockingHit = true;
	//			DrawDebugCapsule(GetWorld(), HR.ImpactPoint, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Green, false, 1.25f, 0, 1.0f);
	//			DrawDebugCapsule(GetWorld(), HandLocation + HandSlipVector, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Blue, false, 1.25f, 0, 1.0f);
	//			break;
	//		}
	//	}
	//}

	//if (bHadValidBlockingHit)
	//{
	//	const FVector MoveDelta = HandLocation + HandSlipVector;
	//	MoveHandGrabLocation(HandData, MoveDelta); // This should be called inside CMC after calculating our separate "HandSlipVelocity"
	//	return true;
	//}
	//
	//DrawDebugCapsule(GetWorld(), HandLocation + HandSlipVector, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Red, false, 1.25f, 0, 1.0f);
	//// TODO:
	//// Maybe do an extra check on the stretchratio to see if the hands just let go?
	//// Or maybe have a hand resistance buffer, that gets added to each frame we slip the hand, and once it reaches a threshold, we let go.
	//return false;
}

FVector APrototype1Character::ValidateHandSlipTarget(const FHandsContextData& HandData, const FVector& SlipTarget)
{
	const FVector HandLocation = HandData.GetHandLocation();
	const FQuat HandRotation = GetHandRotation(HandData).Quaternion();

	DrawDebugDirectionalArrow(GetWorld(), HandLocation, SlipTarget, 1.0f, FColor::Red, false, 10.25f, 0, 0.5f);
	DrawDebugCapsule(GetWorld(), HandLocation, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Yellow, false, 10.25f, 0, 1.0f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	TArray<FHitResult> HitResults;
	GetWorld()->SweepMultiByChannel(HitResults, HandLocation, SlipTarget, HandRotation, ECollisionChannel::ECC_PhysicsBody, HandData.HandCollisionShape, QueryParams);

	for (const FHitResult& HR : HitResults)
	{
		if (HR.bBlockingHit)
		{
			if (HR.GetActor() == HandData.HitActor)
			{
				const float AngleDiff = FMath::RadiansToDegrees(FMath::Asin(HR.Normal | HandData.GetHandNormal()));
				DrawDebugCapsule(GetWorld(), HR.ImpactPoint, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Green, false, 10.25f, 0, 1.0f);
				DrawDebugCapsule(GetWorld(), SlipTarget, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Blue, false, 10.25f, 0, 1.0f);
				GEngine->AddOnScreenDebugMessage(36, 10.25f, FColor::Yellow, FString::Printf(TEXT("Angle Diff of Slip Target Normal and Current Hand Normal: %f"), AngleDiff));

				return SlipTarget;
			}
		}
	}

	return HandLocation;
}

FVector APrototype1Character::MoveHandGrabLocation(FHandsContextData& HandData, const FVector& MoveDelta)
{
	const FVector HandLocation = HandData.GetHandLocation();
	const FVector HandNormal = HandData.GetHandNormal();
	const FQuat HandRotation = GetHandRotation(HandData).Quaternion();

	const FVector HandMoveDestination = HandLocation + MoveDelta;

	DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandMoveDestination, 10.0f, FColor::Yellow, false, 0.02f, 0, 1.0f);
	DrawDebugCapsule(GetWorld(), HandMoveDestination, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Yellow, false, 0.02f, 0, 1.0f);
	DrawDebugCapsule(GetWorld(), HandLocation, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Yellow, false, 0.02f, 0, 1.0f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	TArray<FHitResult> MoveDeltaHitResults;
	if (GetWorld()->SweepMultiByChannel(MoveDeltaHitResults, HandMoveDestination, HandMoveDestination * 1.01f, HandRotation, ECollisionChannel::ECC_PhysicsBody, HandData.HandCollisionShape, QueryParams))
	{
		for (const FHitResult& MoveDeltaHitResult : MoveDeltaHitResults)
		{
			const int AngleSign = FMath::Sign(MoveDeltaHitResult.ImpactNormal | FVector::UpVector);
			if (AngleSign >= 0)
			{
				const float AngleDiff = FMath::RadiansToDegrees(FMath::Acos(MoveDeltaHitResult.ImpactNormal | HandNormal));
				if (AngleDiff <= MaxSlipHandAngle)
				{
					DrawDebugCapsule(GetWorld(), MoveDeltaHitResult.Location, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Purple, false, 0.02f, 0, 1.0f);

					HandData.StoreHit(MoveDeltaHitResult, MoveDeltaHitResult.Location, MoveDeltaHitResult.ImpactNormal);
					return MoveDeltaHitResult.Location;//HandMoveDestination;
				}
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("[APrototype1Character::MoveHandGrabLocation] Got a trace with a negative incline."));
				GEngine->AddOnScreenDebugMessage(18, 5.f, FColor::Yellow, TEXT("[APrototype1Character::MoveHandGrabLocation] Got a trace with a negative incline."));
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[APrototype1Character::MoveHandGrabLocation] Hand slipped off surface."));
	GEngine->AddOnScreenDebugMessage(18, 5.f, FColor::Red, TEXT("[APrototype1Character::MoveHandGrabLocation] Hand slipped off surface."));

	ReleaseHand(HandData);
	return HandLocation;
}

FVector APrototype1Character::GetHandLocation(int HandIndex) const
{
	return GetHandLocation(GetHandData(HandIndex));
}

FVector APrototype1Character::GetHandLocation(const FHandsContextData& HandData) const
{
	return HandData.GetHandLocation();
}

FVector APrototype1Character::GetSafeHandLocation(int HandIndex) const
{
	return GetSafeHandLocation(GetHandData(HandIndex));
}

FVector APrototype1Character::GetSafeHandLocation(const FHandsContextData& HandData) const
{
	if (!HandData.IsInteracting())
	{
		const FTransform MeshToWorld = Mesh1P->GetComponentToWorld();
		FVector FinalHandLocation = MeshToWorld.TransformPosition(HandData.LocalHandIdleLocation);

		if (!HandData.CanInteract)
		{
			// Early out, since we aren't grabbing, and hand can't interact. Might remove this in the future, in case I want to add a "hold" hand up functionality. (To grab something)
			return FinalHandLocation;
		}

		const FVector UpperArmLocation = Mesh1P->GetBoneLocation(HandData.UpperArmBoneName);
		const FVector LowerArmLocation = Mesh1P->GetBoneLocation(HandData.LowerArmBoneName);
		constexpr float ExtraLengthMult = 1.2f;
		const float MaxBoneLength = ((FinalHandLocation-LowerArmLocation).Length() + (LowerArmLocation-UpperArmLocation).Length()) * ExtraLengthMult; // This is just to trace a path.
		
		const FQuat HandRotation = Mesh1P->GetBoneTransform(HandData.HandBoneName).TransformRotation(HandData.GetCollisionPrimitiveRotation());
		
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		FHitResult HitResult;
		if (GetWorld()->SweepSingleByChannel(HitResult, UpperArmLocation, FinalHandLocation, HandRotation, ECollisionChannel::ECC_PhysicsBody, HandData.HandCollisionShape, QueryParams))
		{
			//DrawDebugCapsule(GetWorld(), FinalHandLocation, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Red, false, 0.02f, 0, 1.0f);
			FinalHandLocation = HitResult.Location;
			//DrawDebugCapsule(GetWorld(), FinalHandLocation, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Yellow, false, 0.02f, 0, 1.0f);
		}
		else
		{
			//DrawDebugCapsule(GetWorld(), FinalHandLocation, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Yellow, false, 0.02f, 0, 1.0f);
		}
		
		return FinalHandLocation;
	}

	return HandData.GetHandLocation() + HandData.GetHandNormal() * HandSafeZone;
}

FVector APrototype1Character::GetHandNormal(int HandIndex) const
{
	FHandsContextData HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	return GetHandNormal(HandData);
}

FVector APrototype1Character::GetHandNormal(const FHandsContextData& HandData) const
{
	return HandData.GetHandNormal();
}

FRotator APrototype1Character::GetHandRotation(int HandIndex) const
{
	return GetHandRotation(GetHandData(HandIndex));
}

FRotator APrototype1Character::GetHandRotation(const FHandsContextData& HandData) const
{
	if (!HandData.IsInteracting())
	{
		const FTransform MeshToWorld = Mesh1P->GetComponentTransform();
		return MeshToWorld.TransformRotation(HandData.LocalHandIdleRotation.Quaternion()).Rotator();
		//return HandData.LocalHandIdleRotation;
	}

	// Flip RightHand only.
	// Perhaps get CapsuleComponent()->GetUpVector instead of camera?
	return HandData.GetHandRotation(HandData.HandIndex == 0, GetCapsuleComponent()->GetRightVector(), -FirstPersonCameraComponent->GetUpVector());
}

FVector APrototype1Character::RotateToHand(const FHandsContextData& HandData, const FVector& WorldRelative) const
{
	//const FVector HandRelativeUp = FVector::VectorPlaneProject(-FirstPersonCameraComponent->GetUpVector(), HandData.GetHandNormal());
	//const FQuat HandToWorldTransform = FQuat::FindBetweenNormals(FVector::UpVector, -HandRelativeUp).Inverse();
	//const FQuat HandToWorldTransform = GetHandRotation(HandData).Quaternion().Inverse();
	const FQuat HandToWorldTransform = HandData.HandToWorldTransform;

	const FVector HandLoc = HandData.GetHandLocation();
	DrawDebugCoordinateSystem(GetWorld(), HandLoc, HandToWorldTransform.Rotator(), 10.0f, false, 0.15f, 0, 1.0f);
	DrawDebugDirectionalArrow(GetWorld(), HandLoc, HandLoc + HandToWorldTransform.RotateVector(WorldRelative), 10.f, FColor::White, false, 0.15f, 0, 1.0f);

	return HandToWorldTransform.RotateVector(WorldRelative);
}

FVector APrototype1Character::RotateToWorld(const FHandsContextData& HandData, const FVector& HandRelative) const
{
	//const FVector HandRelativeUp = FVector::VectorPlaneProject(-FirstPersonCameraComponent->GetUpVector(), HandData.GetHandNormal());
	//const FQuat WorldToHandTransform = FQuat::FindBetweenNormals(FVector::UpVector, -HandRelativeUp);
	//const FQuat WorldToHandTransform = GetHandRotation(HandData).Quaternion();
	const FQuat WorldToHandTransform = HandData.WorldToHandTransform;

	const FVector HandLoc = HandData.GetHandLocation() - FVector::UpVector * 10.0f;
	DrawDebugCoordinateSystem(GetWorld(), HandLoc, WorldToHandTransform.Rotator(), 15.0f, false, 0.15f, 0, 1.0f);
	DrawDebugDirectionalArrow(GetWorld(), HandLoc, HandLoc + WorldToHandTransform.RotateVector(HandRelative), 10.f, FColor::White, false, 0.15f, 0, 1.0f);

	return WorldToHandTransform.RotateVector(HandRelative);
}

bool APrototype1Character::IsInteracting() const
{
	return LeftHandData.IsInteracting() || RightHandData.IsInteracting();
}

bool APrototype1Character::IsHandInteracting(int HandIndex) const
{
	return IsHandInteracting((HandIndex == 0) ? RightHandData : LeftHandData);
}

bool APrototype1Character::IsHandInteracting(const FHandsContextData& HandData) const
{
	return HandData.IsInteracting();
}

bool APrototype1Character::IsClimbing() const
{
	return LeftHandData.IsInteractClimbing() || RightHandData.IsInteractClimbing();
}

bool APrototype1Character::IsHandClimbing(int HandIndex) const
{
	return IsHandClimbing((HandIndex == 0) ? RightHandData : LeftHandData);
}

bool APrototype1Character::IsHandClimbing(const FHandsContextData& HandData) const
{
	return HandData.IsInteractClimbing();
}

bool APrototype1Character::IsGrabbing() const
{
	return LeftHandData.IsInteractGrabbing() || RightHandData.IsInteractGrabbing();
}

bool APrototype1Character::IsHandGrabbing(int HandIndex) const
{
	return IsHandGrabbing((HandIndex == 0) ? RightHandData : LeftHandData);
}

bool APrototype1Character::IsHandGrabbing(const FHandsContextData& HandData) const
{
	return HandData.IsInteractGrabbing();
}

bool APrototype1Character::CanHandInteract(int HandIndex) const
{
	return CanHandInteract((HandIndex == 0) ? RightHandData : LeftHandData);
}

bool APrototype1Character::CanHandInteract(const FHandsContextData& HandData) const
{
	return HandData.CanInteract;
}

bool APrototype1Character::CanUseYaw(const FRotator& Delta, float LookAxisValue) const
{
	if (IsFreeLooking)
	{
		if (Delta.Yaw > FreeLookYawAngleLimit && LookAxisValue < 0.25f)
		{
			return false;
		}
		if (Delta.Yaw < -FreeLookYawAngleLimit && LookAxisValue > 0.0f)
		{
			return false;
		}
	}

	return true;
}

bool APrototype1Character::CanUsePitch(const FRotator& Delta, float LookAxisValue) const
{
	if (IsFreeLooking)
	{
		if (Delta.Pitch > FreeLookPitchAngleLimit && LookAxisValue > 0.0f)
		{
			return false;
		}
		if (Delta.Pitch < -FreeLookPitchAngleLimit && LookAxisValue < 0.0f)
		{
			return false;
		}
	}

	return true;
}

bool APrototype1Character::CanJumpInternal_Implementation() const
{
	const bool CharacterCanJump = Super::CanJumpInternal_Implementation();
	const bool IsCoyoteTime = CoyoteTimer > 0.f;

	//GEngine->AddOnScreenDebugMessage(22, 2.5f, FColor::Purple, FString::Printf(TEXT("CharacterCanJump: %i - IsCoyoteTimer: %i (%f)"), CharacterCanJump, IsCoyoteTime, CoyoteTimer));

	return CharacterCanJump || IsCoyoteTime;
}

void APrototype1Character::InteractR(const FInputActionValue& Value)
{
	Interact(0);
}

void APrototype1Character::StopInteractR(const FInputActionValue& Value)
{
	RightHandGrabInputBuffer = 0.f;
	StopInteracting(0);
}

void APrototype1Character::InteractL(const FInputActionValue& Value)
{
	Interact(1);
}

void APrototype1Character::StopInteractL(const FInputActionValue& Value)
{
	LeftHandGrabInputBuffer = 0.f;
	StopInteracting(1);
}


// Hand is not ready to grab.
//		const FQuat HandRotation = FQuat::Identity;// GetHandRotation(HandData).Quaternion();
//
//		const FVector SweepTraceStart = TraceEnd;
//		FVector SweepTraceStartFixed = SweepTraceStart;		
//
//		const FVector SweepTraceEnd = ClavicleBoneLocation + ClimberMovementComponent->UpdatedComponent->GetForwardVector() * TraceVerticalExtension * (ArmsLengthUnits);// +ClavicleShoulderLength);
//		FVector SweepTraceEndFixed = SweepTraceEnd;
//		FVector SweepPlaneNormal = -ClimberMovementComponent->UpdatedComponent->GetForwardVector();
//		if (GetWorld()->LineTraceSingleByChannel(HitResult, ClavicleBoneLocation, SweepTraceEnd, ECollisionChannel::ECC_PhysicsBody, QueryParams))
//		{			
//			SweepTraceEndFixed = HitResult.Location;
//			SweepPlaneNormal = HitResult.Normal;	
//		}			
//		
//		const FVector SweepDir = SweepTraceStartFixed - SweepTraceEndFixed;
//		// Add a max distance here, this is for gamefeel, as in, if we are aiming too far from a surface, it wouldn't feel natural to have the game always detect that surface.
//		const FVector SweepDirProjected = FVector::VectorPlaneProject(SweepDir, SweepPlaneNormal);		
//		SweepTraceStartFixed = SweepDirProjected + SweepTraceEndFixed + SweepPlaneNormal * HandData.HandCollisionShape.GetCapsuleRadius() * 2.f; // + Some offset in the normal direction.
//
//#ifdef M_DEBUG_ENABLED
//		if (MDebugHelper::ShouldDrawTraceDebug())
//		{
//			DrawDebugCapsule(GetWorld(), SweepTraceStart, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Red, false, 0.02f, 0, 0.5f);
//			DrawDebugCapsule(GetWorld(), SweepTraceEnd, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Red, false, 0.02f, 0, 0.5f);
//			DrawDebugCapsule(GetWorld(), SweepTraceEndFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Yellow, false, 0.02f, 0, 0.75f);
//			//UKismetSystemLibrary::DrawDebugPlane(GetWorld(), FPlane(SweepTraceEndFixed, SweepPlaneNormal), SweepTraceEndFixed, 100.0f, FLinearColor::White, 0.02f);
//			DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceEndFixed + SweepDir, 1.0f, FColor::Yellow, false, 0.02f, 0, 0.5f);
//			DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceEndFixed + SweepDirProjected, 1.0f, FColor::Green, false, 0.02f, 0, 0.5f);
//		}
//#endif
//
//
//		for (int BinarySweepCount = 0; BinarySweepCount < 3; BinarySweepCount++)
//		{
//			if (GetWorld()->SweepSingleByChannel(HitResult, SweepTraceStartFixed, SweepTraceEndFixed, HandRotation, ECollisionChannel::ECC_PhysicsBody, HandData.HandCollisionShape, QueryParams))
//			{
//				const float AngleDiff = FMath::RadiansToDegrees(FMath::Acos(HitResult.ImpactNormal | SweepPlaneNormal));
//				if (AngleDiff <= 35.0f)
//				{
//#ifdef M_DEBUG_ENABLED
//					if (MDebugHelper::ShouldDrawTraceDebug())
//					{
//						DrawDebugCapsule(GetWorld(), SweepTraceStartFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Blue, false, 0.02f, 0, 1.0f);
//						DrawDebugDirectionalArrow(GetWorld(), SweepTraceStartFixed, SweepTraceStartFixed + SweepPlaneNormal * 10.0f, 1.0f, FColor::Orange, false, 0.02f, 0, 0.75f);
//						GEngine->AddOnScreenDebugMessage(37, 0.02f, FColor::Purple, FString::Printf(TEXT("Angle Diff of Hand Sweep Target ImpactNormal and Plane Normal: %f"), AngleDiff));						
//						DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceStartFixed, 1.0f, FColor::Green, false, 0.02f, 0, 1.0f);
//						DrawDebugCapsule(GetWorld(), HitResult.Location, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Green, false, 0.02f, 0, 0.5f);
//						DrawDebugDirectionalArrow(GetWorld(), HitResult.Location, HitResult.Location + HitResult.Normal * 10.0f, 1.0f, FColor::Green, false, 0.02f, 0, 0.75f);
//						DrawDebugCapsule(GetWorld(), HitResult.ImpactPoint, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Purple, false, 0.02f, 0, 1.0f);
//						DrawDebugDirectionalArrow(GetWorld(), HitResult.ImpactPoint, HitResult.ImpactPoint + HitResult.ImpactNormal * 10.0f, 1.0f, FColor::Purple, false, 0.02f, 0, 0.75f);
//					}
//#endif
//
//					// Hand is ready to grab.
//
//					HandData.CurrentFrameTracedHitResult = HitResult;
//					HandData.CanInteract = true;
//
//					// Now we check for the input buffer in case we have a pending input on this hand
//					float& HandInputBuffer = (HandData.HandIndex == 0) ? RightHandGrabInputBuffer : LeftHandGrabInputBuffer;
//					if (HandInputBuffer > 0.f)
//					{
//						GEngine->AddOnScreenDebugMessage(40, 5.f, FColor::Yellow, TEXT("Triggering Grab from Input Buffer as we have a valid trace!"));
//						Interact(HandData.HandIndex);
//						HandInputBuffer = 0.f;
//					}
//					return;
//				}
//#ifdef M_DEBUG_ENABLED
//				else if (MDebugHelper::ShouldDrawTraceDebug())
//				{
//					DrawDebugCapsule(GetWorld(), SweepTraceStartFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Red, false, 0.02f, 0, 0.25f);
//					DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceStartFixed, 1.0f, FColor::Red, false, 0.02f, 0, 0.25f);
//				}
//#endif
//			}
//#ifdef M_DEBUG_ENABLED
//			else if (MDebugHelper::ShouldDrawTraceDebug())
//			{
//				DrawDebugCapsule(GetWorld(), SweepTraceStartFixed, HandData.HandCollisionShape.GetCapsuleHalfHeight(), HandData.HandCollisionShape.GetCapsuleRadius(), HandRotation, FColor::Magenta, false, 0.02f, 0, 0.25f);
//				DrawDebugDirectionalArrow(GetWorld(), SweepTraceEndFixed, SweepTraceStartFixed, 1.0f, FColor::Yellow, false, 0.02f, 0, 0.25f);
//			}
//#endif
//
//			SweepTraceStartFixed = UKismetMathLibrary::VLerp(SweepTraceStartFixed, SweepTraceStart, 0.5f);	// Binary lerp.	
//		}
