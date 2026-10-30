#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TerragenWorld.generated.h"

class UMaterialInterface;

/**
 * One Terragen world for ATerragenSkybox: the materials the plugin's Tools/ue_setup.py generates for it, and how
 * its Terragen coordinates line up with the level. The world itself (terrain layers, sun, water, sky and so on) is
 * compiled into those materials' shaders from Shaders/Worlds/<Name>.ush, so a world is swapped by swapping this
 * asset, not by editing values.
 *
 * ue_setup.py creates one per world in Tools/worlds.json, as /Terragen/Worlds/<Name>/DA_TerragenWorld_<Name>.
 */
UCLASS(BlueprintType)
class TERRAGEN_API UTerragenWorld : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Name shown in tools and menus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World")
	FText DisplayName;

	/** The terrain pass (M_TerragenRender_<Name>). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Materials")
	TSoftObjectPtr<UMaterialInterface> RenderMaterial;

	/** The sky and reflection-sky passes (M_TerragenRenderSky_<Name>). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Materials")
	TSoftObjectPtr<UMaterialInterface> SkyRenderMaterial;

	/** The sky sphere's material. Empty keeps the actor's own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Materials")
	TSoftObjectPtr<UMaterialInterface> DisplayMaterial;

	/** Terragen camera position (x, y in grid points, z in height units) at the skybox actor's location. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Placement")
	FVector TerragenStart = FVector::ZeroVector;

	/** One Terragen unit (a grid step) in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Placement", meta = (ClampMin = "1.0"))
	float CentimetresPerUnit = 1500.f;

	/** Terragen heading (degrees from +y towards +x) seen when looking along Unreal's +X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Placement")
	float HeadingAtPlusX = 18.f;

	/** Heading of the atlas's Front face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Placement")
	float AtlasHeading = 18.f;

	/** The sun the world was rendered with, in Terragen terms; the skybox turns its SunLight to match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Sun")
	float SunHeading = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Sun", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float SunAltitude = 45.f;

	/** Angular diameter of the sun disc in degrees; the skybox gives SunLight this source angle, for Terragen-like soft shadows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Sun", meta = (ClampMin = "0.0"))
	float SunDiameter = 1.f;

	/**
	 * The world's sunlight at TerragenStart, linear, in Terragen's units (1 = white): the sun colour through the
	 * atmosphere (TG_MeshLightProbe in TerragenMesh.ush, measured by Tools/ue_mesh_setup.py). The skybox gives
	 * SunLight pi times this, so Unreal's Lambert shading of a mesh matches Terragen's light. Black = leave the
	 * light alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Light")
	FLinearColor SunColour = FLinearColor::Black;

	/** Light function for SunLight: the clouds' shadow (or uniform dimming) at each lit point (M_TerragenSunLight_<Name>). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Light")
	TSoftObjectPtr<UMaterialInterface> SunLightFunction;

	/** The world's background light as one linear colour; the skybox gives SkyLight a constant cubemap of it. Black = leave the sky light alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Light")
	FLinearColor AmbientColour = FLinearColor::Black;

	/** Lit material that shades any mesh with this world's surface layers (M_TerragenMesh_<Name>). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Meshes")
	TSoftObjectPtr<UMaterialInterface> MeshMaterial;

	/** Unlit variant with Terragen's own light, no mesh shadows (M_TerragenMeshUnlit_<Name>). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Meshes")
	TSoftObjectPtr<UMaterialInterface> MeshUnlitMaterial;

	/** Post-process that replaces the tonemapper with the world's atmosphere, tone curve and atlas resolution (PP_TerragenLook_<Name>). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terragen World|Meshes")
	TSoftObjectPtr<UMaterialInterface> LookMaterial;
};
