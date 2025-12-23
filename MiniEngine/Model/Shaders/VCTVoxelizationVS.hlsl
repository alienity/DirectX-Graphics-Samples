// VoxelizationVS.hlsl
#include "VCTVoxelCommon.hlsli"

struct VS_INPUT
{
    float3 pos : POSITION;
    float3 color : COLOR;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : WORLD_POS;
    float3 color : COLOR;
};

ConstantBuffer<VoxelCB> g_CB : register(b0);

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(g_CB.ViewProj, float4(input.pos, 1.0f));
    output.worldPos = input.pos;
    output.color = input.color;
    return output;
}