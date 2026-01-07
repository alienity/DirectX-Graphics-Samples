#include "../Common.hlsli"
#include "../Lighting.hlsli"
#include "VXGIRenderer.hlsli"

struct InstanceCB
{
    float4x4 modelToShadow;
    float3 viewerPos;
    int _pad0;
};

CONSTANTBUFFER(g_xInstance, InstanceCB, CBSLOT_RENDERER_INSTANCE);

Texture2D<float3> texBaseColor : register(t0);
Texture2D<float3> texSpecular : register(t1);
Texture2D<float4> texEmissive : register(t2);
Texture2D<float3> texNormal : register(t3);
//Texture2D<float4> texLightmap		: register(t4);
//Texture2D<float4> texReflection	: register(t5);
Texture2D<float> texSSAO : register(t12);
Texture2D<float> texShadow : register(t13);
Texture3D<float4> input_previous_radiance : register(t14);

RWTexture3D<uint> output_atomic : register(u0);

float4x4 getModelToShadow()
{
    return g_xInstance.modelToShadow;
}

float3 getViewerPos()
{
    return g_xInstance.viewerPos;
}

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

[RootSignature(Voxel_RootSig)]
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

	float3 viewDir = normalize(P - getViewerPos());
	float3 shadowCoord = mul(getModelToShadow(), float4(P, 1.0)).xyz;
	
    float4 baseColor = float4(1, 1, 1, 1);
    float lod_bias = 0;
    baseColor *= float4(texBaseColor.SampleBias(defaultSampler, uvset, lod_bias).rgb, 1.0f);

    float3 emissiveColor = float3(0, 0, 0);
    float4 emissiveMap = texEmissive.Sample(defaultSampler, uvset);
    emissiveColor *= emissiveMap.rgb * emissiveMap.a;

    float3 N = normalize(input.N);

    uint2 pixelPos = uint2(input.pos.xy);
    float3 diffuseAlbedo = texBaseColor.Sample(defaultSampler, uvset);
    float3 colorSum = 0;
    {
        float ao = texSSAO[pixelPos];
        colorSum += ApplyAmbientLight(diffuseAlbedo, ao, AmbientColor);
    }

    float gloss = 128.0;
    float3 specularAlbedo = float3(0.56, 0.56, 0.56);
    float specularMask = texSpecular.Sample(defaultSampler, uvset).g;
    colorSum += ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, gloss, N, viewDir, SunDirection, SunColor, shadowCoord, texShadow);

    ShadeLights(colorSum, pixelPos,
		diffuseAlbedo,
		specularAlbedo,
		specularMask,
		gloss,
		N,
		viewDir,
		P
		);

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
        //float3 directLight_direction = lighting.direct.diffuse * direction_weights.x;
        float3 directLight_direction = colorSum * direction_weights.x;
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
        //float3 directLight_direction = lighting.direct.diffuse * direction_weights.y;
        float3 directLight_direction = colorSum * direction_weights.y;
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
        //float3 directLight_direction = lighting.direct.diffuse * direction_weights.z;
        float3 directLight_direction = colorSum * direction_weights.z;
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

}
