#ifndef VOXELIZATION_HLSL
#define VOXELIZATION_HLSL

#include "VCTCommon.hlsli"
#include "VCTintersection.hlsli"
#include "VCTConversion.hlsli"
#include "VCTAtomicOperations.hlsli"
#include "VCTSettings.hlsli"

cbuffer VoxelizationConstants : register(b0)
{
    int u_clipmapLevel;
    int u_clipmapResolution;
    int u_clipmapResolutionWithBorder;

    float3 u_regionMin;
    float3 u_regionMax;
    float3 u_prevRegionMin;
    float3 u_prevRegionMax;
    float u_downsampleTransitionRegionSize;
    float u_maxExtent;
    float u_voxelSize;

    float4x4 u_viewProj[3];
    float4x4 u_viewProjInv[3];
    float2 u_viewportSizes[3];
}

// Computation of an extended triangle in clip space based on 
// "Conservative Rasterization", GPU Gems 2 Chapter 42 by Jon Hasselgren, Tomas Akenine-Möller and Lennart Ohlsson:
// http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter42.html
void computeExtendedTriangle(float2 halfPixelSize, float3 triangleNormalClip, inout float4 trianglePositionsClip[3],
                             out float4 triangleAABBClip)
{
    float trianglePlaneD = dot(trianglePositionsClip[0].xyz, triangleNormalClip);
    float nSign = sign(triangleNormalClip.z);

    // Compute plane equations
    float3 plane[3];
    plane[0] = cross(trianglePositionsClip[0].xyw - trianglePositionsClip[2].xyw, trianglePositionsClip[2].xyw);
    plane[1] = cross(trianglePositionsClip[1].xyw - trianglePositionsClip[0].xyw, trianglePositionsClip[0].xyw);
    plane[2] = cross(trianglePositionsClip[2].xyw - trianglePositionsClip[1].xyw, trianglePositionsClip[1].xyw);

    // Move the planes by the appropriate semidiagonal
    plane[0].z -= nSign * dot(halfPixelSize, abs(plane[0].xy));
    plane[1].z -= nSign * dot(halfPixelSize, abs(plane[1].xy));
    plane[2].z -= nSign * dot(halfPixelSize, abs(plane[2].xy));

    // Compute triangle AABB in clip space
    triangleAABBClip.xy = min(trianglePositionsClip[0].xy,
                              min(trianglePositionsClip[1].xy, trianglePositionsClip[2].xy));
    triangleAABBClip.zw = max(trianglePositionsClip[0].xy,
                              max(trianglePositionsClip[1].xy, trianglePositionsClip[2].xy));

    triangleAABBClip.xy -= halfPixelSize;
    triangleAABBClip.zw += halfPixelSize;

    for (int i = 0; i < 3; ++i)
    {
        // Compute intersection of the planes
        trianglePositionsClip[i].xyw = cross(plane[i], plane[(i + 1) % 3]);
        trianglePositionsClip[i].xyw /= trianglePositionsClip[i].w;
        trianglePositionsClip[i].z = -(trianglePositionsClip[i].x * triangleNormalClip.x +
            trianglePositionsClip[i].y * triangleNormalClip.y -
            trianglePlaneD) / triangleNormalClip.z;
    }
}

int3 computeImageCoords(float3 posW)
{
    // Avoid floating point imprecision issues by clamping to narrowed bounds
    float c = u_voxelSize * 0.25; // Error correction constant
    posW = clamp(posW, u_regionMin + c, u_regionMax - c);

    float3 clipCoords = transformPosWToClipUVW(posW, u_maxExtent);

    // The & (u_clipmapResolution - 1) (aka % u_clipmapResolution) is important here because
    // clipCoords can be in [0,1] and thus cause problems at the border (value of 1) of the physical
    // clipmap since the computed value would be 1 * u_clipmapResolution and thus out of bounds.
    // The reason is that in transformPosWToClipUVW the frac() operation is used and due to floating point
    // precision limitations the operation can return 1 instead of the mathematically correct fraction.
    int3 imageCoords = int3(clipCoords * u_clipmapResolution) & (u_clipmapResolution - 1);

#ifdef VOXEL_TEXTURE_WITH_BORDER
    imageCoords += int3(BORDER_WIDTH, BORDER_WIDTH, BORDER_WIDTH);
#endif

    // Target the correct clipmap level
    imageCoords.y += u_clipmapResolutionWithBorder * u_clipmapLevel;

    return imageCoords;
}

// Store voxel color - Atomic operation average (6 faces)
void storeVoxelColorAtomicRGBA8Avg6Faces(RWTexture3D<uint> image, float3 posW, float4 color)
{
    int3 imageCoords = computeImageCoords(posW);

    for (int i = 0; i < 6; ++i)
    {
        int3 faceCoord = imageCoords + int3(u_clipmapResolutionWithBorder * i, 0, 0);
        imageAtomicRGBA8Avg(image, faceCoord, color);
    }
}

// Store voxel color - R32UI format (packed as RGBA8)
void storeVoxelColorR32UIRGBA8(RWTexture3D<uint> image, float3 posW, float4 color)
{
    int3 imageCoords = computeImageCoords(posW);
    uint packedColor = convertVec4ToRGBA8(color * 255.0f);

    for (int i = 0; i < 6; ++i)
    {
        int3 faceCoord = imageCoords + int3(u_clipmapResolutionWithBorder * i, 0, 0);
        image[faceCoord] = packedColor;
    }
}

// Store voxel color - RGBA8UI format
void storeVoxelColorRGBA8(RWTexture3D<uint4> image, float3 posW, float4 color)
{
    int3 imageCoords = computeImageCoords(posW);
    uint4 colorU = uint4(color * 255.0f);

    for (int i = 0; i < 6; ++i)
    {
        int3 faceCoord = imageCoords + int3(u_clipmapResolutionWithBorder * i, 0, 0);
        image[faceCoord] = colorU;
    }
}

// Store voxel color - RGBA8 format (float)
void storeVoxelColorRGBA8(RWTexture3D<float4> image, float3 posW, float4 color)
{
    int3 imageCoords = computeImageCoords(posW);

    for (int i = 0; i < 6; ++i)
    {
        int3 faceCoord = imageCoords + int3(u_clipmapResolutionWithBorder * i, 0, 0);
        image[faceCoord] = color;
    }
}

// Store voxel color - Atomic operation average (with weight and face index)
void storeVoxelColorAtomicRGBA8Avg(RWTexture3D<uint> image, float3 posW, float4 color, int3 faceIndices, float3 weight)
{
    int3 imageCoords = computeImageCoords(posW);

    // Process three faces, each with different weight
    int3 faceCoordX = imageCoords + int3(faceIndices.x * u_clipmapResolutionWithBorder, 0, 0);
    imageAtomicRGBA8Avg(image, faceCoordX, float4(color.rgb * weight.x, 1.0f));

    int3 faceCoordY = imageCoords + int3(faceIndices.y * u_clipmapResolutionWithBorder, 0, 0);
    imageAtomicRGBA8Avg(image, faceCoordY, float4(color.rgb * weight.y, 1.0f));

    int3 faceCoordZ = imageCoords + int3(faceIndices.z * u_clipmapResolutionWithBorder, 0, 0);
    imageAtomicRGBA8Avg(image, faceCoordZ, float4(color.rgb * weight.z, 1.0f));
}

#endif // VOXELIZATION_HLSL
