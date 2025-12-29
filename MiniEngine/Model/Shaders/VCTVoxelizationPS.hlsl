#define _RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t10, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "CBV(b1), " \
    "DescriptorTable(UAV(u0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL), " \
    "SRV(t20, visibility = SHADER_VISIBILITY_VERTEX), " \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "comparisonFunc = COMPARISON_GREATER_EQUAL," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)"

#include "Common.hlsli"

Texture2D<float4> baseColorTexture : register(t0);
Texture2D<float3> emissiveTexture : register(t3);
Texture2D<float3> normalTexture : register(t4);

SamplerState baseColorSampler : register(s0);
SamplerState emissiveSampler : register(s3);
SamplerState normalSampler : register(s4);

Texture2D<float> texSunShadow : register(t13);

cbuffer MaterialConstants : register(b0)
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float normalTextureScale;
    uint flags;
    float3 _Padding;
}

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProj;
    float4x4 SunShadowMatrix;
    float3 ViewerPos;
    float3 SunDirection;
    float3 SunIntensity;
    float _pad;
}

RWTexture3D<float4> voxelColorVolume : register(u0);
RWTexture3D<float4> voxelNormalVolume : register(u1);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPositionFrag : WORLDPOS;
    float3 normalFrag : WORLDNORM;
};

[RootSignature(_RootSig)]
void main(PSInput input)
{
    int3 idx = WorldToVoxelIndex(input.worldPos);
    if (!IsValid(idx))
        return;

    float dist = distance(input.worldPos, ViewerPos);

    float4 current = voxelColorVolume[idx];

    if (current.a == 0.0f || dist < current.a)
    {
		float3 texAlbedo = baseColorTexture.Sample(baseColorSampler, input.uv).rgb;
        float3 texNormal = normalTexture.Sample(normalSampler, input.uv).rgb;

        voxelColorVolume[idx] = float4(texAlbedo.rgb, dist);
		voxelNormalVolume[idx] = float4(texNormal.rgb, 1.0f);
    }
}
