// Compiles the shaders the Unreal materials use as real pixel shaders, to catch HLSL-only errors.
// RENDER_ATLAS selects the atlas renderer (M_TerragenRender); otherwise the sky lookup (M_TerragenSkyDisplay).
// SKY_WORLD renders with Prototype2World (sky, clouds, atmosphere, water reflection on) instead of
// MountainTop, so the sky code is compiled live rather than folded away.
// LEGACY_MATERIAL=1 makes exactly the calls of the Custom nodes Tools/ue_setup.py wrote into the first version
// of the user's materials, with the same input names and types, so a shader change can't break existing assets.
// LEGACY_MATERIAL=2 does the same for the current split materials: RENDER_ATLAS=1 is M_TerragenRender (terrain
// pass filled from the sky atlas), RENDER_ATLAS=2 is M_TerragenRenderSky (sky pass). LEGACY_MATERIAL=3 is what
// Tools/ue_setup.py writes now.
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

#if LEGACY_MATERIAL == 3

// The current materials: M_TerragenRender with the reflection-sky atlas (RENDER_ATLAS=1), M_TerragenRenderSky
// with the sky cull, the reflection mode and the atmosphere quality (RENDER_ATLAS=2).
Texture2D SkyAtlas : register(t4);
SamplerState SkyAtlasSampler : register(s1);
Texture2D ReflSky : register(t5);

cbuffer Params : register(b0)
{
	float4 CameraPos;
	float4 ReflCameraPos;
	float AtlasHeading;
	float FaceSize;
	float StartDistance;
	float TerrainClearance;
	float SkyFaceSize;
	float ReflFaceSize;
	float CloudQuality;
	float AtmosQuality;
	float Mode;
};

#if SKY_WORLD
#define TG_CHECK_CLOUDS CloudTex
#define TG_CHECK_SKY_CLOUDS CloudTex
#else
#define TG_CHECK_CLOUDS NoiseTex
#define TG_CHECK_SKY_CLOUDS HeightTex
#endif

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
#if RENDER_ATLAS == 2
	TGWorld W = TG_SkyQuality(TG_CHECK_WORLD(), CloudQuality, AtmosQuality);
	return TG_RenderAtlasTexelSky(W, HeightTex, TG_CHECK_SKY_CLOUDS, uv, CameraPos.xyz, AtlasHeading, FaceSize, StartDistance, TerrainClearance, Mode);
#else
	return TG_RenderAtlasTexelComposite(TG_CHECK_WORLD(), HeightTex, NoiseTex, TG_CHECK_CLOUDS, SkyAtlas, SkyAtlasSampler, ReflSky, uv, CameraPos.xyz, ReflCameraPos.xyz, AtlasHeading, FaceSize, SkyFaceSize, ReflFaceSize, StartDistance, TerrainClearance);
#endif
}

#elif LEGACY_MATERIAL == 2

Texture2D SkyAtlas : register(t4);
SamplerState SkyAtlasSampler : register(s1);

cbuffer Params : register(b0)
{
	float4 CameraPos;
	float AtlasHeading;
	float FaceSize;
	float StartDistance;
	float TerrainClearance;
	float SkyFaceSize;
	float CloudQuality;
};

#if SKY_WORLD
#define TG_CHECK_CLOUDS CloudTex
#define TG_CHECK_SKY_CLOUDS CloudTex
#else
#define TG_CHECK_CLOUDS NoiseTex
#define TG_CHECK_SKY_CLOUDS HeightTex
#endif

// M_TerragenRender's "Terragen terrain texel" node.
float4 TerrainTexel(float2 UV)
{
	return TG_RenderAtlasTexelComposite(TG_CHECK_WORLD(), HeightTex, NoiseTex, TG_CHECK_CLOUDS, SkyAtlas, SkyAtlasSampler, UV, CameraPos.xyz, AtlasHeading, FaceSize, SkyFaceSize, StartDistance, TerrainClearance);
}

// M_TerragenRenderSky's "Terragen sky texel" node.
float4 SkyTexel(float2 UV)
{
	TGWorld W = TG_CHECK_WORLD();
	W.Sky.CloudSamples = max(1.0, floor(W.Sky.CloudSamples * CloudQuality + 0.5));
	return TG_RenderAtlasTexelSky(W, HeightTex, TG_CHECK_SKY_CLOUDS, UV, CameraPos.xyz, AtlasHeading, FaceSize, TerrainClearance);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
#if RENDER_ATLAS == 2
	return SkyTexel(uv);
#else
	return TerrainTexel(uv);
#endif
}

#elif LEGACY_MATERIAL

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
