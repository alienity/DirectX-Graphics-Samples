#include "../Common.hlsli"
#include "VXGIRenderer.hlsli"

cbuffer VSConstants : register(b2)
{
    float4x4 modelMatrix;
    float4x4 modelMatrixIT;
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
    float2 texCoord : TexCoord0;
    float3 normalW : Normal;
#ifndef VOXELIZATION_GEOMETRY_SHADER_ENABLED
    float3 P : POSITION3D;
#endif // VOXELIZATION_GEOMETRY_SHADER_ENABLED
};

// [RootSignature(Voxelize_RootSig)]
VSOutput main(VSInput input)
{
    VSOutput vsOutput;

    vsOutput.position = mul(modelMatrix, float4(input.position, 1.0f));
    vsOutput.texCoord = input.texcoord0;
    vsOutput.normalW = mul(modelMatrixIT, float4(input.normal, 0.0f)).xyz;

#ifndef VOXELIZATION_GEOMETRY_SHADER_ENABLED
    vsOutput.P = vsOutput.position.xyz;
    
    VoxelClipMap clipmap = g_xFrameVoxel.vxgi.clipmaps[g_xVoxelizer.clipmap_index];

    // World space -> Voxel grid space:
    vsOutput.position.xyz = (vsOutput.position.xyz - clipmap.center) / clipmap.voxelSize;

    // Project onto dominant axis:
    const uint frustum_index = input.GetInstancePointer().GetCameraIndex();
    switch (frustum_index)
    {
    default:
    case 0:
        Out.pos.xyz = Out.pos.xyz;
        break;
    case 1:
        Out.pos.xyz = Out.pos.zyx;
        break;
    case 2:
        Out.pos.xyz = Out.pos.xzy;
        break;

        // Test: if rendered with 6 frustums, double sided voxelization happens here:
    case 3:
        Out.pos.xyz = Out.pos.xyz * float3(1, 1, -1);
        Out.N *= -1;
        break;
    case 4:
        Out.pos.xyz = Out.pos.zyx * float3(1, 1, -1);
        Out.N *= -1;
        break;
    case 5:
        Out.pos.xyz = Out.pos.xzy * float3(1, 1, -1);
        Out.N *= -1;
        break;
    }
    
#endif
    
    return vsOutput;
}
