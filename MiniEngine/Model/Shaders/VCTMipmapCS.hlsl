// MipmapCS.hlsl
#include "VCTVoxelCommon.hlsli"

// u0: src level (read)
// u1: dst level (write)
RWTexture3D<float4> g_SrcVolume : register(u0);
RWTexture3D<float4> g_DstVolume : register(u1);

// 根着色器常量传入当前 mip level（实际未用，dispatch size 已隐含）
cbuffer MipCB : register(b0)
{
    uint g_SrcLevel;
}

[numthreads(4, 4, 4)]
void main(uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // 对 2x2x2 邻域采样平均（box filter）
    float4 sum = 0;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
            {
                uint3 srcIdx = dispatchThreadId * 2 + uint3(x, y, z);
                sum += g_SrcVolume[srcIdx];
            }
    g_DstVolume[dispatchThreadId] = sum / 8.0f;
}