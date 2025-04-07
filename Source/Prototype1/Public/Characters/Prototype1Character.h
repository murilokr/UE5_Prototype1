// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"

#include "Prototype1Character.generated.h"

struct FKSphylElem;
class UCameraComponent;
class UInputComponent;
class UInputAction;
class UInputMappingContext;
class USkeletalMeshComponent;
class ClimberCharacterMovementComponent;

struct FInputActionValue;
struct FHitResult;

enum EInteractType : int;

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
	TEnumAsByte<EInteractType> InteractionType;

	// This will also be used for interactables. (Maybe have an interact type? i.e: climbing surface, interact, physical object grab, etc.)
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	bool CanInteract;

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FName ClavicleBoneName = "clavicle_r";

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FName UpperArmBoneName = "upperarm_r";

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FName LowerArmBoneName = "lowerarm_r";

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FName HandBoneName = "hand_r";

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	bool IsOverstretched = false;

	// TODO: have a "IsExertingForce" detection for each hand (i.e: mouse input, hand higher than the other, in relaxed or overstretched state).
	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	bool IsExertingForce = false;

	// Location of the hand grab in local player space
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector HandObjectLocalLocation;

	// Location of the hand, local to the climbable surface.
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector HandSurfaceLocalLocation;

	// Normal of the hand surface normal, local to the climbable surface.
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector HandSurfaceLocalNormal;

	// These are locally static, but change in world, so we'll have to convert it when needed.
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHandIdleLocation;

	// These are locally static, but change in world, so we'll have to convert it when needed.
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FRotator LocalHandIdleRotation;

	// Hit Result Stuff
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	TObjectPtr<AActor> HitActor;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	TObjectPtr<UPrimitiveComponent> HitComponent;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FName HitBoneName;

	FQuat WorldToHandTransform;
	FQuat HandToWorldTransform;

	// Actual hand hitbox.
	FKShapeElem* HandCollisionPrimitive = nullptr;
	FCollisionShape HandCollisionShape;

	FQuat GetCollisionPrimitiveRotation() const;

	void ResetHandState();

	bool IsInteracting() const { return InteractionType > 0 && InteractionType < 4; }
	bool IsInteractClimbing() const;
	bool IsInteractGrabbing() const;

	// Hand Location
	FVector GetHandLocation() const;

	FVector GetHandNormal() const;

	FRotator GetHandRotation(bool bShouldFlip, const FVector RelativeRight, const FVector RelativeUp) const;

	FRotator GetGrabRotation(const FVector RelativeRight, const FVector RelativeUp) const;

	void StoreHit(const FHitResult& HitResult, const FVector& OverrideHitLocation = FVector::ZeroVector, const FVector& OverrideHitNormal = FVector::ZeroVector);

	// Per frame values
	FHitResult CurrentFrameTracedHitResult = FHitResult(-1.0f);
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, Transient, meta = (AllowPrivateAccess = "true"))
	UPhysicsAsset* Mesh1PPhysicsAsset;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	TObjectPtr<class UPhysicsHandleComponent> PhysicsHandle;

	/** JointTarget for Left Elbow. This is used for IK Animations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* JointTarget_ElbowL;

	/** JointTarget for Right Elbow. This is used for IK Animations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* JointTarget_ElbowR;

	/** Left Clavicle location in camera local space */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* LeftClavicle_Local;

	/** Right Clavicle location in camera local space */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* RightClavicle_Local;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Duck Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* DuckAction;

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

	/** Death Properties */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay, meta = (AllowPrivateAccess = "true"))
	float FallToDeathTimeDuration = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay, meta = (AllowPrivateAccess = "true"))
	float FallToDeathMinSpeed = -550.f;
	/** Death Properties */
	
	/** Game Feel Improvements */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	float MouseClimbingSensitivity = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay, meta = (AllowPrivateAccess = "true"))
	float CoyoteTimeDuration = 0.275f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climber Camera Manager")
	float GrabInputBufferDuration = 0.75f;
	/** Game Feel Improvements */
	
	APrototype1Character(const FObjectInitializer& ObjectInitializer);

	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns FirstPersonCameraComponent subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	UFUNCTION(BlueprintNativeEvent)
	void OnFallDeath();

	UFUNCTION(BlueprintImplementableEvent)
	void OnStartGrab(const FHandsContextData& HandData, int HandIndex);

	UFUNCTION(BlueprintImplementableEvent)
	void OnEndGrab(const FHandsContextData& HandData, int HandIndex);

	/** Hand Utility Functions */
	FHandsContextData& GetMutableHandData(int HandIndex);
	const FHandsContextData& GetHandData(int HandIndex) const;

	UFUNCTION(BlueprintPure)
	FVector CalculateArmConstraint(int HandIndex, float DeltaSeconds, const FVector& BodyOffset, FVector& RootDeltaFix, FVector& ArmSpringForce);
	FVector CalculateArmConstraint(FHandsContextData& HandData, float DeltaSeconds, const FVector& BodyOffset, FVector& RootDeltaFix, FVector& ArmSpringForce);

	bool TryToSlipHand(FHandsContextData& HandData, const FVector& ArmVector, float ArmMaxRelaxedLength, float DeltaSeconds);
	FVector ValidateHandSlipTarget(const FHandsContextData& HandData, const FVector& SlipTarget);
	FVector MoveHandGrabLocation(FHandsContextData& HandData, const FVector& MoveDelta);

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

	FVector RotateToHand(const FHandsContextData& HandData, const FVector& WorldRelative) const;
	FVector RotateToWorld(const FHandsContextData& HandData, const FVector& HandRelative) const;

	UFUNCTION(BlueprintPure)
	bool IsInteracting() const;

	UFUNCTION(BlueprintPure)
	bool IsHandInteracting(int HandIndex) const;
	bool IsHandInteracting(const FHandsContextData& HandData) const;

	UFUNCTION(BlueprintPure)
	bool IsClimbing() const;
	
	UFUNCTION(BlueprintPure)
	bool IsHandClimbing(int HandIndex) const;
	bool IsHandClimbing(const FHandsContextData& HandData) const;

	UFUNCTION(BlueprintPure)
	bool IsGrabbing() const;

	UFUNCTION(BlueprintPure)
	bool IsHandGrabbing(int HandIndex) const;
	bool IsHandGrabbing(const FHandsContextData& HandData) const;

	UFUNCTION(BlueprintPure)
	bool CanHandInteract(int HandIndex) const;
	bool CanHandInteract(const FHandsContextData& HandData) const;

	UFUNCTION(BlueprintCallable)
	void ReleaseHand(int HandIndex);

	void ReleaseHand(const FHandsContextData& HandData);
	/** End of Hand Utility Functions */

	/** Free Look Functions */
	void ResetLook();

	// Used to indicate if we are lerping the camera back to its original rotation.
	UFUNCTION(BlueprintPure)
	bool IsLookingBack() const;

	UFUNCTION(BlueprintPure)
	float GetLookBackBlend() const;

	FRotator GetFreeLookPreviousControlRotation() const { return FreeLookControlRotation; };
	/** End of Free Look Functions */

	UFUNCTION(BlueprintPure)
	bool IsAlive() const { return bIsAlive; }
	
	// If the time is ticking for us to fall to the death
	UFUNCTION(BlueprintPure)
	bool IsAlmostFallingToDeath() const { return FallToDeathTimer > 0.f; }
	
	UFUNCTION(BlueprintCallable)
	void StartFallingToDeathTime();

	UFUNCTION(BlueprintCallable)
	void StopFallingToDeathTime();

	UFUNCTION(BlueprintCallable)
	void StartCoyoteTime();

	UFUNCTION(BlueprintCallable)
	void StopCoyoteTime();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climbing|Physical Arms")
	FHandsContextData LeftHandData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climbing|Physical Arms")
	FHandsContextData RightHandData;

	// Prevents overstretching. We let go if grabbed location is beyond this length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	float ArmsLengthUnits = 55.f;

	// How much above ArmsLengthUnits are we going to allow when dragging given Min and Max angle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms", meta=(UIMin="1", UIMax="2", ClampMin="1", ClampMax="2"))
	float ArmStretchMultiplier = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float ArmMinRelaxedT = 0.7f;
	
	// How much along the arm length is considered to be in a relaxed state.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float ArmRelaxedT = 0.8f;

	// The minimum angle from shoulder to hand (2D) to start sampling ArmStretchMultiplier with ArmStretchMultiplierCurve to apply onto ArmsLengthUnits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms", meta = (UIMin = "0", UIMax = "90", ClampMin = "0", ClampMax = "90"))
	float ArmStretchMultiplierMinAngle = 1.2f;

	// The maximum angle from shoulder to hand (2D) to start sampling ArmStretchMultiplier with ArmStretchMultiplierCurve to apply onto ArmsLengthUnits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms", meta = (UIMin = "0", UIMax = "90", ClampMin = "0", ClampMax = "90"))
	float ArmStretchMultiplierMaxAngle = 1.2f;

	// The curve float that will sample between 0.1 (MinAngle-MaxAngle) to apply ArmStretchMultiplier onto ArmsLengthUnits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	TObjectPtr<UCurveFloat> ArmStretchMultiplierCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms", meta = (UIMin = "0", UIMax = "2", ClampMin = "0", ClampMax = "2"))
	float MinStretchRatioToSlip = 0.87f;

	// HandSafeZone is how much units towards HandNormal we will set as HandPosition, this is to give a safe space to place the hand, without clipping geometry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	float HandSafeZone = 10.0f;

	// This is actually the Physical Length of the line segment. Add radius for both ends to compute total length (aka: height)
	// Deprecated. See @HandsContextData.HandCollisionPrimitive
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	float HandPhysicalLength = 9.635022f;

	// Deprecated. See @HandsContextData.HandCollisionPrimitive
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	float HandPhysicalHeight = 22.673f;

	// Deprecated. See @HandsContextData.HandCollisionPrimitive
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	float HandPhysicalRadius = 6.519027f;

	// ClavicleShoulderLength is used to calculate if an arm is in range to grab something, this multiplier is to add or reduce a bit from that distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	float ClavicleShoulderLengthMultiplier = 0.5f;

	// Max surface angle that is allowed to slip, anything above this will automatically cause the hand to release.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	float MaxSlipHandAngle = 35.0f;

	// Elbow Lerping Properties
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	FVector LeftHandIdlePositionLocal = FVector(30.f, -15.f, 155.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	FRotator LeftHandIdleRotationLocal = FRotator(72.366f, 82.496f, 176.927f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	FVector RightHandIdlePositionLocal = FVector(30.f, 15.f, 155.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Physical Arms")
	FRotator RightHandIdleRotationLocal = FRotator(72.366f, -82.496f, 176.927f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Camera")
	FRotator DefaultCameraRotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Camera")
	float LookBackTime = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Camera")
	float FreeLookYawAngleLimit = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Camera")
	float FreeLookPitchAngleLimit = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Trace")
	float ArmConeTraceAngle = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Trace")
	int ArmConeTraceSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing|Character")
	bool IsUsingFullBody = true;

protected:
	virtual void BeginPlay();
	virtual void Tick(float DeltaSeconds);

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	// Jump and Duck might end up only being used as debug for the flying mode.
	virtual void Jump() override;
	void Duck();

#if WITH_EDITOR
	void FlyDown();
#endif

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	// This is used for when we want to look around while grabbing something.
	void BeginFreeLook(const FInputActionValue& Value);

	// This is used to end look around while grabbing something.
	void EndFreeLook(const FInputActionValue& Value);

	bool CanUseYaw(const FRotator& Delta, float LookAxisValue) const;

	bool CanUsePitch(const FRotator& Delta, float LookAxisValue) const;

	virtual bool CanJumpInternal_Implementation() const override;

	/** Called for interacting input Right */
	void InteractR(const FInputActionValue& Value);

	/** Called for interacting input Right */
	void StopInteractR(const FInputActionValue& Value);

	/** Called for interacting input Left */
	void InteractL(const FInputActionValue& Value);

	/** Called for interacting input Left */
	void StopInteractL(const FInputActionValue& Value);

	void TraceForHand(FHandsContextData& HandData);

	/** Called for interacting input */
	void Interact(const int HandIndex);

	/** Called for interacting input */
	void StopInteracting(const int HandIndex);

	/** Called for mouse input for moving the hands */
	void MoveHand(FHandsContextData& HandData, FVector2D LookAxisVector);

	void MoveGrabbedObject(FHandsContextData& HandData);

	void StoreGrabInputBuffer(const FHandsContextData& HandData);
	void ProcessInputBuffers(float DeltaSeconds);

	void SetElbowSetup(const int HandIndex, const EElbowSetupType& ElbowSetupType);
	void InterpHandsAndElbow(const int HandIndex, float DeltaSeconds);

	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

	void SetupHandRuntimeContextData(FHandsContextData& HandData) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsFreeLooking;

private:

	bool bIsAlive;
	float FallToDeathTimer = 0.f;
	
	FRotator FreeLookControlRotation;
	float LookBackTimer = 0.f;

	float CoyoteTimer = 0.f;

	float RightHandGrabInputBuffer;
	float LeftHandGrabInputBuffer;

	// Cached "heavy" calculations for perf.
	float ArmsLengthUnitsSquared;
	float ClavicleShoulderLength;
};

