#ifndef VXGIRENDERER_H
#define VXGIRENDERER_H

#include "VXGICommon.hlsli"

struct FrameVoxelCB
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
};

struct CameraCB
{
    ShaderCamera cameras[16];
};

CONSTANTBUFFER(g_xFrameVoxel, FrameVoxelCB, CBSLOT_RENDERER_FRAME);
CONSTANTBUFFER(g_xCamera, CameraCB, CBSLOT_RENDERER_CAMERA);

SamplerState sampler_linear_clamp : register(s9);

#define Voxel_RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, space = 0, visibility = SHADER_VISIBILITY_PIXEL), " \
    "CBV(b1, space = 0, visibility = SHADER_VISIBILITY_PIXEL), " \
    "CBV(b0, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b1, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b2, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b3, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "DescriptorTable(SRV(t0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t10, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(UAV(u0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s9, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_ALL)," \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "comparisonFunc = COMPARISON_GREATER_EQUAL," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)"

#endif