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

struct GSInput
{
    float3 worldPositionGeom : WORLDPOS;
    float3 normalGeom : WORLDNORM;
};

struct GSOutput
{
    float3 worldPositionFrag : WORLDPOS;
    float3 normalFrag : WORLDNORM;
    float4 position : SV_POSITION;
};

[maxvertexcount(3)]
void main(triangle GSInput input[3], inout TriangleStream<GSOutput> outputStream)
{
    float3 p1 = input[1].worldPositionGeom - input[0].worldPositionGeom;
    float3 p2 = input[2].worldPositionGeom - input[0].worldPositionGeom;
    float3 p = abs(cross(p1, p2));
    
    [unroll]
    for (uint i = 0; i < 3; ++i)
    {
        GSOutput output;
        output.worldPositionFrag = input[i].worldPositionGeom;
        output.normalFrag = input[i].normalGeom;
        
        if (p.z > p.x && p.z > p.y)
        {
            output.position = float4(output.worldPositionFrag.x, output.worldPositionFrag.y, 0, 1);
        }
        else if (p.x > p.y && p.x > p.z)
        {
            output.position = float4(output.worldPositionFrag.y, output.worldPositionFrag.z, 0, 1);
        }
        else
        {
            output.position = float4(output.worldPositionFrag.x, output.worldPositionFrag.z, 0, 1);
        }
        
        outputStream.Append(output);
    }
    
    outputStream.RestartStrip();
}