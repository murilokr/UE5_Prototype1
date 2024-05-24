#include "ClimberCameraManager.h"
#include "Prototype1Character.h"

#include "Kismet/KismetMathLibrary.h"

// Result vector is change of velocity in each component.
static FVector VectorsMagnitude(const TArray<FVector>& list, FVector& minVector, FVector& maxVector)
{
	if (list.Num() == 0) return FVector::ZeroVector;

	maxVector = list[list.Num() - 1].GetAbs();
	minVector = list[0].GetAbs();


	return maxVector - minVector;
}

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
				const FRotator CameraRotation = GetCameraRotation();
				const FVector UnrotatedCurVelocity = CameraRotation.UnrotateVector(ClimberCMC->Velocity); // Locally transformed velocity
				ProcessVelocity(UnrotatedCurVelocity, DeltaTime);

				FVector MinVec;
				FVector MaxVec;
				const FVector VelocityDelta = VectorsMagnitude(VelocityWindowCache, MinVec, MaxVec);
				GEngine->AddOnScreenDebugMessage(10, DeltaTime, FColor::Yellow, FString::Printf(TEXT("Velocity Delta: %s (Min: %s - Max: %s)"),
					*VelocityDelta.ToString(), *MinVec.ToString(), *MaxVec.ToString()));

				GEngine->AddOnScreenDebugMessage(11, DeltaTime, FColor::Yellow, FString::Printf(TEXT("Velocity Unrotated: %s"),
					*UnrotatedCurVelocity.ToString()));

				// Check for velocity change per component.
				bool VelocityXThreshold = MinAbsVelocityChangePerAxis.X > 0 && FMath::Abs(VelocityDelta.X) > MinAbsVelocityChangePerAxis.X;
				bool VelocityYThreshold = MinAbsVelocityChangePerAxis.Y > 0 && FMath::Abs(VelocityDelta.Y) > MinAbsVelocityChangePerAxis.Y;
				bool VelocityZThreshold = MinAbsVelocityChangePerAxis.Z > 0 && FMath::Abs(VelocityDelta.Z) > MinAbsVelocityChangePerAxis.Z;

				float XT = (VelocityXThreshold && LagPerAxis.X > 0) ? DeltaTime * LagPerAxis.X : 1.f;
			
				const FMinimalViewInfo TargetPOV = OutVT.POV;
				
				const FVector UnrotatedCurLocation = CameraRotation.UnrotateVector(OldPOV.Location);
				const FVector UnrotatedTargetLocation = CameraRotation.UnrotateVector(TargetPOV.Location);

				//const bool ExtrapolateY = 

				const FVector TLocation = FVector(
					FMath::Lerp(UnrotatedCurLocation.X, UnrotatedTargetLocation.X, (LagPerAxis.X > 0) ? DeltaTime * LagPerAxis.X : 1.f),
					FMath::Lerp(UnrotatedCurLocation.Y, UnrotatedTargetLocation.Y, (LagPerAxis.Y > 0) ? DeltaTime * LagPerAxis.Y : 1.f),
					FMath::Lerp(UnrotatedCurLocation.Z, UnrotatedTargetLocation.Z, (LagPerAxis.Z > 0) ? DeltaTime * LagPerAxis.Z : 1.f)
				);

				OutVT.POV.Location = CameraRotation.RotateVector(TLocation);

				/*GEngine->AddOnScreenDebugMessage(10, DeltaTime, FColor::Yellow, FString::Printf(TEXT("OldPOV.Location: %s - TargetPOV.Location: %s - OutVT.POV.Location: %s"),
					*OldPOV.Location.ToString(), *TargetPOV.Location.ToString(), *OutVT.POV.Location.ToString()));*/
			}
		}
	}

	//ViewTarget.Target.Get()
	Velocity = (ViewTarget.POV.Location - OldPOV.Location / DeltaTime);
	OldPOV = OutVT.POV;
}

void AClimberCameraManager::ProcessVelocity(FVector LocalCurVelocity, float DeltaSeconds)
{
	VelocityWindowCache.Add(LocalCurVelocity);
	DeltaSecondsWindowCache.Add(DeltaSeconds);

	float Sum = Algo::Accumulate(DeltaSecondsWindowCache, 0.0f);

	while (Sum > (VelocityWindowCacheDuration))
	{
		VelocityWindowCache.RemoveAt(0);
		DeltaSecondsWindowCache.RemoveAt(0);

		Sum = Algo::Accumulate(DeltaSecondsWindowCache, 0.0f);
	}
}
