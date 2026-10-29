#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerragenSkybox.generated.h"

class ADirectionalLight;
class ASkyLight;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UStaticMeshComponent;
class UTerragenWorld;
class UTextureCube;
class UTextureRenderTarget2D;

/** How the terrain atlas, the picture the sky sphere shows, is laid out. */
UENUM(BlueprintType)
enum class ETerragenProjection : uint8
{
	/** Six cube faces in the old skyboxes' cross layout. Sharper towards the faces' edges than at their centres, with a seam along each cube edge. */
	Cube,
	/** One equirectangular sphere map, twice as wide as tall. Equally sharp all round the horizon and seamless; costs about a third more per frame. */
	Sphere,
};

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
 *
 * Which Terragen world is drawn comes from TerragenWorld (a UTerragenWorld asset, DA_TerragenWorld_<Name>): it
 * sets the materials and the placement below. Swap it in the details panel, or at run time with
 * SetTerragenWorld. Without one, the materials and placement are set here directly.
 *
 * The sphere (SkyMesh) only carries the picture: its material looks each pixel's view direction up in the
 * terrain atlas, so the mesh's shape doesn't matter. Projection chooses the atlas's layout.
 */
UCLASS(Blueprintable)
class PROTOTYPE1_API ATerragenSkybox : public AActor
{
	GENERATED_BODY()

public:
	ATerragenSkybox();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

	/**
	 * Switches to another Terragen world: takes its materials and placement, turns SunLight to its sun and starts
	 * over, so the next frame redraws everything (a one-off cost of a full redraw). In the editor, press Redraw
	 * From Actor Location to preview it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox")
	void SetTerragenWorld(UTerragenWorld* NewWorld);

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

	/** The Terragen world to draw. Sets the materials and placement below, which are then read-only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox")
	TObjectPtr<UTerragenWorld> TerragenWorld;

	/**
	 * Optional: a directional light to turn to the world's sun whenever the world is set. In a game it must be
	 * Movable (or Stationary) to turn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox")
	TObjectPtr<ADirectionalLight> SunLight;

	/** Optional: a sky light to give the world's background light (a constant cubemap), for meshes using the world's MeshMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Meshes")
	TObjectPtr<ASkyLight> SkyLight;

	/**
	 * When the world is set, also give SunLight the world's sun colour, intensity and source angle, and SkyLight its
	 * background light, so meshes shaded with the world's MeshMaterial are lit as Terragen lights the terrain.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox|Meshes")
	bool DriveLights = true;

	/**
	 * Where the mesh materials and the look post-process read the placement from (MPC_Terragen): TerragenStart, this
	 * actor's location, CentimetresPerUnit, the headings, the atlas's face size and projection, and SkyRadius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Meshes")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	/** A cubemap of constant white for SkyLight (T_TerragenWhiteCube); tinted and scaled to the world's ambient. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Meshes")
	TSoftObjectPtr<UTextureCube> AmbientCubemap;

	/** Writes the placement to ParameterCollection. Happens on its own whenever it can change. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Meshes")
	void UpdateParameterCollection();

	/** Sets SunLight and SkyLight from the world (see DriveLights). Happens on its own when the world is set. */
	UFUNCTION(BlueprintCallable, Category = "Terragen Skybox|Meshes")
	void ApplyLights();

	/** Material that renders the terrain atlas, filling sky from the sky atlas (M_TerragenRender). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox", meta = (EditCondition = "TerragenWorld == nullptr"))
	TSoftObjectPtr<UMaterialInterface> RenderMaterial;

	/** Material that renders the sky-only and reflection atlases (M_TerragenRenderSky). Without it, RenderMaterial must render everything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox", meta = (EditCondition = "TerragenWorld == nullptr"))
	TSoftObjectPtr<UMaterialInterface> SkyRenderMaterial;

	/** Material on the sky sphere that looks the view direction up in the atlas (M_TerragenSkyDisplay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox", meta = (EditCondition = "TerragenWorld == nullptr"))
	TSoftObjectPtr<UMaterialInterface> DisplayMaterial;

	/** Layout of the terrain atlas: cube faces, or a sphere map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen Skybox|Quality")
	ETerragenProjection Projection = ETerragenProjection::Cube;

	/**
	 * Width of the terrain atlas in pixels. Cube: four faces wide and as tall, 2048 giving 512-pixel faces.
	 * Sphere: half as tall, 2048 giving 0.18 degrees per pixel.
	 */
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

	/** Terrain tiles per face side; 4 gives 16 tiles per face (128 pixels each at a 2048 atlas). A sphere map uses tiles of the same size. */
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (EditCondition = "TerragenWorld == nullptr"))
	FVector TerragenStart = FVector(164.0, 223.0, 45.647);

	/** One Terragen unit (a grid step) in centimetres; the MountainTop terrain uses 13.5 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (ClampMin = "1.0", EditCondition = "TerragenWorld == nullptr"))
	float CentimetresPerUnit = 1350.f;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (EditCondition = "TerragenWorld == nullptr"))
	float HeadingAtPlusX = 18.f;

	/** Heading of the atlas's Front face; 18 matches the old skybox frames. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terragen Skybox", meta = (EditCondition = "TerragenWorld == nullptr"))
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

	void ApplyWorld(bool TurnSunLight);
	void ResetDrawing();
	bool EnsureMaterials();
	bool MaterialsReady() const;
	UTextureRenderTarget2D* CreateAtlas(int32 Size, bool FloatingPoint = false, bool SphereMap = false) const;
	void BuildTiles(FAtlasState& State, int32 AtlasSize, int32 TilesPerSide) const;
	void BuildSphereTiles(FAtlasState& State, int32 AtlasWidth, int32 TilesPerFace) const;
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
