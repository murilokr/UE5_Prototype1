#include "ClimberCameraManager.h"
#include "Prototype1Character.h"

#include "Kismet/KismetMathLibrary.h"

// Definir os valores padrão
AClimberCameraManager::AClimberCameraManager()
{
}

void AClimberCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime)
{
	if (OutVT.Target)
	{
		// Super implementation.
		OutVT.Target->CalcCamera(DeltaTime, OutVT.POV);
	}

	if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(GetOwningPlayerController()->GetPawn()))
	{
		if (UClimberCharacterMovementComponent* ClimberCMC = ClimberCharacter->GetCustomCharacterMovement())
		{
			
			if (!OldPOV.Location.IsZero())
			{
				const FMinimalViewInfo TargetPOV = OutVT.POV;
				const FRotator CameraRotation = GetCameraRotation();

				const FVector UnrotatedCurLocation = CameraRotation.UnrotateVector(OldPOV.Location);
				const FVector UnrotatedTargetLocation = CameraRotation.UnrotateVector(TargetPOV.Location);

				const FVector TLocation = FVector(
					FMath::Lerp(UnrotatedCurLocation.X, UnrotatedTargetLocation.X, (LagPerAxis.X > 0) ? DeltaTime * LagPerAxis.X : 1.f),
					FMath::Lerp(UnrotatedCurLocation.Y, UnrotatedTargetLocation.Y, (LagPerAxis.Y > 0) ? DeltaTime * LagPerAxis.Y : 1.f),
					FMath::Lerp(UnrotatedCurLocation.Z, UnrotatedTargetLocation.Z, (LagPerAxis.Z > 0) ? DeltaTime * LagPerAxis.Z : 1.f)
				);

				OutVT.POV.Location = CameraRotation.RotateVector(TLocation);

				GEngine->AddOnScreenDebugMessage(10, DeltaTime, FColor::Yellow, FString::Printf(TEXT("OldPOV.Location: %s - TargetPOV.Location: %s - OutVT.POV.Location: %s"),
					*OldPOV.Location.ToString(), *TargetPOV.Location.ToString(), *OutVT.POV.Location.ToString()));
			}
		}
	}

	//ViewTarget.Target.Get()
	Velocity = (ViewTarget.POV.Location - OldPOV.Location / DeltaTime);
	OldPOV = OutVT.POV;
}

float AClimberCameraManager::ProcessVelocity(FVector LocalCurVelocity, float DeltaSeconds)
{
	const float VelocityCacheWindowDuration = 0.8f;

	float Sum = VelocityCache.Num() * DeltaSeconds;

	while (Sum > (VelocityCacheWindowDuration))
	{
		VelocityCache.RemoveAt(0);
		Sum = VelocityCache.Num() * DeltaSeconds;
	}

	if (VelocityCache.Num() > 0)
	{
		return Algo::Accumulate(VelocityCache, 0.0f) / VelocityCache.Num();
	}

	return 0.0f;
}
