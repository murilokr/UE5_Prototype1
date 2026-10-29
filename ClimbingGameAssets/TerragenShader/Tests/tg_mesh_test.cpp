// tg_mesh_test.cpp - checks Shaders/TerragenMesh.ush (Terragen's shading on meshes, and the look post-process)
// against the terrain renderer it reuses, on the CPU.
//
//   1. surface:    a mesh face lying on the terrain (position on the heightfield, normal from its gradient)
//                  shades like the terrain there: TG_MeshSurface vs TG_SurfaceTree, and TG_ShadeMesh vs
//                  TG_ShadeTerrain, at random points and footprints.
//   2. continuity: the bump-plane blend has no seam: a tiny turn of the normal across a 45-degree boundary
//                  changes the colour no more than the same turn inside one plane's region; every face
//                  direction gives a finite colour and a unit normal.
//   3. atmosphere: TG_AtmosphereFast vs TG_Atmosphere (the per-step march) over random segments, in display
//                  levels after the tone curve, for step budgets of 2 to 16: exact within the budget, and how
//                  far the coarser march is off beyond it.
//   4. mapping:    TG_FromUEOffset matches ATerragenSkybox::ToTerragen; TG_ToUEDirection inverts
//                  TG_FromUEDirection; TG_LinearToSRGB inverts TG_SRGBToLinear on every 8-bit value.
//   5. snapping:   TG_AtlasSnap's corners are texel centres around the direction, within its cube face, with
//                  weights that reproduce the direction's own atlas position (cube and sphere map).
//   5b. jitter:    PP_TerragenSoften's per-texel jitter belongs to the corners' texels, stays in range, is
//                  uniform and the same across the sphere map's seam; its prefilter taps centre on the jittered
//                  point and span half a texel.
//   6. cost:       CPU time per sample of the mesh surface (by bump-level cap), TG_ShadeTerrain, the march
//                  and the fast atmosphere.
// Prints one line per check, PASS or FAIL, and returns the number of failures.
//
// With --gpu <file> it instead compares the GPU's TG_MeshCheckTexel (M_TerragenMeshCheck, written by
// Tools/ue_mesh_setup.py: a first line "start x y z", then lines of "mode x y r g b") with the CPU's.
//
// Usage: tg_mesh_test <height.r16> <noise128.r16> [--clouds file.r8] [--gpu mesh_gpu.txt]
// The world is chosen at compile time, as for tg_render (TG_WORLD_FILE / TG_WORLD_FN, see build.cmd).

#include "hlsl_shim.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <algorithm>

namespace hlsl
{
#include "TerragenMesh.ush"
#ifndef TG_WORLD_FILE
#define TG_WORLD_FILE "Worlds/MountainTop.ush"
#define TG_WORLD_FN TGWorld_MountainTop
#define TG_WORLD_CAMERA TGWorld_MountainTop_CameraPos
#endif
#include TG_WORLD_FILE
}  // namespace hlsl

using namespace hlsl;

#define TG_STR2(x) #x
#define TG_STR(x) TG_STR2(x)

static std::vector<uint16_t> ReadFile16(const char* path, bool eightBit, int& side)
{
	FILE* f = std::fopen(path, "rb");
	if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(100); }
	std::fseek(f, 0, SEEK_END);
	long bytes = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> raw(bytes);
	if (std::fread(raw.data(), 1, bytes, f) != (size_t)bytes) { std::fprintf(stderr, "short read %s\n", path); std::exit(100); }
	std::fclose(f);
	long n = eightBit ? bytes : bytes / 2;
	std::vector<uint16_t> v(n);
	for (long i = 0; i < n; i++)
		v[i] = eightBit ? (uint16_t)(raw[i] * 257) : (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
	side = (int)std::lround(std::sqrt((double)n));
	return v;
}

static int failures = 0;

static void Report(bool ok, const char* name, const std::string& detail)
{
	std::printf("%s  %-34s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
	if (!ok) failures++;
}

static std::string Fmt(const char* fmt, ...)
{
	char buf[512];
	va_list a;
	va_start(a, fmt);
	std::vsnprintf(buf, sizeof buf, fmt, a);
	va_end(a);
	return buf;
}

static float MaxAbs3(float3 a, float3 b)
{
	return std::max(std::fabs(a.x - b.x), std::max(std::fabs(a.y - b.y), std::fabs(a.z - b.z)));
}

// Display levels (0..255) between two linear colours after the world's tone curve.
static float LevelDiff(TGWorld W, float3 a, float3 b)
{
	return MaxAbs3(TG_ToneMap(W, a), TG_ToneMap(W, b)) * 255.0f;
}

static double Percentile(std::vector<float> v, double p)
{
	if (v.empty()) return 0.0;
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1)))];
}

static double Mean(const std::vector<float>& v)
{
	double s = 0.0;
	for (float x : v) s += x;
	return v.empty() ? 0.0 : s / v.size();
}

int main(int argc, char** argv)
{
	if (argc < 3) { std::fprintf(stderr, "usage: tg_mesh_test <height.r16> <noise128.r16> [--clouds file.r8]\n"); return 100; }
	const char* cloudPath = nullptr;
	const char* gpuPath = nullptr;
	for (int i = 3; i < argc; i++)
	{
		if (!std::strcmp(argv[i], "--clouds") && i + 1 < argc) cloudPath = argv[++i];
		else if (!std::strcmp(argv[i], "--gpu") && i + 1 < argc) gpuPath = argv[++i];
		else { std::fprintf(stderr, "unknown option %s\n", argv[i]); return 100; }
	}
	TGWorld W = TG_WORLD_FN();
	int hside = 0, nside = 0, cside = 1;
	std::vector<uint16_t> heights = ReadFile16(argv[1], false, hside);
	std::vector<uint16_t> noise = ReadFile16(argv[2], false, nside);
	std::vector<uint16_t> clouds = cloudPath ? ReadFile16(cloudPath, true, cside) : std::vector<uint16_t>(1, 128 * 257);
	Texture2D HeightTex{heights.data(), hside, hside};
	Texture2D NoiseTex{noise.data(), nside, nside};
	Texture2D CloudTex{clouds.data(), cside, cside};
	TGSunInfo S = TG_MakeSun(W);
	const float n = (float)W.Terrain.Cells;
	std::mt19937 rng(12345);
	auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
	std::printf("world %s, %d cells\n", TG_STR(TG_WORLD_FN), W.Terrain.Cells);

	if (gpuPath)
	{
		FILE* f = std::fopen(gpuPath, "r");
		if (!f) { std::fprintf(stderr, "cannot open %s\n", gpuPath); return 100; }
		float3 start;
		if (std::fscanf(f, " start %f %f %f", &start.x, &start.y, &start.z) != 3) { std::fprintf(stderr, "no start line\n"); return 100; }
		std::vector<float> diff[4];
		int mode, x, y;
		float r, g, b;
		while (std::fscanf(f, " %d %d %d %f %f %f", &mode, &x, &y, &r, &g, &b) == 6 && mode >= 0 && mode < 4)
		{
			float3 cpu = TG_MeshCheckTexel(W, HeightTex, NoiseTex, CloudTex, float2((float)x, (float)y), start, (float)mode);
			float3 gpu(r, g, b);
			// Albedo and display colour in levels, normals in degrees, linear colour in display levels.
			if (mode == 1)
			{
				float3 a = normalize(cpu * 2.0f - float3(1, 1, 1)), c = normalize(gpu * 2.0f - float3(1, 1, 1));
				diff[1].push_back(std::acos(std::min(1.0f, dot(a, c))) / TG_DEG);
			}
			else
				diff[mode].push_back(mode == 2 ? LevelDiff(W, cpu, gpu) : MaxAbs3(cpu, gpu) * 255.0f);
		}
		std::fclose(f);
		// probe.txt next to it: M_TerragenMeshProbe at the same start (half floats, so to about 1 part in 1000).
		std::string probePath = std::string(gpuPath);
		probePath = probePath.substr(0, probePath.find_last_of("/\\") + 1) + "probe.txt";
		if (FILE* pf = std::fopen(probePath.c_str(), "r"))
		{
			float3 gs, ga;
			if (std::fscanf(pf, " sun %f %f %f ambient %f %f %f", &gs.x, &gs.y, &gs.z, &ga.x, &ga.y, &ga.z) == 6)
			{
				float4 cs = TG_MeshLightProbe(W, CloudTex, start, 0.0f), ca = TG_MeshLightProbe(W, CloudTex, start, 1.0f);
				float rel = std::max(MaxAbs3(gs, float3(cs.x, cs.y, cs.z)) / std::max(cs.x, 1e-6f), MaxAbs3(ga, float3(ca.x, ca.y, ca.z)) / std::max(ca.z, 1e-6f));
				Report(rel < 2e-3f, "gpu vs cpu: light probe", Fmt("GPU sun %.5f %.5f %.5f vs CPU %.5f %.5f %.5f; ambient %.5f vs %.5f; worst %.2g relative",
				       gs.x, gs.y, gs.z, cs.x, cs.y, cs.z, ga.z, ca.z, rel));
			}
			std::fclose(pf);
		}
		const char* names[4] = {"gpu vs cpu: albedo", "gpu vs cpu: shading normal", "gpu vs cpu: unlit lit colour", "gpu vs cpu: look colour"};
		const char* units[4] = {"levels", "degrees", "display levels", "display levels"};
		for (int m = 0; m < 4; m++)
			Report(!diff[m].empty() && Mean(diff[m]) <= 0.5 && Percentile(diff[m], 0.99) <= 2.0, names[m],
			       Fmt("%zu texels: mean %.3f, p99 %.2f, max %.2f %s", diff[m].size(), Mean(diff[m]), Percentile(diff[m], 0.99), Percentile(diff[m], 1.0), units[m]));
		std::printf("%d failed\n", failures);
		return failures;
	}

	// ---------------------------------------------------------------- 1. surface on the terrain
	{
		std::vector<float> albedo, normal, shade, shadeSteep;
		int exact = 0;
		const int N = 3000;
		for (int s = 0; s < N; s++)
		{
			float x = U(8.0f, n - 8.0f), y = U(8.0f, n - 8.0f);
			// Footprints from close up (a few metres away) to a few kilometres, log-uniform.
			float fp = std::exp2(U(-10.0f, -1.0f));
			float h = TG_TerrainHeight(HeightTex, W.Terrain, x, y);
			float2 g0 = float2(TG_TerrainHeight(HeightTex, W.Terrain, x + 0.5f, y) - TG_TerrainHeight(HeightTex, W.Terrain, x - 0.5f, y),
			                   TG_TerrainHeight(HeightTex, W.Terrain, x, y + 0.5f) - TG_TerrainHeight(HeightTex, W.Terrain, x, y - 0.5f));
			float3 N0 = normalize(float3(-g0.x, -g0.y, 1.0f));
			TGSurface t = TG_SurfaceTree(W, HeightTex, NoiseTex, x, y, g0, fp);
			// The upward plane only, as a face lying on the terrain uses (no blend: the terrain is never that steep
			// in the test's comparison of planes, but force it so the comparison is like for like).
			TGMeshSurface m = TG_MeshSurfaceOnAxis(W, NoiseTex, float3(x, y, h), N0, fp, TG_MAX_LEVELS - 1, 2);
			float3 tn = normalize(float3(-t.Grad.x, -t.Grad.y, 1.0f));
			float da = MaxAbs3(t.Colour, m.Colour) * 255.0f;
			float dn = std::acos(std::min(1.0f, dot(tn, m.Normal))) / TG_DEG;
			albedo.push_back(da);
			normal.push_back(dn);
			if (da == 0.0f && dn < 1e-3f) exact++;
			TGShadeResult rt = TG_ShadeTerrain(W, HeightTex, NoiseTex, CloudTex, S, float3(x, y, h), fp);
			TGShadeResult rm = TG_ShadeMesh(W, HeightTex, NoiseTex, CloudTex, float3(x, y, h), N0, fp, TG_MAX_LEVELS - 1, 1.0f);
			// Up to about 38 degrees TG_MeshSurface uses the upward plane alone, as the terrain does. Steeper, it
			// blends in a side plane's bump pattern (a different, equally Terragen-like pattern): counted apart.
			bool upwardOnly = std::max(std::fabs(N0.x), std::fabs(N0.y)) < 0.8f * std::fabs(N0.z);
			(upwardOnly ? shade : shadeSteep).push_back(LevelDiff(W, rt.Linear, rm.Linear));
		}
		// The only intended difference: the terrain reads the jittered point's height off the heightfield, the
		// mesh off its own tangent plane. Both agree wherever the jitter stays on the same terrain triangle.
		Report(Percentile(albedo, 0.99) <= 2.0 && Mean(albedo) <= 0.25, "surface: albedo vs terrain",
		       Fmt("mean %.3f, p99 %.2f, max %.2f levels; %d of %d identical", Mean(albedo), Percentile(albedo, 0.99), Percentile(albedo, 1.0), exact, N));
		Report(Percentile(normal, 0.99) <= 1.0, "surface: bumped normal vs terrain",
		       Fmt("mean %.4f, p99 %.3f, max %.3f degrees", Mean(normal), Percentile(normal, 0.99), Percentile(normal, 1.0)));
		Report(Percentile(shade, 0.99) <= 2.0 && Mean(shade) <= 0.25, "surface: lit colour vs TG_ShadeTerrain",
		       Fmt("%zu faces up to 38 deg: mean %.3f, p99 %.2f, max %.2f display levels", shade.size(), Mean(shade), Percentile(shade, 0.99), Percentile(shade, 1.0)));
		std::printf("info  %-34s %zu steeper faces (side-plane bump blended in): mean %.2f, median %.2f display levels\n", "surface: steep faces vs terrain",
		            shadeSteep.size(), Mean(shadeSteep), Percentile(shadeSteep, 0.5));
	}

	// ---------------------------------------------------------------- 2. continuity and sanity on any face
	{
		// A blend is continuous if the colour change across a plane boundary shrinks with the turn that crosses it;
		// a seam would leave a jump however small the turn. Terragen's own level choices are integer steps of the
		// coverage, so now and then a tiny turn changes the colour anywhere: the control is the same tiny turn well
		// inside one plane's region.
		std::vector<float> seam, seamSmall, control;
		int bad = 0;
		const int N = 2000;
		for (int s = 0; s < N; s++)
		{
			float3 P(U(8.0f, n - 8.0f), U(8.0f, n - 8.0f), U(W.Terrain.MinHeight, W.Terrain.MaxHeight));
			float fp = std::exp2(U(-9.0f, -3.0f));
			// Any direction: finite colour, unit normal on the same side as the geometric one.
			float3 d = normalize(float3(U(-1, 1), U(-1, 1), U(-1, 1)));
			TGMeshSurface m = TG_MeshSurface(W, NoiseTex, P, d, fp, TG_MAX_LEVELS - 1);
			bool finite = std::isfinite(m.Colour.x) && std::isfinite(m.Colour.y) && std::isfinite(m.Colour.z) && std::isfinite(m.Normal.x);
			if (!finite || std::fabs(length(m.Normal) - 1.0f) > 1e-3f || dot(m.Normal, d) <= 0.0f) bad++;
			// Either side of a plane boundary (|a| = |b| for two components): a tiny turn across it.
			float a = U(0.2f, 0.6f);
			float3 base = s % 3 == 0 ? float3(a, U(-0.3f, 0.3f), a) : (s % 3 == 1 ? float3(a, a, U(-0.3f, 0.3f)) : float3(U(-0.3f, 0.3f), a, a));
			float3 e = s % 3 == 0 ? float3(1e-4f, 0, -1e-4f) : (s % 3 == 1 ? float3(1e-4f, -1e-4f, 0) : float3(0, 1e-4f, -1e-4f));
			TGMeshSurface m1 = TG_MeshSurface(W, NoiseTex, P, normalize(base + e), fp, TG_MAX_LEVELS - 1);
			TGMeshSurface m2 = TG_MeshSurface(W, NoiseTex, P, normalize(base - e), fp, TG_MAX_LEVELS - 1);
			seam.push_back(MaxAbs3(m1.Colour, m2.Colour) * 255.0f);
			TGMeshSurface m3 = TG_MeshSurface(W, NoiseTex, P, normalize(base + e * 0.01f), fp, TG_MAX_LEVELS - 1);
			TGMeshSurface m4 = TG_MeshSurface(W, NoiseTex, P, normalize(base - e * 0.01f), fp, TG_MAX_LEVELS - 1);
			seamSmall.push_back(MaxAbs3(m3.Colour, m4.Colour) * 255.0f);
			// The control: the same component made clearly dominant, so only one plane is used on both sides.
			float3 inside = s % 3 == 0 ? float3(base.x * 2.5f, base.y, base.z) : (s % 3 == 1 ? float3(base.x * 2.5f, base.y, base.z) : float3(base.x, base.y * 2.5f, base.z));
			TGMeshSurface c1 = TG_MeshSurface(W, NoiseTex, P, normalize(inside + e), fp, TG_MAX_LEVELS - 1);
			TGMeshSurface c2 = TG_MeshSurface(W, NoiseTex, P, normalize(inside - e), fp, TG_MAX_LEVELS - 1);
			control.push_back(MaxAbs3(c1.Colour, c2.Colour) * 255.0f);
		}
		Report(bad == 0, "continuity: every face direction", Fmt("%d of %d with a bad colour or normal", bad, N));
		// A turn 100 times smaller should change the colour about 100 times less, but for the control's rare level jumps.
		Report(Mean(seamSmall) <= Mean(seam) * 0.05 + Mean(control) * 2.0 + 0.01, "continuity: across plane boundaries",
		       Fmt("turn 2e-4: mean %.3f, p99 %.2f levels; turn 2e-6: mean %.4f, p99 %.3f; control (one plane, 2e-4) mean %.3f",
		           Mean(seam), Percentile(seam, 0.99), Mean(seamSmall), Percentile(seamSmall, 0.99), Mean(control)));
	}

	// ---------------------------------------------------------------- 3. atmosphere: capped march vs march
	if (W.Atmos.On != 0.0f)
	{
		std::printf("info  atmosphere step %.3g units (%.0f m at 15 m per unit)\n", W.Atmos.Step, W.Atmos.Step * 15.0f);
		// The materials' default budget (Tools/ue_mesh_setup.py) must reach the sky sphere (10 km) uncapped.
		const int defaultSegments = 24;
		float skyUnits = 1000000.0f / 1500.0f;
		Report(std::ceil(skyUnits / W.Atmos.Step) <= defaultSegments, "atmosphere: default budget covers sky",
		       Fmt("%d steps to 10 km, budget %d", (int)std::ceil(skyUnits / W.Atmos.Step), defaultSegments));
		for (int segs : {2, 4, 8, 16})
		{
			// Within the step budget TG_AtmosphereFast is the march (exact); beyond, the march with longer steps.
			std::vector<float> diff, far;
			std::mt19937 r2(777);
			auto V = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(r2); };
			const int N = 600;
			for (int s = 0; s < N; s++)
			{
				float3 cam(V(32.0f, n - 32.0f), V(32.0f, n - 32.0f), 0.0f);
				cam.z = TG_TerrainHeight(HeightTex, W.Terrain, cam.x, cam.y) + V(0.1f, 20.0f);
				// Distances from 15 cm to about 4.5 km (0.01 to 300 units), log-uniform, mostly level or downwards.
				float dist = std::exp2(V(-6.6f, 8.2f));
				float3 dir = normalize(float3(V(-1, 1), V(-1, 1), V(-0.6f, 0.25f)));
				float3 p1 = cam + dir * dist;
				float3 colour(V(0.0f, 0.8f), V(0.0f, 0.8f), V(0.0f, 0.8f));
				float glow = TG_Glow(W, dot(dir, S.Dir), 1.0f, W.Atmos.GlowFlag);
				float3 a = TG_Atmosphere(W, CloudTex, S, cam, p1, glow, colour);
				float3 b = TG_AtmosphereFast(W, CloudTex, S, cam, p1, glow, colour, segs);
				(std::ceil(dist / W.Atmos.Step) <= segs ? diff : far).push_back(LevelDiff(W, a, b));
			}
			Report(Percentile(diff, 1.0) == 0.0, Fmt("atmosphere: <= %d steps is the march", segs).c_str(),
			       Fmt("%zu segments, max %.2f display levels", diff.size(), Percentile(diff, 1.0)));
			// Not a pass/fail check: the coarser march is a fallback for worlds with fine steps, and Terragen's march
			// depends strongly on its step, so this shows what the cap would cost there.
			if (!far.empty())
				std::printf("info  %-34s %zu longer segments: mean %.2f, p99 %.2f, max %.2f display levels\n", Fmt("atmosphere: capped at %d steps", segs).c_str(),
				            far.size(), Mean(far), Percentile(far, 0.99), Percentile(far, 1.0));
		}
	}
	else
	{
		Report(true, "atmosphere", "world has no atmosphere; TG_AtmosphereFast returns the colour");
	}

	// ---------------------------------------------------------------- 4. mapping
	{
		// ATerragenSkybox::ToTerragen (FollowHorizontal on), written out as the actor does it, in double.
		float worst = 0.0f, worstDir = 0.0f;
		for (int s = 0; s < 1000; s++)
		{
			double heading = U(-180.0f, 180.0f), cm = U(100.0f, 3000.0f);
			float3 start(U(0, n), U(0, n), U(-20, 120));
			double ox = U(-5e5f, 5e5f), oy = U(-5e5f, 5e5f), oz = U(-1e5f, 1e5f);
			double H = heading * 3.14159265358979 / 180.0, X = oy / cm, Y = ox / cm;
			float3 actor((float)(start.x + X * std::cos(H) + Y * std::sin(H)), (float)(start.y - X * std::sin(H) + Y * std::cos(H)), (float)(start.z + oz / cm));
			float3 shader = TG_FromUEOffset(float3((float)ox, (float)oy, (float)oz), start, (float)cm, (float)heading);
			worst = std::max(worst, MaxAbs3(actor, shader));
			float3 d = normalize(float3(U(-1, 1), U(-1, 1), U(-1, 1)));
			worstDir = std::max(worstDir, MaxAbs3(d, TG_ToUEDirection(TG_FromUEDirection(d, (float)heading), (float)heading)));
		}
		Report(worst < 2e-3f, "mapping: position vs actor ToTerragen", Fmt("max %.2g units", worst));
		Report(worstDir < 1e-5f, "mapping: direction round trip", Fmt("max %.2g", worstDir));
		int off = 0;
		for (int v = 0; v < 256; v++)
		{
			float x = v / 255.0f;
			float3 back = TG_LinearToSRGB(TG_SRGBToLinear(float3(x, x, x)));
			if (std::fabs(back.x - x) * 255.0f > 0.01f) off++;
		}
		Report(off == 0, "mapping: sky sRGB round trip", Fmt("%d of 256 byte values off by more than 0.01", off));
		// The lit material's light: the light's colour (the probe, at P) times its light function (the clouds) is
		// Terragen's sunlight at P, as TG_ShadeMesh uses it.
		float worstLight = 0.0f;
		for (int s = 0; s < 500; s++)
		{
			float3 P(U(0, n), U(0, n), U(W.Terrain.MinHeight, W.Terrain.MaxHeight + 40.0f));
			float4 probe = TG_MeshLightProbe(W, CloudTex, P, 0.0f);
			float3 lit = float3(probe.x, probe.y, probe.z) * TG_MeshCloudLight(W, CloudTex, P);
			worstLight = std::max(worstLight, MaxAbs3(lit, TG_MeshSunLight(W, CloudTex, S, P)));
		}
		Report(worstLight < 1e-5f, "light: colour x light function", Fmt("max %.2g vs TG_MeshSunLight", worstLight));
		// Where the clouds' shadow is, around the start at its altitude (for placing test meshes in the sun).
		if (W.CloudShadows != 0.0f)
		{
			float3 start = TG_WORLD_CAMERA;
			float best = 1e9f, bx = 0.0f, by = 0.0f;
			int lit = 0, total = 0;
			for (float dy = -40.0f; dy <= 40.0f; dy += 0.5f)
				for (float dx = -40.0f; dx <= 40.0f; dx += 0.5f)
				{
					total++;
					if (TG_MeshCloudLight(W, CloudTex, start + float3(dx, dy, 0.0f)) > 0.9f)
					{
						lit++;
						if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; bx = dx; by = dy; }
					}
				}
			std::printf("info  %-34s %.3f at the world camera; %d of %d points within 40 units in sun (> 0.9), nearest at (%+.1f, %+.1f) units\n",
			            "light: cloud shadow", TG_MeshCloudLight(W, CloudTex, start), lit, total, bx, by);
		}
	}

	// ---------------------------------------------------------------- 5. atlas snapping
	for (float proj : {0.0f, 1.0f})
	{
		const float face = 512.0f, heading = 18.0f;
		float2 size = proj != 0.0f ? float2(4 * face, 2 * face) : float2(4 * face, 4 * face);
		int bad = 0;
		float worst = 0.0f;
		const int N = 20000;
		for (int s = 0; s < N; s++)
		{
			float3 d = normalize(float3(U(-1, 1), U(-1, 1), U(-1, 1)));
			TGAtlasSnap sn = TG_AtlasSnap(d, heading, face, proj);
			float2 uv = TG_ProjectionUV(d, heading, face, proj);
			float3 corners[4] = {sn.D00, sn.D10, sn.D01, sn.D11};
			float2 px[4];
			for (int k = 0; k < 4; k++)
			{
				float2 c = TG_ProjectionUV(corners[k], heading, face, proj);
				px[k] = float2(c.x * size.x - 0.5f, c.y * size.y - 0.5f);
				// A texel centre, inside the direction's own cube face.
				if (std::fabs(px[k].x - std::round(px[k].x)) > 0.02f || std::fabs(px[k].y - std::round(px[k].y)) > 0.02f) bad++;
				if (proj == 0.0f && TG_AtlasFaceAt(float2(c.x * size.x, c.y * size.y), face) != TG_AtlasFaceAt(float2(uv.x * size.x, uv.y * size.y), face)) bad++;
			}
			if (sn.F.x < 0.0f || sn.F.x > 1.0f || sn.F.y < 0.0f || sn.F.y > 1.0f) bad++;
			// The weights put the blend back at the direction's own atlas position (the sphere map's x wraps, so
			// a corner across the seam reads back a whole width away).
			float x1 = px[1].x;
			if (proj != 0.0f && x1 - px[0].x < -size.x * 0.5f) x1 += size.x;
			if (proj != 0.0f && x1 - px[0].x > size.x * 0.5f) x1 -= size.x;
			float bx = px[0].x + (x1 - px[0].x) * sn.F.x, by = px[0].y + (px[2].y - px[0].y) * sn.F.y;
			float ex = std::fabs(bx - (uv.x * size.x - 0.5f));
			if (proj != 0.0f) ex = std::fmin(ex, std::fabs(ex - size.x));
			// Clamped corners at a face's last texel row are on purpose: the weight is then on the other one.
			bool clamped = px[1].x == px[0].x || px[2].y == px[0].y;
			if (!clamped) worst = std::max(worst, std::max(ex, std::fabs(by - (uv.y * size.y - 0.5f))));
		}
		Report(bad == 0 && worst < 0.02f, proj != 0.0f ? "snapping: sphere map" : "snapping: cube cross",
		       Fmt("%d bad corners or weights; blend position off by up to %.3g texels", bad, worst));
	}

	// ---------------------------------------------------------------- 5b. jitter and prefilter (PP_TerragenSoften)
	{
		const float face = 512.0f, heading = 18.0f;
		// The texel indices the snap reports are the texels its directions are (so the jitter belongs to them).
		int badIndex = 0;
		for (float proj : {0.0f, 1.0f})
			for (int s = 0; s < 5000; s++)
			{
				float3 d = normalize(float3(U(-1, 1), U(-1, 1), U(-1, 1)));
				TGAtlasSnap sn = TG_AtlasSnap(d, heading, face, proj);
				float2 t[4] = {sn.T0, float2(sn.T1.x, sn.T0.y), float2(sn.T0.x, sn.T1.y), sn.T1};
				float3 c[4] = {sn.D00, sn.D10, sn.D01, sn.D11};
				for (int k = 0; k < 4; k++)
				{
					float2 uv = TG_ProjectionUV(c[k], heading, face, proj);
					float x = uv.x * sn.Size.x - 0.5f, y = uv.y * sn.Size.y - 0.5f;
					float wx = t[k].x - sn.Size.x * std::floor(t[k].x / sn.Size.x);
					if (std::fabs(x - wx) > 0.02f || std::fabs(y - t[k].y) > 0.02f) badIndex++;
				}
			}
		Report(badIndex == 0, "jitter: texel indices match corners", Fmt("%d of 40000 corners off", badIndex));
		// Range, spread, and the same offset for a texel however it is reached (sphere seam included).
		float lo = 1e9f, hi = -1e9f, sum = 0.0f, sumAbs = 0.0f;
		int seamBad = 0;
		const int N = 20000;
		for (int s = 0; s < N; s++)
		{
			float2 tx((float)(s % 2048), (float)((s * 7) % 1024));
			float2 j = TG_TexelJitter(tx, float2(2048.0f, 1024.0f), 1.0f);
			lo = std::min(lo, std::min(j.x, j.y));
			hi = std::max(hi, std::max(j.x, j.y));
			sum += j.x + j.y;
			sumAbs += std::fabs(j.x) + std::fabs(j.y);
			float2 w = TG_TexelJitter(float2(tx.x - 2048.0f, tx.y), float2(2048.0f, 1024.0f), 1.0f);
			if (w.x != j.x || w.y != j.y) seamBad++;
		}
		Report(lo >= -0.5f && hi < 0.5f && std::fabs(sum / (2 * N)) < 0.01f && std::fabs(sumAbs / (2 * N) - 0.25f) < 0.01f && seamBad == 0,
		       "jitter: range, spread, seam", Fmt("range %.3f..%.3f, mean %.4f, mean |offset| %.4f (uniform: 0.25), %d seam mismatches", lo, hi, sum / (2 * N), sumAbs / (2 * N), seamBad));
		// The four prefilter taps centre on the jittered point and span half a texel; Prefilter 0 is one point.
		float worstTap = 0.0f;
		for (int s = 0; s < 2000; s++)
		{
			float2 centre(U(0, 1), U(0, 1)), jit(U(-0.5f, 0.5f), U(-0.5f, 0.5f));
			float4 ax(U(-0.01f, 0.01f), U(-0.01f, 0.01f), U(-0.01f, 0.01f), U(-0.01f, 0.01f));
			float2 mean(0, 0), p0 = TG_SnapTap(centre, ax, jit, 0.0f, 0);
			for (int k = 0; k < 4; k++)
			{
				float2 t = TG_SnapTap(centre, ax, jit, 1.0f, k);
				mean = mean + t * 0.25f;
				float2 q = TG_SnapTap(centre, ax, jit, 0.0f, k);
				worstTap = std::max(worstTap, std::max(std::fabs(q.x - p0.x), std::fabs(q.y - p0.y)));
			}
			float2 want(centre.x + ax.x * jit.x + ax.z * jit.y, centre.y + ax.y * jit.x + ax.w * jit.y);
			worstTap = std::max(worstTap, std::max(std::fabs(mean.x - want.x), std::fabs(mean.y - want.y)));
			float2 t0 = TG_SnapTap(centre, ax, jit, 1.0f, 0), t3 = TG_SnapTap(centre, ax, jit, 1.0f, 3);
			float2 diag(ax.x * 0.5f + ax.z * 0.5f, ax.y * 0.5f + ax.w * 0.5f);
			worstTap = std::max(worstTap, std::max(std::fabs(t3.x - t0.x - diag.x), std::fabs(t3.y - t0.y - diag.y)));
		}
		Report(worstTap < 1e-6f, "prefilter: tap pattern", Fmt("max %.2g off", worstTap));
		// Screen axes fall back to the other corner pair where a face's last row clamps one.
		float4 a = TG_SnapScreenAxes(float2(0.5f, 0.5f), float2(0.5f, 0.5f), float2(0.5f, 0.51f), float2(0.51f, 0.51f));
		Report(std::fabs(a.x - 0.01f) < 1e-6f && std::fabs(a.w - 0.01f) < 1e-6f, "prefilter: clamped corner axes", Fmt("x axis (%.3f, %.3f), y axis (%.3f, %.3f)", a.x, a.y, a.z, a.w));
	}

	// ---------------------------------------------------------------- 6. cost
	{
		auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::micro>(b - a).count(); };
		const int N = 4000;
		std::vector<float3> P(N), Nn(N);
		std::vector<float> fp(N);
		for (int s = 0; s < N; s++)
		{
			float x = U(8.0f, n - 8.0f), y = U(8.0f, n - 8.0f);
			P[s] = float3(x, y, TG_TerrainHeight(HeightTex, W.Terrain, x, y));
			Nn[s] = normalize(float3(U(-1, 1), U(-1, 1), U(0.2f, 1)));
			// A mesh 2 m to 200 m away with the default 512-pixel atlas faces (2 / 512 radians per texel).
			fp[s] = TG_MeshFootprint(W, U(0.13f, 13.3f), 2.0f / 512.0f);
		}
		float sink = 0.0f;
		std::string line;
		for (int cap : {TG_MAX_LEVELS - 1, 12, 8})
		{
			auto t0 = std::chrono::steady_clock::now();
			for (int s = 0; s < N; s++) sink += TG_MeshSurface(W, NoiseTex, P[s], Nn[s], fp[s], cap).Colour.x;
			line += Fmt("surface cap %d: %.1f us; ", cap, ms(t0, std::chrono::steady_clock::now()) / N);
		}
		auto t1 = std::chrono::steady_clock::now();
		for (int s = 0; s < N; s++) sink += TG_ShadeTerrain(W, HeightTex, NoiseTex, CloudTex, S, P[s], fp[s]).Linear.x;
		auto t2 = std::chrono::steady_clock::now();
		float3 cam = P[0] + float3(0, 0, 2);
		for (int s = 0; s < N; s++) sink += TG_Atmosphere(W, CloudTex, S, cam, P[s], 1.0f, float3(0.3f, 0.3f, 0.3f)).x;
		auto t3 = std::chrono::steady_clock::now();
		for (int s = 0; s < N; s++) sink += TG_AtmosphereFast(W, CloudTex, S, cam, P[s], 1.0f, float3(0.3f, 0.3f, 0.3f), 8).x;
		auto t4 = std::chrono::steady_clock::now();
		line += Fmt("TG_ShadeTerrain %.1f us; march (random far points) %.1f us; fast x8 %.1f us",
		            ms(t1, t2) / N, ms(t2, t3) / N, ms(t3, t4) / N);
		Report(std::isfinite(sink), "cost per sample (1 CPU thread)", line);
	}

	std::printf("%d failed\n", failures);
	return failures;
}
