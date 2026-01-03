#define _RootSig \
    "RootFlags(0), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 10))," \
    "DescriptorTable(UAV(u0, numDescriptors = 10))," \
    "StaticSampler(s10, maxAnisotropy = 8)," \
    "StaticSampler(s11," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "comparisonFunc = COMPARISON_GREATER_EQUAL," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)," \
    "StaticSampler(s12, maxAnisotropy = 8)"

cbuffer MipConstants : register(b0)
{
    uint srcLevel;
}

Texture3D<float4> srcVolume : register(t0);
RWTexture3D<float4> dstVolume : register(u0);

[RootSignature(_RootSig)]
[numthreads(8, 8, 8)]
void main(uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
    float4 sum = 0;
    for (int z = 0; z < 2; ++z)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                uint3 srcIdx = dispatchThreadId * 2 + uint3(x, y, z);
                sum += srcVolume[srcIdx];
            }
        }
    }
    dstVolume[dispatchThreadId] = sum / 8.0f;
}