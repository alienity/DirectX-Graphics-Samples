#include "VXGIRenderer.hlsli"

// cbuffer VSConstants : register(b0)
// {
//     float4x4 modelMatrix;
//     float4x4 modelMatrixIT;
//     int cameraIndex;
//     int3 Pad0;
// };

struct VSConstants
{
    float4x4 modelMatrix;
    float4x4 modelMatrixIT;
    int cameraIndex;
    int3 Pad0;
};

ConstantBuffer<VSConstants> m_xVSConstants : register(b0);

ConstantBuffer<VoxelizerCB> g_xVoxelizer : register(b0, space1);
ConstantBuffer<FrameCB> g_xFrame : register(b1, space1);
ConstantBuffer<CameraCB> g_xCamera : register(b2, space1);

//CONSTANTBUFFER(g_xVoxelizer, VoxelizerCB, CBSLOT_RENDERER_VOXELIZER);
//CONSTANTBUFFER(g_xFrame, FrameCB, CBSLOT_RENDERER_FRAME);
//CONSTANTBUFFER(g_xCamera, CameraCB, CBSLOT_RENDERER_CAMERA);

float4x4 getModelMatrix()
{
    return m_xVSConstants.modelMatrix;
}

float4x4 getModelMatrixIT()
{
    return m_xVSConstants.modelMatrixIT;
}

int getCameraIndex()
{
    return m_xVSConstants.cameraIndex;
}

struct VSInput
{
    float3 pos : POSITION;
    float2 texcoord0 : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv : TexCoord0;
    float3 N : Normal;
    /*
#ifndef VOXELIZATION_GEOMETRY_SHADER_ENABLED
    float3 P : POSITION3D;
#endif // VOXELIZATION_GEOMETRY_SHADER_ENABLED
    */
};

[RootSignature(Voxel_RootSig)]
VSOutput main(VSInput input)
{
    VSOutput Out;

    Out.pos = mul(getModelMatrix(), float4(input.pos, 1.0f));
    Out.uv = input.texcoord0;
    Out.N = mul(getModelMatrixIT(), float4(input.normal, 0.0f)).xyz;
    /*
#ifndef VOXELIZATION_GEOMETRY_SHADER_ENABLED
    Out.P = Out.pos.xyz;
    
    VoxelClipMap clipmap = g_xFrame.vxgi.clipmaps[g_xVoxelizer.clipmap_index];

    // World space -> Voxel grid space:
    Out.pos.xyz = (Out.pos.xyz - clipmap.center) / clipmap.voxelSize;

    // Project onto dominant axis:
    const uint frustum_index = getCameraIndex();
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
    
	// Voxel grid space -> Clip space
    Out.pos.xy *= g_xFrame.vxgi.resolution_rcp;
    Out.pos.zw = 1;
#endif
    */
    return Out;
}
