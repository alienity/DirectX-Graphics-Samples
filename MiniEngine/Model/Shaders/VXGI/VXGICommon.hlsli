#ifndef VXGICOMMON_H
#define VXGICOMMON_H

#include "VXGIGlobal.hlsli"

#define PASTE1(a, b) a##b
#define PASTE(a, b) PASTE1(a, b)
#define CBUFFER(name, slot) cbuffer name : register(PASTE(b, slot), space1)
#define CONSTANTBUFFER(name, type, slot) ConstantBuffer< type > name : register(PASTE(b, slot), space1)

#define CBSLOT_RENDERER_FRAME					0
#define CBSLOT_RENDERER_CAMERA					1
#define CBSLOT_RENDERER_VOXELIZER				2

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

    //int texture_radiance;
    //int texture_sdf;
    //int padding0;
    //int padding1;

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

#define VOXELIZATION_CHANNEL_BASECOLOR_R 0
#define VOXELIZATION_CHANNEL_BASECOLOR_G 1
#define VOXELIZATION_CHANNEL_BASECOLOR_B 2
#define VOXELIZATION_CHANNEL_BASECOLOR_A 3
#define VOXELIZATION_CHANNEL_EMISSIVE_R 4
#define VOXELIZATION_CHANNEL_EMISSIVE_G 5
#define VOXELIZATION_CHANNEL_EMISSIVE_B 6
#define VOXELIZATION_CHANNEL_DIRECTLIGHT_R 7
#define VOXELIZATION_CHANNEL_DIRECTLIGHT_G 8
#define VOXELIZATION_CHANNEL_DIRECTLIGHT_B 9
#define VOXELIZATION_CHANNEL_NORMAL_R 10
#define VOXELIZATION_CHANNEL_NORMAL_G 11
#define VOXELIZATION_CHANNEL_FRAGMENT_COUNTER 12
#define VOXELIZATION_CHANNEL_COUNT 13

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

#endif