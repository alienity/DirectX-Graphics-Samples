#ifndef VXGIRENDERER_H
#define VXGIRENDERER_H

#include "VXGICommon.hlsli"

struct FrameCB
{
    uint options; // wi::renderer bool options packed into bitmask (OPTION_BIT_ values)
    float time;
    float time_previous;
    float delta_time;

    uint frame_count;
    uint temporalaa_samplerotation;
    int texture_shadowatlas_index;
    int texture_shadowatlas_transparent_index;

    VXGI vxgi;
};

struct ShaderSphere
{
    float3 center;
    float radius;
};

struct ShaderClusterBounds
{
    ShaderSphere sphere;

    float3 cone_axis;
    float cone_cutoff;
};

struct ShaderFrustum
{
    // Frustum planes:
    //	0 : near
    //	1 : far
    //	2 : left
    //	3 : right
    //	4 : top
    //	5 : bottom
    float4 planes[6];

    inline bool intersects(ShaderSphere sphere)
    {
        uint infrustum = 1;
        infrustum &= dot(planes[0], float4(sphere.center, 1)) > -sphere.radius;
        infrustum &= dot(planes[1], float4(sphere.center, 1)) > -sphere.radius;
        infrustum &= dot(planes[2], float4(sphere.center, 1)) > -sphere.radius;
        infrustum &= dot(planes[3], float4(sphere.center, 1)) > -sphere.radius;
        infrustum &= dot(planes[4], float4(sphere.center, 1)) > -sphere.radius;
        infrustum &= dot(planes[5], float4(sphere.center, 1)) > -sphere.radius;
        return infrustum != 0;
    }
};

struct ShaderFrustumCorners
{
    // topleft, topright, bottomleft, bottomright
    float4 cornersNEAR[4];
    float4 cornersFAR[4];

    inline float3 screen_to_nearplane(float2 uv)
    {
        float3 posTOP = lerp(cornersNEAR[0], cornersNEAR[1], uv.x);
        float3 posBOTTOM = lerp(cornersNEAR[2], cornersNEAR[3], uv.x);
        return lerp(posTOP, posBOTTOM, uv.y);
    }
    inline float3 screen_to_farplane(float2 uv)
    {
        float3 posTOP = lerp(cornersFAR[0], cornersFAR[1], uv.x);
        float3 posBOTTOM = lerp(cornersFAR[2], cornersFAR[3], uv.x);
        return lerp(posTOP, posBOTTOM, uv.y);
    }
    inline float3 screen_to_world(float2 uv, float lineardepthNormalized)
    {
        return lerp(screen_to_nearplane(uv), screen_to_farplane(uv), lineardepthNormalized);
    }
};

struct ShaderCamera
{
    float4x4 view_projection;

    float3 position;
    uint output_index; // viewport or rendertarget array index

    float4 clip_plane;
    float4 reflection_plane; // not clip plane (not reversed when camera is under), but the original plane

    float3 forward;
    float z_near;

    float3 up;
    float z_far;

    float z_near_rcp;
    float z_far_rcp;
    float z_range;
    float z_range_rcp;

    float4x4 view;
    float4x4 projection;
    float4x4 inverse_view;
    float4x4 inverse_projection;
    float4x4 inverse_view_projection;

    ShaderFrustum frustum;
    ShaderFrustumCorners frustum_corners;

    float2		temporalaa_jitter;
    float2		temporalaa_jitter_prev;

    float4x4	previous_view;
    float4x4	previous_projection;
    float4x4	previous_view_projection;
    float4x4	previous_inverse_view_projection;
    float4x4	reflection_view_projection;
    float4x4	reflection_inverse_view_projection;
    float4x4	reprojection; // view_projection_inverse_matrix * previous_view_projection_matrix

    float2		aperture_shape;
    float		aperture_size;
    float		focal_length;

    float2 canvas_size;
    float2 canvas_size_rcp;
		   
    uint2 internal_resolution;
    float2 internal_resolution_rcp;

    uint4 scissor; // scissor in physical coordinates (left,top,right,bottom) range: [0, internal_resolution]
    float4 scissor_uv; // scissor in screen UV coordinates (left,top,right,bottom) range: [0, 1]
};

struct CameraCB
{
    ShaderCamera cameras[16];
};

CONSTANTBUFFER(g_xFrame, FrameCB, CBSLOT_RENDERER_FRAME);
CONSTANTBUFFER(g_xCamera, CameraCB, CBSLOT_RENDERER_CAMERA);

#define Voxel_RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
	"RootConstants(num32BitConstants=12, b999), " \
	"CBV(b0, space = 0, visibility = SHADER_VISIBILITY_PIXEL), " \
	"CBV(b1, space = 0, visibility = SHADER_VISIBILITY_PIXEL), " \
	"CBV(b0, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
	"CBV(b1, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
	"CBV(b2, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "DescriptorTable(CBV(b3, space = 1, numDescriptors = 10), visibility = SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(SRV(t0, numDescriptors = 20), visibility = SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(UAV(u0, numDescriptors = 10), visibility = SHADER_VISIBILITY_ALL)," \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, comparisonFunc = COMPARISON_GREATER_EQUAL, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s100, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s101, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s102, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s103, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_POINT)," \
    "StaticSampler(s104, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_POINT)," \
    "StaticSampler(s105, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_POINT)," \
    "StaticSampler(s106, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
    "StaticSampler(s107, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
    "StaticSampler(s108, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
    "StaticSampler(s109, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc = COMPARISON_GREATER_EQUAL),"


#endif