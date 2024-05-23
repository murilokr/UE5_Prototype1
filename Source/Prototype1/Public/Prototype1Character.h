// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"

#include "ClimberCharacterMovementComponent.h"

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
	FVector GetHandLocation() const;

	FVector GetHandNormal() const;

	FRotator GetHandRotation(bool bShouldFlip, const FVector RelativeUp) const;

	// Grab Target Position.
	FVector GetGrabPosition(const FVector TraceStart, const FVector TraceDir) const;
};


UCLASS(config=Game)
class APrototype1Character : public ACharacter
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure)
	FORCEINLINE UClimberCharacterMovementComponent* GetCustomCharacterMovement() const { return ClimberMovementComponent; }

protected:
	UPROPERTY(Category = Character, VisibleAnywhere, BlueprintReadOnly)
	UClimberCharacterMovementComponent* ClimberMovementComponent;

public:
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
	
	APrototype1Character(const FObjectInitializer& ObjectInitializer);

	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns FirstPersonCameraComponent subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	UFUNCTION(BlueprintImplementableEvent)
	void OnStartGrab(const FHandsContextData& HandData, int HandIndex);

	UFUNCTION(BlueprintImplementableEvent)
	void OnEndGrab(const FHandsContextData& HandData, int HandIndex);

	/** Hand Utility Functions */
	FHandsContextData& GetMutableHandData(int HandIndex);
	const FHandsContextData& GetHandData(int HandIndex) const;

	UFUNCTION(BlueprintPure)
	FVector GetArmVector(int HandIndex, const FVector& BodyOffset, bool& OutIsOverstretched, FVector& RootDeltaFix) const;
	FVector GetArmVector(const FHandsContextData& HandData, const FVector& BodyOffset, bool& OutIsOverstretched, FVector& RootDeltaFix) const;

	UFUNCTION(BlueprintPure)
	FVector GetHandLocation(int HandIndex) const;
	FVector GetHandLocation(const FHandsContextData& HandData) const;

	UFUNCTION(BlueprintPure)
	FVector GetSafeHandLocation(int HandIndex) const;
	FVector GetSafeHandLocation(const FHandsContextData& HandData) const;

	// Hand Normal
	UFUNCTION(BlueprintPure)
	FVector GetHandNormal(int HandIndex) const;
	FVector GetHandNormal(const FHandsContextData& HandData) const;

	// Hand Rotation
	UFUNCTION(BlueprintPure)
	FRotator GetHandRotation(int HandIndex) const;

	// IsGrabbing
	UFUNCTION(BlueprintPure)
	bool IsGrabbing() const;
	/** End of Hand Utility Functions */

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climbing - Physical Arms")
	FHandsContextData LeftHandData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climbing - Physical Arms")
	FHandsContextData RightHandData;

	// Prevents overstretching. We let go if grabbed location is beyond this length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	float ArmsLengthUnits = 55.f;

	// HandSafeZone is how much units towards HandNormal we will set as HandPosition, this is to give a safe space to place the hand, without clipping geometry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	float HandSafeZone = 10.0f;

	// ClavicleShoulderLength is used to calculate if an arm is in range to grab something, this multiplier is to add or reduce a bit from that distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	float ClavicleShoulderLengthMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	float FreeLookYawAngleLimit = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	float FreeLookPitchAngleLimit = 40.0f;

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

	bool CanUseYaw(const FRotator& Delta, float LookAxisValue) const;

	bool CanUsePitch(const FRotator& Delta, float LookAxisValue) const;

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
	FRotator FreeLookControlRotation;

	// Cached "heavy" calculations for perf.
	float ArmsLengthUnitsSquared;
	float ClavicleShoulderLength;
};

