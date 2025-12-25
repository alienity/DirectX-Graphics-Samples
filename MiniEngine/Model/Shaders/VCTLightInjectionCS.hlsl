#include "Common.hlsli"
#include "Lighting.hlsli"

#define _RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
	"CBV(b1)"

Texture3D<float4> voxelAlbedoVol : register(t0);
Texture3D<float4> voxelNormalVol : register(t1);

Texture2D<float> texShadow : register(t13);

RWTexture3D<float4> voxelRadianceVol : register(u0);

cbuffer VolumeConstants : register(b0)
{
    float4x4 worldToProjection;
    float4x4 worldToShadow;
    float3 viewDir;
    uint _Pad0;
    float3 viewerPos;
    float voxelSize; // = 20 / 128
    float3 voxelWorldMin; // [-10, -10, -10]
    uint voxelRes; // = 128
};

void ShadeLightsVCT(inout float3 colorSum, uint2 pixelPos,
	float3 diffuseAlbedo, // Diffuse albedo
	float3 normal,
	float3 viewDir,
	float3 worldPos
	)
{
    return ShadeLights(colorSum, pixelPos,
		diffuseAlbedo,
		float3(0, 0, 0),
		0,
		0,
		normal,
		viewDir,
		worldPos
		);
}

float3 VoxelIndexToWorld(uint3 idx)
{
    return voxelWorldMin + (idx + 0.5f) * voxelSize;
}

int3 WorldToVoxelIndex(float3 worldPos)
{
    return int3((worldPos - voxelWorldMin) / voxelSize);
}

bool IsValidVoxelIndex(int3 idx)
{
    return all(idx >= 0) && all(idx < int(voxelRes));
}

float3 DecodeNormal(float3 n)
{
    return n * 2.0f - 1.0f; // [0,1] to [-1,1]
}

[RootSignature(_RootSig)]
[numthreads(4, 4, 4)]
void main(uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId >= voxelRes))
        return;
    
    float4 albedoData = voxelAlbedoVol[dispatchThreadId];
    if (albedoData.a == 0.0f)
    {
        voxelRadianceVol[dispatchThreadId] = 0;
        return;
    }

    float3 diffuseAlbedo = albedoData.rgb;
    float3 worldPos = VoxelIndexToWorld(dispatchThreadId);
    
    float3 normal = DecodeNormal(voxelNormalVol[dispatchThreadId].rgb);
    normal = normalize(normal);
    
    float3 shadowCoord = mul(worldToShadow, float4(worldPos, 1.0)).xyz;

    float3 colorSum = 0;
    {
        colorSum += ApplyAmbientLight(diffuseAlbedo, 1, AmbientColor);
    }

    float3 specularAlbedo = float3(0, 0, 0);
    float specularMask = 0;
    float3 tmpViewDir = normalize(viewDir);
    colorSum += ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, 0, normal, tmpViewDir, SunDirection, SunColor, shadowCoord, texShadow);

    ShadeLights(colorSum, pixelPos,
		diffuseAlbedo,
		specularAlbedo,
		specularMask,
		gloss,
		normal,
		viewDir,
		vsOutput.worldPos
		);













    float3 totalLight = 0;
    
    totalLight += AmbientColor * albedo;
    
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