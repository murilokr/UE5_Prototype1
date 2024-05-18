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

UCLASS()
class PROTOTYPE1_API UClimberCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

private:
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void PhysCustom(float DeltaSeconds, int32 Iterations) override;

	void PhysClimbing(float DeltaSeconds, int32 Iterations);

	bool IsClimbing() const;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector HandMoveDir;
};

