#ifndef VOXELIZATION_FRAG_HLSL
#define VOXELIZATION_FRAG_HLSL

#include "VCTVoxelization.hlsli"
#include "VCTCommon.hlsli"
#include "VCTIntersection.hlsli"
#include "VCTConversion.hlsli"
#include "VCTAtomicOperations.hlsli"
#include "VCTSettings.hlsli"

bool isInsideDownsampleRegion(float3 posW)
{
    return u_clipmapLevel > 0 && all(posW >= u_prevRegionMin + float3(u_downsampleTransitionRegionSize)) && 
                                 all(posW <= u_prevRegionMax - float3(u_downsampleTransitionRegionSize));
}

bool isOutsideVoxelizationRegion(float3 posW)
{
    return any(posW < u_regionMin) || any(posW > u_regionMax);
}

#ifdef CONSERVATIVE_VOXELIZATION

struct ConservativeVoxelizationFragmentInput
{
    float3 posW : TEXCOORD0;
    float3 posClip : TEXCOORD1;
    float4 triangleAABB : TEXCOORD2;
    float3 trianglePosW[3] : TEXCOORD3;
    int faceIdx : TEXCOORD6;
};

bool cvIntersectsTriangle(float3 posW, ConservativeVoxelizationFragmentInput in_cvFrag)
{
    AABBox3D b;
    b.minPos = floor(posW / u_voxelSize) * u_voxelSize;
    b.maxPos = b.minPos + float3(u_voxelSize);
    
    return aabbIntersectsTriangle(b, in_cvFrag.trianglePosW[0], in_cvFrag.trianglePosW[1], in_cvFrag.trianglePosW[2]);
}

bool cvIsValidVoxelizationCandidate(float3 posW, ConservativeVoxelizationFragmentInput in_cvFrag)
{
    if(isInsideDownsampleRegion(posW) || isOutsideVoxelizationRegion(posW))
        return false;
        
    return cvIntersectsTriangle(posW, in_cvFrag);
}

void cvStoreVoxelColor(RWTexture3D<float4> image, float4 color, ConservativeVoxelizationFragmentInput in_cvFrag)
{
    // There are potentially 3 voxels in the depth direction that can overlap the triangle
    float3 posW0 = in_cvFrag.posW;
    float3 posW1 = in_cvFrag.posW - u_voxelSize * MAIN_AXES[gl_ViewportIndex];
    float3 posW2 = in_cvFrag.posW + u_voxelSize * MAIN_AXES[gl_ViewportIndex];

    if (!isOutsideVoxelizationRegion(posW0) && cvIntersectsTriangle(posW0, in_cvFrag))
        storeVoxelColorRGBA8(image, posW0, color);

    if (!isOutsideVoxelizationRegion(posW1) && cvIntersectsTriangle(posW1, in_cvFrag))
        storeVoxelColorRGBA8(image, posW1, color);

    if (!isOutsideVoxelizationRegion(posW2) && cvIntersectsTriangle(posW2, in_cvFrag))
        storeVoxelColorRGBA8(image, posW2, color);
}

void cvStoreVoxelColor(RWTexture3D<uint> image, float4 color, ConservativeVoxelizationFragmentInput in_cvFrag)
{
    // There are potentially 3 voxels in the depth direction that can overlap the triangle
    float3 posW0 = in_cvFrag.posW;
    float3 posW1 = in_cvFrag.posW - u_voxelSize * MAIN_AXES[gl_ViewportIndex];
    float3 posW2 = in_cvFrag.posW + u_voxelSize * MAIN_AXES[gl_ViewportIndex];

    if (cvIsValidVoxelizationCandidate(posW0, in_cvFrag))
        storeVoxelColorR32UIRGBA8(image, posW0, color);

    if (cvIsValidVoxelizationCandidate(posW1, in_cvFrag))
        storeVoxelColorR32UIRGBA8(image, posW1, color);

    if (cvIsValidVoxelizationCandidate(posW2, in_cvFrag))
        storeVoxelColorR32UIRGBA8(image, posW2, color);
}

void cvStoreVoxelColorAvg(RWTexture3D<uint> image, float4 color, int3 faceIndices, float3 weight, ConservativeVoxelizationFragmentInput in_cvFrag)
{
    // There are potentially 3 voxels in the depth direction that can overlap the triangle
    float3 posW0 = in_cvFrag.posW;
    float3 posW1 = in_cvFrag.posW - u_voxelSize * MAIN_AXES[gl_ViewportIndex];
    float3 posW2 = in_cvFrag.posW + u_voxelSize * MAIN_AXES[gl_ViewportIndex];

    if (cvIsValidVoxelizationCandidate(posW0, in_cvFrag))
        storeVoxelColorAtomicRGBA8Avg(image, posW0, color, faceIndices, weight);

    if (cvIsValidVoxelizationCandidate(posW1, in_cvFrag))
        storeVoxelColorAtomicRGBA8Avg(image, posW1, color, faceIndices, weight);

    if (cvIsValidVoxelizationCandidate(posW2, in_cvFrag))
        storeVoxelColorAtomicRGBA8Avg(image, posW2, color, faceIndices, weight);
}

void cvStoreVoxelColorAvg(RWTexture3D<uint> image, float4 color, ConservativeVoxelizationFragmentInput in_cvFrag)
{
    // There are potentially 3 voxels in the depth direction that can overlap the triangle
    float3 posW0 = in_cvFrag.posW;
    float3 posW1 = in_cvFrag.posW - u_voxelSize * MAIN_AXES[gl_ViewportIndex];
    float3 posW2 = in_cvFrag.posW + u_voxelSize * MAIN_AXES[gl_ViewportIndex];

    if (cvIsValidVoxelizationCandidate(posW0, in_cvFrag))
        storeVoxelColorAtomicRGBA8Avg6Faces(image, posW0, color);

    if (cvIsValidVoxelizationCandidate(posW1, in_cvFrag))
        storeVoxelColorAtomicRGBA8Avg6Faces(image, posW1, color);

    if (cvIsValidVoxelizationCandidate(posW2, in_cvFrag))
        storeVoxelColorAtomicRGBA8Avg6Faces(image, posW2, color);
}

bool cvFailsPreConditions(ConservativeVoxelizationFragmentInput in_cvFrag)
{
    return (in_cvFrag.posClip.x < in_cvFrag.triangleAABB.x || in_cvFrag.posClip.y < in_cvFrag.triangleAABB.y || 
            in_cvFrag.posClip.x > in_cvFrag.triangleAABB.z || in_cvFrag.posClip.y > in_cvFrag.triangleAABB.w);
}

#endif // CONSERVATIVE_VOXELIZATION

bool failsPreConditions(float3 posW)
{
    return isInsideDownsampleRegion(posW) || isOutsideVoxelizationRegion(posW);
}

#endif // VOXELIZATION_GEOM_HLSL
