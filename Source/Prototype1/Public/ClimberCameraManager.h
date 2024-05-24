#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ClimberCameraManager.generated.h"

UCLASS()
class PROTOTYPE1_API AClimberCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	// Define os valores padrão para propriedades deste empty
	AClimberCameraManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Climber Camera Manager")
	FVector LagPerAxis;

protected:
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime) override;

	float ProcessVelocity(float DeltaSeconds);

private:
	UPROPERTY(Transient)
	FVector Velocity;

	TArray<float> VelocityCache;

	FMinimalViewInfo OldPOV;
};
