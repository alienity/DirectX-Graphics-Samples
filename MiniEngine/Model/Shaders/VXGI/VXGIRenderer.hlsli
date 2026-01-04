#ifndef VXGIRENDERER_H
#define VXGIRENDERER_H

#include "VXGICommon.hlsli"

struct FrameVoxelCB
{
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

#endif