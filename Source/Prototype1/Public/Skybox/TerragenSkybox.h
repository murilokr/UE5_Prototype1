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
 * Three atlases, all laid out like the old skybox crosses made by Scripts/GenerateSkyboxTexture.py:
 *  - the sky atlas (SkyRenderMaterial): sky, clouds and sun only. It changes slowly with the camera and is the
 *    costly part per pixel, so it is small and redrawn rarely, a few tiles at a time. Directions where the
 *    terrain always hides the sky are skipped.
 *  - the reflection atlas (SkyRenderMaterial in reflection mode, floating point): the sky as the water reflects
 *    it, which the terrain pass would otherwise compute for every water texel. Small and redrawn rarely too.
 *  - the terrain atlas (RenderMaterial): the terrain, with rays that miss it filled in from the sky atlas, so it
 *    is the whole picture. A large sphere centred on the camera shows it through DisplayMaterial.
 * The atlases are split into tiles. Every frame, terrain tiles drawn from more than TerrainRedrawDistance away
 * are redrawn: the ones inside the player camera's view first, the most out of date first, up to
 * TerrainTilesPerFrame. Off-screen tiles catch up with what is left, nearest the view first.
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

	/** Draws both atlases completely, as seen from an Unreal location. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox")
	void RedrawFrom(const FVector& UnrealLocation);

	/** Draws the sky as seen from this actor, for previewing in the editor. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Terragen Skybox")
	void RedrawFromActorLocation();

	/** The terrain atlas the sky sphere shows (the whole picture). Valid once something has been drawn. */
	UFUNCTION(BlueprintPure, Category = "Terragen Skybox")
	UTextureRenderTarget2D* GetAtlas() const { return TerrainTarget; }

	/** The sky-only atlas. */
	UFUNCTION(BlueprintPure, Category = "Terragen Skybox")
	UTextureRenderTarget2D* GetSkyAtlas() const { return SkyTarget; }

	/** The reflection-sky atlas (linear colour), or null when ReflectionAtlasSize is 0. */
	UFUNCTION(BlueprintPure, Category = "Terragen Skybox")
	UTextureRenderTarget2D* GetReflectionAtlas() const { return ReflectionTarget; }

	/** Average wall time of redrawing both atlases completely, in milliseconds, waiting for the GPU to finish. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	float MeasureRedrawMilliseconds(int32 Iterations = 4);

	/** Like MeasureRedrawMilliseconds, for one pass: the whole sky atlas, or the whole terrain atlas. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	float MeasurePassMilliseconds(bool SkyPass, int32 Iterations = 4);

	/** Like MeasurePassMilliseconds, for the whole reflection atlas. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	float MeasureReflectionMilliseconds(int32 Iterations = 4);

	/**
	 * Draws the whole sky and reflection atlases, then only the terrain tiles a camera with this view can see
	 * (any number of them). Clears the terrain atlas first when ClearFirst is set, so a screenshot from that view shows whether
	 * the culling missed anything. Returns the number of terrain tiles drawn.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	int32 DrawVisibleTiles(const FVector& UnrealLocation, const FRotator& ViewRotation, float FieldOfView, float AspectRatio, bool ClearFirst);

	/** Average wall time of redrawing only the terrain tiles visible in that view, in milliseconds. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	float MeasureVisibleMilliseconds(const FVector& UnrealLocation, const FRotator& ViewRotation, float FieldOfView, float AspectRatio, int32 Iterations = 4);

	/** Blocks until pending texture, mesh and shader builds are done. Editor only; a no-op in games. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Debug")
	static void FinishCompilation();

	/** Material that renders the terrain atlas, filling sky from the sky atlas (M_TerragenRender). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TSoftObjectPtr<UMaterialInterface> RenderMaterial;

	/** Material that renders the sky-only and reflection atlases (M_TerragenRenderSky). Without it, RenderMaterial must render everything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TSoftObjectPtr<UMaterialInterface> SkyRenderMaterial;

	/** Material on the sky sphere that looks the view direction up in the atlas (M_TerragenSkyDisplay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TSoftObjectPtr<UMaterialInterface> DisplayMaterial;

	/** Size of the terrain atlas in pixels, four faces wide; 2048 gives 512-pixel faces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Quality", meta = (ClampMin = "256", ClampMax = "8192"))
	int32 TerrainAtlasSize = 2048;

	/** Size of the sky atlas. The sky is soft, so it can be smaller than the terrain atlas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Quality", meta = (ClampMin = "128", ClampMax = "8192"))
	int32 SkyAtlasSize = 1024;

	/**
	 * Size of the reflection atlas, the sky the water reflects. Waves blur reflections, so it can be small.
	 * 0 computes every reflection exactly in the terrain pass instead, which costs much more over water.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Quality", meta = (ClampMin = "0", ClampMax = "4096"))
	int32 ReflectionAtlasSize = 512;

	/** Terrain tiles per face side; 4 gives 16 tiles per face (128 pixels each at a 2048 atlas). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Quality", meta = (ClampMin = "1", ClampMax = "16"))
	int32 TilesPerFaceSide = 4;

	/** Sky atlas tiles per face side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Quality", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SkyTilesPerFaceSide = 2;

	/** Scales the number of cloud samples in the sky passes (1 = the world's own). 0.5 stays within a level or two of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Quality", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float CloudQuality = 0.5f;

	/**
	 * Scales the number of atmosphere steps in the sky passes (1 = the world's own). 0.5 is Terragen's atmosphere
	 * accuracy one notch lower: the sky pass costs about 40% less, but the sky under the clouds comes out up to
	 * 10 levels brighter in the Prototype2World test.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Quality", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float AtmosQuality = 1.f;

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

	/** Camera movement in centimetres after which a terrain tile is redrawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Updates", meta = (ClampMin = "0.0"))
	float TerrainRedrawDistance = 5.f;

	/** Camera movement in centimetres after which a sky or reflection tile is redrawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Updates", meta = (ClampMin = "0.0"))
	float SkyRedrawDistance = 2000.f;

	/**
	 * Most terrain tiles redrawn per frame (visible ones first). With enough for every visible tile (about 40 for
	 * a 90-degree view at 4 tiles per face side) the whole view is redrawn each frame; fewer spreads it over
	 * frames. The very first draw always does all of them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Updates", meta = (ClampMin = "1"))
	int32 TerrainTilesPerFrame = 48;

	/** Most sky tiles redrawn per frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Updates", meta = (ClampMin = "1"))
	int32 SkyTilesPerFrame = 2;

	/** Most reflection faces redrawn per frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Updates", meta = (ClampMin = "1", ClampMax = "6"))
	int32 ReflectionFacesPerFrame = 1;

	/** Extra angle, in degrees, around the camera's view when deciding which tiles it can see. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Updates", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float CullingMargin = 5.f;

	/** Radius of the sky sphere in centimetres; level geometry beyond it is hidden by the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "1000.0"))
	float SkyRadius = 1000000.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TObjectPtr<UStaticMeshComponent> SkyMesh;

private:
	/** A square of an atlas: where it is, and the cone of Terragen view directions it covers. */
	struct FAtlasTile
	{
		int32 Face = 0;
		FVector2D PixelOrigin = FVector2D::ZeroVector;
		double PixelSize = 0.0;
		FVector Direction = FVector::ForwardVector;
		double Radius = 0.0;   // radians from Direction to the furthest corner
	};

	/** Drawing state of one atlas: its tiles and where each was last drawn from. */
	struct FAtlasState
	{
		TArray<FAtlasTile> Tiles;
		TArray<FVector> DrawnAt;
		TArray<bool> Drawn;
		bool AnyDrawn = false;
	};

	bool EnsureMaterials();
	bool MaterialsReady() const;
	UTextureRenderTarget2D* CreateAtlas(int32 Size, bool FloatingPoint = false) const;
	void BuildTiles(FAtlasState& State, int32 AtlasSize, int32 TilesPerSide) const;
	FVector TerragenViewDirection(const FRotator& ViewRotation) const;
	static double ViewHalfAngle(float FieldOfView, float AspectRatio);
	TArray<int32> PickTiles(const FAtlasState& State, const FVector& Camera, double Threshold, const FVector& ViewDir, double HalfAngle, int32 Budget, bool OnlyVisible) const;
	static TArray<int32> AllTiles(const FAtlasState& State);
	void SetSkyQuality(UMaterialInstanceDynamic* Material) const;
	void DrawSky(TConstArrayView<int32> Tiles, const FVector& Camera);
	void DrawReflection(TConstArrayView<int32> Faces, const FVector& Camera);
	void DrawTerrain(TConstArrayView<int32> Tiles, const FVector& Camera);
	void DrawTiles(UTextureRenderTarget2D* Target, UMaterialInstanceDynamic* Material, FAtlasState& State, TConstArrayView<int32> Tiles, const FVector& Camera);
	bool HasSkyPass() const { return SkyMID != nullptr; }
	bool HasReflectionPass() const { return ReflectionMID != nullptr; }

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TerrainMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkyMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ReflectionMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMID;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TerrainTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> SkyTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ReflectionTarget;

	FAtlasState Terrain;
	FAtlasState Sky;
	FAtlasState Reflection;
};
