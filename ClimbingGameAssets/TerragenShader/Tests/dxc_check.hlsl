// Compiles the shaders the Unreal materials use as real pixel shaders, to catch HLSL-only errors.
// RENDER_ATLAS selects the atlas renderer (M_TerragenRender); otherwise the sky lookup (M_TerragenSkyDisplay).
#include "TerragenUE.ush"
#include "Worlds/MountainTop.ush"

Texture2D HeightTex : register(t0);
Texture2D NoiseTex : register(t1);
Texture2D Atlas : register(t2);
SamplerState AtlasSampler : register(s0);

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
	return TG_RenderAtlasTexel(TGWorld_MountainTop(), HeightTex, NoiseTex, uv, CameraPos, AtlasHeading, FaceSize, StartDistance, Clearance);
#else
	float2 a = TG_AtlasUV(TG_FromUEDirection(ViewDir, HeadingAtPlusX), AtlasHeading, FaceSize);
	float3 c = TG_SRGBToLinear(Atlas.SampleLevel(AtlasSampler, a, 0.0).rgb);
	return float4(c, 1.0);
#endif
}
