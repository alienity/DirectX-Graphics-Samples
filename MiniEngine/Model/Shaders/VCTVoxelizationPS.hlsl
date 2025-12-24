#define _RootSig \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 2))," \
    "DescriptorTable(Sampler(s0, numDescriptors = 2))," \
	"CBV(b1), " \
    "DescriptorTable(UAV(u0, numDescriptors = 2))"

cbuffer MaterialConstants : register(b0)
{
    float4 baseColorFactor;
    float normalTextureScale;
    float3 _pad0;
}

Texture2D<float4> baseColorTexture : register(t0);
Texture2D<float3> normalTexture : register(t1);

SamplerState baseColorSampler : register(s0);
SamplerState normalSampler : register(s1);

cbuffer GlobalConstants : register(b1)
{
    float3 ViewerPos;
    float VoxelSize; // = 20 / 128
    float3 VoxelWorldMin; // [-10, -10, -10]
    uint VoxelRes; // = 128
}

RWTexture3D<float4> voxelColorVolume : register(u0);
RWTexture3D<float4> voxelNormalVolume : register(u1);

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 worldPos : WORLD_POS;
    float2 uv : TEXCOORD0;
};

int3 WorldToVoxelIndex(float3 w)
{
    return int3((w - VoxelWorldMin) / VoxelSize);
}

bool IsValid(int3 idx)
{
    return all(idx >= 0) && all(idx < int(VoxelRes));
}

[RootSignature(_RootSig)]
void main(PSInput input)
{
    int3 idx = WorldToVoxelIndex(input.worldPos);
    if (!IsValid(idx))
        return;

    float dist = distance(input.worldPos, ViewerPos);

    float4 current = voxelColorVolume[idx];

    if (current.a == 0.0f || dist < current.a)
    {
		float3 texAlbedo = baseColorTexture.Sample(baseColorSampler, input.uv).rgb;
        float3 texNormal = normalTexture.Sample(normalSampler, input.uv).rgb;

        voxelColorVolume[idx] = float4(texAlbedo.rgb, dist);
		voxelNormalVolume[idx] = float4(texNormal.rgb, 1.0f);
    }
}
