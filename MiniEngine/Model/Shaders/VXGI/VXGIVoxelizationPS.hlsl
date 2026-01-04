#include "../Common.hlsli"
#include "VXGIRenderer.hlsli"

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

Texture2D<float3> texBaseColor : register(t0);
Texture2D<float3> texSpecular : register(t1);
//Texture2D<float4> texEmissive		: register(t2);
Texture2D<float3> texNormal : register(t3);
//Texture2D<float4> texLightmap		: register(t4);
//Texture2D<float4> texReflection	: register(t5);
Texture2D<float> texSSAO : register(t12);
Texture2D<float> texShadow : register(t13);
Texture3D<float4> input_previous_radiance : register(t14);

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

RWTexture3D<uint> output_atomic : register(u0);

void VoxelAtomicAverage(inout RWTexture3D<uint> output, in uint3 dest, in float4 color)
{
    float4 addingColor = float4(color.rgb, 1);
    uint newValue = PackVoxelColor(float4(addingColor.rgb, 1.0 / MAX_VOXEL_ALPHA));
    uint expectedValue = 0;
    uint actualValue;

    InterlockedCompareExchange(output[dest], expectedValue, newValue, actualValue);
    while (actualValue != expectedValue)
    {
        expectedValue = actualValue;

        color = UnpackVoxelColor(actualValue);
        color.a *= MAX_VOXEL_ALPHA;

        color.rgb *= color.a;

        color += addingColor;

        color.rgb /= color.a;

        color.a /= MAX_VOXEL_ALPHA;
        newValue = PackVoxelColor(color);

        InterlockedCompareExchange(output[dest], expectedValue, newValue, actualValue);
    }
}

// Note: centroid interpolation is used to avoid floating voxels in some cases
struct PSInput
{
    float4 pos : SV_POSITION;
    centroid float2 uv : TexCoord0;
    centroid float3 N : NORMAL;
    centroid float3 P : POSITION3D;

#ifdef VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED
	nointerpolation float3 aabb_min : AABB_MIN;
	nointerpolation float3 aabb_max : AABB_MAX;
#endif // VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED
	
    inline float2 GetUVSet()
    {
        float2 ret = uv;
        //ret.xy = mad(ret.xy, GetMaterial().texMulAdd.xy, GetMaterial().texMulAdd.zw);
        return ret;
    }
};

void main(PSInput input)
{
    float2 uvset = input.uv;
    float3 P = input.P;

    VoxelClipMap clipmap = g_xFrameVoxel.vxgi.clipmaps[g_xVoxelizer.clipmap_index];
    float3 uvw = g_xFrameVoxel.vxgi.world_to_clipmap(P, clipmap);
    if (!is_saturated(uvw))
        return;

#ifdef VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED
	uint3 clipmap_pixel = uvw * g_xFrameVoxel.vxgi.resolution;
	float3 clipmap_uvw_center = (clipmap_pixel + 0.5) * g_xFrameVoxel.vxgi.resolution_rcp;
	float3 voxel_center = g_xFrameVoxel.vxgi.clipmap_to_world(clipmap_uvw_center, clipmap);
	AABB voxel_aabb;
	voxel_aabb.c = voxel_center;
	voxel_aabb.e = clipmap.voxelSize;
	AABB triangle_aabb;
	AABBfromMinMax(triangle_aabb, input.aabb_min, input.aabb_max);
	if (!IntersectAABB(voxel_aabb, triangle_aabb))
		return;
#endif // VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED

    float4 baseColor = float4(1, 1, 1, 1);
	[branch]
    if (material.textures[BASECOLORMAP].IsValid() && (g_xFrameVoxel.options & OPTION_BIT_DISABLE_ALBEDO_MAPS) == 0)
    {
        float lod_bias = 0;
        if (material.IsTransparent() || material.GetAlphaTest() > 0)
        {
			// If material is non opaque, then we apply bias to avoid sampling such a low
			//	mip level in which alpha is completely gone (helps with trees)
            lod_bias = -10;
        }
        baseColor *= material.textures[BASECOLORMAP].SampleBias(sampler_linear_wrap, uvsets, lod_bias);
    }

    float3 emissiveColor = material.GetEmissive();
	[branch]
    if (any(emissiveColor) && material.textures[EMISSIVEMAP].IsValid())
    {
        float4 emissiveMap = material.textures[EMISSIVEMAP].Sample(sampler_linear_wrap, uvsets);
        emissiveColor *= emissiveMap.rgb * emissiveMap.a;
    }

    float3 N = normalize(input.N);

    Lighting lighting;
    lighting.create(0, 0, 0, 0);

    Surface surface;
    surface.init();
    surface.P = P;
    surface.N = N;
    surface.create(material, baseColor, 0, 0);
    surface.roughness = material.GetRoughness();
    surface.sss = material.GetSSS();
    surface.sss_inv = material.GetSSSInverse();
    surface.layerMask = material.layerMask;
    surface.update();

    ForwardLighting(surface, lighting);

	// output:
    uint3 writecoord = floor(uvw * g_xFrameVoxel.vxgi.resolution);
    writecoord.z *= VOXELIZATION_CHANNEL_COUNT; // de-interleaved channels

    float3 aniso_direction = N;

#if 0
	// This voxelization is faster but less accurate:
	uint face_offset = cubemap_to_uv(aniso_direction).z * g_xFrameVoxel.vxgi.resolution;
	float4 baseColor_direction = baseColor;
	float3 emissive_direction = emissiveColor;
	float3 directLight_direction = lighting.direct.diffuse;
	float2 normal_direction = encode_oct(N) * 0.5 + 0.5;
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_BASECOLOR_R)], PackVoxelChannel(baseColor_direction.r));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_BASECOLOR_G)], PackVoxelChannel(baseColor_direction.g));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_BASECOLOR_B)], PackVoxelChannel(baseColor_direction.b));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_BASECOLOR_A)], PackVoxelChannel(baseColor_direction.a));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_EMISSIVE_R)], PackVoxelChannel(emissive_direction.r));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_EMISSIVE_G)], PackVoxelChannel(emissive_direction.g));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_EMISSIVE_B)], PackVoxelChannel(emissive_direction.b));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_R)], PackVoxelChannel(directLight_direction.r));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_G)], PackVoxelChannel(directLight_direction.g));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_B)], PackVoxelChannel(directLight_direction.b));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_NORMAL_R)], PackVoxelChannel(normal_direction.r));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_NORMAL_G)], PackVoxelChannel(normal_direction.g));
	InterlockedAdd(output_atomic[writecoord + uint3(face_offset, 0, VOXELIZATION_CHANNEL_FRAGMENT_COUNTER)], 1);

#else
	// This is slower but more accurate voxelization, by weighted voxel writes into multiple directions:
    float3 face_offsets = float3(
		aniso_direction.x > 0 ? 0 : 1,
		aniso_direction.y > 0 ? 2 : 3,
		aniso_direction.z > 0 ? 4 : 5
		) * g_xFrameVoxel.vxgi.resolution;
    float3 direction_weights = abs(N);

    if (direction_weights.x > 0)
    {
        float4 baseColor_direction = baseColor * direction_weights.x;
        float3 emissive_direction = emissiveColor * direction_weights.x;
        float3 directLight_direction = lighting.direct.diffuse * direction_weights.x;
        float2 normal_direction = encode_oct(N * direction_weights.x) * 0.5 + 0.5;
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_BASECOLOR_R)], PackVoxelChannel(baseColor_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_BASECOLOR_G)], PackVoxelChannel(baseColor_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_BASECOLOR_B)], PackVoxelChannel(baseColor_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_BASECOLOR_A)], PackVoxelChannel(baseColor_direction.a));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_EMISSIVE_R)], PackVoxelChannel(emissive_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_EMISSIVE_G)], PackVoxelChannel(emissive_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_EMISSIVE_B)], PackVoxelChannel(emissive_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_R)], PackVoxelChannel(directLight_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_G)], PackVoxelChannel(directLight_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_B)], PackVoxelChannel(directLight_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_NORMAL_R)], PackVoxelChannel(normal_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_NORMAL_G)], PackVoxelChannel(normal_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.x, 0, VOXELIZATION_CHANNEL_FRAGMENT_COUNTER)], 1);
    }
    if (direction_weights.y > 0)
    {
        float4 baseColor_direction = baseColor * direction_weights.y;
        float3 emissive_direction = emissiveColor * direction_weights.y;
        float3 directLight_direction = lighting.direct.diffuse * direction_weights.y;
        float2 normal_direction = encode_oct(N * direction_weights.y) * 0.5 + 0.5;
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_BASECOLOR_R)], PackVoxelChannel(baseColor_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_BASECOLOR_G)], PackVoxelChannel(baseColor_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_BASECOLOR_B)], PackVoxelChannel(baseColor_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_BASECOLOR_A)], PackVoxelChannel(baseColor_direction.a));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_EMISSIVE_R)], PackVoxelChannel(emissive_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_EMISSIVE_G)], PackVoxelChannel(emissive_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_EMISSIVE_B)], PackVoxelChannel(emissive_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_R)], PackVoxelChannel(directLight_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_G)], PackVoxelChannel(directLight_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_B)], PackVoxelChannel(directLight_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_NORMAL_R)], PackVoxelChannel(normal_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_NORMAL_G)], PackVoxelChannel(normal_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.y, 0, VOXELIZATION_CHANNEL_FRAGMENT_COUNTER)], 1);
    }
    if (direction_weights.z > 0)
    {
        float4 baseColor_direction = baseColor * direction_weights.z;
        float3 emissive_direction = emissiveColor * direction_weights.z;
        float3 directLight_direction = lighting.direct.diffuse * direction_weights.z;
        float2 normal_direction = encode_oct(N * direction_weights.z) * 0.5 + 0.5;
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_BASECOLOR_R)], PackVoxelChannel(baseColor_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_BASECOLOR_G)], PackVoxelChannel(baseColor_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_BASECOLOR_B)], PackVoxelChannel(baseColor_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_BASECOLOR_A)], PackVoxelChannel(baseColor_direction.a));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_EMISSIVE_R)], PackVoxelChannel(emissive_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_EMISSIVE_G)], PackVoxelChannel(emissive_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_EMISSIVE_B)], PackVoxelChannel(emissive_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_R)], PackVoxelChannel(directLight_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_G)], PackVoxelChannel(directLight_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_B)], PackVoxelChannel(directLight_direction.b));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_NORMAL_R)], PackVoxelChannel(normal_direction.r));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_NORMAL_G)], PackVoxelChannel(normal_direction.g));
        InterlockedAdd(output_atomic[writecoord + uint3(face_offsets.z, 0, VOXELIZATION_CHANNEL_FRAGMENT_COUNTER)], 1);
    }
#endif


//#if 0
//	uint face_offset = cubemap_to_uv(aniso_direction).z * g_xFrameVoxel.vxgi.resolution;
//	VoxelAtomicAverage(output_radiance, writecoord + uint3(face_offset, 0, 0), color);
//	VoxelAtomicAverage(output_opacity, writecoord + uint3(face_offset, 0, 0), color.aaaa);
//#else
//	float3 face_offsets = float3(
//		aniso_direction.x > 0 ? 0 : 1,
//		aniso_direction.y > 0 ? 2 : 3,
//		aniso_direction.z > 0 ? 4 : 5
//		) * g_xFrameVoxel.vxgi.resolution;
//	float3 direction_weights = abs(N);
//	VoxelAtomicAverage(output_radiance, writecoord + uint3(face_offsets.x, 0, 0), color * direction_weights.x);
//	VoxelAtomicAverage(output_radiance, writecoord + uint3(face_offsets.y, 0, 0), color * direction_weights.y);
//	VoxelAtomicAverage(output_radiance, writecoord + uint3(face_offsets.z, 0, 0), color * direction_weights.z);
//	VoxelAtomicAverage(output_opacity, writecoord + uint3(face_offsets.x, 0, 0), color.aaaa * direction_weights.x);
//	VoxelAtomicAverage(output_opacity, writecoord + uint3(face_offsets.y, 0, 0), color.aaaa * direction_weights.y);
//	VoxelAtomicAverage(output_opacity, writecoord + uint3(face_offsets.z, 0, 0), color.aaaa * direction_weights.z);
//#endif

	//bool done = false;
	//while (!done)
	//{
	//	// acquire lock:
	//	uint locked;
	//	InterlockedCompareExchange(lock[writecoord], 0, 1, locked);
	//	if (locked == 0)
	//	{
	//		float4 average = output_albedo[writecoord];
	//		float3 average_normal = output_normal[writecoord];

	//		average.a += 1;
	//		average.rgb += color.rgb;
	//		average_normal.rgb += N * 0.5 + 0.5;

	//		output_albedo[writecoord] = average;
	//		output_normal[writecoord] = average_normal;

	//		InterlockedExchange(lock[writecoord], 0, locked);
	//		done = true;
	//	}
	//}
}








/*
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
*/