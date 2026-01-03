// LightingPS.hlsl
#include "VCTVoxelCommon.hlsli"

// t0: 体素体积（带 Mipmaps）
Texture3D<float4> g_VoxelVolume : register(t0);
SamplerState g_LinearClamp : register(s0);

// 世界坐标 → 体素 UVW [0,1]
float3 WorldToVoxelUVW(float3 worldPos)
{
    return (worldPos - g_VoxelWorldMin) / (g_VoxelSize * g_VoxelRes);
}

// 锥形追踪：返回间接辐射度（radiance）
float3 ConeTrace(
    float3 origin,
    float3 direction,
    float coneAperture, // 锥角半径（弧度或比例，这里用比例）
    int maxSteps = 32,
    float stepSize = 0.2f
)
{
    float3 pos = origin;
    float dist = 0.0f;
    float3 accumulated = 0.0f;
    float weight = 1.0f;

    for (int i = 0; i < maxSteps; ++i)
    {
        float3 uvw = WorldToVoxelUVW(pos);

        // 越界检测
        if (any(uvw < 0.0f) || any(uvw > 1.0f))
            break;

        // 计算当前锥在体素空间的半径 → 选择 mip level
        float coneRadiusInVoxels = dist * coneAperture * g_VoxelRes;
        float mipLevel = log2(coneRadiusInVoxels + 1e-5f); // 避免除零
        mipLevel = clamp(mipLevel, 0.0f, 7.0f); // 假设 8 级 mipmap

        // 采样预过滤的体素数据
        float3 radiance = g_VoxelVolume.SampleLevel(g_LinearClamp, uvw, mipLevel).rgb;

        // 累加（带衰减权重）
        accumulated += radiance * weight;
        weight *= 0.9f; // 距离衰减

        if (weight < 0.01f)
            break;

        // 步进
        dist += stepSize;
        pos += direction * stepSize;
    }

    return accumulated;
}

// ===== 主像素着色器 =====
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : WORLD_POS;
    float3 normal : NORMAL;
    float3 albedo : ALBEDO;
};

float4 main(PS_INPUT input) : SV_Target
{
    // 归一化
    float3 N = normalize(input.normal);
    float3 albedo = input.albedo;

    // 直接光照（简化：环境光 + 点光源）
    float3 directLight = albedo * 0.2; // ambient

    // === 间接光照：Voxel Cone Tracing ===
    float3 indirectDiffuse = ConeTrace(input.worldPos, N, 0.5f); // 大锥 → 漫反射 GI

    // 合并（可乘 albedo，也可不乘——取决于体素存的是 radiance 还是 irradiance）
    float3 finalColor = directLight + indirectDiffuse;

    return float4(finalColor, 1.0f);
}