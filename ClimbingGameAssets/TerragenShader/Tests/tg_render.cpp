// tg_render.cpp - renders Terragen.ush on the CPU so it can be compared with real Terragen output.
//
// It compiles the shader source itself (through hlsl_shim.h), renders the six cube faces the way
// Scripts/GenSkyboxAnimation.tgs does (heading +0/90/180/270, plus up and down), and writes per face:
//   <Face>.bmp  display colour, quantised like Terragen's 16-bit buffer then 8-bit BMP
//   <Face>.aux  float32 per pixel: kind, linear rgb, albedo rgb, sun term, distance, alpha, N.L, N.z
//
// With --atlas it instead renders the whole skybox cross through TG_RenderAtlasTexel (TerragenUE.ush), the
// same function the Unreal material runs, to <out dir>/Atlas.bmp; and it checks that TG_AtlasUV is the
// inverse of TG_AtlasDirection.
//
// --clearance c lifts the camera to at least c units above the terrain under it (TG_KeepAboveTerrain), as
// the Unreal sky does when the player is below the Terragen ground; off by default.
//
// --clouds <name>_clouds.r8 supplies the cloud map (Tools/make_cloud_map.py) for worlds with the sky on.
// The world is chosen at compile time (TG_WORLD_FILE / TG_WORLD_FN, see build.cmd); --sun overrides its
// sun heading and altitude, for renders made after the sun was moved.
//
// Usage: tg_render <height.r16> <noise128.r16> <out dir> [--cam x y z] [--heading deg] [--size n]
//                  [--faces Front,Left,...] [--atlas] [--clearance c] [--clouds file.r8] [--mask]
//                  [--sun heading altitude]

#include "hlsl_shim.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>

namespace hlsl
{
#include "TerragenUE.ush"
#ifndef TG_WORLD_FILE
#define TG_WORLD_FILE "Worlds/MountainTop.ush"
#define TG_WORLD_FN TGWorld_MountainTop
#define TG_WORLD_CAMERA TGWorld_MountainTop_CameraPos
#define TG_WORLD_HEADING TGWorld_MountainTop_CameraHeading
#endif
#include TG_WORLD_FILE
}  // namespace hlsl

using namespace hlsl;

static std::vector<uint16_t> ReadR16(const char* path, size_t expectSquare, int& side)
{
	FILE* f = std::fopen(path, "rb");
	if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
	std::fseek(f, 0, SEEK_END);
	long n = std::ftell(f) / 2;
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint16_t> v(n);
	if (std::fread(v.data(), 2, n, f) != (size_t)n) { std::fprintf(stderr, "short read %s\n", path); std::exit(1); }
	std::fclose(f);
	side = (int)std::lround(std::sqrt((double)n));
	if ((long)side * side != n || (expectSquare && (size_t)side != expectSquare)) { std::fprintf(stderr, "%s is not square\n", path); std::exit(1); }
	return v;
}

// 8-bit texels widened so the 16-bit Texture2D shim returns v / 255, like an 8-bit UNORM texture.
static std::vector<uint16_t> ReadR8(const char* path, int& side)
{
	FILE* f = std::fopen(path, "rb");
	if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
	std::fseek(f, 0, SEEK_END);
	long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> b(n);
	if (std::fread(b.data(), 1, n, f) != (size_t)n) { std::fprintf(stderr, "short read %s\n", path); std::exit(1); }
	std::fclose(f);
	side = (int)std::lround(std::sqrt((double)n));
	if ((long)side * side != n) { std::fprintf(stderr, "%s is not square\n", path); std::exit(1); }
	std::vector<uint16_t> v(n);
	for (long i = 0; i < n; i++) v[i] = (uint16_t)(b[i] * 257);
	return v;
}

static void WriteBmp(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb)
{
	int stride = (w * 3 + 3) & ~3;
	std::vector<uint8_t> body(stride * h, 0);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const uint8_t* s = &rgb[(y * w + x) * 3];
			uint8_t* d = &body[(h - 1 - y) * stride + x * 3];
			d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
		}
	uint8_t hdr[54] = {'B', 'M'};
	auto put32 = [&](int off, uint32_t v) { std::memcpy(hdr + off, &v, 4); };
	put32(2, 54 + (uint32_t)body.size()); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
	hdr[26] = 1; hdr[28] = 24; put32(34, (uint32_t)body.size());
	FILE* f = std::fopen(path.c_str(), "wb");
	std::fwrite(hdr, 1, 54, f);
	std::fwrite(body.data(), 1, body.size(), f);
	std::fclose(f);
}

struct Face { const char* Name; float HeadingOffset; float Pitch; };

static uint8_t Quantise(float v)
{
	// Terragen's 16-bit buffer value, then the top 8 bits as in its BMP output.
	int q16 = (int)(v * 32767.0f + 0.5f);
	return (uint8_t)(clamp(q16, 0, 32767) >> 7);
}

template <class F> static void ParallelRows(int rows, F&& fn)
{
	std::atomic<int> next{0};
	unsigned n = std::thread::hardware_concurrency();
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < (n ? n : 4); t++)
		pool.emplace_back([&]() { for (int y; (y = next++) < rows;) fn(y); });
	for (auto& t : pool) t.join();
}

// Every atlas texel inside a face must map to a direction that maps back to the same texel.
static int CheckAtlasRoundTrip(float heading, int faceSize)
{
	int atlas = faceSize * 4, bad = 0, total = 0;
	for (int y = 0; y < atlas; y++)
		for (int x = 0; x < atlas; x++)
		{
			float2 uv((x + 0.5f) / atlas, (y + 0.5f) / atlas);
			float4 d = TG_AtlasDirection(uv, heading, (float)faceSize);
			if (d.w == 0.0f) continue;
			float2 back = TG_AtlasUV(float3(d.x, d.y, d.z), heading, (float)faceSize);
			total++;
			if (std::fabs(back.x * atlas - (x + 0.5f)) > 0.01f || std::fabs(back.y * atlas - (y + 0.5f)) > 0.01f) bad++;
		}
	std::printf("atlas round trip: %d of %d texels off by more than 0.01 px\n", bad, total);
	return bad;
}

int main(int argc, char** argv)
{
	if (argc < 4) { std::fprintf(stderr, "usage: tg_render <height.r16> <noise128.r16> <out dir> [options]\n"); return 1; }
	TGWorld W = TG_WORLD_FN();
	float3 cam = TG_WORLD_CAMERA;
	float heading = TG_WORLD_HEADING;
	int size = 512;
	bool atlasMode = false;
	bool maskMode = false;
	float clearance = -1.0f;
	const char* cloudPath = nullptr;
	std::string faces = "Front,Left,Back,Right,Up,Down";
	for (int i = 4; i < argc; i++)
	{
		if (!std::strcmp(argv[i], "--cam") && i + 3 < argc) { cam = float3((float)atof(argv[i + 1]), (float)atof(argv[i + 2]), (float)atof(argv[i + 3])); i += 3; }
		else if (!std::strcmp(argv[i], "--heading") && i + 1 < argc) { heading = (float)atof(argv[++i]); }
		else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) { size = atoi(argv[++i]); }
		else if (!std::strcmp(argv[i], "--faces") && i + 1 < argc) { faces = argv[++i]; }
		else if (!std::strcmp(argv[i], "--atlas")) { atlasMode = true; }
		else if (!std::strcmp(argv[i], "--mask")) { maskMode = true; }
		else if (!std::strcmp(argv[i], "--clearance") && i + 1 < argc) { clearance = (float)atof(argv[++i]); }
		else if (!std::strcmp(argv[i], "--clouds") && i + 1 < argc) { cloudPath = argv[++i]; }
		else if (!std::strcmp(argv[i], "--sun") && i + 2 < argc) { W.SunHeading = (float)atof(argv[i + 1]); W.SunAltitude = (float)atof(argv[i + 2]); i += 2; }
		else { std::fprintf(stderr, "unknown option %s\n", argv[i]); return 1; }
	}

	int hside = 0, nside = 0;
	std::vector<uint16_t> heights = ReadR16(argv[1], W.Terrain.Cells + 1, hside);
	std::vector<uint16_t> noise = ReadR16(argv[2], 128, nside);
	Texture2D HeightTex{heights.data(), hside, hside};
	Texture2D NoiseTex{noise.data(), nside, nside};
	int cside = 1;
	std::vector<uint16_t> clouds = cloudPath ? ReadR8(cloudPath, cside) : std::vector<uint16_t>(1, 128 * 257);
	Texture2D CloudTex{clouds.data(), cside, cside};
	std::string outDir = argv[3];

	if (atlasMode)
	{
		int bad = CheckAtlasRoundTrip(heading, size);
		int atlas = size * 4;
		std::vector<uint8_t> rgb(atlas * atlas * 3);
		ParallelRows(atlas, [&](int y)
		{
			for (int x = 0; x < atlas; x++)
			{
				float2 uv((x + 0.5f) / atlas, (y + 0.5f) / atlas);
				float4 c = TG_RenderAtlasTexel(W, HeightTex, NoiseTex, CloudTex, uv, cam, heading, (float)size, 0.0f, clearance);
				uint8_t* d = &rgb[(y * atlas + x) * 3];
				d[0] = Quantise(c.x); d[1] = Quantise(c.y); d[2] = Quantise(c.z);
			}
		});
		WriteBmp(outDir + "/Atlas.bmp", atlas, atlas, rgb);
		std::printf("Atlas done\n");
		return bad ? 2 : 0;
	}

	cam = TG_KeepAboveTerrain(W, HeightTex, cam, clearance);
	std::printf("camera %.4f %.4f %.4f\n", cam.x, cam.y, cam.z);

	if (maskMode)
	{
		// Visibility only, for camera searches (Tools/find_camera.py): <Face>.mask, one byte per pixel,
		// 1 = terrain or water.
		const Face faceSet[] = {{"Front", 0, 0}, {"Left", 90, 0}, {"Back", 180, 0}, {"Right", 270, 0}, {"Up", 270, 90}, {"Down", 270, -90}};
		for (const Face& face : faceSet)
		{
			if (faces.find(face.Name) == std::string::npos) continue;
			std::vector<uint8_t> m(size * size);
			ParallelRows(size, [&](int y)
			{
				for (int x = 0; x < size; x++)
				{
					float3 dir = normalize(TG_CameraRay(heading + face.HeadingOffset, face.Pitch, 1.0f, float2(x + 0.5f, y + 0.5f), float2((float)size, (float)size)));
					m[y * size + x] = TG_Trace(W, HeightTex, cam, dir, 0.0f).Kind != 0 ? 1 : 0;
				}
			});
			FILE* f = std::fopen((outDir + "/" + face.Name + ".mask").c_str(), "wb");
			std::fwrite(m.data(), 1, m.size(), f);
			std::fclose(f);
		}
		return 0;
	}

	// The face set of GenSkyboxAnimation.tgs: front heading H, left H+90, back H+180, right H+270,
	// up and down at H+270.
	const Face all[] = {{"Front", 0, 0}, {"Left", 90, 0}, {"Back", 180, 0}, {"Right", 270, 0}, {"Up", 270, 90}, {"Down", 270, -90}};
	for (const Face& face : all)
	{
		if (faces.find(face.Name) == std::string::npos) continue;
		std::vector<uint8_t> rgb(size * size * 3);
		const int auxN = 12;
		std::vector<float> aux(size * size * auxN);
		ParallelRows(size, [&](int y)
		{
			for (int x = 0; x < size; x++)
			{
				float3 dir = TG_CameraRay(heading + face.HeadingOffset, face.Pitch, 1.0f, float2(x + 0.5f, y + 0.5f), float2((float)size, (float)size));
				TGShadeResult r = TG_ShadeRay(W, HeightTex, NoiseTex, CloudTex, cam, dir, 2.0f / size, 0.0f);
				float3 c = TG_ToneMap(W, r.Linear);
				uint8_t* d = &rgb[(y * size + x) * 3];
				d[0] = Quantise(c.x); d[1] = Quantise(c.y); d[2] = Quantise(c.z);
				float* a = &aux[(y * size + x) * auxN];
				a[0] = (float)r.Kind; a[1] = r.Linear.x; a[2] = r.Linear.y; a[3] = r.Linear.z;
				a[4] = r.Albedo.x; a[5] = r.Albedo.y; a[6] = r.Albedo.z; a[7] = r.SunTerm; a[8] = r.Distance; a[9] = r.Alpha;
				a[10] = r.NdotL; a[11] = r.NormalZ;
			}
		});
		WriteBmp(outDir + "/" + face.Name + ".bmp", size, size, rgb);
		FILE* f = std::fopen((outDir + "/" + face.Name + ".aux").c_str(), "wb");
		std::fwrite(aux.data(), sizeof(float), aux.size(), f);
		std::fclose(f);
		std::printf("%s done\n", face.Name);
	}
	return 0;
}
