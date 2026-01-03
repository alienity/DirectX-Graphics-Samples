#include "Common.hlsli"
#include "Lighting.hlsli"

#define Voxelize_RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(Sampler(s0, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(SRV(t10, numDescriptors = 10), visibility = SHADER_VISIBILITY_PIXEL)," \
    "DescriptorTable(UAV(u0, numDescriptors = 2), visibility = SHADER_VISIBILITY_PIXEL)," \
    "CBV(b1), " \
    "SRV(t20, visibility = SHADER_VISIBILITY_VERTEX), " \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "comparisonFunc = COMPARISON_GREATER_EQUAL," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)"

Texture2D<float3> texDiffuse : register(t0);
Texture2D<float3> texSpecular : register(t1);
//Texture2D<float4> texEmissive		: register(t2);
Texture2D<float3> texNormal : register(t3);
//Texture2D<float4> texLightmap		: register(t4);
//Texture2D<float4> texReflection	: register(t5);
Texture2D<float> texSSAO : register(t12);
Texture2D<float> texShadow : register(t13);

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProj;
    float4x4 SunShadowMatrix;
    float3 ViewerPos;
    float3 ViewerRight; // Right vector of orthographic camera
    float3 ViewerUp; // Up vector of orthographic camera
    float3 ViewerForward; // Forward vector of orthographic camera
    float OrthoWidth; // Width of orthographic view
    float OrthoHeight; // Height of orthographic view
    float OrthoNear; // Near plane of orthographic view
    float OrthoFar; // Far plane of orthographic view
    float _pad;
}

RWTexture3D<float4> rwVoxelColorTexture : register(u0);
RWTexture3D<float4> rwVoxelNormalTexture : register(u1);

struct VSOutput
{
    sample float4 position : SV_Position;
    sample float3 worldPos : WorldPos;
    sample float2 uv : TexCoord0;
    sample float3 viewDir : TexCoord1;
    sample float3 shadowCoord : TexCoord2;
    sample float3 normal : Normal;
    sample float3 tangent : Tangent;
    sample float3 bitangent : Bitangent;
};

float3 scaleAndBias(float3 p)
{
    return 0.5f * p + float3(0.5f, 0.5f, 0.5f);
}

// Check if point is inside orthographic view volume
bool isInsideOrthoVolume(float3 p)
{
    // Calculate position relative to camera
    float3 relPos = p - ViewerPos;
    float rightProj = dot(relPos, ViewerRight);
    float upProj = dot(relPos, ViewerUp);
    float forwardProj = dot(relPos, ViewerForward);
    
    // Check if point is within orthographic bounds
    return abs(rightProj) <= OrthoWidth * 0.5f &&
           abs(upProj) <= OrthoHeight * 0.5f &&
           forwardProj >= OrthoNear && forwardProj <= OrthoFar;
}

[RootSignature(Voxelize_RootSig)]
void main(VSOutput vsOutput)
{
    if (!isInsideOrthoVolume(vsOutput.position))
        return;

    uint2 pixelPos = uint2(vsOutput.position.xy);
#define SAMPLE_TEX(texName) texName.Sample(defaultSampler, vsOutput.uv)

    float3 diffuseAlbedo = SAMPLE_TEX(texDiffuse);
    float3 colorSum = 0;
    {
        float ao = texSSAO[pixelPos];
        colorSum += ApplyAmbientLight(diffuseAlbedo, ao, AmbientColor);
    }

    float gloss = 128.0;
    float3 normal;
    {
        normal = SAMPLE_TEX(texNormal) * 2.0 - 1.0;
        AntiAliasSpecular(normal, gloss);
        float3x3 tbn = float3x3(normalize(vsOutput.tangent), normalize(vsOutput.bitangent), normalize(vsOutput.normal));
        normal = normalize(mul(normal, tbn));
    }

    float3 specularAlbedo = float3(0.56, 0.56, 0.56);
    float specularMask = SAMPLE_TEX(texSpecular).g;
    float3 viewDir = normalize(vsOutput.viewDir);
    colorSum += ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, gloss, normal, viewDir, SunDirection, SunColor, vsOutput.shadowCoord, texShadow);

    ShadeLights(colorSum, pixelPos,
		diffuseAlbedo,
		specularAlbedo,
		specularMask,
		gloss,
		normal,
		viewDir,
		vsOutput.worldPos
		);

    
    // Output lighting to 3D texture.
    float3 voxel = scaleAndBias(vsOutput.position);
    uint3 dim;
    rwVoxelColorTexture.GetDimensions(dim.x, dim.y, dim.z);
    int3 voxelCoord = (int3) (float3(dim) * voxel);
    
    // Clamp coordinates to texture bounds
    voxelCoord = clamp(voxelCoord, int3(0, 0, 0), dim - 1);
    
    // Write to voxel texture
    rwVoxelColorTexture[voxelCoord] = float4(colorSum, 1);
    rwVoxelNormalTexture[voxelCoord] = float4(normal * 0.5f + 0.5f, 0);
}
