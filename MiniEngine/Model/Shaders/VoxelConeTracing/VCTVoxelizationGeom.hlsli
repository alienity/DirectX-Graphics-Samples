#ifndef VOXELIZATION_GEOM_HLSL
#define VOXELIZATION_GEOM_HLSL

#include "VCTVoxelization.hlsli"

#ifdef CONSERVATIVE_VOXELIZATION
struct ConservativeVoxelizationFragmentInput
{
    float3 posW;
    float3 posClip;
    float4 triangleAABB;
    float3 trianglePosW[3];
    uint faceIdx;
    uint viewportIndex;
};

#define GSINPUT GSInput input[3]
#define GSPOS(idx) input[idx].position

// Coservative Voxelization based on "Conservative Rasterization", GPU Gems 2 Chapter 42 by Jon Hasselgren, Tomas Akenine-Möller and Lennart Ohlsson:
// http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter42.html
void cvGeometryPass(in GSINPUT, out float4 positionsClip[3],
                    out ConservativeVoxelizationFragmentInput out_cvFrag)
{
    int idx = getDominantAxisIdx(GSPOS(0), GSPOS(1), GSPOS(2));
    out_cvFrag.viewportIndex = idx;

    positionsClip[0] = mul(u_viewProj[idx], float4(GSPOS(0).xyz, 1.0));
    positionsClip[1] = mul(u_viewProj[idx], float4(GSPOS(1).xyz, 1.0));
    positionsClip[2] = mul(u_viewProj[idx], float4(GSPOS(2).xyz, 1.0));

    float2 hPixel = 1.0 / u_viewportSizes[idx];

    float3 triangleNormalClip = normalize(cross(positionsClip[1].xyz - positionsClip[0].xyz,
                                                positionsClip[2].xyz - positionsClip[0].xyz));
    computeExtendedTriangle(hPixel, triangleNormalClip, positionsClip, out_cvFrag.triangleAABB);

    out_cvFrag.faceIdx = idx * 2;
    if (triangleNormalClip.z > 0.0)
        out_cvFrag.faceIdx += 1;

    // Using the original triangle for the intersection tests introduces a slight underestimation
    out_cvFrag.trianglePosW[0] = GSPOS(0).xyz;
    out_cvFrag.trianglePosW[1] = GSPOS(1).xyz;
    out_cvFrag.trianglePosW[2] = GSPOS(2).xyz;
}

void cvEmitVertex(inout ConservativeVoxelizationFragmentInput output, float4 posClip)
{
    output.posClip = posClip.xyz;

    float4 posW = mul(u_viewProjInv[output.viewportIndex], posClip);
    output.posW = posW.xyz / posW.w;
}
#endif // CONSERVATIVE_VOXELIZATION

#endif // VOXELIZATION_GEOM_HLSL
