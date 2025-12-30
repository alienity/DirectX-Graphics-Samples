#include "Common.hlsli"


struct GSInput
{
    float4 position : SV_Position;
    float3 worldPos : WorldPos;
    float2 texCoord : TexCoord0;
    float3 viewDir : TexCoord1;
    float3 shadowCoord : TexCoord2;
    float3 normal : Normal;
    float3 tangent : Tangent;
    float3 bitangent : Bitangent;
#if ENABLE_TRIANGLE_ID
    uint vertexID : TexCoord3;
#endif
};

struct GSOutput
{
    float4 position : SV_Position;
    float3 worldPos : WorldPos;
    float2 texCoord : TexCoord0;
    float3 viewDir : TexCoord1;
    float3 shadowCoord : TexCoord2;
    float3 normal : Normal;
    float3 tangent : Tangent;
    float3 bitangent : Bitangent;
#if ENABLE_TRIANGLE_ID
    uint vertexID : TexCoord3;
#endif
};

[maxvertexcount(3)]
void main(triangle GSInput input[3], inout TriangleStream<GSOutput> outputStream)
{
    float3 p1 = input[1].worldPos - input[0].worldPos;
    float3 p2 = input[2].worldPos - input[0].worldPos;
    float3 p = abs(cross(p1, p2));
    
    [unroll]
    for (uint i = 0; i < 3; ++i)
    {
        GSOutput output;
        output.worldPos = input[i].worldPos;
        output.texCoord = input[i].texCoord;
        output.viewDir = input[i].viewDir;
        output.shadowCoord = input[i].shadowCoord;
        output.normal = input[i].normal;
        output.tangent = input[i].tangent;
        output.bitangent = input[i].bitangent;
#if ENABLE_TRIANGLE_ID
		output.vertexID = input[i].vertexID;
#endif
        
        if (p.z > p.x && p.z > p.y)
        {
            output.position = float4(input[i].position.x, input[i].position.y, 0, 1);
        }
        else if (p.x > p.y && p.x > p.z)
        {
            output.position = float4(input[i].position.y, input[i].position.z, 0, 1);
        }
        else
        {
            output.position = float4(input[i].position.x, input[i].position.z, 0, 1);
        }
        
        outputStream.Append(output);
    }
    
    outputStream.RestartStrip();
}