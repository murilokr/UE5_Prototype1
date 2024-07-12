// Copyright Epic Games, Inc. All Rights Reserved.

#include "Prototype1Character.h"
#include "Prototype1Projectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include <Kismet/KismetSystemLibrary.h>
#include <Kismet/KismetMathLibrary.h>

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

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
	Mesh1P->SetupAttachment(MeshPivot);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeLocation(FVector(10.f, 0.f, -180.82879f));
	//Mesh1P->SetRelativeRotation(FRotator(0.9f, -19.19f, 5.2f));
	//Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));
	//Mesh1P->SetRelativeLocation(FVector(0.f, 0.f, -121.414395f));

	// Create a CameraComponent
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(Mesh1P);
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 180.82879f)); // Position the camera
	//FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
	//FirstPersonCameraComponent->SetRelativeLocation(FVector((40.881380f, 0.f, 60.f)); // my overriden values.
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	JointTarget_ElbowL = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JointTarget_ElbowL"));
	JointTarget_ElbowL->SetupAttachment(Mesh1P);
	JointTarget_ElbowL->SetRelativeLocation(FVector(-300.f, -1000.f, 0.f));
	JointTarget_ElbowL->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
	JointTarget_ElbowR = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JointTarget_ElbowR"));
	JointTarget_ElbowR->SetupAttachment(Mesh1P);
	JointTarget_ElbowR->SetRelativeLocation(FVector(-300.f, 1000.f, 0.f));
	JointTarget_ElbowR->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
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

	RightHandData.HandIndex = 0;
	LeftHandData.HandIndex = 1;

	LeftHandData.LocalHandIdleLocation = LeftHandIdlePositionLocal;
	LeftHandData.LocalHandIdleRotation = LeftHandIdleRotationLocal;

	RightHandData.LocalHandIdleLocation = RightHandIdlePositionLocal;
	RightHandData.LocalHandIdleRotation = RightHandIdleRotationLocal;

	SetElbowSetup(0, ESETUP_Idle);
	SetElbowSetup(1, ESETUP_Idle);

	// Since we start on the ground, default movement mode will be Walking.
	ClimberMovementComponent->SetMovementMode(EMovementMode::MOVE_Walking);

	/** Caching some "heavy" calculations that will be used everyframe */
	ArmsLengthUnitsSquared = FMath::Square(ArmsLengthUnits);

	// Get Shoulder-Clavicle Length (Taking reference from right arm, since left/right should be the same, under a certain error margin).
	const FVector ClavicleBoneLocation = Mesh1P->GetBoneLocation(FName("clavicle_r"));
	const FVector ShoulderBoneLocation = Mesh1P->GetBoneLocation(FName("upperarm_r"));
	ClavicleShoulderLength = (ShoulderBoneLocation - ClavicleBoneLocation).Length() * ClavicleShoulderLengthMultiplier;
}

//////////////////////////////////////////////////////////////////////////// Input

void APrototype1Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APrototype1Character::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APrototype1Character::Look);
		EnhancedInputComponent->BindAction(FreeLookAction, ETriggerEvent::Started, this, &APrototype1Character::BeginFreeLook);
		EnhancedInputComponent->BindAction(FreeLookAction, ETriggerEvent::Completed, this, &APrototype1Character::EndFreeLook);

		// Grabbing
		EnhancedInputComponent->BindAction(GrabActionL, ETriggerEvent::Started, this, &APrototype1Character::GrabL);
		EnhancedInputComponent->BindAction(GrabActionL, ETriggerEvent::Completed, this, &APrototype1Character::StopGrabL);
		EnhancedInputComponent->BindAction(GrabActionR, ETriggerEvent::Started, this, &APrototype1Character::GrabR);
		EnhancedInputComponent->BindAction(GrabActionR, ETriggerEvent::Completed, this, &APrototype1Character::StopGrabR);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void APrototype1Character::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	InterpHandsAndElbow(0, DeltaSeconds);
	InterpHandsAndElbow(1, DeltaSeconds);

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

	if (Controller != nullptr)
	{
		// add movement 
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

void APrototype1Character::ReleaseHand(int HandIndex)
{
	StopGrabbing(HandIndex);
}

void APrototype1Character::ReleaseHand(const FHandsContextData& HandData)
{
	StopGrabbing(HandData.HandIndex);
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
	IsFreeLooking = true;
	//FreeLookControlRotation = GetControlRotation();
	bUseControllerRotationYaw = false;
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
}

void APrototype1Character::EndFreeLook(const FInputActionValue& Value)
{
	// Reset camera to ControlRotation.
	/*if (AController* MyController = GetController())
	{
		MyController->SetControlRotation(FreeLookControlRotation);
	}*/

	IsFreeLooking = false;
	bUseControllerRotationYaw = true;

	FirstPersonCameraComponent->SetRelativeRotation(FQuat::Identity);
	FirstPersonCameraComponent->bUsePawnControlRotation = false;
}

void APrototype1Character::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// If we are grabbing something, send mouse input to hands instead.
	// But if we are looking around, ignore sending data to hands.
	if (IsGrabbing() && !IsFreeLooking)
	{
		MoveHand(LeftHandData, LookAxisVector);
		MoveHand(RightHandData, LookAxisVector);
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

void APrototype1Character::MoveHand(FHandsContextData& HandData, FVector2D LookAxisVector)
{
	if (!HandData.IsGrabbing)
	{
		return;
	}

	const FVector HandLocation = HandData.GetHandLocation();
	const FVector HandNormal = HandData.GetHandNormal();
	const FVector HandRelativeUp = FVector::VectorPlaneProject(-FirstPersonCameraComponent->GetUpVector(), HandNormal);
	const FRotator GrabRot = FRotationMatrix::MakeFromXZ(HandNormal, HandRelativeUp).Rotator();

	// MoveDir is negated from MouseInput, because Mouse movement is set to INVERTED. Might want to add a check here if I plan on adding mouse settings later.
	const FVector MouseInput = GrabRot.RotateVector(FVector(0, LookAxisVector.X, LookAxisVector.Y));
	const FVector MoveDir = -MouseInput;
	ClimberMovementComponent->HandMoveDir += MoveDir;

	// Debugs
	GEngine->AddOnScreenDebugMessage(0, 2.5f, FColor::Yellow, FString::Printf(TEXT("%s"), *MouseInput.ToString()));
	GEngine->AddOnScreenDebugMessage(1, 2.5f, FColor::Blue, FString::Printf(TEXT("%s"), *MoveDir.ToString()));

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


	// TODO: This might get used to add "Fake" anchor objects. (Objects that will fall once you grab them, to add risks)
	//const FVector GrabTargetPosition = HandData.GetGrabPosition(TraceStart, TraceDir);

	// When we want to GRAB and MOVE an object. do this. Also consider adding a PhysicsHandle instead, if the object is grabbable.
	//const float DragForce = 500.0f;
	//const FVector MoveObjectVectorWithForce = (GrabTargetPosition - HandLocation) * (DragForce / DeltaSeconds);
}

void APrototype1Character::Grab(int HandIndex)
{
	FHandsContextData& HandData = (HandIndex == 0) ? RightHandData : LeftHandData;
	if (HandData.IsGrabbing)
	{
		// Something went wrong.
		return;
	}

	// Calculating VerticalExtension that goes from 1 to 2. This is to increase a bit on the trace distance if looking upwards.
	const float TraceVerticalExtension = FMath::Max(1 + (FVector::UpVector | FirstPersonCameraComponent->GetForwardVector()), 1.f);
	GEngine->AddOnScreenDebugMessage(5, 2.5f, FColor::Yellow, FString::Printf(TEXT("Trace Vertical Extension: %f"), TraceVerticalExtension));

	const FName ClavicleBone = (HandIndex == 0) ? FName("clavicle_r") : FName("clavicle_l");
	const FVector ClavicleBoneLocation = Mesh1P->GetBoneLocation(ClavicleBone);
	const FVector TraceStart = ClavicleBoneLocation + FirstPersonCameraComponent->GetForwardVector();
	const FVector TraceEnd = ClavicleBoneLocation + FirstPersonCameraComponent->GetForwardVector() * TraceVerticalExtension * (ArmsLengthUnits + ClavicleShoulderLength);
	const FVector TraceDir = TraceEnd - TraceStart;
	const float TraceLength = TraceDir.SquaredLength();

	// You can use FCollisionQueryParams to further configure the query
	// Here we add ourselves to the ignored list so we won't block the trace
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult HitResult;
	GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECollisionChannel::ECC_PhysicsBody, QueryParams);
	//DrawDebugLine(GetWorld(), TraceStart, TraceEnd, HitResult.bBlockingHit ? FColor::Blue : FColor::Red, false, 2.5f, 0, 1.25f);

	DrawDebugLine(GetWorld(), TraceStart, TraceEnd, (HitResult.bBlockingHit) ? FColor::Green : FColor::Red, false, 5.0f, 0, 1.0f);

	// Nothing was hit.
	if (!HitResult.bBlockingHit)
	{
		return;
	}

	SetElbowSetup(HandIndex, ESETUP_Climbing);

	HandData.IsGrabbing = true;
	HandData.HitActor = HitResult.GetActor();
	HandData.HitComponent = HitResult.GetComponent();
	HandData.HitBoneName = HitResult.BoneName;

	// Calculate GrabPositionT
	//HandData.GrabPositionT = 0.0f;
	//const FVector PositionDir = HitResult.Location - TraceStart;
	//if ((TraceDir | PositionDir) >= 0.0f)
	//{
	//	// HitResult.Location is inline with TraceDir.
	//	HandData.GrabPositionT = FMath::Clamp(PositionDir.SquaredLength() / TraceLength, 0.0f, 1.0f);
	//}

	FTransform HitBoneWorldToLocalTransform = HitResult.GetActor()->GetActorTransform(); //HitResult.Component->GetSocketTransform(HitResult.BoneName).Inverse();
	HandData.LocalHandLocation = HitBoneWorldToLocalTransform.InverseTransformPosition(HitResult.Location);
	HandData.LocalHandNormal = HitBoneWorldToLocalTransform.InverseTransformVector(HitResult.Normal);

	ClimberMovementComponent->SetHandGrabbing(HandData);

	OnStartGrab(HandData, HandIndex);
}

void APrototype1Character::StopGrabbing(int HandIndex)
{
	FHandsContextData& HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	if (!HandData.IsGrabbing)
	{
		// We weren't grabbing anything.
		return;
	}

	SetElbowSetup(HandIndex, ESETUP_Idle);

	HandData.IsGrabbing = false;

	ClimberMovementComponent->ReleaseHand(HandData);

	OnEndGrab(HandData, HandIndex);
}

void APrototype1Character::SetElbowSetup(const int HandIndex, const EElbowSetupType& ElbowSetupType)
{
	float& TargetElbowLerpTime = (HandIndex == 0) ? RightArmLerpTime : LeftArmLerpTime;
	FElbowSetup& TargetElbowSetup = (HandIndex == 0) ? CurrentRightArmSetup : CurrentLeftArmSetup;

	for (const FElbowSetup& ElbowSetup : ElbowsSetups)
	{
		if (ElbowSetup.SetupType == ElbowSetupType)
		{
			// We can get the desired location from the setup.
			TargetElbowSetup = ElbowSetup;

			TargetElbowLerpTime = ElbowSetup.LerpInDuration;
		}
	}
}

void APrototype1Character::InterpHandsAndElbow(const int HandIndex, float DeltaSeconds)
{
	float& TargetArmLerpTime = (HandIndex == 0) ? RightArmLerpTime : LeftArmLerpTime;

	if (TargetArmLerpTime > 0.f)
	{
		FElbowSetup TargetArmSetup = (HandIndex == 0) ? CurrentRightArmSetup : CurrentLeftArmSetup;
		for (const FElbowSetup& ElbowSetup : ElbowsSetups)
		{
			if (ElbowSetup.SetupType == TargetArmSetup.SetupType)
			{
				UStaticMeshComponent* ElbowComponent = (HandIndex == 0) ? JointTarget_ElbowR : JointTarget_ElbowL;

				TargetArmLerpTime = FMath::Max(TargetArmLerpTime - DeltaSeconds, 0.f);
				const float TimeNormalized = 1.f - (TargetArmLerpTime / ElbowSetup.LerpInDuration);

				const FVector TargetElbowLocation = (HandIndex == 0) ? ElbowSetup.RightElbowRelativeLocation : ElbowSetup.LeftElbowRelativeLocation;
				const FVector ElbowLocation = FMath::Lerp(ElbowComponent->GetRelativeLocation(), TargetElbowLocation, TimeNormalized);
				ElbowComponent->SetRelativeLocation(ElbowLocation);
			}
		}
	}
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

FVector APrototype1Character::CalculateArmConstraint(int HandIndex, float DeltaSeconds, const FVector& BodyOffset, bool& OutIsOverstretched, FVector& RootDeltaFix)
{
	return CalculateArmConstraint(GetMutableHandData(HandIndex), DeltaSeconds, BodyOffset, OutIsOverstretched, RootDeltaFix);
}

FVector APrototype1Character::CalculateArmConstraint(FHandsContextData& HandData, float DeltaSeconds, const FVector& BodyOffset, bool& OutIsOverstretched, FVector& RootDeltaFix)
{
	RootDeltaFix = FVector::ZeroVector;

	FVector HandLocation = GetSafeHandLocation(HandData);
	const FVector HandNormal = HandData.GetHandNormal();
	const FVector HandRelativeUp = FVector::VectorPlaneProject(FirstPersonCameraComponent->GetUpVector(), HandNormal).GetSafeNormal();

	const FVector ShoulderOffset = Mesh1P->GetBoneLocation(HandData.ShoulderBoneName) + BodyOffset;

	FVector OutArmVector = ShoulderOffset - HandLocation;
	const FVector ArmVectorProjected = FVector::VectorPlaneProject(OutArmVector, HandNormal).GetSafeNormal();

	// Check if we need to apply Stretch Multiplier
	const float Angle = FMath::RadiansToDegrees(FMath::Asin(ArmVectorProjected | HandRelativeUp));
	const float AngleNormalized = (Angle - ArmStretchMultiplierMinAngle) / (ArmStretchMultiplierMaxAngle - ArmStretchMultiplierMinAngle);
	const float StretchMultiplier = FMath::Max(1 + (ArmStretchMultiplierCurve->GetFloatValue(AngleNormalized) * (ArmStretchMultiplier - 1)), 1.0f);

	// Check if arm is stretched with added multiplier.
	const float ArmMaxRelaxedLength = FMath::Square(ArmsLengthUnits * StretchMultiplier);
	float ArmCurrentLength = OutArmVector.SizeSquared();
	const float StretchRatio = FMath::Max((ArmCurrentLength - ArmMaxRelaxedLength) / ArmMaxRelaxedLength, 0.0f);
	OutIsOverstretched = ArmCurrentLength >= ArmMaxRelaxedLength;

	GEngine->AddOnScreenDebugMessage(6, 0.1f, FColor::Emerald, FString::Printf(TEXT("Stretch Ratio: %f"), StretchRatio));
	//GEngine->AddOnScreenDebugMessage(6, 0.1f, FColor::Emerald, FString::Printf(TEXT("Angle: %f (n: %f) - Stretch Multiplier: %f - Initial Arms Length: %f - Final Arms Length: %f"), 
		//Angle, AngleNormalized, StretchMultiplier, ArmsLengthUnits, ArmsLengthUnits * StretchMultiplier));

	// If Arm is Overstretched, then we'll want to move the root so as to get the shoulder in the correct position such as ArmVector.Length() == ArmsLengthUnitsSquared
	if (OutIsOverstretched)
	{
		const FVector FixedArmVector = OutArmVector.GetSafeNormal() * ArmsLengthUnits * StretchMultiplier;
		//DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + FixedArmVector, 1.0f, FColor::Green, false, 0.25f, 0, 0.5f);

		if (TryToSlipHand(HandData, FixedArmVector, StretchRatio, DeltaSeconds))
		{
			HandLocation = GetSafeHandLocation(HandData);
			// TODO: Need to find a way to have a hand grip strength to drive how much we slip vs how much we compensate by the overstretching.
		}

		const FVector RootLocation = ClimberMovementComponent->UpdatedComponent->GetComponentLocation() + BodyOffset;
		const FVector ShoulderRootDir = RootLocation - ShoulderOffset;
		//DrawDebugDirectionalArrow(GetWorld(), ShoulderOffset, RootLocation, 1.0f, FColor::Yellow, false, 0.25f, 0, 0.5f);

		// ArmDiff is where the shoulder SHOULD be to fix overstretching.
		const FVector ArmDiff = HandLocation + FixedArmVector - ShoulderOffset;
		//DrawDebugDirectionalArrow(GetWorld(), ShoulderOffset, ShoulderOffset + ArmDiff, 1.0f, FColor::Purple, false, 0.25f, 0, 1.0f);

		// RootDelta is where the root should be to fix the shoulder, so this is our final fix vector
		RootDeltaFix = (ShoulderOffset + ArmDiff + ShoulderRootDir) - RootLocation;
		//DrawDebugDirectionalArrow(GetWorld(), RootLocation, RootLocation + RootDeltaFix, 1.0f, FColor::Blue, false, 0.25f, 0, 1.0f);
	}

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

	const FVector HandSlipVector = -HandRelativeUp * (1 + SlipRatio);
	DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + HandSlipVector, 1.0f, FColor::Red, false, 1.25f, 0, 0.5f);
	
	GEngine->AddOnScreenDebugMessage(16, 0.5f, FColor::Yellow, TEXT("SLIPPING!"));
	GEngine->AddOnScreenDebugMessage(17, 0.5f, FColor::Emerald, FString::Printf(TEXT("Slip Vector Length: %f - Arm Vector Length: %f (%f)"),
		HandSlipVector.Size(), ProjectedArmVector.Size(), ArmVector.Size()));

	const float HandPhysicalHeight = 9.635022f;
	const float HandPhysicalRadius = 6.519027f;

	DrawDebugCapsule(GetWorld(), HandLocation, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Yellow, false, 1.25f, 0, 1.0f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	//// We need to add a trace here, to make sure that when we slip to the vector direction, we don't end up where there isn't anything to hold on to.
	FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(HandPhysicalRadius, HandPhysicalHeight);
	TArray<FHitResult> HitResults;
	GetWorld()->SweepMultiByChannel(HitResults, HandLocation + HandSlipVector, HandLocation, HandRotation, ECollisionChannel::ECC_PhysicsBody, CapsuleShape, QueryParams);

	FHitResult BestHitResult;
	bool bHadValidBlockingHit = false;
	for (const FHitResult& HR : HitResults)
	{
		if (HR.bBlockingHit)
		{
			if (HR.GetActor() == HandData.HitActor)
			{
				BestHitResult = HR;
				bHadValidBlockingHit = true;
				DrawDebugCapsule(GetWorld(), HR.ImpactPoint, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Green, false, 1.25f, 0, 1.0f);
				DrawDebugCapsule(GetWorld(), HandLocation + HandSlipVector, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Blue, false, 1.25f, 0, 1.0f);
				break;
			}
		}
	}

	if (bHadValidBlockingHit)
	{
		const FVector MoveDelta = HandLocation + HandSlipVector;
		FHitResult MoveDeltaHitResult;
		GetWorld()->SweepSingleByChannel(MoveDeltaHitResult, MoveDelta, MoveDelta * 1.01f, HandRotation, ECollisionChannel::ECC_PhysicsBody, CapsuleShape, QueryParams);
		if (!MoveDeltaHitResult.bBlockingHit)
		{
			UE_LOG(LogTemp, Error, TEXT("[APrototype1Character::TryToSlipHand] Something went incredibly wrong here."));
			GEngine->AddOnScreenDebugMessage(18, 5.f, FColor::Red, TEXT("[APrototype1Character::TryToSlipHand] Something went incredibly wrong here."));
			return false;
		}

		DrawDebugCapsule(GetWorld(), MoveDeltaHitResult.ImpactPoint, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Purple, false, 1.25f, 0, 1.0f);

		FTransform HandLocalToWorldTransform = HandData.HitActor->GetActorTransform(); //HitActor should remain the same.
		HandData.LocalHandLocation = HandLocalToWorldTransform.InverseTransformPosition(MoveDelta);
		HandData.LocalHandNormal = HandLocalToWorldTransform.InverseTransformVector(MoveDeltaHitResult.ImpactNormal);
		return true;
	}
	
	DrawDebugCapsule(GetWorld(), HandLocation + HandSlipVector, HandPhysicalHeight, HandPhysicalRadius, HandRotation, FColor::Red, false, 1.25f, 0, 1.0f);
	// TODO:
	// Maybe do an extra check on the stretchratio to see if the hands just let go?
	// Or maybe have a hand resistance buffer, that gets added to each frame we slip the hand, and once it reaches a threshold, we let go.
	return false;
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
	if (!HandData.IsGrabbing)
	{
		const FTransform MeshToWorld = Mesh1P->GetComponentToWorld();
		return MeshToWorld.TransformPosition(HandData.LocalHandIdleLocation);
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
	if (!HandData.IsGrabbing)
	{
		const FTransform MeshToWorld = Mesh1P->GetComponentTransform();
		return MeshToWorld.TransformRotation(HandData.LocalHandIdleRotation.Quaternion()).Rotator();
		//return HandData.LocalHandIdleRotation;
	}

	// Flip RightHand only.
	const FVector HandRelativeUp = FVector::VectorPlaneProject(-FirstPersonCameraComponent->GetUpVector(), HandData.GetHandNormal());
	return HandData.GetHandRotation(HandData.HandIndex == 0, HandRelativeUp);
}

FVector APrototype1Character::RotateToHand(const FHandsContextData& HandData, const FVector& WorldRelative) const
{
	const FVector HandRelativeUp = FVector::VectorPlaneProject(-FirstPersonCameraComponent->GetUpVector(), HandData.GetHandNormal());
	FQuat HandToWorldTransform = FQuat::FindBetweenNormals(FVector::UpVector, -HandRelativeUp).Inverse();

	return HandToWorldTransform.RotateVector(WorldRelative);
}

FVector APrototype1Character::RotateToWorld(const FHandsContextData& HandData, const FVector& HandRelative) const
{
	const FVector HandRelativeUp = FVector::VectorPlaneProject(-FirstPersonCameraComponent->GetUpVector(), HandData.GetHandNormal());
	FQuat WorldToHandTransform = FQuat::FindBetweenNormals(FVector::UpVector, -HandRelativeUp);

	return WorldToHandTransform.RotateVector(HandRelative);
}

bool APrototype1Character::IsGrabbing() const
{
	return LeftHandData.IsGrabbing || RightHandData.IsGrabbing;
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

void APrototype1Character::GrabR(const FInputActionValue& Value)
{
	Grab(0);
}

void APrototype1Character::StopGrabR(const FInputActionValue& Value)
{
	StopGrabbing(0);
}

void APrototype1Character::GrabL(const FInputActionValue& Value)
{
	Grab(1);
}

void APrototype1Character::StopGrabL(const FInputActionValue& Value)
{
	StopGrabbing(1);
}

FVector FHandsContextData::GetHandLocation() const
{
	if (!HitActor)
	{
		return FVector();
	}

	FTransform HitBoneLocalToWorldTransform = HitActor->GetActorTransform();
	return HitBoneLocalToWorldTransform.TransformPosition(LocalHandLocation);
}

FVector FHandsContextData::GetHandNormal() const
{
	if (!HitActor)
	{
		return FVector();
	}

	FTransform HitBoneLocalToWorldTransform = HitActor->GetActorTransform();
	return HitBoneLocalToWorldTransform.TransformVector(LocalHandNormal);
}

FRotator FHandsContextData::GetHandRotation(bool bShouldFlip, const FVector RelativeUp) const
{
	FVector HandNormal = GetHandNormal();
	
	const FRotator GrabRot = FRotationMatrix::MakeFromXZ(HandNormal, RelativeUp).Rotator();
	FVector FixedYAxis = GrabRot.RotateVector(-FVector::YAxisVector);

	// Invert the normal.
	if (bShouldFlip)
	{	
		HandNormal = -HandNormal;
	}
	
	const FRotator HandRotation = FRotationMatrix::MakeFromYZ(HandNormal, FixedYAxis).Rotator();//FRotationMatrix::MakeFromY(HandNormal).Rotator();

	// Debug
	DrawDebugCoordinateSystem(GEngine->GetWorld(), GetHandLocation(), HandRotation, 10.0f, false, -1.0f, 0, 1.0f);

	// Hand bones are oriented towards Y, which is why we don't get OrientationVector.
	return HandRotation;
}

FVector FHandsContextData::GetGrabPosition(const FVector TraceStart, const FVector TraceDir) const
{
	return TraceStart + TraceDir * GrabPositionT;
}