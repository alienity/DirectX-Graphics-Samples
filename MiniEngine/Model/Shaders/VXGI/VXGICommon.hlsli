#ifndef VXGICOMMON_H
#define VXGICOMMON_H

#define PI 3.14159265358979323846
#define SQRT2 1.41421356237309504880
#define FLT_MAX 3.402823466e+38
#define FLT_EPSILON 1.192092896e-07
#define GOLDEN_RATIO 1.6180339887
#define M_TO_SKY_UNIT 0.001
#define SKY_UNIT_TO_M rcp(M_TO_SKY_UNIT)
#define MEDIUMP_FLT_MAX 65504.0
#define SPHERE_SAMPLING_PDF rcp(4 * PI)
#define HEMISPHERE_SAMPLING_PDF rcp(2 * PI)

#define PASTE1(a, b) a##b
#define PASTE(a, b) PASTE1(a, b)
#define CBUFFER(name, slot) cbuffer name : register(PASTE(b, slot), space1)
#define CONSTANTBUFFER(name, type, slot) ConstantBuffer< type > name : register(PASTE(b, slot), space1)

#define CBSLOT_RENDERER_FRAME					0
#define CBSLOT_RENDERER_CAMERA					1
#define CBSLOT_RENDERER_VOXELIZER				2
#define CBSLOT_RENDERER_INSTANCE			    3

// If enabled, geometry shader will be used to voxelize, and axis will be selected by geometry shader
//	If disabled, vertex shader with instance replication will be used for each axis
#define VOXELIZATION_GEOMETRY_SHADER_ENABLED

// If enabled, conservative rasterization will be used to voxelize
//	This can more accurately voxelize thin geometry, but slower
//#define VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED

// Number of clipmaps, each doubling in size:
static const uint VXGI_CLIPMAP_COUNT = 6;

struct VoxelClipMap
{
    float3 center; // center of clipmap volume in world space
    float voxelSize; // half-extent of one voxel
};

struct VXGI
{
    uint resolution; // voxel grid resolution
    float resolution_rcp; // 1.0 / voxel grid resolution
    float stepsize; // raymarch step size in voxel space units
    float max_distance; // maximum raymarch distance for voxel GI in world-space

    int texture_radiance;
    int texture_sdf;
    int padding0;
    int padding1;

    VoxelClipMap clipmaps[VXGI_CLIPMAP_COUNT];

    float3 world_to_clipmap(in float3 P, in VoxelClipMap clipmap)
    {
        float3 diff = (P - clipmap.center) * resolution_rcp / clipmap.voxelSize;
        float3 uvw = diff * float3(0.5f, -0.5f, 0.5f) + 0.5f;
        return uvw;
    }

    float3 clipmap_to_world(in float3 uvw, in VoxelClipMap clipmap)
    {
        float3 P = uvw * 2 - 1;
        P.y *= -1;
        P *= clipmap.voxelSize;
        P *= resolution;
        P += clipmap.center;
        return P;
    }
};

struct VoxelizerCB
{
    int3 offsetfromPrevFrame;
    int clipmap_index;
};

CONSTANTBUFFER(g_xVoxelizer, VoxelizerCB, CBSLOT_RENDERER_VOXELIZER);

enum VOXELIZATION_CHANNEL
{
    VOXELIZATION_CHANNEL_BASECOLOR_R,
	VOXELIZATION_CHANNEL_BASECOLOR_G,
	VOXELIZATION_CHANNEL_BASECOLOR_B,
	VOXELIZATION_CHANNEL_BASECOLOR_A,
	VOXELIZATION_CHANNEL_EMISSIVE_R,
	VOXELIZATION_CHANNEL_EMISSIVE_G,
	VOXELIZATION_CHANNEL_EMISSIVE_B,
	VOXELIZATION_CHANNEL_DIRECTLIGHT_R,
	VOXELIZATION_CHANNEL_DIRECTLIGHT_G,
	VOXELIZATION_CHANNEL_DIRECTLIGHT_B,
	VOXELIZATION_CHANNEL_NORMAL_R,
	VOXELIZATION_CHANNEL_NORMAL_G,
	VOXELIZATION_CHANNEL_FRAGMENT_COUNTER,

	VOXELIZATION_CHANNEL_COUNT,
};


// Cones from: https://github.com/compix/VoxelConeTracingGI/blob/master/assets/shaders/voxelConeTracing/finalLightingPass.frag

//#define USE_32_CONES
#ifdef USE_32_CONES
// 32 Cones for higher quality (16 on average per hemisphere)
static const int DIFFUSE_CONE_COUNT = 32;
static const float DIFFUSE_CONE_APERTURE = 0.628319f;

static const float3 DIFFUSE_CONE_DIRECTIONS[32] = {
	float3(0.898904f, 0.435512f, 0.0479745f),
	float3(0.898904f, -0.435512f, -0.0479745f),
	float3(0.898904f, 0.0479745f, -0.435512f),
	float3(0.898904f, -0.0479745f, 0.435512f),
	float3(-0.898904f, 0.435512f, -0.0479745f),
	float3(-0.898904f, -0.435512f, 0.0479745f),
	float3(-0.898904f, 0.0479745f, 0.435512f),
	float3(-0.898904f, -0.0479745f, -0.435512f),
	float3(0.0479745f, 0.898904f, 0.435512f),
	float3(-0.0479745f, 0.898904f, -0.435512f),
	float3(-0.435512f, 0.898904f, 0.0479745f),
	float3(0.435512f, 0.898904f, -0.0479745f),
	float3(-0.0479745f, -0.898904f, 0.435512f),
	float3(0.0479745f, -0.898904f, -0.435512f),
	float3(0.435512f, -0.898904f, 0.0479745f),
	float3(-0.435512f, -0.898904f, -0.0479745f),
	float3(0.435512f, 0.0479745f, 0.898904f),
	float3(-0.435512f, -0.0479745f, 0.898904f),
	float3(0.0479745f, -0.435512f, 0.898904f),
	float3(-0.0479745f, 0.435512f, 0.898904f),
	float3(0.435512f, -0.0479745f, -0.898904f),
	float3(-0.435512f, 0.0479745f, -0.898904f),
	float3(0.0479745f, 0.435512f, -0.898904f),
	float3(-0.0479745f, -0.435512f, -0.898904f),
	float3(0.57735f, 0.57735f, 0.57735f),
	float3(0.57735f, 0.57735f, -0.57735f),
	float3(0.57735f, -0.57735f, 0.57735f),
	float3(0.57735f, -0.57735f, -0.57735f),
	float3(-0.57735f, 0.57735f, 0.57735f),
	float3(-0.57735f, 0.57735f, -0.57735f),
	float3(-0.57735f, -0.57735f, 0.57735f),
	float3(-0.57735f, -0.57735f, -0.57735f)
};
#else // 16 cones for lower quality (8 on average per hemisphere)
static const int DIFFUSE_CONE_COUNT = 16;
static const float DIFFUSE_CONE_APERTURE = 0.872665f;

static const float3 DIFFUSE_CONE_DIRECTIONS[16] =
{
    float3(0.57735f, 0.57735f, 0.57735f),
	float3(0.57735f, -0.57735f, -0.57735f),
	float3(-0.57735f, 0.57735f, -0.57735f),
	float3(-0.57735f, -0.57735f, 0.57735f),
	float3(-0.903007f, -0.182696f, -0.388844f),
	float3(-0.903007f, 0.182696f, 0.388844f),
	float3(0.903007f, -0.182696f, 0.388844f),
	float3(0.903007f, 0.182696f, -0.388844f),
	float3(-0.388844f, -0.903007f, -0.182696f),
	float3(0.388844f, -0.903007f, 0.182696f),
	float3(0.388844f, 0.903007f, -0.182696f),
	float3(-0.388844f, 0.903007f, 0.182696f),
	float3(-0.182696f, -0.388844f, -0.903007f),
	float3(0.182696f, 0.388844f, -0.903007f),
	float3(-0.182696f, 0.388844f, 0.903007f),
	float3(0.182696f, -0.388844f, 0.903007f)
};
#endif

static const float __hdrRange = 10.0f; // HDR to SDR packing scale
static const uint MAX_VOXEL_RGB = 511; // 9 bits for RGB
static const uint MAX_VOXEL_ALPHA = 31; // 5 bits for alpha (alpha is needed for atomic average)
static const float DARK_PACKING_POW = 8; // improves precision for dark colors

uint PackVoxelColor(in float4 color)
{
    color.rgb /= __hdrRange;
    color = saturate(color);
    color.rgb = pow(color.rgb, 1.0 / DARK_PACKING_POW);
    uint retVal = 0;
    retVal |= (uint) (color.r * MAX_VOXEL_RGB) << 0u;
    retVal |= (uint) (color.g * MAX_VOXEL_RGB) << 9u;
    retVal |= (uint) (color.b * MAX_VOXEL_RGB) << 18u;
    retVal |= (uint) (color.a * MAX_VOXEL_ALPHA) << 27u;
    return retVal;
}

float4 UnpackVoxelColor(in uint colorMask)
{
    float4 retVal;
    retVal.r = (float) ((colorMask >> 0u) & MAX_VOXEL_RGB) / MAX_VOXEL_RGB;
    retVal.g = (float) ((colorMask >> 9u) & MAX_VOXEL_RGB) / MAX_VOXEL_RGB;
    retVal.b = (float) ((colorMask >> 18u) & MAX_VOXEL_RGB) / MAX_VOXEL_RGB;
    retVal.a = (float) ((colorMask >> 27u) & MAX_VOXEL_ALPHA) / MAX_VOXEL_ALPHA;
    retVal = saturate(retVal);
    retVal.rgb = pow(retVal.rgb, DARK_PACKING_POW);
    retVal.rgb *= __hdrRange;
    return retVal;
}

uint PackVoxelChannel(float value)
{
    return uint(value * 1024);
}
float UnpackVoxelChannel(uint value)
{
    return float(value) / 1024.0f;
}




// attribute computation with barycentric interpolation
//	a0 : attribute at triangle corner 0
//	a1 : attribute at triangle corner 1
//	a2 : attribute at triangle corner 2
//  bary : (u,v) barycentrics [same as you get from raytracing]; w is computed as 1 - u - w
//	computation can be also written as: p0 * w + p1 * u + p2 * v
template<
typename T>
inline T attribute_at_bary(in T a0, in T a1, in T a2, in float2 bary)
{
    return mad(a0, 1 - bary.x - bary.y, mad(a1, bary.x, a2 * bary.y));
}
template<
typename T>
inline T attribute_at_bary(in T a0, in T a1, in T a2, in half2 bary)
{
    return mad(a0, 1 - bary.x - bary.y, mad(a1, bary.x, a2 * bary.y));
}

// bilinear interpolation of gathered values based on pixel fraction
inline float bilinear(float4 gather, float2 pixel_frac)
{
    const float top_row = lerp(gather.w, gather.z, pixel_frac.x);
    const float bottom_row = lerp(gather.x, gather.y, pixel_frac.x);
    return lerp(top_row, bottom_row, pixel_frac.y);
}
inline half bilinear(half4 gather, half2 pixel_frac)
{
    const half top_row = lerp(gather.w, gather.z, pixel_frac.x);
    const half bottom_row = lerp(gather.x, gather.y, pixel_frac.x);
    return lerp(top_row, bottom_row, pixel_frac.y);
}

template<typename T>
inline bool is_saturated(T a)
{
    return all(a == saturate(a));
}

inline uint align(uint value, uint alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}
inline uint2 align(uint2 value, uint2 alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}
inline uint3 align(uint3 value, uint3 alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}
inline uint4 align(uint4 value, uint4 alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}



// Expands a 10-bit integer into 30 bits
// by inserting 2 zeros after each bit.
inline uint expandBits(uint v)
{
	v = (v * 0x00010001u) & 0xFF0000FFu;
	v = (v * 0x00000101u) & 0x0F00F00Fu;
	v = (v * 0x00000011u) & 0xC30C30C3u;
	v = (v * 0x00000005u) & 0x49249249u;
	return v;
}

// Calculates a 30-bit Morton code for the
// given 3D point located within the unit cube [0,1].
inline uint morton3D(in float3 pos)
{
	pos.x = min(max(pos.x * 1024, 0), 1023);
	pos.y = min(max(pos.y * 1024, 0), 1023);
	pos.z = min(max(pos.z * 1024, 0), 1023);
	uint xx = expandBits((uint)pos.x);
	uint yy = expandBits((uint)pos.y);
	uint zz = expandBits((uint)pos.z);
	return xx * 4 + yy * 2 + zz;
}


// Octahedral encodings:
//	Journal of Computer Graphics Techniques Vol. 3, No. 2, 2014 http://jcgt.org

// Returns +/-1
half2 signNotZero(half2 v)
{
	return half2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}
// Assume normalized input. Output is on [-1, 1] for each component.
half2 encode_oct(in half3 v)
{
	// Project the sphere onto the octahedron, and then onto the xy plane
	half2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
	// Reflect the folds of the lower hemisphere over the diagonals
	return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * signNotZero(p)) : p;
}
half3 decode_oct(half2 e)
{
	half3 v = half3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
	return normalize(v);
}

// Assume normalized input on +Z hemisphere.
// Output is on [-1, 1].
half2 encode_hemioct(in half3 v)
{
	// Project the hemisphere onto the hemi-octahedron,
	// and then into the xy plane
	half2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + v.z));
	// Rotate and scale the center diamond to the unit square
	return half2(p.x + p.y, p.x - p.y);
}
half3 decode_hemioct(half2 e)
{
	// Rotate and scale the unit square back to the center diamond
	half2 temp = half2(e.x + e.y, e.x - e.y) * 0.5;
	half3 v = half3(temp, 1.0 - abs(temp.x) - abs(temp.y));
	return normalize(v);
}

// Source: https://github.com/GPUOpen-Effects/FidelityFX-Denoiser/blob/master/ffx-shadows-dnsr/ffx_denoiser_shadows_util.h
//  LANE TO 8x8 MAPPING
//  ===================
//  00 01 08 09 10 11 18 19 
//  02 03 0a 0b 12 13 1a 1b
//  04 05 0c 0d 14 15 1c 1d
//  06 07 0e 0f 16 17 1e 1f 
//  20 21 28 29 30 31 38 39 
//  22 23 2a 2b 32 33 3a 3b
//  24 25 2c 2d 34 35 3c 3d
//  26 27 2e 2f 36 37 3e 3f 
uint bitfield_extract(uint src, uint off, uint bits) { uint mask = (1u << bits) - 1; return (src >> off) & mask; } // ABfe
uint bitfield_insert(uint src, uint ins, uint bits) { uint mask = (1u << bits) - 1; return (ins & mask) | (src & (~mask)); } // ABfiM
uint2 remap_lane_8x8(uint lane) {
	return uint2(bitfield_insert(bitfield_extract(lane, 2u, 3u), lane, 1u)
		, bitfield_insert(bitfield_extract(lane, 3u, 3u)
			, bitfield_extract(lane, 1u, 2u), 2u));
}


static const half2x2 BayerMatrix2 =
{
	1.0 / 5.0, 3.0 / 5.0,
	4.0 / 5.0, 2.0 / 5.0
};

static const half3x3 BayerMatrix3 =
{
	3.0 / 10.0, 7.0 / 10.0, 4.0 / 10.0,
	6.0 / 10.0, 1.0 / 10.0, 9.0 / 10.0,
	2.0 / 10.0, 8.0 / 10.0, 5.0 / 10.0
};

static const half4x4 BayerMatrix4 =
{
	1.0 / 17.0, 9.0 / 17.0, 3.0 / 17.0, 11.0 / 17.0,
	13.0 / 17.0, 5.0 / 17.0, 15.0 / 17.0, 7.0 / 17.0,
	4.0 / 17.0, 12.0 / 17.0, 2.0 / 17.0, 10.0 / 17.0,
	16.0 / 17.0, 8.0 / 17.0, 14.0 / 17.0, 6.0 / 17.0
};

static const half BayerMatrix8[8][8] =
{
	{ 1.0 / 65.0, 49.0 / 65.0, 13.0 / 65.0, 61.0 / 65.0, 4.0 / 65.0, 52.0 / 65.0, 16.0 / 65.0, 64.0 / 65.0 },
	{ 33.0 / 65.0, 17.0 / 65.0, 45.0 / 65.0, 29.0 / 65.0, 36.0 / 65.0, 20.0 / 65.0, 48.0 / 65.0, 32.0 / 65.0 },
	{ 9.0 / 65.0, 57.0 / 65.0, 5.0 / 65.0, 53.0 / 65.0, 12.0 / 65.0, 60.0 / 65.0, 8.0 / 65.0, 56.0 / 65.0 },
	{ 41.0 / 65.0, 25.0 / 65.0, 37.0 / 65.0, 21.0 / 65.0, 44.0 / 65.0, 28.0 / 65.0, 40.0 / 65.0, 24.0 / 65.0 },
	{ 3.0 / 65.0, 51.0 / 65.0, 15.0 / 65.0, 63.0 / 65.0, 2.0 / 65.0, 50.0 / 65.0, 14.0 / 65.0, 62.0 / 65.0 },
	{ 35.0 / 65.0, 19.0 / 65.0, 47.0 / 65.0, 31.0 / 65.0, 34.0 / 65.0, 18.0 / 65.0, 46.0 / 65.0, 30.0 / 65.0 },
	{ 11.0 / 65.0, 59.0 / 65.0, 7.0 / 65.0, 55.0 / 65.0, 10.0 / 65.0, 58.0 / 65.0, 6.0 / 65.0, 54.0 / 65.0 },
	{ 43.0 / 65.0, 27.0 / 65.0, 39.0 / 65.0, 23.0 / 65.0, 42.0 / 65.0, 26.0 / 65.0, 38.0 / 65.0, 22.0 / 65.0 }
};


inline half ditherMask2(in min16uint2 pixel)
{
	return BayerMatrix2[pixel.x % 2][pixel.y % 2];
}

inline half ditherMask3(in min16uint2 pixel)
{
	return BayerMatrix3[pixel.x % 3][pixel.y % 3];
}

inline half ditherMask4(in min16uint2 pixel)
{
	return BayerMatrix4[pixel.x % 4][pixel.y % 4];
}

inline half ditherMask8(in min16uint2 pixel)
{
	return BayerMatrix8[pixel.x % 8][pixel.y % 8];
}

inline half dither(in min16uint2 pixel)
{
	return ditherMask8(pixel);
}

// For every value of BayerMatrix8, this contains: half2(sin(value * 2 * PI), cos(value * 2 * PI))
static const half2 BayerMatrix8_sincos[8][8] = {
	{half2(0.096514, 0.995332),half2(-0.999708, 0.024164),half2(0.951057, 0.309017),half2(-0.377095, 0.926175),half2(0.377095, 0.926175),half2(-0.951056, 0.309017),half2(0.999708, 0.024164),half2(-0.096514, 0.995332),},
	{half2(-0.048314, -0.998832),half2(0.997373, -0.072435),half2(-0.935016, -0.354605),half2(0.331908, -0.943312),half2(-0.331908, -0.943312),half2(0.935016, -0.354605),half2(-0.997373, -0.072434),half2(0.048313, -0.998832),},
	{half2(0.764316, 0.644842),half2(-0.698511, 0.715599),half2(0.464723, 0.885456),half2(-0.916792, 0.399365),half2(0.916792, 0.399364),half2(-0.464723, 0.885456),half2(0.698511, 0.715599),half2(-0.764316, 0.644842),},
	{half2(-0.732269, -0.681016),half2(0.663123, -0.748511),half2(-0.421401, -0.906874),half2(0.896427, -0.443192),half2(-0.896427, -0.443191),half2(0.421401, -0.906874),half2(-0.663123, -0.748511),half2(0.732269, -0.681016),},
	{half2(0.285946, 0.958246),half2(-0.976441, 0.215784),half2(0.992709, 0.120537),half2(-0.192127, 0.981370),half2(0.192127, 0.981370),half2(-0.992709, 0.120537),half2(0.976441, 0.215784),half2(-0.285946, 0.958246),},
	{half2(-0.239316, -0.970942),half2(0.964876, -0.262708),half2(-0.985726, -0.168357),half2(0.144489, -0.989506),half2(-0.144489, -0.989506),half2(0.985726, -0.168357),half2(-0.964876, -0.262707),half2(0.239316, -0.970942),},
	{half2(0.873968, 0.485983),half2(-0.548012, 0.836470),half2(0.626185, 0.779674),half2(-0.822984, 0.568065),half2(0.822984, 0.568065),half2(-0.626185, 0.779675),half2(0.548013, 0.836470),half2(-0.873968, 0.485984),},
	{half2(-0.849468, -0.527640),half2(0.506960, -0.861970),half2(-0.587786, -0.809017),half2(0.794578, -0.607163),half2(-0.794578, -0.607162),half2(0.587785, -0.809017),half2(-0.506960, -0.861970),half2(0.849468, -0.527640),},
};
inline half2 dither_sincos(in min16uint2 pixel)
{
	return BayerMatrix8_sincos[pixel.x % 8][pixel.y % 8];
}
inline half2x2 dither_rot2x2(in min16uint2 pixel)
{
	half2 sincos = dither_sincos(pixel);
	return half2x2(
		sincos.y, -sincos.x,
		sincos.x, sincos.y
	);
}

#endif