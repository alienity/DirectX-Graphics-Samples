#include "../Common.hlsli"
#include "VXGIRenderer.hlsli"

#define Offsetprev_RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b1, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b2, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b3, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL)," \
	"DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
	    "comparisonFunc = COMPARISON_GREATER_EQUAL," \
	    "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)"


Texture3D<float4> input_previous_radiance : register(t0);
RWTexture3D<float4> output_radiance : register(u0);

[RootSignature(Offsetprev_RootSig)]
[numthreads(8, 8, 8)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    DTid.y += g_xVoxelizer.clipmap_index * g_xFrame.vxgi.resolution;

	for (uint i = 0; i < 6 + DIFFUSE_CONE_COUNT; ++i)
	{
		uint3 dst = DTid;
        dst.x += i * g_xFrame.vxgi.resolution;

		float4 radiance_prev = 0;

		if (any(g_xVoxelizer.offsetfromPrevFrame))
		{
			int3 coord = dst - g_xVoxelizer.offsetfromPrevFrame;
            int aniso_face_start_x = i * g_xFrame.vxgi.resolution;
            int aniso_face_end_x = aniso_face_start_x + g_xFrame.vxgi.resolution;
            int clipmap_face_start_y = g_xVoxelizer.clipmap_index * g_xFrame.vxgi.resolution;
            int clipmap_face_end_y = clipmap_face_start_y + g_xFrame.vxgi.resolution;

			if (
				coord.x >= aniso_face_start_x && coord.x < aniso_face_end_x &&
				coord.y >= clipmap_face_start_y && coord.y < clipmap_face_end_y &&
				coord.z >= 0 && coord.z < g_xFrame.vxgi.resolution
				)
			{
				radiance_prev = input_previous_radiance[coord];
			}
			else
			{
				radiance_prev = 0;
			}
		}
		else
		{
			radiance_prev = input_previous_radiance[dst];
		}

		output_radiance[dst] = radiance_prev;
	}
}
