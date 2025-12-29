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

cbuffer MeshConstants : register(b0)
{
    float4x4 ModelToWorldMatrix; // Object to world
    float4x4 WorldToModelMatrix; // World to Object
};

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProjMatrix;
}

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float3 worldPositionGeom : WORLDPOS;
    float3 normalGeom : WORLDNORM;
    float4 position : SV_POSITION;
};

[RootSignature(_RootSig)]
VSOutput main(VSInput input)
{
    VSOutput output;
    
    // Transform position to world space
    output.worldPositionGeom = mul(ModelToWorldMatrix, float4(input.position, 1.0)).xyz;
    
    // Transform normal to world space using normal matrix (transpose of inverse model matrix)
    output.normalGeom = normalize(mul((float3x3) transpose(WorldToModelMatrix), input.normal));
    
    // Transform to clip space
    output.position = mul(ViewProjMatrix, float4(output.worldPositionGeom, 1.0));
    
    return output;
}