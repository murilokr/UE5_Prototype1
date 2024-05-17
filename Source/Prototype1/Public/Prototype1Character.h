// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Prototype1Character.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);


USTRUCT(BlueprintType)
struct FHandsContextData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	bool IsGrabbing;

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FName HandBoneName = "hand_r";

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FName ShoulderBoneName = "upperarm_r";

	// Maybe we'll remove this.
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	float GrabPositionT;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	FHitResult HitResult;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHandLocation;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHandNormal;

	// Hand Location
	FVector GetHandLocation();

	FVector GetHandNormal();

	FRotator GetHandRotation(bool bShouldFlip, const FVector ActorRight);

	// Grab Target Position.
	FVector GetGrabPosition(const FVector TraceStart, const FVector TraceDir);
};


UCLASS(config=Game)
class APrototype1Character : public ACharacter
{
	GENERATED_BODY()

public:

	/** Collider to use when climbing. RootComponent(CapsuleMesh) is used only when grounded/walking or falling */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UCapsuleComponent* ClimbingCollider;

	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* Mesh1P;

	/** JointTarget for Left Elbow. This is used for IK Animations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* JointTarget_ElbowL;

	/** JointTarget for Right Elbow. This is used for IK Animations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* JointTarget_ElbowR;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction; // Mouse
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* FreeLookAction; // Alt-key, Arma like movement.

	/** Grab Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* GrabActionL;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* GrabActionR;
	
	APrototype1Character();

	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns FirstPersonCameraComponent subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	UFUNCTION(BlueprintImplementableEvent)
	void OnStartGrab(const FHandsContextData& HandData, int HandIndex);

	UFUNCTION(BlueprintImplementableEvent)
	void OnEndGrab(const FHandsContextData& HandData, int HandIndex);

	/** Hand Utility Functions */
	UFUNCTION(BlueprintPure)
	const FVector GetHandLocation(int HandIndex);

	// Hand Normal
	UFUNCTION(BlueprintPure)
	const FVector GetHandNormal(int HandIndex);

	// Hand Rotation
	UFUNCTION(BlueprintPure)
	const FRotator GetHandRotation(int HandIndex);

	// IsGrabbing
	UFUNCTION(BlueprintPure)
	const bool IsGrabbing();
	/** End of Hand Utility Functions */

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PhysicalArms)
	FHandsContextData LeftHandData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = PhysicalArms)
	FHandsContextData RightHandData;

	// Prevents overstretching. We let go if grabbed location is beyond this length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = PhysicalArms)
	float ArmsLengthUnits = 54.5f;

	// HandSafeZone is how much units towards HandNormal we will set as HandPosition, this is to give a safe space to place the hand, without clipping geometry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = PhysicalArms)
	float HandSafeZone = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = PhysicalArms)
	float FreeLookAngleLimit = 35.0f;

protected:
	virtual void BeginPlay();

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	// This is used for when we want to look around while grabbing something.
	void BeginFreeLook(const FInputActionValue& Value);

	// This is used to end look around while grabbing something.
	void EndFreeLook(const FInputActionValue& Value);

	bool CanUseYaw(const FRotator& Delta, float LookAxisValue);

	bool CanUsePitch(const FRotator& Delta, float LookAxisValue);

	/** Called for grabbing input Right */
	void GrabR(const FInputActionValue& Value);

	/** Called for grabbing input Right */
	void StopGrabR(const FInputActionValue& Value);

	/** Called for grabbing input Left */
	void GrabL(const FInputActionValue& Value);

	/** Called for grabbing input Left */
	void StopGrabL(const FInputActionValue& Value);

	/** Called for grabbing input */
	void Grab(const int HandIndex);

	void MoveHand(FHandsContextData& HandData, FVector2D LookAxisVector);

	/** Called for grabbing input */
	void StopGrabbing(const int HandIndex);

	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsFreeLooking;

private:
	UPROPERTY()
	TEnumAsByte<ECollisionEnabled::Type> CapsuleComponentCollisionType;

	UPROPERTY()
	FRotator FreeLookControlRotation;
};

