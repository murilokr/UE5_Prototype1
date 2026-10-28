#include "Skybox/TerragenSkybox.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Skybox/TerragenWorld.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace TerragenSkyboxAtlas
{
	constexpr int32 NumFaces = 6;

	// Top-left corner of each face in face sizes, in TerragenUE.ush's face order: Front, Left, Back, Right, Up, Down.
	const FVector2D FaceOrigin[] = { {2.0, 1.5}, {1.0, 1.5}, {0.0, 1.5}, {3.0, 1.5}, {1.0, 0.5}, {1.0, 2.5} };

	// Names of the material parameters the setup script creates.
	const FName CameraPos(TEXT("CameraPos"));
	const FName AtlasHeading(TEXT("AtlasHeading"));
	const FName FaceSize(TEXT("FaceSize"));
	const FName SkyFaceSize(TEXT("SkyFaceSize"));
	const FName SkyAtlas(TEXT("SkyAtlas"));
	const FName StartDistance(TEXT("StartDistance"));
	const FName HeadingAtPlusX(TEXT("HeadingAtPlusX"));
	const FName CloudQuality(TEXT("CloudQuality"));
	const FName AtmosQuality(TEXT("AtmosQuality"));
	const FName Mode(TEXT("Mode"));                    // sky material: 0 sky, 1 reflection sky
	const FName ReflSky(TEXT("ReflSky"));
	const FName ReflFaceSize(TEXT("ReflFaceSize"));
	const FName ReflCameraPos(TEXT("ReflCameraPos"));
	const FName Atlas(TEXT("Atlas"));
	const FName HeightTex(TEXT("HeightTex"));
	const FName NoiseTex(TEXT("NoiseTex"));
	const FName CloudTex(TEXT("CloudTex"));   // only on sky-world render materials
	const FName TerrainClearance(TEXT("TerrainClearance"));

	// TG_CameraRay (Terragen.ush): heading from +y towards +x, pitch up positive, pixel row 0 at the top.
	FVector CameraRay(double HeadingDeg, double PitchDeg, double PixelX, double PixelY, double Size)
	{
		const double H = FMath::DegreesToRadians(HeadingDeg);
		const double P = FMath::DegreesToRadians(PitchDeg);
		const FVector F(FMath::Sin(H) * FMath::Cos(P), FMath::Cos(H) * FMath::Cos(P), FMath::Sin(P));
		const FVector R(FMath::Cos(H), -FMath::Sin(H), 0.0);
		const FVector U = FVector::CrossProduct(R, F);
		const double Half = Size * 0.5;
		return (F + R * ((PixelX - Half) / Half) + U * ((Half - PixelY) / Half)).GetSafeNormal();
	}

	// TG_AtlasDirection (TerragenUE.ush) for a point given in pixels inside a face, as laid out in the atlas.
	FVector FaceDirection(int32 Face, double AtlasHeadingDeg, double LocalX, double LocalY, double Size)
	{
		const double HeadingOffset = Face < 4 ? 90.0 * Face : 270.0;
		const double Pitch = Face == 4 ? 90.0 : (Face == 5 ? -90.0 : 0.0);
		// The side faces are mirrored left-right in the atlas, Up and Down top-bottom.
		const double X = Face < 4 ? Size - LocalX : LocalX;
		const double Y = Face >= 4 ? Size - LocalY : LocalY;
		return CameraRay(AtlasHeadingDeg + HeadingOffset, Pitch, X, Y, Size);
	}
}

ATerragenSkybox::ATerragenSkybox()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the camera has moved this frame, so the sphere stays centred on it and the redraw sees the new view.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RenderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MyStuff/Skybox/Terragen/M_TerragenRender.M_TerragenRender")));
	SkyRenderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MyStuff/Skybox/Terragen/M_TerragenRenderSky.M_TerragenRenderSky")));
	DisplayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MyStuff/Skybox/Terragen/M_TerragenSkyDisplay.M_TerragenSkyDisplay")));

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

void ATerragenSkybox::PostLoad()
{
	Super::PostLoad();
	// The world asset may have changed since the level was saved.
	ApplyWorld(false);
}

void ATerragenSkybox::BeginPlay()
{
	Super::BeginPlay();
	ApplyWorld(true);
	EnsureMaterials();
}

#if WITH_EDITOR
void ATerragenSkybox::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, TerragenWorld) || Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, SunLight))
	{
		ApplyWorld(true);
		ResetDrawing();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, RenderMaterial) || Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, SkyRenderMaterial) ||
	         Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, DisplayMaterial) || Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, TerrainAtlasSize) ||
	         Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, SkyAtlasSize) || Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, ReflectionAtlasSize) ||
	         Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, TilesPerFaceSide) || Name == GET_MEMBER_NAME_CHECKED(ATerragenSkybox, SkyTilesPerFaceSide))
	{
		// The material instances and render targets are made from these; make them again on the next draw.
		ResetDrawing();
	}
	// Reruns construction, which puts the (new) display material on the sphere.
	Super::PostEditChangeProperty(Event);
}
#endif

void ATerragenSkybox::SetTerragenWorld(UTerragenWorld* NewWorld)
{
	TerragenWorld = NewWorld;
	ApplyWorld(true);
	ResetDrawing();
	if (UMaterialInterface* Display = DisplayMaterial.LoadSynchronous())
	{
		SkyMesh->SetMaterial(0, Display);
	}
}

void ATerragenSkybox::ApplyWorld(bool TurnSunLight)
{
	if (!TerragenWorld)
	{
		return;
	}
	RenderMaterial = TerragenWorld->RenderMaterial;
	SkyRenderMaterial = TerragenWorld->SkyRenderMaterial;
	if (!TerragenWorld->DisplayMaterial.IsNull())
	{
		DisplayMaterial = TerragenWorld->DisplayMaterial;
	}
	TerragenStart = TerragenWorld->TerragenStart;
	CentimetresPerUnit = TerragenWorld->CentimetresPerUnit;
	HeadingAtPlusX = TerragenWorld->HeadingAtPlusX;
	AtlasHeading = TerragenWorld->AtlasHeading;
	if (TurnSunLight && SunLight)
	{
		// A Terragen heading H is Unreal yaw H - HeadingAtPlusX; the light points away from the sun.
		SunLight->SetActorRotation(FRotator(-TerragenWorld->SunAltitude, FRotator::NormalizeAxis(TerragenWorld->SunHeading - HeadingAtPlusX - 180.f), 0.f));
	}
}

void ATerragenSkybox::ResetDrawing()
{
	// EnsureMaterials makes everything again, and the first draw after it redraws every atlas completely.
	TerrainMID = nullptr;
	SkyMID = nullptr;
	ReflectionMID = nullptr;
	DisplayMID = nullptr;
	TerrainTarget = nullptr;
	SkyTarget = nullptr;
	ReflectionTarget = nullptr;
	Terrain = FAtlasState();
	Sky = FAtlasState();
	Reflection = FAtlasState();
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
		// Drawing now would fill tiles with the fallback material and mark them as done.
		return;
	}

	float Aspect = 16.f / 9.f;
	if (const APlayerController* Controller = Camera->GetOwningPlayerController())
	{
		int32 Width = 0, Height = 0;
		Controller->GetViewportSize(Width, Height);
		if (Width > 0 && Height > 0)
		{
			Aspect = float(Width) / float(Height);
		}
	}
	const FVector ViewDir = TerragenViewDirection(Camera->GetCameraRotation());
	const double HalfAngle = ViewHalfAngle(Camera->GetFOVAngle(), Aspect);
	const FVector Wanted = ToTerragen(Eye);

	// The sky and reflections first, so terrain tiles drawn this frame use the new ones. Until something has
	// been drawn, draw everything, so no atlas starts out partly black.
	if (HasSkyPass())
	{
		const int32 SkyBudget = Sky.AnyDrawn ? SkyTilesPerFrame : Sky.Tiles.Num();
		const TArray<int32> SkyTiles = PickTiles(Sky, Wanted, SkyRedrawDistance / CentimetresPerUnit, ViewDir, HalfAngle, SkyBudget, false);
		if (SkyTiles.Num() > 0)
		{
			DrawSky(SkyTiles, Wanted);
		}
	}
	if (HasReflectionPass())
	{
		const int32 ReflectionBudget = Reflection.AnyDrawn ? ReflectionFacesPerFrame : Reflection.Tiles.Num();
		const TArray<int32> Faces = PickTiles(Reflection, Wanted, SkyRedrawDistance / CentimetresPerUnit, ViewDir, HalfAngle, ReflectionBudget, false);
		if (Faces.Num() > 0)
		{
			DrawReflection(Faces, Wanted);
		}
	}
	const int32 TerrainBudget = Terrain.AnyDrawn ? TerrainTilesPerFrame : Terrain.Tiles.Num();
	const TArray<int32> Tiles = PickTiles(Terrain, Wanted, TerrainRedrawDistance / CentimetresPerUnit, ViewDir, HalfAngle, TerrainBudget, false);
	if (Tiles.Num() > 0)
	{
		DrawTerrain(Tiles, Wanted);
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

FVector ATerragenSkybox::TerragenViewDirection(const FRotator& ViewRotation) const
{
	// TG_FromUEDirection (TerragenUE.ush).
	const FVector UE = ViewRotation.Vector();
	const double H = FMath::DegreesToRadians(HeadingAtPlusX);
	const double X = UE.Y;
	const double Y = UE.X;
	return FVector(X * FMath::Cos(H) + Y * FMath::Sin(H), -X * FMath::Sin(H) + Y * FMath::Cos(H), UE.Z);
}

double ATerragenSkybox::ViewHalfAngle(float FieldOfView, float AspectRatio)
{
	// Half the angle across the view's diagonal: a cone that contains the whole view frustum.
	const double TanH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FieldOfView, 1.f, 170.f) * 0.5));
	const double TanV = TanH / FMath::Max(AspectRatio, 0.01f);
	return FMath::Atan(FMath::Sqrt(TanH * TanH + TanV * TanV));
}

void ATerragenSkybox::RedrawFrom(const FVector& UnrealLocation)
{
	if (!EnsureMaterials())
	{
		return;
	}
	if (!MaterialsReady())
	{
		FinishCompilation();
	}
	const FVector Camera = ToTerragen(UnrealLocation);
	if (HasSkyPass())
	{
		DrawSky(AllTiles(Sky), Camera);
	}
	if (HasReflectionPass())
	{
		DrawReflection(AllTiles(Reflection), Camera);
	}
	DrawTerrain(AllTiles(Terrain), Camera);
}

TArray<int32> ATerragenSkybox::AllTiles(const FAtlasState& State)
{
	TArray<int32> All;
	for (int32 i = 0; i < State.Tiles.Num(); i++)
	{
		All.Add(i);
	}
	return All;
}

void ATerragenSkybox::RedrawFromActorLocation()
{
	RedrawFrom(GetActorLocation());
}

int32 ATerragenSkybox::DrawVisibleTiles(const FVector& UnrealLocation, const FRotator& ViewRotation, float FieldOfView, float AspectRatio, bool ClearFirst)
{
	if (!EnsureMaterials())
	{
		return 0;
	}
	if (!MaterialsReady())
	{
		FinishCompilation();
	}
	const FVector Camera = ToTerragen(UnrealLocation);
	if (ClearFirst)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, TerrainTarget, FLinearColor::Black);
	}
	if (HasSkyPass())
	{
		DrawSky(AllTiles(Sky), Camera);
	}
	if (HasReflectionPass())
	{
		DrawReflection(AllTiles(Reflection), Camera);
	}
	// Every tile counts as stale here (threshold -1); only visibility decides.
	const TArray<int32> Tiles = PickTiles(Terrain, Camera, -1.0, TerragenViewDirection(ViewRotation), ViewHalfAngle(FieldOfView, AspectRatio), Terrain.Tiles.Num(), true);
	DrawTerrain(Tiles, Camera);
	return Tiles.Num();
}

float ATerragenSkybox::MeasureRedrawMilliseconds(int32 Iterations)
{
	if (!EnsureMaterials() || Iterations < 1)
	{
		return -1.f;
	}
	// Reading a pixel back waits for all queued GPU work, before and after the timed draws.
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, TerrainTarget, 0, 0);
	const double Start = FPlatformTime::Seconds();
	for (int32 i = 0; i < Iterations; i++)
	{
		RedrawFrom(GetActorLocation());
	}
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, TerrainTarget, 0, 0);
	return float((FPlatformTime::Seconds() - Start) * 1000.0 / Iterations);
}

float ATerragenSkybox::MeasurePassMilliseconds(bool SkyPass, int32 Iterations)
{
	if (!EnsureMaterials() || Iterations < 1 || (SkyPass && !HasSkyPass()))
	{
		return -1.f;
	}
	const FVector Camera = ToTerragen(GetActorLocation());
	const TArray<int32> All = AllTiles(SkyPass ? Sky : Terrain);
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, TerrainTarget, 0, 0);
	const double Start = FPlatformTime::Seconds();
	for (int32 i = 0; i < Iterations; i++)
	{
		SkyPass ? DrawSky(All, Camera) : DrawTerrain(All, Camera);
	}
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, SkyPass ? SkyTarget : TerrainTarget, 0, 0);
	return float((FPlatformTime::Seconds() - Start) * 1000.0 / Iterations);
}

float ATerragenSkybox::MeasureReflectionMilliseconds(int32 Iterations)
{
	if (!EnsureMaterials() || Iterations < 1 || !HasReflectionPass())
	{
		return -1.f;
	}
	const FVector Camera = ToTerragen(GetActorLocation());
	const TArray<int32> All = AllTiles(Reflection);
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, TerrainTarget, 0, 0);
	const double Start = FPlatformTime::Seconds();
	for (int32 i = 0; i < Iterations; i++)
	{
		DrawReflection(All, Camera);
	}
	// A floating-point target reads back as linear colour; any read waits for the GPU all the same.
	UKismetRenderingLibrary::ReadRenderTargetRawPixel(this, ReflectionTarget, 0, 0);
	return float((FPlatformTime::Seconds() - Start) * 1000.0 / Iterations);
}

float ATerragenSkybox::MeasureVisibleMilliseconds(const FVector& UnrealLocation, const FRotator& ViewRotation, float FieldOfView, float AspectRatio, int32 Iterations)
{
	if (!EnsureMaterials() || Iterations < 1)
	{
		return -1.f;
	}
	const FVector Camera = ToTerragen(UnrealLocation);
	const TArray<int32> Tiles = PickTiles(Terrain, Camera, -1.0, TerragenViewDirection(ViewRotation), ViewHalfAngle(FieldOfView, AspectRatio), Terrain.Tiles.Num(), true);
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, TerrainTarget, 0, 0);
	const double Start = FPlatformTime::Seconds();
	for (int32 i = 0; i < Iterations; i++)
	{
		DrawTerrain(Tiles, Camera);
	}
	UKismetRenderingLibrary::ReadRenderTargetPixel(this, TerrainTarget, 0, 0);
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

bool ATerragenSkybox::MaterialsReady() const
{
	for (const UMaterialInstanceDynamic* MID : { TerrainMID.Get(), SkyMID.Get(), ReflectionMID.Get(), DisplayMID.Get() })
	{
		if (MID && MID->GetMaterial()->IsCompiling())
		{
			return false;
		}
	}
	if (!TerrainMID || !DisplayMID)
	{
		return false;
	}
#if WITH_EDITOR
	// In the editor, textures build asynchronously after loading. Until then the height texture reads as a
	// placeholder and the whole sky would render as terrain. Sky worlds also read a cloud map.
	for (const UMaterialInstanceDynamic* MID : { TerrainMID.Get(), SkyMID.Get() })
	{
		for (const FName Name : { TerragenSkyboxAtlas::HeightTex, TerragenSkyboxAtlas::NoiseTex, TerragenSkyboxAtlas::CloudTex })
		{
			UTexture* Texture = nullptr;
			if (MID && MID->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Texture) && Texture && Texture->IsCompiling())
			{
				return false;
			}
		}
	}
#endif
	return true;
}

UTextureRenderTarget2D* ATerragenSkybox::CreateAtlas(int32 Size, bool FloatingPoint) const
{
	// 8-bit linear, so the shader's display values are stored as they are (like Terragen's 8-bit output); the
	// reflection atlas holds linear colour, which goes above 1, so it is half-float.
	UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(const_cast<ATerragenSkybox*>(this), Size, Size, FloatingPoint ? RTF_RGBA16f : RTF_RGBA8, FLinearColor::Black, false);
	if (Target)
	{
		Target->AddressX = TA_Clamp;
		Target->AddressY = TA_Clamp;
		Target->UpdateResourceImmediate(true);
	}
	return Target;
}

void ATerragenSkybox::BuildTiles(FAtlasState& State, int32 AtlasSize, int32 TilesPerSide) const
{
	const double Face = AtlasSize / 4.0;
	const double Step = Face / TilesPerSide;
	State.Tiles.Reset();
	for (int32 F = 0; F < TerragenSkyboxAtlas::NumFaces; F++)
	{
		for (int32 j = 0; j < TilesPerSide; j++)
		{
			for (int32 i = 0; i < TilesPerSide; i++)
			{
				FAtlasTile Tile;
				Tile.Face = F;
				Tile.PixelOrigin = TerragenSkyboxAtlas::FaceOrigin[F] * Face + FVector2D(i * Step, j * Step);
				Tile.PixelSize = Step;
				const double X0 = i * Step, Y0 = j * Step;
				Tile.Direction = TerragenSkyboxAtlas::FaceDirection(F, AtlasHeading, X0 + Step * 0.5, Y0 + Step * 0.5, Face);
				for (const FVector2D Corner : { FVector2D(X0, Y0), FVector2D(X0 + Step, Y0), FVector2D(X0, Y0 + Step), FVector2D(X0 + Step, Y0 + Step) })
				{
					const FVector D = TerragenSkyboxAtlas::FaceDirection(F, AtlasHeading, Corner.X, Corner.Y, Face);
					Tile.Radius = FMath::Max(Tile.Radius, FMath::Acos(FMath::Clamp(FVector::DotProduct(D, Tile.Direction), -1.0, 1.0)));
				}
				State.Tiles.Add(Tile);
			}
		}
	}
	State.DrawnAt.Init(FVector::ZeroVector, State.Tiles.Num());
	State.Drawn.Init(false, State.Tiles.Num());
	State.AnyDrawn = false;
}

TArray<int32> ATerragenSkybox::PickTiles(const FAtlasState& State, const FVector& Camera, double Threshold, const FVector& ViewDir, double HalfAngle, int32 Budget, bool OnlyVisible) const
{
	struct FCandidate { int32 Index; double Angle; double Stale; bool Visible; };
	TArray<FCandidate> Candidates;
	const double Margin = FMath::DegreesToRadians(CullingMargin);
	for (int32 i = 0; i < State.Tiles.Num(); i++)
	{
		const double Stale = State.Drawn[i] ? FVector::Dist(State.DrawnAt[i], Camera) : TNumericLimits<double>::Max();
		if (Stale <= Threshold)
		{
			continue;
		}
		const FAtlasTile& Tile = State.Tiles[i];
		const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Tile.Direction, ViewDir), -1.0, 1.0));
		const bool Visible = Angle <= HalfAngle + Tile.Radius + Margin;
		if (Visible || !OnlyVisible)
		{
			Candidates.Add({ i, Angle, Stale, Visible });
		}
	}
	// Visible tiles first, the most out of date first: when the budget is short of the view, every visible tile
	// still gets its turn instead of the edges waiting until the camera stops. Off-screen tiles catch up with what
	// is left, nearest the view first, so turning finds them ready.
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.Visible != B.Visible)
		{
			return A.Visible;
		}
		return A.Visible && A.Stale != B.Stale ? A.Stale > B.Stale : A.Angle < B.Angle;
	});
	TArray<int32> Picked;
	for (int32 i = 0; i < Candidates.Num() && Picked.Num() < Budget; i++)
	{
		Picked.Add(Candidates[i].Index);
	}
	return Picked;
}

bool ATerragenSkybox::EnsureMaterials()
{
	if (TerrainMID && DisplayMID && TerrainTarget)
	{
		return true;
	}
	UMaterialInterface* Render = RenderMaterial.LoadSynchronous();
	UMaterialInterface* SkyRender = SkyRenderMaterial.LoadSynchronous();
	UMaterialInterface* Display = DisplayMaterial.LoadSynchronous();
	if (!Render || !Display)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerragenSkybox: missing materials; run ClimbingGameAssets/TerragenShader/Tools/ue_setup.py"));
		return false;
	}
	TerrainTarget = CreateAtlas(TerrainAtlasSize);
	BuildTiles(Terrain, TerrainAtlasSize, TilesPerFaceSide);
	const float TerrainFace = TerrainAtlasSize / 4.f;
	TerrainMID = UMaterialInstanceDynamic::Create(Render, this);
	TerrainMID->SetScalarParameterValue(TerragenSkyboxAtlas::FaceSize, TerrainFace);
	// Without a reflection atlas the terrain pass computes reflections itself (ReflFaceSize 0).
	TerrainMID->SetScalarParameterValue(TerragenSkyboxAtlas::ReflFaceSize, 0.f);
	if (SkyRender)
	{
		SkyTarget = CreateAtlas(SkyAtlasSize);
		BuildTiles(Sky, SkyAtlasSize, SkyTilesPerFaceSide);
		SkyMID = UMaterialInstanceDynamic::Create(SkyRender, this);
		SkyMID->SetScalarParameterValue(TerragenSkyboxAtlas::FaceSize, SkyAtlasSize / 4.f);
		SkyMID->SetScalarParameterValue(TerragenSkyboxAtlas::Mode, 0.f);
		TerrainMID->SetTextureParameterValue(TerragenSkyboxAtlas::SkyAtlas, SkyTarget);
		TerrainMID->SetScalarParameterValue(TerragenSkyboxAtlas::SkyFaceSize, SkyAtlasSize / 4.f);
		if (ReflectionAtlasSize > 0)
		{
			ReflectionTarget = CreateAtlas(ReflectionAtlasSize, true);
			BuildTiles(Reflection, ReflectionAtlasSize, 1);
			ReflectionMID = UMaterialInstanceDynamic::Create(SkyRender, this);
			ReflectionMID->SetScalarParameterValue(TerragenSkyboxAtlas::FaceSize, ReflectionAtlasSize / 4.f);
			ReflectionMID->SetScalarParameterValue(TerragenSkyboxAtlas::Mode, 1.f);
			TerrainMID->SetTextureParameterValue(TerragenSkyboxAtlas::ReflSky, ReflectionTarget);
			TerrainMID->SetScalarParameterValue(TerragenSkyboxAtlas::ReflFaceSize, ReflectionAtlasSize / 4.f);
		}
	}
	DisplayMID = UMaterialInstanceDynamic::Create(Display, this);
	DisplayMID->SetTextureParameterValue(TerragenSkyboxAtlas::Atlas, TerrainTarget);
	DisplayMID->SetScalarParameterValue(TerragenSkyboxAtlas::FaceSize, TerrainFace);
	SkyMesh->SetMaterial(0, DisplayMID);
	return true;
}

void ATerragenSkybox::SetSkyQuality(UMaterialInstanceDynamic* Material) const
{
	Material->SetScalarParameterValue(TerragenSkyboxAtlas::CloudQuality, CloudQuality);
	Material->SetScalarParameterValue(TerragenSkyboxAtlas::AtmosQuality, AtmosQuality);
	// The sky pass skips directions the terrain hides, which depends on the terrain left out in front.
	Material->SetScalarParameterValue(TerragenSkyboxAtlas::StartDistance, StartDistance);
}

void ATerragenSkybox::DrawSky(TConstArrayView<int32> Tiles, const FVector& Camera)
{
	SetSkyQuality(SkyMID);
	DrawTiles(SkyTarget, SkyMID, Sky, Tiles, Camera);
	// Terrain tiles that overlap these took their sky from the old ones: redraw them too.
	for (int32 i = 0; i < Terrain.Tiles.Num(); i++)
	{
		const FAtlasTile& T = Terrain.Tiles[i];
		for (const int32 Index : Tiles)
		{
			const FAtlasTile& S = Sky.Tiles[Index];
			if (FMath::Acos(FMath::Clamp(FVector::DotProduct(T.Direction, S.Direction), -1.0, 1.0)) <= T.Radius + S.Radius)
			{
				Terrain.Drawn[i] = false;
				break;
			}
		}
	}
}

void ATerragenSkybox::DrawReflection(TConstArrayView<int32> Faces, const FVector& Camera)
{
	SetSkyQuality(ReflectionMID);
	DrawTiles(ReflectionTarget, ReflectionMID, Reflection, Faces, Camera);
	// The terrain pass aims its lookups from where the reflection atlas was drawn. Water anywhere can reflect
	// any of it, so every terrain tile is out of date now.
	TerrainMID->SetVectorParameterValue(TerragenSkyboxAtlas::ReflCameraPos, FLinearColor(Camera.X, Camera.Y, Camera.Z, 0.f));
	for (int32 i = 0; i < Terrain.Tiles.Num(); i++)
	{
		Terrain.Drawn[i] = false;
	}
}

void ATerragenSkybox::DrawTerrain(TConstArrayView<int32> Tiles, const FVector& Camera)
{
	TerrainMID->SetScalarParameterValue(TerragenSkyboxAtlas::StartDistance, StartDistance);
	DisplayMID->SetScalarParameterValue(TerragenSkyboxAtlas::AtlasHeading, AtlasHeading);
	DisplayMID->SetScalarParameterValue(TerragenSkyboxAtlas::HeadingAtPlusX, HeadingAtPlusX);
	DrawTiles(TerrainTarget, TerrainMID, Terrain, Tiles, Camera);
}

void ATerragenSkybox::DrawTiles(UTextureRenderTarget2D* Target, UMaterialInstanceDynamic* Material, FAtlasState& State, TConstArrayView<int32> Tiles, const FVector& Camera)
{
	Material->SetVectorParameterValue(TerragenSkyboxAtlas::CameraPos, FLinearColor(Camera.X, Camera.Y, Camera.Z, 0.f));
	Material->SetScalarParameterValue(TerragenSkyboxAtlas::AtlasHeading, AtlasHeading);
	// The shader takes the clearance in Terragen units; negative switches it off.
	Material->SetScalarParameterValue(TerragenSkyboxAtlas::TerrainClearance, KeepAboveTerrain ? TerrainClearance / CentimetresPerUnit : -1.f);

	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
	if (Canvas)
	{
		for (const int32 Index : Tiles)
		{
			// Only the tile's square is drawn; its UVs are the matching part of the whole atlas.
			const FAtlasTile& Tile = State.Tiles[Index];
			const FVector2D TileSize(Tile.PixelSize);
			Canvas->K2_DrawMaterial(Material, Tile.PixelOrigin, TileSize, Tile.PixelOrigin / Size, TileSize / Size);
			State.DrawnAt[Index] = Camera;
			State.Drawn[Index] = true;
		}
		State.AnyDrawn = true;
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}
