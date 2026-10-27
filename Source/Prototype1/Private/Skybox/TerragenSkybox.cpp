#include "Skybox/TerragenSkybox.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace TerragenSkyboxAtlas
{
	// Top-left corner of each face in face sizes, in TerragenUE.ush's face order: Front, Left, Back, Right, Up, Down.
	const FVector2D FaceOrigin[] = { {2.0, 1.5}, {1.0, 1.5}, {0.0, 1.5}, {3.0, 1.5}, {1.0, 0.5}, {1.0, 2.5} };

	// Names of the material parameters the setup script creates.
	const FName CameraPos(TEXT("CameraPos"));
	const FName AtlasHeading(TEXT("AtlasHeading"));
	const FName FaceSize(TEXT("FaceSize"));
	const FName StartDistance(TEXT("StartDistance"));
	const FName HeadingAtPlusX(TEXT("HeadingAtPlusX"));
	const FName Atlas(TEXT("Atlas"));
	const FName HeightTex(TEXT("HeightTex"));
	const FName NoiseTex(TEXT("NoiseTex"));
	const FName TerrainClearance(TEXT("TerrainClearance"));
}

ATerragenSkybox::ATerragenSkybox()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the camera has moved this frame, so the sphere stays centred on it.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RenderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MyStuff/Skybox/Terragen/M_TerragenRender.M_TerragenRender")));
	DisplayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MyStuff/Skybox/Terragen/M_TerragenSkyDisplay.M_TerragenSkyDisplay")));
	RenderTarget = TSoftObjectPtr<UTextureRenderTarget2D>(FSoftObjectPath(TEXT("/Game/MyStuff/Skybox/Terragen/RT_TerragenSky.RT_TerragenSky")));

	// The root stays where the actor is placed (the Terragen origin); only the sphere follows the camera.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SkyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyMesh"));
	SkyMesh->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	SkyMesh->SetStaticMesh(Sphere.Object);
	SkyMesh->SetMobility(EComponentMobility::Movable);
	SkyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyMesh->SetCastShadow(false);
	SkyMesh->bAffectDistanceFieldLighting = false;
	SkyMesh->bNeverDistanceCull = true;
	SkyMesh->SetUsingAbsoluteLocation(true);
	SkyMesh->SetUsingAbsoluteRotation(true);
	SkyMesh->SetUsingAbsoluteScale(true);
}

void ATerragenSkybox::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// The engine sphere has a 50 cm radius. Outside play it sits on the actor; during play it follows the camera.
	SkyMesh->SetWorldScale3D(FVector(SkyRadius / 50.0));
	SkyMesh->SetWorldLocationAndRotation(GetActorLocation(), FRotator::ZeroRotator);
	if (UMaterialInterface* Display = DisplayMaterial.LoadSynchronous())
	{
		SkyMesh->SetMaterial(0, DisplayMID ? DisplayMID.Get() : Display);
	}
}

void ATerragenSkybox::BeginPlay()
{
	Super::BeginPlay();

	for (int32 Face = 0; Face < NumFaces; Face++)
	{
		FaceDrawn[Face] = false;
	}
	if (EnsureMaterials())
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, FLinearColor::Black);
	}
}

void ATerragenSkybox::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera || !EnsureMaterials())
	{
		return;
	}
	const FVector Eye = Camera->GetCameraLocation();
	SkyMesh->SetWorldLocation(Eye);
	if (!MaterialsReady())
	{
		// Drawing now would fill faces with the fallback material and mark them as done.
		return;
	}

	// Round-robin over the faces, redrawing up to FacesPerFrame of those drawn from too far away. Until every
	// face has been drawn once, draw them all, so the sky never starts out partly black.
	const FVector Wanted = ToTerragen(Eye);
	const double Threshold = RedrawDistance / CentimetresPerUnit;
	bool AllDrawn = true;
	for (int32 Face = 0; Face < NumFaces; Face++)
	{
		AllDrawn &= FaceDrawn[Face];
	}
	const int32 Budget = AllDrawn ? FacesPerFrame : NumFaces;
	TArray<int32, TInlineAllocator<NumFaces>> Stale;
	for (int32 i = 0; i < NumFaces && Stale.Num() < Budget; i++)
	{
		const int32 Face = (NextFace + i) % NumFaces;
		if (!FaceDrawn[Face] || FVector::Dist(FaceDrawnAt[Face], Wanted) > Threshold)
		{
			Stale.Add(Face);
		}
	}
	if (Stale.Num() > 0)
	{
		DrawFaces(Stale, Wanted);
		NextFace = (Stale.Last() + 1) % NumFaces;
	}
}

FVector ATerragenSkybox::ToTerragen(const FVector& UnrealLocation) const
{
	const FVector Offset = (UnrealLocation - GetActorLocation()) / CentimetresPerUnit;
	FVector Result = TerragenStart;
	Result.Z += Offset.Z;
	if (FollowHorizontal)
	{
		// Unreal X and Y are Terragen y and x; then turn so Unreal +X looks along HeadingAtPlusX
		// (the same conversion as TG_FromUEDirection in TerragenUE.ush).
		const double Heading = FMath::DegreesToRadians(HeadingAtPlusX);
		const double X = Offset.Y;
		const double Y = Offset.X;
		Result.X += X * FMath::Cos(Heading) + Y * FMath::Sin(Heading);
		Result.Y += -X * FMath::Sin(Heading) + Y * FMath::Cos(Heading);
	}
	return Result;
}

void ATerragenSkybox::RedrawFrom(const FVector& UnrealLocation)
{
	if (EnsureMaterials())
	{
		if (!MaterialsReady())
		{
			FinishCompilation();
		}
		const int32 AllFaces[] = { 0, 1, 2, 3, 4, 5 };
		DrawFaces(AllFaces, ToTerragen(UnrealLocation));
	}
}

bool ATerragenSkybox::MaterialsReady() const
{
	if (!RenderMID || !DisplayMID || RenderMID->GetMaterial()->IsCompiling() || DisplayMID->GetMaterial()->IsCompiling())
	{
		return false;
	}
#if WITH_EDITOR
	// In the editor, textures build asynchronously after loading. Until then the height texture reads as a
	// placeholder and the whole sky would render as terrain.
	for (const FName Name : { TerragenSkyboxAtlas::HeightTex, TerragenSkyboxAtlas::NoiseTex })
	{
		UTexture* Texture = nullptr;
		if (RenderMID->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Texture) && Texture && Texture->IsCompiling())
		{
			return false;
		}
	}
#endif
	return true;
}

void ATerragenSkybox::RedrawFromActorLocation()
{
	RedrawFrom(GetActorLocation());
}

float ATerragenSkybox::MeasureRedrawMilliseconds(int32 Iterations)
{
	if (!EnsureMaterials() || Iterations < 1)
	{
		return -1.f;
	}
	const int32 AllFaces[] = { 0, 1, 2, 3, 4, 5 };
	const FVector Camera = ToTerragen(GetActorLocation());
	// Reading a pixel back waits for all queued GPU work, before and after the timed draws.
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, Target, 0, 0);
	const double Start = FPlatformTime::Seconds();
	for (int32 i = 0; i < Iterations; i++)
	{
		DrawFaces(AllFaces, Camera);
	}
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, Target, 0, 0);
	return float((FPlatformTime::Seconds() - Start) * 1000.0 / Iterations);
}

void ATerragenSkybox::FinishCompilation()
{
#if WITH_EDITOR
	// Textures and meshes build asynchronously in the editor, and shaders compile in the background.
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
#endif
}

bool ATerragenSkybox::EnsureMaterials()
{
	if (RenderMID && DisplayMID && Target)
	{
		return true;
	}
	Target = RenderTarget.LoadSynchronous();
	UMaterialInterface* Render = RenderMaterial.LoadSynchronous();
	UMaterialInterface* Display = DisplayMaterial.LoadSynchronous();
	if (!Target || !Render || !Display)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerragenSkybox: missing render target or materials; run ClimbingGameAssets/TerragenShader/Tools/ue_setup.py"));
		return false;
	}
	const float FaceSize = Target->SizeX / 4.f;
	RenderMID = UMaterialInstanceDynamic::Create(Render, this);
	RenderMID->SetScalarParameterValue(TerragenSkyboxAtlas::FaceSize, FaceSize);
	DisplayMID = UMaterialInstanceDynamic::Create(Display, this);
	DisplayMID->SetTextureParameterValue(TerragenSkyboxAtlas::Atlas, Target);
	DisplayMID->SetScalarParameterValue(TerragenSkyboxAtlas::FaceSize, FaceSize);
	SkyMesh->SetMaterial(0, DisplayMID);
	return true;
}

void ATerragenSkybox::DrawFaces(TConstArrayView<int32> Faces, const FVector& TerragenCamera)
{
	RenderMID->SetVectorParameterValue(TerragenSkyboxAtlas::CameraPos, FLinearColor(TerragenCamera.X, TerragenCamera.Y, TerragenCamera.Z, 0.f));
	RenderMID->SetScalarParameterValue(TerragenSkyboxAtlas::AtlasHeading, AtlasHeading);
	RenderMID->SetScalarParameterValue(TerragenSkyboxAtlas::StartDistance, StartDistance);
	// The shader takes the clearance in Terragen units; negative switches it off.
	RenderMID->SetScalarParameterValue(TerragenSkyboxAtlas::TerrainClearance, KeepAboveTerrain ? TerrainClearance / CentimetresPerUnit : -1.f);
	DisplayMID->SetScalarParameterValue(TerragenSkyboxAtlas::AtlasHeading, AtlasHeading);
	DisplayMID->SetScalarParameterValue(TerragenSkyboxAtlas::HeadingAtPlusX, HeadingAtPlusX);

	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
	if (Canvas)
	{
		const double FaceSize = Size.X / 4.0;
		for (const int32 Face : Faces)
		{
			// Only the face's square is drawn; its UVs are the matching part of the whole atlas.
			const FVector2D Origin = TerragenSkyboxAtlas::FaceOrigin[Face] * FaceSize;
			Canvas->K2_DrawMaterial(RenderMID, Origin, FVector2D(FaceSize), Origin / Size, FVector2D(FaceSize) / Size);
			FaceDrawnAt[Face] = TerragenCamera;
			FaceDrawn[Face] = true;
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}
