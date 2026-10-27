// Compiles the shaders the Unreal materials use as real pixel shaders, to catch HLSL-only errors.
// RENDER_ATLAS selects the atlas renderer (M_TerragenRender); otherwise the sky lookup (M_TerragenSkyDisplay).
// SKY_WORLD renders with Prototype2World (sky, clouds, atmosphere, water reflection on) instead of
// MountainTop, so the sky code is compiled live rather than folded away.
// LEGACY_MATERIAL makes exactly the calls of the Custom nodes Tools/ue_setup.py has already written into the
// user's materials, with the same input names and types, so a shader change can't break existing assets.
#include "TerragenUE.ush"
#if SKY_WORLD
#include "Worlds/Prototype2World.ush"
#define TG_CHECK_WORLD TGWorld_Prototype2World
#else
#include "Worlds/MountainTop.ush"
#define TG_CHECK_WORLD TGWorld_MountainTop
#endif

Texture2D HeightTex : register(t0);
Texture2D NoiseTex : register(t1);
Texture2D Atlas : register(t2);
Texture2D CloudTex : register(t3);
SamplerState AtlasSampler : register(s0);

#if LEGACY_MATERIAL

cbuffer Params : register(b0)
{
	float4 CameraPos;        // a vector parameter, hence .xyz in the node
	float AtlasHeading;
	float FaceSize;
	float StartDistance;
	float TerrainClearance;
	float3 ViewDir;          // CameraVectorWS
	float HeadingAtPlusX;
};

// M_TerragenRender's "Terragen atlas texel" node.
float4 LegacyAtlasTexel(float2 UV)
{
	return TG_RenderAtlasTexel(TGWorld_MountainTop(), HeightTex, NoiseTex, UV, CameraPos.xyz, AtlasHeading, FaceSize, StartDistance, TerrainClearance);
}

// M_TerragenSkyDisplay's "Atlas UV for view direction" and "Display to linear" nodes.
float2 LegacyAtlasLookup()
{
	return TG_AtlasUV(TG_FromUEDirection(-ViewDir, HeadingAtPlusX), AtlasHeading, FaceSize);
}

float3 LegacyDisplayToLinear(float4 C)
{
	return TG_SRGBToLinear(C.rgb);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
#if RENDER_ATLAS
	return LegacyAtlasTexel(uv);
#else
	float4 c = Atlas.SampleLevel(AtlasSampler, LegacyAtlasLookup(), 0.0);
	return float4(LegacyDisplayToLinear(c), 1.0);
#endif
}

#else

cbuffer Params : register(b0)
{
	float3 CameraPos;
	float AtlasHeading;
	float3 ViewDir;
	float FaceSize;
	float HeadingAtPlusX;
	float StartDistance;
	float Clearance;
};

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
#if RENDER_ATLAS
	return TG_RenderAtlasTexel(TG_CHECK_WORLD(), HeightTex, NoiseTex, CloudTex, uv, CameraPos, AtlasHeading, FaceSize, StartDistance, Clearance);
#else
	float2 a = TG_AtlasUV(TG_FromUEDirection(ViewDir, HeadingAtPlusX), AtlasHeading, FaceSize);
	float3 c = TG_SRGBToLinear(Atlas.SampleLevel(AtlasSampler, a, 0.0).rgb);
	return float4(c, 1.0);
#endif
}

#endif
