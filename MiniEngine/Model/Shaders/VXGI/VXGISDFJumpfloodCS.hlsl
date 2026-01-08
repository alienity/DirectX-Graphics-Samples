#include "VXGIRenderer.hlsli"

#define JumpFlood_RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b1, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b2, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b3, space = 1, visibility = SHADER_VISIBILITY_ALL), " \
    "CBV(b999, space = 0, visibility = SHADER_VISIBILITY_ALL), " \
    "DescriptorTable(SRV(t0, numDescriptors = 10), visibility = SHADER_VISIBILITY_ALL)," \
	"DescriptorTable(UAV(u0, numDescriptors = 10), visibility = SHADER_VISIBILITY_ALL), " \
    "StaticSampler(s9, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_ALL)," \
    "StaticSampler(s10, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)," \
    "StaticSampler(s11, visibility = SHADER_VISIBILITY_PIXEL," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
	    "comparisonFunc = COMPARISON_GREATER_EQUAL," \
	    "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8, visibility = SHADER_VISIBILITY_PIXEL)"

Texture3D<float> input_sdf : register(t0);

RWTexture3D<float> output_sdf : register(u0);

struct Push
{
    float jump_size;
};
//PUSHCONSTANT(push, Push);
ConstantBuffer<Push> push : register(b999);

[RootSignature(JumpFlood_RootSig)]
[numthreads(8, 8, 8)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint clipmap_start = g_xVoxelizer.clipmap_index * g_xFrameVoxel.vxgi.resolution;
    uint clipmap_end = clipmap_start + g_xFrameVoxel.vxgi.resolution;
    DTid.y += clipmap_start;

    VoxelClipMap clipmap = g_xFrameVoxel.vxgi.clipmaps[g_xVoxelizer.clipmap_index];
    float voxelSize = clipmap.voxelSize;

    float best_distance = input_sdf[DTid];

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int z = -1; z <= 1; ++z)
            {
                int3 offset = int3(x, y, z) * push.jump_size;
                int3 pixel = DTid + offset;
                if (
					pixel.x >= 0 && pixel.x < g_xFrameVoxel.vxgi.resolution &&
					pixel.y >= clipmap_start && pixel.y < clipmap_end &&
					pixel.z >= 0 && pixel.z < g_xFrameVoxel.vxgi.resolution
					)
                {
                    float sdf = input_sdf[pixel];
                    float distance = sdf + length((float3) offset * voxelSize);

                    if (distance < best_distance)
                    {
                        best_distance = distance;
                    }
                }
            }
        }
    }

    output_sdf[DTid] = best_distance;
}
