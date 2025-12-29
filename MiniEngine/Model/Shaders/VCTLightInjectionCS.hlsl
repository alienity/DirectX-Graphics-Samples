#include "Common.hlsli"
#include "Lighting.hlsli"

Texture3D<float4> voxelAlbedoVol : register(t0);
Texture3D<float4> voxelNormalVol : register(t1);

Texture2D<float> texShadow : register(t13);

RWTexture3D<float4> voxelRadianceVol : register(u0);

cbuffer VolumeConstants : register(b1)
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

#define _RootSig \
    "RootFlags(0), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 10))," \
    "DescriptorTable(Sampler(s0, numDescriptors = 10))," \
    "DescriptorTable(SRV(t10, numDescriptors = 10))," \
    "CBV(b1), " \
    "DescriptorTable(UAV(u0, numDescriptors = 10))," \
    "StaticSampler(s10, maxAnisotropy = 8)," \
    "StaticSampler(s11," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "comparisonFunc = COMPARISON_GREATER_EQUAL," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8)"

[RootSignature(_RootSig)]
[numthreads(8, 8, 8)]
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

    for (uint lightIndex = 0; lightIndex < MAX_LIGHTS; lightIndex += 1)
    {
        LightData lightData = lightBuffer[lightIndex];
        float3 lightWorldPos = lightData.pos;
        float lightCullRadius = sqrt(lightData.radiusSq);

		/*
        bool overlapping = true;
        for (int p = 0; p < 6; p++)
        {
            float d = dot(lightWorldPos, frustumPlanes[p].xyz) + frustumPlanes[p].w;
            if (d < -lightCullRadius)
            {
                overlapping = false;
            }
        }
        
        if (!overlapping)
            continue;
		*/
        
        float3 specularAlbedo = 0;
        float specularMask = 0;
		float gloss = 0;
        
        switch (lightData.type)
        {
            case 0: // sphere
                colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
                break;

            case 1: // cone
                colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
                break;

            case 2: // cone w/ shadow map
                colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
                break;
        }
    }

    voxelRadianceVol[dispatchThreadId] = float4(colorSum.rgb, 0);
    
}