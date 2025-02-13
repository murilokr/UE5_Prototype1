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

	if (AController* OwningController = GetOwningPlayerController())
	{
		if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(OwningController->GetPawn()))
		{
			InitialFOV = ClimberCharacter->FirstPersonCameraComponent->FieldOfView;
			AliveCamLocation = ClimberCharacter->FirstPersonCameraComponent->GetRelativeLocation();
		}
	}
}

void AClimberCameraManager::StartDeathCam()
{
	if (!UseDeathCam)
	{
		return;
	}

	DeathCamTimer = DeathCamDuration;
}

void AClimberCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime)
{
	if (OutVT.Target)
	{
		// Super implementation.
		OutVT.Target->CalcCamera(DeltaTime, OutVT.POV);
	}
	
	if (AController* OwningController = GetOwningPlayerController())
	{
		if (APrototype1Character* ClimberCharacter = Cast<APrototype1Character>(OwningController->GetPawn()))
		{

			// Need to understand if CameraModifiers can do this type of camera work, that way it can save us a lot of time when doing the "Focus At" feature.
			if (ClimberCharacter->IsAlive())
			{
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

				// Deal with rotating the MeshPivot
				// TODO: Have the body rotate towards first hand normal
				if (!ClimberCharacter->FirstPersonCameraComponent->bUsePawnControlRotation && OwningController->IsLocalPlayerController())
				{
					const FRotator PawnViewRotation = ClimberCharacter->GetViewRotation();
					UStaticMeshComponent* MeshPivot = ClimberCharacter->MeshPivot;
					if (!PawnViewRotation.Equals(MeshPivot->GetComponentRotation()))
					{
						MeshPivot->SetWorldRotation(PawnViewRotation);
					}
				}
			}
			else if (UseDeathCam)
			{
				float DeathCamBlend = 1.f;
				if (DeathCamTimer > 0.f)
				{
					// Calculate Blend
					DeathCamTimer -= DeltaTime;
					DeathCamTimer = FMath::Max(DeathCamTimer, 0.f);
					DeathCamBlend = 1.f - FMath::Clamp(DeathCamTimer / DeathCamDuration, 0.f, 1.f);
				}

				// Start to move camera towards relative offset (might be upwards a bit) (we also might want to have two different blend modes here).
				const FVector CameraOffset = FMath::Lerp(AliveCamLocation, AliveCamLocation + DeathCamOffset, DeathCamBlend);
				ClimberCharacter->FirstPersonCameraComponent->SetRelativeLocation(CameraOffset);

				// Point camera towards dead body
				const FVector RagdollDirection = (ClimberCharacter->GetMesh()->GetComponentLocation() - ClimberCharacter->FirstPersonCameraComponent->GetComponentLocation()).GetSafeNormal();
				const FRotator Ragdoll = FRotationMatrix::MakeFromX(RagdollDirection).Rotator();
				DrawDebugCoordinateSystem(GetWorld(), ClimberCharacter->FirstPersonCameraComponent->GetComponentLocation(), Ragdoll, 10.0f, false, 0.016f, 0, 1.0f);
				OwningController->SetControlRotation(Ragdoll);
					
				// Start changing the FOV
				OutVT.POV.FOV = FMath::Lerp(InitialFOV, DeathFOV, DeathCamBlend);
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

	//ViewTarget.Target.Get()
	Velocity = (ViewTarget.POV.Location - OldPOV.Location / DeltaTime);
	OldPOV = OutVT.POV;
}
