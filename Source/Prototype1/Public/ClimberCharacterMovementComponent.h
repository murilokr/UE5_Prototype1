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

	UPROPERTY(Transient)
	APrototype1Character* ClimberCharacterOwner;

private:
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void PhysCustom(float DeltaSeconds, int32 Iterations) override;

	void PhysClimbing(float DeltaSeconds, int32 Iterations);
	bool IsClimbing() const;

	FVector GetHorizontalHandAcceleration(const FVector& InitialAcceleration, const FHandsContextData& HandData);

	virtual float GetMaxBrakingDeceleration() const override;

public:
	void SetHandGrabbing(const FHandsContextData& HandData);
	void ReleaseHand(const FHandsContextData& HandData);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Character Movement: Climbing")
	float MoveIntensityMultiplier = 2.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float ArmStretchIntensityMultiplier = 200.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Character Movement: Climbing")
	float GravityForce = 5000.0f;

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
	FVector HandSlipVelocity;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector HandSlipTarget;

private:
	FVector PrevLeftHandObjectLocation;
	FVector PrevRightHandObjectLocation;
};

