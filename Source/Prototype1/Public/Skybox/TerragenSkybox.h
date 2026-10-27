#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerragenSkybox.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * A sky rendered live the way Terragen 0.9 renders terrain (ClimbingGameAssets/TerragenShader).
 *
 * RenderMaterial draws the six skybox faces into RenderTarget, laid out like the old skybox crosses made by
 * Scripts/GenerateSkyboxTexture.py. A face is redrawn once the camera has moved RedrawDistance since it was
 * last drawn. A large sphere centred on the camera shows the result through DisplayMaterial.
 *
 * The actor's location is the Unreal point that corresponds to TerragenStart, i.e. where frame 1 of the old
 * skybox sequence was taken. Moving the camera from there moves the Terragen camera the same distance, in X, Y
 * and Z (or only in Z with FollowHorizontal off, like the old skyboxes).
 */
UCLASS(Blueprintable)
class PROTOTYPE1_API ATerragenSkybox : public AActor
{
	GENERATED_BODY()

public:
	ATerragenSkybox();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Terragen coordinates (x, y in grid points, z in height units) of an Unreal location. */
	UFUNCTION(BlueprintPure, Category = "Terragen Skybox")
	FVector ToTerragen(const FVector& UnrealLocation) const;

	/** Draws all six faces as seen from an Unreal location. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox")
	void RedrawFrom(const FVector& UnrealLocation);

	/** Draws the sky as seen from this actor, for previewing in the editor. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Terragen Skybox")
	void RedrawFromActorLocation();

	/** Average wall time of a full six-face redraw, in milliseconds, waiting for the GPU to finish. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	float MeasureRedrawMilliseconds(int32 Iterations = 4);

	/** Blocks until pending texture, mesh and shader builds are done. Editor only; a no-op in games. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	static void FinishCompilation();

	/** Material that renders the face atlas (M_TerragenRender). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TSoftObjectPtr<UMaterialInterface> RenderMaterial;

	/** Material on the sky sphere that looks the view direction up in the atlas (M_TerragenSkyDisplay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TSoftObjectPtr<UMaterialInterface> DisplayMaterial;

	/** 8-bit linear (RTF_RGBA8) target, four faces wide and four faces tall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TSoftObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** Terragen camera position at this actor's location: frame 1 of the old MountainTop sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox")
	FVector TerragenStart = FVector(164.0, 223.0, 45.647);

	/** One Terragen unit (a grid step) in centimetres; the MountainTop terrain uses 15 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "1.0"))
	float CentimetresPerUnit = 1500.f;

	/** Move the Terragen camera horizontally with the player too. Off reproduces the old skyboxes, which only climbed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox")
	bool FollowHorizontal = true;

	/**
	 * Where the level and the Terragen terrain don't line up, the player can be below the Terragen ground, and the
	 * whole sky would be the inside of the terrain. This lifts the Terragen camera to TerrainClearance above the
	 * ground (or water) under it. Only applies over the terrain's grid.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox")
	bool KeepAboveTerrain = true;

	/** Minimum height above the Terragen ground, in centimetres, when KeepAboveTerrain is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "0.0", EditCondition = "KeepAboveTerrain"))
	float TerrainClearance = 150.f;

	/** Terragen heading (degrees from +y towards +x) seen when looking along Unreal's +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox")
	float HeadingAtPlusX = 18.f;

	/** Heading of the atlas's Front face; 18 matches the old skybox frames. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox")
	float AtlasHeading = 18.f;

	/** Terrain closer than this many Terragen units is left out, so real level geometry can stand in for it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "0.0"))
	float StartDistance = 0.f;

	/** Camera movement in centimetres after which a face is redrawn. 100 matches the old 1 m skybox frames. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "0.0"))
	float RedrawDistance = 100.f;

	/**
	 * How many stale faces may be redrawn per frame. A full six-face redraw of a 2048 atlas measured about 30 ms,
	 * so one face per frame keeps the cost near 5 ms. The very first draw always does all six.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "1", ClampMax = "6"))
	int32 FacesPerFrame = 1;

	/** Radius of the sky sphere in centimetres; level geometry beyond it is hidden by the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "1000.0"))
	float SkyRadius = 1000000.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TObjectPtr<UStaticMeshComponent> SkyMesh;

private:
	static constexpr int32 NumFaces = 6;

	bool EnsureMaterials();
	bool MaterialsReady() const;
	void DrawFaces(TConstArrayView<int32> Faces, const FVector& TerragenCamera);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RenderMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMID;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	FVector FaceDrawnAt[NumFaces];
	bool FaceDrawn[NumFaces] = {};
	int32 NextFace = 0;
};
