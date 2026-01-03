#include "../Common.hlsli"
#include "VCTVoxelizationCommon.hlsli"

cbuffer VSConstants : register(b0)
{
    float4x4 u_model;
    float4x4 u_modelIT;
};

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normalW : WorldPos;
    float2 texCoord : TexCoord0;
};

[RootSignature(Voxelize_RootSig)]
VSOutput main(VSInput vsInput, uint vertexID : SV_VertexID)
{
    VSOutput vsOutput;

    vsOutput.position = mul(u_model, float4(vsInput.position, 1.0f));
    vsOutput.normalW = mul(u_modelIT, float4(vsInput.normal, 0.0f)).xyz;
    vsOutput.texCoord = vsInput.texcoord0;

    return vsOutput;
}
