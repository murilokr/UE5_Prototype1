#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ObjectMacros.h"

#include "ClimberCharacterMovementComponent.generated.h"

UENUM(BlueprintType)
enum ECustomMovementMode
{
	CMOVE_Climbing      UMETA(DisplayName = "Climbing"),
	CMOVE_MAX			UMETA(Hidden),
};

class APrototype1Character;
struct FHandsContextData;

UCLASS()
class PROTOTYPE1_API UClimberCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

protected:
	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(Transient)
	APrototype1Character* ClimberCharacterOwner;

private:
	void OnStartClimbing();
	void OnEndClimbing();

	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void PhysCustom(float DeltaSeconds, int32 Iterations) override;
	void ForcePullOrPushHorizontalMovementTowardsGrabLocation(FVector& HorizontalHandsControlAcceleration);
	float GetCoyoteGravityForce() const;

	void PhysClimbing(float DeltaSeconds, int32 Iterations);
	bool IsClimbing() const;

	void ComputeHandAccelerations(const int HandIndex, float DeltaTime, FVector& ClimbingAcceleration, FVector& HorizontalHandsControlAcceleration, const FVector& BodyOffset);
	FVector GetHorizontalHandAcceleration(const FVector& InitialAcceleration, const FHandsContextData& HandData);

	virtual float GetMaxBrakingDeceleration() const override;

public:
	void SetHandClimbing(const FHandsContextData& HandData);
	void ReleaseHand(const FHandsContextData& HandData);

	void UpdateHelperSpring(float SpringIntensityFalloffCustomDuration = -1);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Character Movement: Climbing")
	float MoveIntensityMultiplier = 2.0f;

	// Arm Stretch Spring Constant. (Used to pull back the slightly overstretched arm into a relaxed state)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float ArmSpringForceIntensity = 4500.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float ArmSpringDampening = 0.8f;
	
	// Arm Limit Acceleration Feedback. (Used to snap the overstretched arm back into place)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float ArmStretchIntensityMultiplier = 550.0f;

	// Since Gravity is coyote'd. This is the minimum amount of gravity applied to the CMC.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float MinCoyoteGravityForce = 75.0f;

	// Since Gravity is coyote'd. This is the maximum amount of gravity applied to the CMC.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float GravityForce = 500.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float HandsControlAcceleration = 150.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	bool bForceCharacterPullingWhenNotMoving = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float WallFriction = 1.3;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float MaxSlipSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking", meta = (ClampMin = "0", UIMin = "0"))
	float BrakingDecelerationClimbing;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Physics Objects")
	float ForceDampingAppliedToPhysicsObjectMultiplier = 5.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Physics Objects")
	bool UseImpulseOnPhysicsObjects = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector HandMoveDir;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector HandSlipVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector HandSlipTarget = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Helper Spring")
	float HelperSpringMaxIntensity = 4500.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Helper Spring")
	TObjectPtr<UCurveFloat> HelperSpringIntensityFalloffCurve;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Helper Spring")
	float HelperSpringIntensityMoveFalloffDuration = 2.75f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Helper Spring")
	float HelperSpringIntensityIdleFalloffDuration = 0.75f;

private:
	FVector HelperSpringAnchor;
	float HelperSpringIntensity = 0.0f;
	float HelperSpringIntensityFalloffTimer = 0.0f;
	float HelperSpringIntensityFalloffDuration = 0.75f;

	// we need to hold these values inside the hand context data. (perhaps have a "runtime" context data, so we can divide setup and runtime data)
	FVector PrevLeftHandObjectLocation;
	FVector PrevLeftHandObjectVelocity;

	FVector PrevRightHandObjectLocation;
	FVector PrevRightHandObjectVelocity;
};

