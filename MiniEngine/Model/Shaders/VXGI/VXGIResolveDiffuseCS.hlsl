#include "VXGIGlobal.hlsli"
#include "VXGIPostprocess.hlsli"
#include "VoxelConeTracing.hlsli"
#include "VXGIRenderer.hlsli"

#define Resolve_RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
	"RootConstants(num32BitConstants=12, b999), " \
	"CBV(b0, space = 0, visibility = SHADER_VISIBILITY_PIXEL), " \
	"CBV(b1, space = 0, visibility = SHADER_VISIBILITY_PIXEL), " \
	"CBV(b0, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
	"CBV(b1, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
	"CBV(b2, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "DescriptorTable(CBV(b3, space = 1, numDescriptors = 10), visibility = SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(SRV(t0, numDescriptors = 20), visibility = SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(UAV(u0, numDescriptors = 10), visibility = SHADER_VISIBILITY_ALL)," \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, comparisonFunc = COMPARISON_GREATER_EQUAL, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s100, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s101, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s102, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
    "StaticSampler(s103, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_POINT)," \
    "StaticSampler(s104, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_POINT)," \
    "StaticSampler(s105, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_POINT)," \
    "StaticSampler(s106, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
    "StaticSampler(s107, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
    "StaticSampler(s108, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
    "StaticSampler(s109, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc = COMPARISON_GREATER_EQUAL),"

float4x4 getInverseViewProj()
{
	return g_xCamera.cameras[0].inverse_view_projection;
}

ConstantBuffer<PostProcess> postprocess : register(b999);

Texture2D<float> texture_depth : register(t10);
Texture2D<float2> texture_normal : register(t11);

Texture3D<float4> texture_radiance : register(t15);
Texture3D<float> texture_sdf : register(t16);

RWTexture2D<float4> output : register(u0);

[RootSignature(Resolve_RootSig)]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	const uint2 pixel = DTid.xy;
	const float2 uv = ((float2)pixel + 0.5) * postprocess.resolution_rcp;

	const float depth = texture_depth.SampleLevel(sampler_point_clamp, uv, 0).r;
	if (depth == 0)
		return;
	
	const float3 N = decode_oct(texture_normal.SampleLevel(sampler_point_clamp, uv, 0).xy);
	const float3 P = reconstruct_position(uv, depth, getInverseViewProj());

    float4 trace = ConeTraceDiffuse(texture_radiance, texture_sdf, P, N);
	float4 color = float4(trace.rgb, 1);
	//color.rgb += GetAmbient(N) * (1 - trace.a);
	
	output[pixel] = color;
}
