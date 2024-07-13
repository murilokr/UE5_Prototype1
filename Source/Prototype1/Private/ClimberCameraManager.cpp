#include "ClimberCameraManager.h"
#include "Prototype1Character.h"
#include "Camera/CameraComponent.h"

#include "Kismet/KismetMathLibrary.h"

namespace CameraMathUtils
{
	FVector VInterpToPerAxis(const FVector& Current, const FVector& Target, float DeltaTime, FVector InterpSpeedPerAxis)
	{
		// Distance to reach
		const FVector Dist = Target - Current;

		// If distance is too small, just set the desired location
		if (Dist.SizeSquared() < UE_KINDA_SMALL_NUMBER)
		{
			return Target;
		}

		// Delta Move per axis, Clamp so we do not over shoot.
		float DeltaMoveX = Dist.X * FMath::Clamp<float>(DeltaTime * InterpSpeedPerAxis.X, 0.f, 1.f);
		// If no interp speed, jump to Dist value
		DeltaMoveX = (InterpSpeedPerAxis.X <= 0.f) ? Dist.X : DeltaMoveX;

		float DeltaMoveY = Dist.Y * FMath::Clamp<float>(DeltaTime * InterpSpeedPerAxis.Y, 0.f, 1.f);
		// If no interp speed, jump to Dist value
		DeltaMoveY = (InterpSpeedPerAxis.Y <= 0.f) ? Dist.Y : DeltaMoveY;

		float DeltaMoveZ = Dist.Z * FMath::Clamp<float>(DeltaTime * InterpSpeedPerAxis.Z, 0.f, 1.f);
		// If no interp speed, jump to Dist value
		DeltaMoveZ = (InterpSpeedPerAxis.Z <= 0.f) ? Dist.Z : DeltaMoveZ;

		const FVector DeltaMove = FVector(DeltaMoveX, DeltaMoveY, DeltaMoveZ);
		return Current + DeltaMove;
	}
};

AClimberCameraManager::AClimberCameraManager()
{
	UseCameraLagSubstepping = true;
	CameraLagMaxTimeStep = 1.f / 60.f;
}

void AClimberCameraManager::BeginPlay()
{
	CameraLagMaxTimeStep = FMath::Max(CameraLagMaxTimeStep, 1.f / 200.f);
}

void AClimberCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime)
{
	if (AController* OwningController = GetOwningPlayerController())
	{
		if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(OwningController->GetPawn()))
		{
			// Deal with rotating the MeshPivot
			if (!ClimberCharacter->FirstPersonCameraComponent->bUsePawnControlRotation && OwningController->IsLocalPlayerController())
			{
				const FRotator PawnViewRotation = ClimberCharacter->GetViewRotation();
				UStaticMeshComponent* MeshPivot = ClimberCharacter->MeshPivot;
				if (!PawnViewRotation.Equals(MeshPivot->GetComponentRotation()))
				{
					MeshPivot->SetWorldRotation(PawnViewRotation);
				}
			}
			
			// Handle LookBack
			if (ClimberCharacter->IsLookingBack())
			{
				const float Blend = ClimberCharacter->GetLookBackBlend();

				FRotator NewControlRotation = FMath::Lerp(OwningController->GetControlRotation(), ClimberCharacter->GetFreeLookPreviousControlRotation(), Blend);
				if (NewControlRotation.Equals(OwningController->GetControlRotation(), 1e-3f))
				{
					UE_LOG(LogTemp, Display, TEXT("NewControlRotation is equals to ControlRotation. EARLY OUT!"));
					ClimberCharacter->ResetLook();
				}
				else
				{
					OwningController->SetControlRotation(NewControlRotation);

					const FQuat FPRelativeRotation = FQuat::Slerp(ClimberCharacter->FirstPersonCameraComponent->GetRelativeRotation().Quaternion(), FQuat::Identity, Blend);
					ClimberCharacter->FirstPersonCameraComponent->SetRelativeRotation(FPRelativeRotation);
				}
			}

			if (OutVT.Target)
			{
				// Super implementation.
				OutVT.Target->CalcCamera(DeltaTime, OutVT.POV);
			}

			if (UseCustomLagFunction)
			{
				if (!OldPOV.Location.IsZero())
				{
					const FMinimalViewInfo TargetPOV = OutVT.POV;

					const FRotator CameraRotation = ClimberCharacter->GetActorRotation();
					FVector UnrotatedCurLocation = CameraRotation.UnrotateVector(OldPOV.Location);
					FVector UnrotatedTargetLocation = CameraRotation.UnrotateVector(TargetPOV.Location);

					/*GEngine->AddOnScreenDebugMessage(11, DeltaTime, FColor::Yellow, FString::Printf(TEXT("Current Unrotated: %s - Target Unrotated: %s"),
						*UnrotatedCurLocation.ToString(), *UnrotatedTargetLocation.ToString()));*/

					FVector TLocation = UnrotatedTargetLocation;

					if (UseCameraLagSubstepping && DeltaTime > CameraLagMaxTimeStep && LagSpeedPerAxis.SizeSquared() > 0.f)
					{
						const FVector ArmMovementStep = (TLocation - UnrotatedCurLocation) * (1.f / DeltaTime);
						FVector LerpTarget = UnrotatedCurLocation;

						float RemainingTime = DeltaTime;
						while (RemainingTime > UE_KINDA_SMALL_NUMBER)
						{
							const float LerpAmount = FMath::Min(CameraLagMaxTimeStep, RemainingTime);
							LerpTarget += ArmMovementStep * LerpAmount;
							RemainingTime -= LerpAmount;

							TLocation = CameraMathUtils::VInterpToPerAxis(UnrotatedCurLocation, LerpTarget, LerpAmount, LagSpeedPerAxis);
							UnrotatedCurLocation = TLocation;
						}
					}
					else
					{
						TLocation = CameraMathUtils::VInterpToPerAxis(UnrotatedCurLocation, UnrotatedTargetLocation, DeltaTime, LagSpeedPerAxis);
					}

					// Clamp distance if requested
					if (CameraLagMaxDistance > 0.f)
					{
						const FVector FromOrigin = TLocation - UnrotatedTargetLocation;
						if (FromOrigin.SizeSquared() > FMath::Square(CameraLagMaxDistance))
						{
							TLocation = UnrotatedTargetLocation + FromOrigin.GetClampedToMaxSize(CameraLagMaxDistance);
						}
					}

					OutVT.POV.Location = CameraRotation.RotateVector(TLocation);
				}
			}
		}
	}
	else if (OutVT.Target)
	{
		// Super implementation.
		OutVT.Target->CalcCamera(DeltaTime, OutVT.POV);
	}

	//ViewTarget.Target.Get()
	Velocity = (ViewTarget.POV.Location - OldPOV.Location / DeltaTime);
	OldPOV = OutVT.POV;
}
