#ifndef ATOMIC_OPERATIONS_HLSL
#define ATOMIC_OPERATIONS_HLSL

#include "VCTConversion.hlsli"

// Source: OpenGL Insights Chapter 22 "Octree-Based Sparse Voxelization Using the GPU Hardware Rasterizer" by Cyril Crassin and Simon Green
// Modified by adding max iterations to avoid freezes and Timeout Detection and Recovery (TDR)
void imageAtomicRGBA8Avg(RWTexture3D<uint> image, uint3 coords, float4 value)
{
    value.rgb *= 255.0; // optimize following calculations
    uint newVal = convertVec4ToRGBA8(value);
    uint prevStoredVal = 0;
    uint curStoredVal;
    int i = 0;
    const int maxIterations = 100;

    // Using atomic operations of RWTexture3D
    while (i < maxIterations)
    {
        curStoredVal = image[coords];
        uint originalValue;
        InterlockedCompareExchange(image[coords], prevStoredVal, newVal, originalValue);

        if (originalValue == prevStoredVal)
        {
            // Successfully updated, exit the loop
            break;
        }
        else
        {
            // Value has been changed by another thread, update prevStoredVal and continue
            prevStoredVal = originalValue;
        }

        float4 rval = convertRGBA8ToVec4(curStoredVal);
        rval.rgb = (rval.rgb * rval.a); // Denormalize
        float4 curValF = rval + value; // Add
        curValF.rgb /= curValF.a; // Renormalize
        newVal = convertVec4ToRGBA8(curValF);
        ++i;
    }
}

#endif
