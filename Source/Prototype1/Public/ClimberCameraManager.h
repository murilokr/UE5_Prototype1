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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climber Camera Manager")
	FVector MinAbsVelocityChangePerAxis = FVector(0.f, 20.f, 20.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climber Camera Manager")
	float VelocityWindowCacheDuration = 0.12f;

protected:
	virtual void UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime) override;

	void ProcessVelocity(FVector LocalCurVelocity, float DeltaSeconds);

private:
	UPROPERTY(Transient)
	FVector Velocity;

	TArray<FVector> VelocityWindowCache;
	TArray<float> DeltaSecondsWindowCache;

	FMinimalViewInfo OldPOV;
};
