// hlsl_shim.h - just enough HLSL in C++ to compile Terragen.ush on the CPU.
// Include it, then include the .ush files inside `namespace hlsl { ... }` so these overloads hide <cmath>'s.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace hlsl
{
typedef unsigned int uint;

struct float2
{
	float x = 0, y = 0;
	float2() = default;
	float2(float a, float b) : x(a), y(b) {}
};
struct float3
{
	float x = 0, y = 0, z = 0;
	float3() = default;
	float3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct float4
{
	float x = 0, y = 0, z = 0, w = 0;
	float4() = default;
	float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};
struct int3
{
	int x = 0, y = 0, z = 0;
	int3(int a, int b, int c) : x(a), y(b), z(c) {}
};

inline float2 operator+(float2 a, float2 b) { return float2(a.x + b.x, a.y + b.y); }
inline float2 operator-(float2 a, float2 b) { return float2(a.x - b.x, a.y - b.y); }
inline float2 operator*(float2 a, float s) { return float2(a.x * s, a.y * s); }
inline float2 operator*(float s, float2 a) { return a * s; }
inline float3 operator+(float3 a, float3 b) { return float3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline float3 operator-(float3 a, float3 b) { return float3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline float3 operator-(float3 a) { return float3(-a.x, -a.y, -a.z); }
inline float3 operator*(float3 a, float3 b) { return float3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline float3 operator*(float3 a, float s) { return float3(a.x * s, a.y * s, a.z * s); }
inline float3 operator*(float s, float3 a) { return a * s; }
inline float3 operator/(float3 a, float s) { return float3(a.x / s, a.y / s, a.z / s); }

inline float abs(float a) { return std::fabs(a); }
inline float floor(float a) { return std::floor(a); }
inline float sqrt(float a) { return std::sqrt(a); }
inline float exp(float a) { return std::exp(a); }
inline float exp2(float a) { return std::exp2(a); }
inline float log(float a) { return std::log(a); }
inline float log2(float a) { return std::log2(a); }
inline float pow(float a, float b) { return std::pow(a, b); }
inline float sin(float a) { return std::sin(a); }
inline float cos(float a) { return std::cos(a); }
inline float tan(float a) { return std::tan(a); }
// HLSL literals are float but C++ literals are double, so float min/max/clamp take any arithmetic mix;
// the exact int overloads still win for int arguments.
inline int min(int a, int b) { return a < b ? a : b; }
inline int max(int a, int b) { return a > b ? a : b; }
inline int clamp(int v, int lo, int hi) { return min(max(v, lo), hi); }
template <class A, class B> inline float min(A a, B b) { return (float)a < (float)b ? (float)a : (float)b; }
template <class A, class B> inline float max(A a, B b) { return (float)a > (float)b ? (float)a : (float)b; }
template <class A, class B, class C> inline float clamp(A v, B lo, C hi) { return min(max(v, lo), hi); }
inline float saturate(float v) { return clamp(v, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float2 lerp(float2 a, float2 b, float t) { return a + (b - a) * t; }
inline float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }
inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float3 cross(float3 a, float3 b) { return float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float length(float3 a) { return sqrt(dot(a, a)); }
inline float3 normalize(float3 a) { return a / length(a); }

// A Texture2D handle over 16-bit texels, returned normalised like a UNORM16 texture.
struct SamplerState {};
struct Texture2D
{
	const uint16_t* Data = nullptr;
	int Width = 0, Height = 0;
	float4 Load(int3 p) const
	{
		int x = clamp(p.x, 0, Width - 1), y = clamp(p.y, 0, Height - 1);
		return float4(Data[y * Width + x] / 65535.0f, 0.0f, 0.0f, 1.0f);
	}
};
}  // namespace hlsl

#define TG_LOOP
