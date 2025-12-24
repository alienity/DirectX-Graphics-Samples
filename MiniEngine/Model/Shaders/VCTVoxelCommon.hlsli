//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//

#ifndef __VCT_VOXEL_COMMON_HLSLI__
#define __VCT_VOXEL_COMMON_HLSLI__

// Voxel Cone Tracing 公共定义
static const uint   g_VoxelRes = 128;
static const float  g_VoxelWorldSize = 20.0f;
static const float  g_VoxelSize = g_VoxelWorldSize / g_VoxelRes;

// 体素体积的边界
float3 g_VoxelWorldMin = float3(-10.0f, -10.0f, -10.0f);
float3 g_VoxelWorldMax = float3(10.0f, 10.0f, 10.0f);

// 将体素索引转换为世界坐标
float3 VoxelIndexToWorld(uint3 idx)
{
    return g_VoxelWorldMin + (idx + 0.5f) * g_VoxelSize;
}

// 将世界坐标转换为体素索引
int3 WorldToVoxelIndex(float3 worldPos)
{
    return int3((worldPos - g_VoxelWorldMin) / g_VoxelSize);
}

// 检查体素索引是否有效
bool IsValidVoxelIndex(int3 idx)
{
    return all(idx >= 0) && all(idx < int(g_VoxelRes));
}

// 解码法线从[0,1]到[-1,1]
float3 DecodeNormal(float3 n)
{
    return n * 2.0f - 1.0f; // [0,1] to [-1,1]
}

#endif // __VCT_VOXEL_COMMON_HLSLI__