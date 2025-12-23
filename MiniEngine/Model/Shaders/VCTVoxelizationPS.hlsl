#include "VCTVoxelCommon.hlsli"

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : WORLD_POS;
    float3 color : COLOR;
};

// 注意：格式必须支持浮点 alpha！推荐 R16G16B16A16_FLOAT
RWTexture3D<float4> g_VoxelVolume : register(u0);

ConstantBuffer<VoxelCB> g_CB : register(b1);

int3 WorldToVoxelIndex(float3 w)
{
    return int3((w - g_CB.VoxelWorldMin) / g_CB.VoxelSize);
}

bool IsValid(int3 idx)
{
    return all(idx >= 0) && all(idx < int(g_CB.VoxelRes));
}

void main(PS_INPUT input)
{
    int3 idx = WorldToVoxelIndex(input.worldPos);
    if (!IsValid(idx))
        return;

    // 计算当前片段到体素化相机的距离（标量）
    float dist = distance(input.worldPos, g_CB.CameraPos);

    // 读取当前体素值
    float4 current = g_VoxelVolume[idx];

    // 策略：如果体素未初始化（alpha=0）或新距离更小，则写入
    if (current.a == 0.0f || dist < current.a)
    {
        g_VoxelVolume[idx] = float4(input.color, dist);
    }
}