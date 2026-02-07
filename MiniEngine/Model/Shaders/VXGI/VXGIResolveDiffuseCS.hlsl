#include "VXGIGlobal.hlsli"
#include "VXGIPostprocess.hlsli"
#include "VoxelConeTracing.hlsli"
#include "VXGIRenderer.hlsli"

ConstantBuffer<PostProcess> postprocess : register(b999);

CONSTANTBUFFER(g_xVoxelizer, VoxelizerCB, CBSLOT_RENDERER_VOXELIZER);
CONSTANTBUFFER(g_xFrame, FrameCB, CBSLOT_RENDERER_FRAME);
CONSTANTBUFFER(g_xCamera, CameraCB, CBSLOT_RENDERER_CAMERA);

Texture2D<float> texture_depth : register(t10);
Texture2D<float2> texture_normal : register(t11);

Texture3D<float4> texture_radiance : register(t15);
Texture3D<float> texture_sdf : register(t16);

RWTexture2D<float4> output : register(u0);

float4x4 getInverseViewProj()
{
    return g_xCamera.cameras[0].inverse_view_projection;
}

[RootSignature(Voxel_RootSig)]
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

    float4 trace = ConeTraceDiffuse(g_xFrame, texture_radiance, texture_sdf, P, N);
	float4 color = float4(trace.rgb, 1);
	//color.rgb += GetAmbient(N) * (1 - trace.a);
	
	output[pixel] = color;
}
