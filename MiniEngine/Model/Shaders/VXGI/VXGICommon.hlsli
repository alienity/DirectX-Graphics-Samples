#ifndef VXGICOMMON_H
#define VXGICOMMON_H

#define PASTE1(a, b) a##b
#define PASTE(a, b) PASTE1(a, b)
#define CBUFFER(name, slot) cbuffer name : register(PASTE(b, slot))
#define CONSTANTBUFFER(name, type, slot) ConstantBuffer< type > name : register(PASTE(b, slot))

#define CBSLOT_RENDERER_FRAME					0
#define CBSLOT_RENDERER_CAMERA					1
#define CBSLOT_RENDERER_VOXELIZER				3

// If enabled, geometry shader will be used to voxelize, and axis will be selected by geometry shader
//	If disabled, vertex shader with instance replication will be used for each axis
// #define VOXELIZATION_GEOMETRY_SHADER_ENABLED

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
    if (v.z < 0)
        v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
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



#endif