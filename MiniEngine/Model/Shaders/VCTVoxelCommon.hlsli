
// VoxelCommon.hlsli
#ifndef VOXEL_COMMON_HLSLI
#define VOXEL_COMMON_HLSLI

static const uint VOXEL_RES = 128;
static const float VOXEL_WORLD_SIZE = 20.0f;
static const float3 VOXEL_WORLD_MIN = float3(-10.0f, -10.0f, -10.0f);

struct VoxelCB
{
    float4x4 ViewProj; // 正交视图投影
    float3 VoxelWorldMin; // [-10, -10, -10]
    float VoxelSize; // = 20 / 128
    float3 CameraPos; // 当前体素化视角的相机位置（用于计算距离）
    uint VoxelRes; // = 128
};

#endif