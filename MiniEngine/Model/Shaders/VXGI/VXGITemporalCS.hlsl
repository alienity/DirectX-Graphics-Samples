#include "../Common.hlsli"
#include "VXGIRenderer.hlsli"
#include "VoxelConeTracing.hlsli"

#define Temporal_RootSig \
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


Texture3D<half4> input_previous_radiance : register(t0);
Texture3D<uint> input_render_atomic : register(t1);

RWTexture3D<float4> output_radiance : register(u0);
RWTexture3D<float> output_sdf : register(u1);

static const float blend_speed = 0.5;

[RootSignature(Temporal_RootSig)]
[numthreads(8, 8, 8)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float4 aniso_colors[6];

	VoxelClipMap clipmap = g_xFrameVoxel.vxgi.clipmaps[g_xVoxelizer.clipmap_index];
	float sdf = clipmap.voxelSize * 2 * g_xFrameVoxel.vxgi.resolution;

	for (uint i = 0; i < 6 + DIFFUSE_CONE_COUNT; ++i)
	{
		uint3 src = DTid;
		src.x += i * g_xFrameVoxel.vxgi.resolution;

		uint3 dst = src;
		dst.y += g_xVoxelizer.clipmap_index * g_xFrameVoxel.vxgi.resolution;

		half4 radiance = 0;
		if (i < 6)
		{
			src.z *= VOXELIZATION_CHANNEL_COUNT;
			uint count = input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_FRAGMENT_COUNTER)];
			if (count > 0)
			{
				half4 baseColor = 0;
				half3 emissive = 0;
				half3 directLight = 0;
				half3 N = 0;
				baseColor.r = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_BASECOLOR_R)]);
				baseColor.g = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_BASECOLOR_G)]);
				baseColor.b = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_BASECOLOR_B)]);
				baseColor.a = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_BASECOLOR_A)]);
				baseColor /= count;
				emissive.r = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_EMISSIVE_R)]);
				emissive.g = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_EMISSIVE_G)]);
				emissive.b = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_EMISSIVE_B)]);
				emissive /= count;
				directLight.r = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_R)]);
				directLight.g = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_G)]);
				directLight.b = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_DIRECTLIGHT_B)]);
				directLight /= count;
				N.r = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_NORMAL_R)]);
				N.g = UnpackVoxelChannel(input_render_atomic[src + uint3(0, 0, VOXELIZATION_CHANNEL_NORMAL_G)]);
				N /= count;
				N = decode_oct(N.rg * 2 - 1);

				radiance = baseColor;

				// Voxel indirect lighting:
				float3 P = g_xFrameVoxel.vxgi.clipmap_to_world((DTid + 0.5) * g_xFrameVoxel.vxgi.resolution_rcp, clipmap);
				// Lighting lighting;
				// lighting.create(0, 0, 0, 0);
				// lighting.direct.diffuse = directLight;
				// half4 trace = ConeTraceDiffuse(input_previous_radiance, P, N);
				// lighting.indirect.diffuse = trace.rgb;
				// // lighting.indirect.diffuse += GetAmbient(N) * (1 - trace.a);
				// radiance.rgb *= lighting.direct.diffuse / PI + lighting.indirect.diffuse;
				half3 directdiffuse = directLight;
				half4 trace = ConeTraceDiffuse(input_previous_radiance, P, N);
				half3 indirectdiffuse = trace.rgb;
				radiance.rgb *= directdiffuse / PI + indirectdiffuse;
				
				radiance.rgb += emissive;
			}

			if (radiance.a > 0)
			{
				if (any(g_xVoxelizer.offsetfromPrevFrame))
				{
					int3 coord = dst - g_xVoxelizer.offsetfromPrevFrame;
					int aniso_face_start_x = i * g_xFrameVoxel.vxgi.resolution;
					int aniso_face_end_x = aniso_face_start_x + g_xFrameVoxel.vxgi.resolution;
					int clipmap_face_start_y = g_xVoxelizer.clipmap_index * g_xFrameVoxel.vxgi.resolution;
					int clipmap_face_end_y = clipmap_face_start_y + g_xFrameVoxel.vxgi.resolution;

					if (
						coord.x >= aniso_face_start_x && coord.x < aniso_face_end_x &&
						coord.y >= clipmap_face_start_y && coord.y < clipmap_face_end_y &&
						coord.z >= 0 && coord.z < g_xFrameVoxel.vxgi.resolution
						)
					{
						radiance = lerp(input_previous_radiance[dst], radiance, blend_speed);
					}
				}
				else
				{
					radiance = lerp(input_previous_radiance[dst], radiance, blend_speed);
				}
			}
			else
			{
				radiance = 0;
			}
			aniso_colors[i] = radiance;

			if (radiance.a > 0)
			{
				sdf = 0;
			}
		}
		else
		{
			// precompute cone sampling:
			float3 coneDirection = DIFFUSE_CONE_DIRECTIONS[i - 6];
			float3 aniso_direction = -coneDirection;
			uint3 face_offsets = float3(
				aniso_direction.x > 0 ? 0 : 1,
				aniso_direction.y > 0 ? 2 : 3,
				aniso_direction.z > 0 ? 4 : 5
			);
			float3 direction_weights = abs(coneDirection);
			float4 sam =
				aniso_colors[face_offsets.x] * direction_weights.x +
				aniso_colors[face_offsets.y] * direction_weights.y +
				aniso_colors[face_offsets.z] * direction_weights.z
				;
			radiance = sam;
		}

		output_radiance[dst] = radiance;
	}

	uint3 dst_sdf = DTid;
	dst_sdf.y += g_xVoxelizer.clipmap_index * g_xFrameVoxel.vxgi.resolution;
	output_sdf[dst_sdf] = sdf;
}
