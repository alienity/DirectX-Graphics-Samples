#ifndef CONVERSION_HLSL
#define CONVERSION_HLSL

float4 convertRGBA8ToVec4(uint val)
{
    return float4(float((val & 0x000000FF)),
                  float((val & 0x0000FF00) >> 8U),
                  float((val & 0x00FF0000) >> 16U),
                  float((val & 0xFF000000) >> 24U));
}

uint convertVec4ToRGBA8(float4 val)
{
    return (uint(val.w) & 0x000000FF) << 24U |
        (uint(val.z) & 0x000000FF) << 16U |
        (uint(val.y) & 0x000000FF) << 8U |
        (uint(val.x) & 0x000000FF);
}

float3 transformPosWToClipUVW(float3 posW, float maxExtent)
{
    return frac(posW / maxExtent);
}

float3 transformPosWToClipUVW(float3 posW, float3 extent)
{
    return frac(posW / extent);
}

float2 transformPosWToClipUV(float2 posW, float maxExtent)
{
    return frac(posW / maxExtent);
}

float2 transformPosWToClipUV(float2 posW, float2 extent)
{
    return frac(posW / extent);
}

float packShininess(float shininess)
{
    return shininess / 255.0; // assumed 255.0 to be the max possible shininess value
}

float unpackShininess(float shininess)
{
    return shininess * 255.0;
}

float shininessToRoughness(float shininess)
{
    // The conversion from the specular exponent (shininess) to roughness is just a (subjective) approximation
    return sqrt(2.0 / (shininess + 2.0));
}

float3 unpackNormal(float3 packedNormal)
{
    return packedNormal * 2.0 - 1.0;
}

float3 packNormal(float3 normal)
{
    return normal * 0.5 + 0.5;
}

#endif
