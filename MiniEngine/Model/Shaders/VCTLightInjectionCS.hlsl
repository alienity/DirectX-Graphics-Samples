#include "Lighting.hlsli"

#define _RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
	"CBV(b1)"

StructuredBuffer<LightData> lightBuffer : register(t14);
Texture2DArray<float> lightShadowArrayTex : register(t15);
ByteAddressBuffer lightGrid : register(t16);
ByteAddressBuffer lightGridBitMask : register(t17);

Texture3D<float4> voxelAlbedoVol : register(t0);
Texture3D<float4> voxelNormalVol : register(t1);

Texture2D<float> sunShadowMap : register(t2);
SamplerComparisonState sunShadowCmp : register(s0);

RWTexture3D<float4> voxelRadianceVol : register(u0);

// 从MiniEngine光照系统获取光照数据
cbuffer VolumeConstants : register(b0)
{
    float3 VolumeWorldMin;
    float _Pad0;
    float4x4 ShadowViewProj;
};

// MiniEngine光照数据结构
cbuffer PSConstants : register(b1)
{
    float3 SunDirection;
    float3 SunColor;
    float3 AmbientColor;
    float4 ShadowTexelSize;

    float4 InvTileDim;
    uint4 TileCount;
    uint4 FirstLightIndex;

    uint FrameIndexMod2;
};

// 从LightManager获取的光照数据
StructuredBuffer<LightData> lightBuffer : register(t3);

float SampleShadow(float3 worldPos, float3 lightDir)
{
    float4 shadowPos = mul(ShadowViewProj, float4(worldPos, 1.0f));
    float3 shadowUV = shadowPos.xyz / shadowPos.w;
    if (any(shadowUV < 0) || any(shadowUV > 1))
        return 1.0f;
    return sunShadowMap.SampleCmpLevelZero(sunShadowCmp, shadowUV.xy, shadowUV.z);
}

// 计算点光源对voxel的贡献
float3 CalculatePointLightContribution(float3 worldPos, float3 normal, float3 albedo, LightData light)
{
    float3 toLight = light.pos - worldPos;
    float dist = length(toLight);
    if (dist > sqrt(light.radiusSq))
        return 0;
    
    float3 lightDir = toLight / dist;
    float NdotL = max(0, dot(normal, lightDir));
    if (NdotL <= 0)
        return 0;
    
    // 距离衰减
    float attenuation = saturate(1.0f - (dist * dist) / light.radiusSq);
    float3 lightContribution = NdotL * attenuation * light.color * albedo;
    return lightContribution;
}

// 计算方向光对voxel的贡献
float3 CalculateDirectionalLightContribution(float3 worldPos, float3 normal, float3 albedo, float3 dirLightDir, float3 dirLightColor)
{
    float NdotL = max(0, dot(normal, -dirLightDir));
    if (NdotL <= 0)
        return 0;
    
    float shadow = SampleShadow(worldPos, -dirLightDir);
    float3 lightContribution = NdotL * dirLightColor * albedo * shadow;
    return lightContribution;
}

[RootSignature(_RootSig)]
[numthreads(4, 4, 4)]
void main(uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId >= g_VoxelRes))
        return;
    
    float4 albedoData = voxelAlbedoVol[dispatchThreadId];
    if (albedoData.a == 0.0f)
    {
        voxelRadianceVol[dispatchThreadId] = 0;
        return;
    }

    float3 albedo = albedoData.rgb;
    float3 worldPos = VoxelIndexToWorld(dispatchThreadId);
    
    float3 normal = DecodeNormal(voxelNormalVol[dispatchThreadId].rgb);
    normal = normalize(normal); // 确保法线归一化
    
    float3 totalLight = 0;
    
    // 添加环境光
    totalLight += AmbientColor * albedo;
    
    // 计算方向光贡献
    float3 dirLightContribution = CalculateDirectionalLightContribution(worldPos, normal, albedo, SunDirection, SunColor);
    totalLight += dirLightContribution;
    
    // 计算点光源贡献
    uint numPointLights = min(FirstLightIndex.x, 128u); // 使用light manager中的第一个锥形光索引作为点光源数量的上限
    for (uint i = 0; i < numPointLights; ++i)
    {
        // 跳过锥形光和带阴影的锥形光
        if (i >= FirstLightIndex.x) break; // 第一个锥形光索引
        if (lightBuffer[i].type != 0) continue; // 只处理点光源 (type 0)
        
        LightData pointLight = lightBuffer[i];
        float3 pointLightContribution = CalculatePointLightContribution(worldPos, normal, albedo, pointLight);
        totalLight += pointLightContribution;
    }
    
    // 计算锥形光贡献
    uint numConeLights = min(FirstLightIndex.y, 128u); // 使用light manager中的第一个带阴影锥形光索引作为锥形光数量的上限
    for (uint i = FirstLightIndex.x; i < numConeLights; ++i)
    {
        if (i >= FirstLightIndex.y) break; // 第一个带阴影锥形光索引
        if (lightBuffer[i].type != 1) continue; // 只处理锥形光 (type 1)
        
        LightData coneLight = lightBuffer[i];
        float3 coneLightContribution = CalculatePointLightContribution(worldPos, normal, albedo, coneLight);
        totalLight += coneLightContribution;
    }
    
    // 计算带阴影的锥形光贡献
    for (uint i = FirstLightIndex.y; i < 128u; ++i)
    {
        if (lightBuffer[i].type != 2) continue; // 只处理带阴影锥形光 (type 2)
        
        LightData shadowedConeLight = lightBuffer[i];
        float3 shadowedConeLightContribution = CalculatePointLightContribution(worldPos, normal, albedo, shadowedConeLight);
        totalLight += shadowedConeLightContribution;
    }
    
    // 确保光照值在合理范围内
    totalLight = max(totalLight, 0.0f);
    
    float3 radiance = totalLight;
    
    voxelRadianceVol[dispatchThreadId] = float4(radiance, 1.0f);
}