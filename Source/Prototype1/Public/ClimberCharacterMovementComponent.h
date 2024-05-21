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

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Climbing")
	float MoveIntensityMultiplier = 2.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Climbing")
	float ArmStretchIntensityMultiplier = 200.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Climbing")
	float GravityForce = 5000.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Climbing")
	float WallFriction = 1.3;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector HandMoveDir;

	bool bIsArmsStretchVelocityApplied = false;
};

