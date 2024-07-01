// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"

#include "ClimberCharacterMovementComponent.h"

#include "Prototype1Character.generated.h"

class UCameraComponent;
class UInputComponent;
class UInputAction;
class UInputMappingContext;
class USkeletalMeshComponent;

struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UENUM(BlueprintType)
enum EElbowSetupType
{
	ESETUP_Climbing		UMETA(DisplayName = "Elbow - Climbing Setup"),
	ESETUP_Idle			UMETA(DisplayName = "Elbow - Idle Setup"),
	ESETUP_Mantling		UMETA(DisplayName = "Elbow - Mantling Setup"), // Unused
	ESETUP_MAX			UMETA(Hidden),
};

USTRUCT(BlueprintType)
struct FElbowSetup
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TEnumAsByte<EElbowSetupType> SetupType;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector LeftElbowRelativeLocation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector RightElbowRelativeLocation;

	// Lerp property
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float LerpInDuration = 0.5f;
};

USTRUCT(BlueprintType)
struct FHandsContextData
{
	GENERATED_BODY()

	int HandIndex;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	bool IsGrabbing;

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FName ShoulderBoneName = "upperarm_r";

	// Maybe we'll remove this.
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	float GrabPositionT;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHandLocation;

	// These are locally static, but change in world, so we'll have to convert it when needed.
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHandIdleLocation;

	// These are locally static, but change in world, so we'll have to convert it when needed.
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FRotator LocalHandIdleRotation;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHandNormal;

	// Hit Result Stuff
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	TObjectPtr<AActor> HitActor;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	TObjectPtr<UPrimitiveComponent> HitComponent;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FName HitBoneName;

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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* MeshPivot;

	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* Mesh1P;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** JointTarget for Left Elbow. This is used for IK Animations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* JointTarget_ElbowL;

	/** JointTarget for Right Elbow. This is used for IK Animations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* JointTarget_ElbowR;

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

	/** Game Feel Improvements */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay, meta = (AllowPrivateAccess = "true"))
	float CoyoteTimeDuration = 0.275f;
	
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
	FVector GetArmVector(int HandIndex, const FVector& BodyOffset, bool& OutIsOverstretched, FVector& RootDeltaFix, FVector& HandSlipVector) const;
	FVector GetArmVector(const FHandsContextData& HandData, const FVector& BodyOffset, bool& OutIsOverstretched, FVector& RootDeltaFix, FVector& HandSlipVector) const;

	void SlipHand(FHandsContextData& HandData, const FVector& HandSlipVector, float DeltaSeconds);

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
	FRotator GetHandRotation(const FHandsContextData& HandData) const;

	// IsGrabbing
	UFUNCTION(BlueprintPure)
	bool IsGrabbing() const;

	UFUNCTION(BlueprintCallable)
	void ReleaseHand(int HandIndex);

	void ReleaseHand(const FHandsContextData& HandData);
	/** End of Hand Utility Functions */

	UFUNCTION(BlueprintCallable)
	void StartCoyoteTime();

	UFUNCTION(BlueprintCallable)
	void StopCoyoteTime();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climbing - Physical Arms")
	FHandsContextData LeftHandData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climbing - Physical Arms")
	FHandsContextData RightHandData;

	// Prevents overstretching. We let go if grabbed location is beyond this length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	float ArmsLengthUnits = 55.f;

	// How much above ArmsLengthUnits are we going to allow when dragging given Min and Max angle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms", meta=(UIMin="1", UIMax="2", ClampMin="1", ClampMax="2"))
	float ArmStretchMultiplier = 1.2f;

	// The minimum angle from shoulder to hand (2D) to start sampling ArmStretchMultiplier with ArmStretchMultiplierCurve to apply onto ArmsLengthUnits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms", meta = (UIMin = "0", UIMax = "90", ClampMin = "0", ClampMax = "90"))
	float ArmStretchMultiplierMinAngle = 1.2f;

	// The maximum angle from shoulder to hand (2D) to start sampling ArmStretchMultiplier with ArmStretchMultiplierCurve to apply onto ArmsLengthUnits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms", meta = (UIMin = "0", UIMax = "90", ClampMin = "0", ClampMax = "90"))
	float ArmStretchMultiplierMaxAngle = 1.2f;

	// The curve float that will sample between 0.1 (MinAngle-MaxAngle) to apply ArmStretchMultiplier onto ArmsLengthUnits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	TObjectPtr<UCurveFloat> ArmStretchMultiplierCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms", meta = (UIMin = "0", UIMax = "2", ClampMin = "0", ClampMax = "2"))
	float MinStretchRatioToSlip = 0.15f;

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

	// Elbow Lerping Properties
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	TArray<FElbowSetup> ElbowsSetups;

	UPROPERTY(Transient, BlueprintReadOnly)
	FElbowSetup CurrentLeftArmSetup;
	UPROPERTY(Transient, BlueprintReadOnly)
	FElbowSetup CurrentRightArmSetup;

	UPROPERTY(Transient, BlueprintReadOnly)
	float LeftArmLerpTime;
	UPROPERTY(Transient, BlueprintReadOnly)
	float RightArmLerpTime;
	// End of Elbow Lerping Properties

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	FVector LeftHandIdlePositionLocal = FVector(30.f, -15.f, 155.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	FRotator LeftHandIdleRotationLocal = FRotator(72.366f, 82.496f, 176.927f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	FVector RightHandIdlePositionLocal = FVector(30.f, 15.f, 155.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing - Physical Arms")
	FRotator RightHandIdleRotationLocal = FRotator(72.366f, -82.496f, 176.927f);

protected:
	virtual void BeginPlay();
	virtual void Tick(float DeltaSeconds);

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

	virtual bool CanJumpInternal_Implementation() const override;

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

	void SetElbowSetup(const int HandIndex, const EElbowSetupType& ElbowSetupType);
	void InterpHandsAndElbow(const int HandIndex, float DeltaSeconds);

	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsFreeLooking;

private:

	FRotator FreeLookControlRotation;

	float CoyoteTimer = 0.f;

	// Cached "heavy" calculations for perf.
	float ArmsLengthUnitsSquared;
	float ClavicleShoulderLength;
};

