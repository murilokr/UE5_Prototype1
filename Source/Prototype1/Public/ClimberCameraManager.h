#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ClimberCameraManager.generated.h"

UCLASS()
class PROTOTYPE1_API AClimberCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AClimberCameraManager();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climber Camera Manager")
	bool UseCustomLagFunction = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Climber Camera Manager", meta = (editcondition = "UseCustomLagFunction"))
	FVector LagSpeedPerAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climber Camera Manager", meta = (editcondition = "UseCustomLagFunction", ClampMin = "0.0", UIMin = "0.0"))
	float CameraLagMaxDistance;

	/**
	 * If UseCameraLagSubstepping is true, sub-step camera damping so that it handles fluctuating frame rates well (though this comes at a cost).
	 * @see CameraLagMaxTimeStep and USpringArmComponent
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climber Camera Manager", AdvancedDisplay)
	bool UseCameraLagSubstepping = true;

	/** Max time step used when sub-stepping camera lag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climber Camera Manager", AdvancedDisplay, meta = (editcondition = "UseCameraLagSubstepping", ClampMin = "0.005", ClampMax = "0.5", UIMin = "0.005", UIMax = "0.5"))
	float CameraLagMaxTimeStep;

protected:
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime) override;

private:
	UPROPERTY(Transient)
	FVector Velocity;

	FMinimalViewInfo OldPOV;
};
