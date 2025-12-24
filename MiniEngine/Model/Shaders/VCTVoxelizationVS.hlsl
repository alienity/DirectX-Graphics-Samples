#define _RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
	"CBV(b1)"

cbuffer MeshConstants : register(b0)
{
    float4x4 WorldMatrix; // Object to world
    float3x3 WorldIT; // Object normal to world normal
};

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProjMatrix;
}

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv0 : TEXCOORD0;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 worldPos : WORLD_POS;
	float2 uv : TEXCOORD0;
};

[RootSignature(_RootSig)]
VSOutput main(VSInput input)
{
    VSOutput output;
    output.pos = mul(ViewProjMatrix, float4(input.pos, 1.0f));
    output.worldPos = input.pos;
    output.uv = input.uv0;
    return output;
}