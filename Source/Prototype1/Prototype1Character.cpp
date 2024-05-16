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
#include "GameFramework/CharacterMovementComponent.h"
#include <Kismet/KismetMathLibrary.h>

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// APrototype1Character

APrototype1Character::APrototype1Character()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	CapsuleComponentCollisionType = GetCapsuleComponent()->GetCollisionEnabled();

	// Create a CapsuleComponent
	ClimbingCollider = CreateDefaultSubobject<UCapsuleComponent>(TEXT("ClimbingCollider"));
	ClimbingCollider->SetupAttachment(GetCapsuleComponent());
	ClimbingCollider->InitCapsuleSize(55.f, 55.f); // Actually a sphere, not a capsule :P
	ClimbingCollider->SetRelativeLocation(FVector(0.f, 0.f, 41.f));
	ClimbingCollider->SetCollisionObjectType(ECollisionChannel::ECC_Pawn);

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(GetCapsuleComponent());
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	//Mesh1P->SetRelativeRotation(FRotator(0.9f, -19.19f, 5.2f));
	//Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));
	Mesh1P->SetRelativeLocation(FVector(0.f, 0.f, -121.414395f));

	// Create a CameraComponent
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(Mesh1P);
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
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

	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Custom, 0);
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

void APrototype1Character::BeginFreeLook(const FInputActionValue& Value)
{
	IsFreeLooking = true;
	bUseControllerRotationYaw = false;
}

void APrototype1Character::EndFreeLook(const FInputActionValue& Value)
{
	IsFreeLooking = false;
	bUseControllerRotationYaw = true;
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
	const FRotator GrabRot = FRotationMatrix::MakeFromXY(HandNormal, FirstPersonCameraComponent->GetRightVector()).Rotator();

	const FVector MouseMove = FVector(0, LookAxisVector.X, LookAxisVector.Y);
	const FVector GrabTargetPosition = HandLocation + GrabRot.RotateVector(MouseMove);
	GEngine->AddOnScreenDebugMessage(0, 2.5f, FColor::Yellow, FString::Printf(TEXT("%s"), *MouseMove.ToString()));
	GEngine->AddOnScreenDebugMessage(1, 2.5f, FColor::Green, FString::Printf(TEXT("%s"), *GrabTargetPosition.ToString()));

	// Debugs
	/** GrabRot relative to HandLocation */
	DrawDebugCoordinateSystem(GetWorld(), HandLocation, GrabRot, 10.0f, false, -1.0f, 0, 1.0f);

	/** HandLocation */
	DrawDebugSphere(GetWorld(), HandLocation, 50.0f, 6, FLinearColor(1.0f, 0.39f, 0.87f, 1.0f).ToFColorSRGB());

	/** HandNormal pointing outwards from HandLocation */
	DrawDebugDirectionalArrow(GetWorld(), HandLocation, HandLocation + (HandNormal * 12.0f), 1.0f, FLinearColor(0.18f, 0.57f, 1.0f, 1.0f).ToFColorSRGB(), false, -1.0f, 0, 1.0f);

	/** Arrow pointing from HandLocation to GrabTargetPosition */
	DrawDebugDirectionalArrow(GetWorld(), HandLocation, GrabTargetPosition, 1.0f, FLinearColor(0.46f, 1.0f, 0.15f, 1.0f).ToFColorSRGB(), false, -1.0f, 0, 1.0f);
	// End of Debugs

	// This effectively only moves the Hand visual.
	//LHCollision.SetRelativeLocation(HandData.LocalHitLocation);

	const FVector DragOffset = HandLocation - GrabTargetPosition;
	//GetMovementComponent()->AddInputVector(DragOffset); // Acceleration.
	GetMovementComponent()->Velocity += DragOffset; // Velocity
	//SetActorLocation(GetActorLocation() + DragOffset);


	// TODO Optimize
	/*const float MinSearchDist = 0.2125 * 100.0f;
	const float MaxSearchDist = 2.0f * 100.0f;
	const FVector TraceStart = FirstPersonCameraComponent->GetComponentLocation() + FirstPersonCameraComponent->GetForwardVector() * FVector(MinSearchDist);
	const FVector TraceEnd = FirstPersonCameraComponent->GetComponentLocation() + FirstPersonCameraComponent->GetForwardVector() * FVector(MaxSearchDist);
	const FVector TraceDir = TraceEnd - TraceStart;*/
	// End of TODO Optimize
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

	const float MinSearchDist = 0.2125 * 100.0f;
	const float MaxSearchDist = 2.0f * 100.0f;
	const FVector TraceStart = FirstPersonCameraComponent->GetComponentLocation() + FirstPersonCameraComponent->GetForwardVector() * FVector(MinSearchDist);
	const FVector TraceEnd = FirstPersonCameraComponent->GetComponentLocation() + FirstPersonCameraComponent->GetForwardVector() * FVector(MaxSearchDist);
	const FVector TraceDir = TraceEnd - TraceStart;
	const float TraceLength = TraceDir.SquaredLength();

	// You can use FCollisionQueryParams to further configure the query
	// Here we add ourselves to the ignored list so we won't block the trace
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult HitResult;
	GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECollisionChannel::ECC_PhysicsBody, QueryParams);
	//DrawDebugLine(GetWorld(), TraceStart, TraceEnd, HitResult.bBlockingHit ? FColor::Blue : FColor::Red, false, 2.5f, 0, 1.25f);

	// Nothing was hit.
	if (!HitResult.bBlockingHit)
	{
		return;
	}

	HandData.IsGrabbing = true;
	HandData.HitResult = HitResult;

	// Calculate GrabPositionT
	HandData.GrabPositionT = 0.0f;
	const FVector PositionDir = HitResult.Location - TraceStart;
	if ((TraceDir | PositionDir) >= 0.0f)
	{
		// HitResult.Location is inline with TraceDir.
		HandData.GrabPositionT = FMath::Clamp(PositionDir.SquaredLength() / TraceLength, 0.0f, 1.0f);
	}

	const FTransform HitBoneWorldToLocalTransform = HitResult.Component->GetSocketTransform(HitResult.BoneName).Inverse();
	HandData.LocalHandLocation = HitBoneWorldToLocalTransform.TransformPosition(HitResult.Location);
	HandData.LocalHandNormal = HitBoneWorldToLocalTransform.TransformVectorNoScale(HitResult.Normal);

	// Disable Root CapsuleComponent collision and enable ClimbingCollider if other hand is not grabbing.
	FHandsContextData OtherHand = (HandIndex == 0) ? LeftHandData : RightHandData;
	if (!OtherHand.IsGrabbing)
	{
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ClimbingCollider->SetCollisionEnabled(CapsuleComponentCollisionType);
	}


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

	HandData.IsGrabbing = false;

	// Re-enable Root CapsuleComponent collision and disable ClimbingCollider if other hand is not grabbing.
	FHandsContextData OtherHand = (HandIndex == 0) ? LeftHandData : RightHandData;
	if (!OtherHand.IsGrabbing)
	{
		GetCapsuleComponent()->SetCollisionEnabled(CapsuleComponentCollisionType);
		ClimbingCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	OnStartGrab(HandData, HandIndex);
}

const FVector APrototype1Character::GetHandLocation(int HandIndex)
{
	FHandsContextData HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	return HandData.GetHandLocation() + HandData.GetHandNormal() * HandSafeZone;
}

const FVector APrototype1Character::GetHandNormal(int HandIndex)
{
	FHandsContextData HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	return HandData.GetHandNormal();
}

const FRotator APrototype1Character::GetHandRotation(int HandIndex)
{
	FHandsContextData HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	// Flip RightHand only.
	return HandData.GetHandRotation(HandIndex == 0, FirstPersonCameraComponent->GetRightVector());
}

const bool APrototype1Character::IsGrabbing()
{
	return LeftHandData.IsGrabbing || RightHandData.IsGrabbing;
}

bool APrototype1Character::CanUseYaw(const FRotator& Delta, float LookAxisValue)
{
	if (IsFreeLooking)
	{
		if (Delta.Yaw > FreeLookAngleLimit && LookAxisValue < 0.0f)
		{
			return false;
		}
		if (Delta.Yaw < -FreeLookAngleLimit && LookAxisValue > 0.0f)
		{
			return false;
		}
	}

	return true;
}

bool APrototype1Character::CanUsePitch(const FRotator& Delta, float LookAxisValue)
{
	if (IsFreeLooking)
	{
		if (Delta.Pitch > FreeLookAngleLimit && LookAxisValue > 0.0f)
		{
			return false;
		}
		if (Delta.Pitch < -FreeLookAngleLimit && LookAxisValue < 0.0f)
		{
			return false;
		}
	}

	return true;
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

FVector FHandsContextData::GetHandLocation()
{
	if (!HitResult.Component.IsValid())
	{
		return FVector();
	}

	const FTransform HitBoneLocalToWorldTransform = HitResult.Component->GetSocketTransform(HitResult.BoneName);

	return HitBoneLocalToWorldTransform.TransformPosition(LocalHandLocation);
}

FVector FHandsContextData::GetHandNormal()
{
	if (!HitResult.Component.IsValid())
	{
		return FVector();
	}

	const FTransform HitBoneLocalToWorldTransform = HitResult.Component->GetSocketTransform(HitResult.BoneName);

	return HitBoneLocalToWorldTransform.TransformVectorNoScale(LocalHandNormal);
}

FRotator FHandsContextData::GetHandRotation(bool bShouldFlip, const FVector CameraRight)
{
	FVector HandNormal = GetHandNormal();
	
	const FRotator GrabRot = FRotationMatrix::MakeFromXY(HandNormal, CameraRight).Rotator();
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

FVector FHandsContextData::GetGrabPosition(const FVector TraceStart, const FVector TraceDir)
{
	return TraceStart + TraceDir * GrabPositionT;
}