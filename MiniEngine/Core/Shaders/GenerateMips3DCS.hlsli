#include "CommonRS.hlsli"

#ifndef NON_POWER_OF_TWO
#define NON_POWER_OF_TWO 0
#endif

RWTexture3D<float4> OutMip1 : register(u0);
RWTexture3D<float4> OutMip2 : register(u1);
RWTexture3D<float4> OutMip3 : register(u2);
RWTexture3D<float4> OutMip4 : register(u3);
Texture3D<float4> SrcMip : register(t0);
SamplerState BilinearClamp : register(s0);

cbuffer CB0 : register(b0)
{
    float3 TexelSize; // 1.0 / OutMip1.Dimensions
    uint SrcMipLevel;	// Texture level of source mip
    uint NumMipLevels;	// Number of OutMips to write: [1, 4]
    float3 _Pad;
}

// The reason for separating channels is to reduce bank conflicts in the
// local data memory controller.  A large stride will cause more threads
// to collide on the same memory bank.
groupshared float gs_R[512];
groupshared float gs_G[512];
groupshared float gs_B[512];
groupshared float gs_A[512];

void StoreColor( uint Index, float4 Color )
{
    gs_R[Index] = Color.r;
    gs_G[Index] = Color.g;
    gs_B[Index] = Color.b;
    gs_A[Index] = Color.a;
}

float4 LoadColor( uint Index )
{
    return float4( gs_R[Index], gs_G[Index], gs_B[Index], gs_A[Index]);
}

float3 ApplySRGBCurve(float3 x)
{
    // This is exactly the sRGB curve
    //return select(x < 0.0031308, 12.92 * x, 1.055 * pow(abs(x), 1.0 / 2.4) - 0.055);
     
    // This is cheaper but nearly equivalent
    return select(x < 0.0031308, 12.92 * x, 1.13005 * sqrt(abs(x - 0.00228)) - 0.13448 * x + 0.005719);
}

float4 PackColor(float4 Linear)
{
#ifdef CONVERT_TO_SRGB
    return float4(ApplySRGBCurve(Linear.rgb), Linear.a);
#else
    return Linear;
#endif
}

[RootSignature(Common_RootSig)]
[numthreads( 8, 8, 8 )]
void main( uint GI : SV_GroupIndex, uint3 DTid : SV_DispatchThreadID )
{
    // One bilinear sample is insufficient when scaling down by more than 2x.
    // You will slightly undersample in the case where the source dimension
    // is odd.  This is why it's a really good idea to only generate mips on
    // power-of-two sized textures.  Trying to handle the undersampling case
    // will force this shader to be slower and more complicated as it will
    // have to take more source texture samples.
#if NON_POWER_OF_TWO == 0
    float3 UVW = TexelSize * (DTid.xyz + 0.5);
    float4 Src1 = SrcMip.SampleLevel(BilinearClamp, UVW, SrcMipLevel);
#elif NON_POWER_OF_TWO == 1
    // > 2:1 in X dimension
    // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    // horizontally.
    float3 UVW1 = TexelSize * (DTid.xyz + float3(0.25, 0.5, 0.5));
    float3 Off = TexelSize * float3(0.5, 0.0, 0.0);
    float4 Src1 = 0.5 * (SrcMip.SampleLevel(BilinearClamp, UVW1, SrcMipLevel) +
        SrcMip.SampleLevel(BilinearClamp, UVW1 + Off, SrcMipLevel));
#elif NON_POWER_OF_TWO == 2
    // > 2:1 in Y dimension
    // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    // vertically.
    float3 UVW1 = TexelSize * (DTid.xyz + float3(0.5, 0.25, 0.5));
    float3 Off = TexelSize * float3(0.0, 0.5, 0.0);
    float4 Src1 = 0.5 * (SrcMip.SampleLevel(BilinearClamp, UVW1, SrcMipLevel) +
        SrcMip.SampleLevel(BilinearClamp, UVW1 + Off, SrcMipLevel));
#elif NON_POWER_OF_TWO == 4
    // > 2:1 in Z dimension
    // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    // in depth direction.
    float3 UVW1 = TexelSize * (DTid.xyz + float3(0.5, 0.5, 0.25));
    float3 Off = TexelSize * float3(0.0, 0.0, 0.5);
    float4 Src1 = 0.5 * (SrcMip.SampleLevel(BilinearClamp, UVW1, SrcMipLevel) +
        SrcMip.SampleLevel(BilinearClamp, UVW1 + Off, SrcMipLevel));
#elif NON_POWER_OF_TWO == 3
    // > 2:1 in X and Y dimensions
    // Use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    // in both X and Y directions.
    float3 UVW1 = TexelSize * (DTid.xyz + float3(0.25, 0.25, 0.5));
    float3 OffX = TexelSize * float3(0.5, 0.0, 0.0);
    float3 OffY = TexelSize * float3(0.0, 0.5, 0.0);
    float3 OffXY = TexelSize * float3(0.5, 0.5, 0.0);
    float4 Src1 = SrcMip.SampleLevel(BilinearClamp, UVW1, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffX, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffY, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffXY, SrcMipLevel);
    Src1 *= 0.25;
#elif NON_POWER_OF_TWO == 5
    // > 2:1 in X and Z dimensions
    // Use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    // in both X and Z directions.
    float3 UVW1 = TexelSize * (DTid.xyz + float3(0.25, 0.5, 0.25));
    float3 OffX = TexelSize * float3(0.5, 0.0, 0.0);
    float3 OffZ = TexelSize * float3(0.0, 0.0, 0.5);
    float3 OffXZ = TexelSize * float3(0.5, 0.0, 0.5);
    float4 Src1 = SrcMip.SampleLevel(BilinearClamp, UVW1, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffX, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffZ, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffXZ, SrcMipLevel);
    Src1 *= 0.25;
#elif NON_POWER_OF_TWO == 6
    // > 2:1 in Y and Z dimensions
    // Use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    // in both Y and Z directions.
    float3 UVW1 = TexelSize * (DTid.xyz + float3(0.5, 0.25, 0.25));
    float3 OffY = TexelSize * float3(0.0, 0.5, 0.0);
    float3 OffZ = TexelSize * float3(0.0, 0.0, 0.5);
    float3 OffYZ = TexelSize * float3(0.0, 0.5, 0.5);
    float4 Src1 = SrcMip.SampleLevel(BilinearClamp, UVW1, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffY, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffZ, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + OffYZ, SrcMipLevel);
    Src1 *= 0.25;
#elif NON_POWER_OF_TWO == 7
    // > 2:1 in all dimensions (X, Y, and Z)
    // Use 8 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
    // in all directions.
    float3 UVW1 = TexelSize * (DTid.xyz + float3(0.25, 0.25, 0.25));
    float3 HalfTexel = TexelSize * 0.5;
    float4 Src1 = SrcMip.SampleLevel(BilinearClamp, UVW1, SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + float3(HalfTexel.x, 0.0, 0.0), SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + float3(0.0, HalfTexel.y, 0.0), SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + float3(0.0, 0.0, HalfTexel.z), SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + float3(HalfTexel.x, HalfTexel.y, 0.0), SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + float3(HalfTexel.x, 0.0, HalfTexel.z), SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + float3(0.0, HalfTexel.y, HalfTexel.z), SrcMipLevel);
    Src1 += SrcMip.SampleLevel(BilinearClamp, UVW1 + float3(HalfTexel.x, HalfTexel.y, HalfTexel.z), SrcMipLevel);
    Src1 *= 0.125;
#endif

    OutMip1[DTid.xyz] = PackColor(Src1);

    // A scalar (constant) branch can exit all threads coherently.
    if (NumMipLevels == 1)
        return;

    // Without lane swizzle operations, the only way to share data with other
    // threads is through LDS.
    StoreColor(GI, Src1);

    // This guarantees all LDS writes are complete and that all threads have
    // executed all instructions so far (and therefore have issued their LDS
    // write instructions.)
    GroupMemoryBarrierWithGroupSync();

    // With low three bits for X and middle three bits for Y and high three bits for Z, this bit mask
    // (binary: 001001001) checks that X and Y and Z are even.
    if ((GI & 0x49) == 0)
    {
        float4 Src2 = LoadColor(GI + 0x01);
        float4 Src3 = LoadColor(GI + 0x08);
        float4 Src4 = LoadColor(GI + 0x09);
        float4 Src5 = LoadColor(GI + 0x40);
        float4 Src6 = LoadColor(GI + 0x41);
        float4 Src7 = LoadColor(GI + 0x48);
        float4 Src8 = LoadColor(GI + 0x49);
        Src1 = 0.125 * (Src1 + Src2 + Src3 + Src4 + Src5 + Src6 + Src7 + Src8);

        OutMip2[DTid.xyz / 2] = PackColor(Src1);
        StoreColor(GI, Src1);
    }

    if (NumMipLevels == 2)
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask (binary: 011011011) checks that X and Y and Z are multiples of four.
    if ((GI & 0xDB) == 0)
    {
        float4 Src2 = LoadColor(GI + 0x02);
        float4 Src3 = LoadColor(GI + 0x10);
        float4 Src4 = LoadColor(GI + 0x12);
        float4 Src5 = LoadColor(GI + 0xC0);  // Z offset (64)
        float4 Src6 = LoadColor(GI + 0xC2);  // X+Z offset (64+2)
        float4 Src7 = LoadColor(GI + 0xD0);  // Y+Z offset (64+16)
        float4 Src8 = LoadColor(GI + 0xD2);  // X+Y+Z offset (64+16+2)
        Src1 = 0.125 * (Src1 + Src2 + Src3 + Src4 + Src5 + Src6 + Src7 + Src8);

        OutMip3[DTid.xyz / 4] = PackColor(Src1);
        StoreColor(GI, Src1);
    }

    if (NumMipLevels == 3)
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask would be 111111111 (X & Y & Z multiples of 8), but only one
    // thread fits that criteria.
    if (GI == 0)
    {
        float4 Src2 = LoadColor(GI + 0x04);
        float4 Src3 = LoadColor(GI + 0x20);
        float4 Src4 = LoadColor(GI + 0x24);
        float4 Src5 = LoadColor(GI + 0x100);  // Z offset (64)
        float4 Src6 = LoadColor(GI + 0x104);  // X+Z offset (64+4)
        float4 Src7 = LoadColor(GI + 0x120);  // Y+Z offset (64+32)
        float4 Src8 = LoadColor(GI + 0x124);  // X+Y+Z offset (64+32+4)
        Src1 = 0.125 * (Src1 + Src2 + Src3 + Src4 + Src5 + Src6 + Src7 + Src8);

        OutMip4[DTid.xyz / 8] = PackColor(Src1);
    }
}
