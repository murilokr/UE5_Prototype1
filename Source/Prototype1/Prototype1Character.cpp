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

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// APrototype1Character

APrototype1Character::APrototype1Character()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
		
	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	//Mesh1P->SetRelativeRotation(FRotator(0.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));

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

void APrototype1Character::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// If we are grabbing something, send mouse input to hands instead.
	if (IsGrabbing()) //|| IsHoldingAlt())
	{
		MoveHand(LeftHandData, LookAxisVector);
		MoveHand(RightHandData, LookAxisVector);
		return;
	}

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void APrototype1Character::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void APrototype1Character::MoveHand(FHandsContextData& HandData, FVector2D LookAxisVector)
{
	if (!HandData.IsGrabbing)
	{
		return;
	}

	const FVector HandLocation = HandData.GetHitLocation();
	const FVector HandNormal = HandData.GetHitNormal();
	//const FRotator GrabRot = HandNormal.ToOrientationRotator();
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
	SetActorLocation(GetActorLocation() + DragOffset);


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
	if (TraceDir.Dot(PositionDir) >= 0.0f)
	{
		// HitResult.Location is inline with TraceDir.
		HandData.GrabPositionT = FMath::Clamp(PositionDir.SquaredLength() / TraceLength, 0.0f, 1.0f);
	}

	const FTransform HitBoneWorldToLocalTransform = HitResult.Component->GetSocketTransform(HitResult.BoneName).Inverse();
	HandData.LocalHitLocation = HitBoneWorldToLocalTransform.TransformPosition(HitResult.Location);
	HandData.LocalHitNormal = HitBoneWorldToLocalTransform.TransformVectorNoScale(HitResult.Normal);

	// Need to enable this later.
	//LHCollision.SetRelativeLocation(HandData.LocalHitLocation);
	//OnStartGrabbing();
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
}

const FVector APrototype1Character::GetHitLocation(int HandIndex)
{
	FHandsContextData HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	return HandData.GetHitLocation();;
}

const FVector APrototype1Character::GetHitNormal(int HandIndex)
{
	FHandsContextData HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	return HandData.GetHitNormal();
}

const FRotator APrototype1Character::GetHandRotation(int HandIndex)
{
	FHandsContextData HandData = (HandIndex == 0) ? RightHandData : LeftHandData;

	return HandData.GetHandRotation();
}

const bool APrototype1Character::IsGrabbing()
{
	return LeftHandData.IsGrabbing || RightHandData.IsGrabbing;
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


FVector FHandsContextData::GetGrabPosition(const FVector TraceStart, const FVector TraceDir)
{
	return TraceStart + TraceDir * GrabPositionT;
}

FVector FHandsContextData::GetHitLocation()
{
	if (!HitResult.Component.IsValid())
	{
		return FVector();
	}

	const FTransform HitBoneLocalToWorldTransform = HitResult.Component->GetSocketTransform(HitResult.BoneName);

	return HitBoneLocalToWorldTransform.TransformPosition(LocalHitLocation);
}

FVector FHandsContextData::GetHitNormal()
{
	if (!HitResult.Component.IsValid())
	{
		return FVector();
	}

	const FTransform HitBoneLocalToWorldTransform = HitResult.Component->GetSocketTransform(HitResult.BoneName);

	return HitBoneLocalToWorldTransform.TransformVectorNoScale(LocalHitNormal);
}

FRotator FHandsContextData::GetHandRotation()
{
	const FVector HandNormal = GetHitNormal();
	const FRotator HandRotation = FRotationMatrix::MakeFromY(HandNormal).Rotator();
	
	// If right hand, we might want to Inverse the Rotator.

	// Debug
	DrawDebugCoordinateSystem(GEngine->GetWorld(), GetHitLocation(), HandRotation, 10.0f, false, -1.0f, 0, 1.0f);

	// Hand bones are oriented towards Y, which is why we don't get OrientationVector.
	return HandRotation;
}
