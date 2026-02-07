#include "../LightGrid.hlsli"
#include "VXGIRenderer.hlsli"

struct PSConstants
{
    float3 SunDirection;
    float3 SunColor;
    float3 AmbientColor;
    float4 ShadowTexelSize;

    float4 InvTileDim;
    uint4 TileCount;
    uint4 FirstLightIndex;

    uint FrameIndexMod2;
    
    float4x4 modelToShadow;
    float3 viewerPos;
};

ConstantBuffer<PSConstants> m_xPSConstants : register(b0);

ConstantBuffer<VoxelizerCB> g_xVoxelizer : register(b0, space1);
ConstantBuffer<FrameCB> g_xFrame : register(b1, space1);
ConstantBuffer<CameraCB> g_xCamera : register(b2, space1);

// CONSTANTBUFFER(g_xVoxelizer, VoxelizerCB, CBSLOT_RENDERER_VOXELIZER);
// CONSTANTBUFFER(g_xFrame, FrameCB, CBSLOT_RENDERER_FRAME);
// CONSTANTBUFFER(g_xCamera, CameraCB, CBSLOT_RENDERER_CAMERA);

Texture2D<float3> texBaseColor : register(t0);
Texture2D<float3> texSpecular : register(t1);
Texture2D<float4> texEmissive : register(t2);
Texture2D<float3> texNormal : register(t3);
//Texture2D<float4> texLightmap		: register(t4);
//Texture2D<float4> texReflection	: register(t5);
Texture2D<float> texSSAO : register(t12);
Texture2D<float> texShadow : register(t13);

StructuredBuffer<LightData> lightBuffer : register(t14);
Texture2DArray<float> lightShadowArrayTex : register(t15);
ByteAddressBuffer lightGrid : register(t16);
ByteAddressBuffer lightGridBitMask : register(t17);

Texture3D<float4> input_previous_radiance : register(t18);

RWTexture3D<uint> output_atomic : register(u0);

float3 GetSunColor()
{
    return m_xPSConstants.SunColor;
}

float3 GetSunDirection()
{
    return m_xPSConstants.SunDirection;
}

float3 GetAmbientColor()
{
    return m_xPSConstants.AmbientColor;
}

float4 GetShadowTexelSize()
{
    return m_xPSConstants.ShadowTexelSize;
}

float4 GetInvTileDim()
{
    return m_xPSConstants.InvTileDim;
}

uint4 GetTileCount()
{
    return m_xPSConstants.TileCount;
}

uint4 GetFirstLightIndex()
{
    return m_xPSConstants.FirstLightIndex;
}

uint GetFrameIndexMod2()
{
    return m_xPSConstants.FrameIndexMod2;
}

float4x4 GetModelToShadow()
{
    return m_xPSConstants.modelToShadow;
}

float3 GetViewerPos()
{
    return m_xPSConstants.viewerPos;
}

float3 GetLightingColor()
{
    return m_xPSConstants.SunColor + m_xPSConstants.AmbientColor;
}

float4 GetTileInfo()
{
    return float4(m_xPSConstants.TileCount.xyz, m_xPSConstants.FrameIndexMod2);
}

bool IsEvenFrame()
{
    return (m_xPSConstants.FrameIndexMod2 == 0);
}


void AntiAliasSpecular(inout float3 texNormal, inout float gloss)
{
    float normalLenSq = dot(texNormal, texNormal);
    float invNormalLen = rsqrt(normalLenSq);
    texNormal *= invNormalLen;
    float normalLen = normalLenSq * invNormalLen;
    float flatness = saturate(1 - abs(ddx(normalLen)) - abs(ddy(normalLen)));
    gloss = exp2(lerp(0, log2(gloss), flatness));
}

// Apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec)
{
    float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
    specular = lerp(specular, 1, fresnel);
    diffuse = lerp(diffuse, 0, fresnel);
}

float3 ApplyAmbientLight(
    float3 diffuse, // Diffuse albedo
    float ao, // Pre-computed ambient-occlusion
    float3 lightColor // Radiance of ambient light
    )
{
    return ao * diffuse * lightColor;
}

float GetDirectionalShadow(float3 ShadowCoord, Texture2D<float> texShadow)
{
#ifdef SINGLE_SAMPLE
    float result = texShadow.SampleCmpLevelZero( sampler_cmp_depth, ShadowCoord.xy, ShadowCoord.z );
#else
    const float Dilation = 2.0;
    float d1 = Dilation * GetShadowTexelSize().x * 0.125;
    float d2 = Dilation * GetShadowTexelSize().x * 0.875;
    float d3 = Dilation * GetShadowTexelSize().x * 0.625;
    float d4 = Dilation * GetShadowTexelSize().x * 0.375;
    float result = (
        2.0 * texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy, ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(-d2, d1), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(-d1, -d2), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(d2, -d1), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(d1, d2), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(-d4, d3), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(-d3, -d4), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(d4, -d3), ShadowCoord.z) +
        texShadow.SampleCmpLevelZero(sampler_cmp_depth, ShadowCoord.xy + float2(d3, d4), ShadowCoord.z)
        ) / 10.0;
#endif
    return result * result;
}

float GetShadowConeLight(uint lightIndex, float3 shadowCoord)
{
    float result = lightShadowArrayTex.SampleCmpLevelZero(
        sampler_cmp_depth, float3(shadowCoord.xy, lightIndex), shadowCoord.z);
    return result * result;
}

float3 ApplyLightCommon(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 lightDir, // World-space vector from point to light
    float3 lightColor // Radiance of directional light
    )
{
    float3 halfVec = normalize(lightDir - viewDir);
    float nDotH = saturate(dot(halfVec, normal));

    FSchlick(diffuseColor, specularColor, lightDir, halfVec);

    float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;

    float nDotL = saturate(dot(normal, lightDir));

    return nDotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

float3 ApplyDirectionalLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 lightDir, // World-space vector from point to light
    float3 lightColor, // Radiance of directional light
    float3 shadowCoord, // Shadow coordinate (Shadow map UV & light-relative Z)
	Texture2D<float> ShadowMap
    )
{
    float shadow = GetDirectionalShadow(shadowCoord, ShadowMap);

    return shadow * ApplyLightCommon(
        diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        lightDir,
        lightColor
        );
}

float3 ApplyPointLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadiusSq,
    float3 lightColor // Radiance of directional light
    )
{
    float3 lightDir = lightPos - worldPos;
    float lightDistSq = dot(lightDir, lightDir);
    float invLightDist = rsqrt(lightDistSq);
    lightDir *= invLightDist;

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    return distanceFalloff * ApplyLightCommon(
        diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        lightDir,
        lightColor
        );
}

float3 ApplyConeLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadiusSq,
    float3 lightColor, // Radiance of directional light
    float3 coneDir,
    float2 coneAngles
    )
{
    float3 lightDir = lightPos - worldPos;
    float lightDistSq = dot(lightDir, lightDir);
    float invLightDist = rsqrt(lightDistSq);
    lightDir *= invLightDist;

    // modify 1/d^2 * R^2 to fall off at a fixed radius
    // (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
    float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
    distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

    float coneFalloff = dot(-lightDir, coneDir);
    coneFalloff = saturate((coneFalloff - coneAngles.y) * coneAngles.x);

    return (coneFalloff * distanceFalloff) * ApplyLightCommon(
        diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        lightDir,
        lightColor
        );
}

float3 ApplyConeShadowedLight(
    float3 diffuseColor, // Diffuse albedo
    float3 specularColor, // Specular albedo
    float specularMask, // Where is it shiny or dingy?
    float gloss, // Specular power
    float3 normal, // World-space normal
    float3 viewDir, // World-space vector from eye to point
    float3 worldPos, // World-space fragment position
    float3 lightPos, // World-space light position
    float lightRadiusSq,
    float3 lightColor, // Radiance of directional light
    float3 coneDir,
    float2 coneAngles,
    float4x4 shadowTextureMatrix,
    uint lightIndex
    )
{
    float4 shadowCoord = mul(shadowTextureMatrix, float4(worldPos, 1.0));
    shadowCoord.xyz *= rcp(shadowCoord.w);
    float shadow = GetShadowConeLight(lightIndex, shadowCoord.xyz);

    return shadow * ApplyConeLight(
        diffuseColor,
        specularColor,
        specularMask,
        gloss,
        normal,
        viewDir,
        worldPos,
        lightPos,
        lightRadiusSq,
        lightColor,
        coneDir,
        coneAngles
        );
}

// options for F+ variants and optimizations
#if 0 // SM6.0
#define _WAVE_OP
#endif

// options for F+ variants and optimizations
#ifdef _WAVE_OP // SM 6.0 (new shader compiler)

// choose one of these:
//# define BIT_MASK
#define BIT_MASK_SORTED
//# define SCALAR_LOOP
//# define SCALAR_BRANCH

// enable to amortize latency of vector read in exchange for additional VGPRs being held
#define LIGHT_GRID_PRELOADING

// configured for 32 sphere lights, 64 cone lights, and 32 cone shadowed lights
#define POINT_LIGHT_GROUPS			1
#define SPOT_LIGHT_GROUPS			2
#define SHADOWED_SPOT_LIGHT_GROUPS	1
#define POINT_LIGHT_GROUPS_TAIL			POINT_LIGHT_GROUPS
#define SPOT_LIGHT_GROUPS_TAIL				POINT_LIGHT_GROUPS_TAIL + SPOT_LIGHT_GROUPS
#define SHADOWED_SPOT_LIGHT_GROUPS_TAIL	SPOT_LIGHT_GROUPS_TAIL + SHADOWED_SPOT_LIGHT_GROUPS

uint GetGroupBits(uint groupIndex, uint tileIndex, uint lightBitMaskGroups[4])
{
#ifdef LIGHT_GRID_PRELOADING
    return lightBitMaskGroups[groupIndex];
#else
    return lightGridBitMask.Load(tileIndex * 16 + groupIndex * 4);
#endif
}

uint WaveOr(uint mask)
{
    return WaveActiveBitOr(mask);
}

uint64_t Ballot64(bool b)
{
    uint4 ballots = WaveActiveBallot(b);
    return (uint64_t)ballots.y << 32 | (uint64_t)ballots.x;
}

#endif // _WAVE_OP

// Helper function for iterating over a sparse list of bits.  Gets the offset of the next
// set bit, clears it, and returns the offset.
uint PullNextBit(inout uint bits)
{
    uint bitIndex = firstbitlow(bits);
    bits ^= 1u << bitIndex;
    return bitIndex;
}

void ShadeLights(inout float3 colorSum, uint2 pixelPos,
	float3 diffuseAlbedo, // Diffuse albedo
	float3 specularAlbedo, // Specular albedo
	float specularMask, // Where is it shiny or dingy?
	float gloss,
	float3 normal,
	float3 viewDir,
	float3 worldPos
	)
{
    uint2 tilePos = GetTilePos(pixelPos, GetInvTileDim().xy);
    uint tileIndex = GetTileIndex(tilePos, GetTileCount().x);
    uint tileOffset = GetTileOffset(tileIndex);

    // Light Grid Preloading setup
    uint lightBitMaskGroups[4] = { 0, 0, 0, 0 };
#if defined(LIGHT_GRID_PRELOADING)
    uint4 lightBitMask = lightGridBitMask.Load4(tileIndex * 16);
    
    lightBitMaskGroups[0] = lightBitMask.x;
    lightBitMaskGroups[1] = lightBitMask.y;
    lightBitMaskGroups[2] = lightBitMask.z;
    lightBitMaskGroups[3] = lightBitMask.w;
#endif

#define POINT_LIGHT_ARGS \
    diffuseAlbedo, \
    specularAlbedo, \
    specularMask, \
    gloss, \
    normal, \
    viewDir, \
    worldPos, \
    lightData.pos, \
    lightData.radiusSq, \
    lightData.color

#define CONE_LIGHT_ARGS \
    POINT_LIGHT_ARGS, \
    lightData.coneDir, \
    lightData.coneAngles

#define SHADOWED_LIGHT_ARGS \
    CONE_LIGHT_ARGS, \
    lightData.shadowTextureMatrix, \
    lightIndex

#if defined(BIT_MASK)
    uint64_t threadMask = Ballot64(tileIndex != ~0); // attempt to get starting exec mask

    for (uint groupIndex = 0; groupIndex < 4; groupIndex++)
    {
        // combine across threads
        uint groupBits = WaveOr(GetGroupBits(groupIndex, tileIndex, lightBitMaskGroups));

        while (groupBits != 0)
        {
            uint bitIndex = PullNextBit(groupBits);
            uint lightIndex = 32 * groupIndex + bitIndex;

            LightData lightData = lightBuffer[lightIndex];

            if (lightIndex < GetFirstLightIndex().x) // sphere
            {
                colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
            }
            else if (lightIndex < GetFirstLightIndex().y) // cone
            {
                colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
            }
            else // cone w/ shadow map
            {
                colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
            }
        }
    }

#elif defined(BIT_MASK_SORTED)

    // Get light type groups - these can be predefined as compile time constants to enable unrolling and better scheduling of vector reads
    uint pointLightGroupTail		= POINT_LIGHT_GROUPS_TAIL;
    uint spotLightGroupTail			= SPOT_LIGHT_GROUPS_TAIL;
    uint spotShadowLightGroupTail	= SHADOWED_SPOT_LIGHT_GROUPS_TAIL;

    uint groupBitsMasks[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++)
    {
        // combine across threads
        groupBitsMasks[i] = WaveOr(GetGroupBits(i, tileIndex, lightBitMaskGroups));
    }

    uint groupIndex;

    for (groupIndex = 0; groupIndex < pointLightGroupTail; groupIndex++)
    {
        uint groupBits = groupBitsMasks[groupIndex];

        while (groupBits != 0)
        {
            uint bitIndex = PullNextBit(groupBits);
            uint lightIndex = 32 * groupIndex + bitIndex;

            // sphere
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
        }
    }

    for (groupIndex = pointLightGroupTail; groupIndex < spotLightGroupTail; groupIndex++)
    {
        uint groupBits = groupBitsMasks[groupIndex];

        while (groupBits != 0)
        {
            uint bitIndex = PullNextBit(groupBits);
            uint lightIndex = 32 * groupIndex + bitIndex;

            // cone
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
        }
    }

    for (groupIndex = spotLightGroupTail; groupIndex < spotShadowLightGroupTail; groupIndex++)
    {
        uint groupBits = groupBitsMasks[groupIndex];

        while (groupBits != 0)
        {
            uint bitIndex = PullNextBit(groupBits);
            uint lightIndex = 32 * groupIndex + bitIndex;

            // cone w/ shadow map
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
        }
    }

#elif defined(SCALAR_LOOP)
    uint64_t threadMask = Ballot64(tileOffset != ~0); // attempt to get starting exec mask
    uint64_t laneBit = 1ull << WaveGetLaneIndex();

    while ((threadMask & laneBit) != 0) // is this thread waiting to be processed?
    { // exec is now the set of remaining threads
        // grab the tile offset for the first active thread
        uint uniformTileOffset = WaveReadLaneFirst(tileOffset);
        // mask of which threads have the same tile offset as the first active thread
        uint64_t uniformMask = Ballot64(tileOffset == uniformTileOffset);

        if (any((uniformMask & laneBit) != 0)) // is this thread one of the current set of uniform threads?
        {
            uint tileLightCount = lightGrid.Load(uniformTileOffset + 0);
            uint tileLightCountSphere = (tileLightCount >> 0) & 0xff;
            uint tileLightCountCone = (tileLightCount >> 8) & 0xff;
            uint tileLightCountConeShadowed = (tileLightCount >> 16) & 0xff;

            uint tileLightLoadOffset = uniformTileOffset + 4;
            uint n;

            // sphere
            for (n = 0; n < tileLightCountSphere; n++, tileLightLoadOffset += 4)
            {
                uint lightIndex = lightGrid.Load(tileLightLoadOffset);
                LightData lightData = lightBuffer[lightIndex];
                colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
            }

            // cone
            for (n = 0; n < tileLightCountCone; n++, tileLightLoadOffset += 4)
            {
                uint lightIndex = lightGrid.Load(tileLightLoadOffset);
                LightData lightData = lightBuffer[lightIndex];
                colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
            }

            // cone w/ shadow map
            for (n = 0; n < tileLightCountConeShadowed; n++, tileLightLoadOffset += 4)
            {
                uint lightIndex = lightGrid.Load(tileLightLoadOffset);
                LightData lightData = lightBuffer[lightIndex];
                colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
            }
        }

        // strip the current set of uniform threads from the exec mask for the next loop iteration
        threadMask &= ~uniformMask;
    }

#elif defined(SCALAR_BRANCH)

    if (Ballot64(tileOffset == WaveReadLaneFirst(tileOffset)) == ~0ull)
    {
        // uniform branch
        tileOffset = WaveReadLaneFirst(tileOffset);

        uint tileLightCount = lightGrid.Load(tileOffset + 0);
        uint tileLightCountSphere = (tileLightCount >> 0) & 0xff;
        uint tileLightCountCone = (tileLightCount >> 8) & 0xff;
        uint tileLightCountConeShadowed = (tileLightCount >> 16) & 0xff;

        uint tileLightLoadOffset = tileOffset + 4;
        uint n;

        // sphere
        for (n = 0; n < tileLightCountSphere; n++, tileLightLoadOffset += 4)
        {
            uint lightIndex = lightGrid.Load(tileLightLoadOffset);
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
        }

        // cone
        for (n = 0; n < tileLightCountCone; n++, tileLightLoadOffset += 4)
        {
            uint lightIndex = lightGrid.Load(tileLightLoadOffset);
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
        }

        // cone w/ shadow map
        for (n = 0; n < tileLightCountConeShadowed; n++, tileLightLoadOffset += 4)
        {
            uint lightIndex = lightGrid.Load(tileLightLoadOffset);
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
        }
    }
    else
    {
        // divergent branch
        uint tileLightCount = lightGrid.Load(tileOffset + 0);
        uint tileLightCountSphere = (tileLightCount >> 0) & 0xff;
        uint tileLightCountCone = (tileLightCount >> 8) & 0xff;
        uint tileLightCountConeShadowed = (tileLightCount >> 16) & 0xff;

        uint tileLightLoadOffset = tileOffset + 4;
        uint n;

        // sphere
        for (n = 0; n < tileLightCountSphere; n++, tileLightLoadOffset += 4)
        {
            uint lightIndex = lightGrid.Load(tileLightLoadOffset);
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
        }

        // cone
        for (n = 0; n < tileLightCountCone; n++, tileLightLoadOffset += 4)
        {
            uint lightIndex = lightGrid.Load(tileLightLoadOffset);
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
        }

        // cone w/ shadow map
        for (n = 0; n < tileLightCountConeShadowed; n++, tileLightLoadOffset += 4)
        {
            uint lightIndex = lightGrid.Load(tileLightLoadOffset);
            LightData lightData = lightBuffer[lightIndex];
            colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
        }
    }

#else // SM 5.0 (no wave intrinsics)

    uint tileLightCount = lightGrid.Load(tileOffset + 0);
    uint tileLightCountSphere = (tileLightCount >> 0) & 0xff;
    uint tileLightCountCone = (tileLightCount >> 8) & 0xff;
    uint tileLightCountConeShadowed = (tileLightCount >> 16) & 0xff;

    uint tileLightLoadOffset = tileOffset + 4;

    // sphere
    uint n;
    for (n = 0; n < tileLightCountSphere; n++, tileLightLoadOffset += 4)
    {
        uint lightIndex = lightGrid.Load(tileLightLoadOffset);
        LightData lightData = lightBuffer[lightIndex];
        colorSum += ApplyPointLight(POINT_LIGHT_ARGS);
    }

    // cone
    for (n = 0; n < tileLightCountCone; n++, tileLightLoadOffset += 4)
    {
        uint lightIndex = lightGrid.Load(tileLightLoadOffset);
        LightData lightData = lightBuffer[lightIndex];
        colorSum += ApplyConeLight(CONE_LIGHT_ARGS);
    }

    // cone w/ shadow map
    for (n = 0; n < tileLightCountConeShadowed; n++, tileLightLoadOffset += 4)
    {
        uint lightIndex = lightGrid.Load(tileLightLoadOffset);
        LightData lightData = lightBuffer[lightIndex];
        colorSum += ApplyConeShadowedLight(SHADOWED_LIGHT_ARGS);
    }
#endif
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

    VoxelClipMap clipmap = g_xFrame.vxgi.clipmaps[g_xVoxelizer.clipmap_index];
    float3 uvw = g_xFrame.vxgi.world_to_clipmap(P, clipmap);
    if (!is_saturated(uvw))
        return;

#ifdef VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED
	uint3 clipmap_pixel = uvw * g_xFrame.vxgi.resolution;
	float3 clipmap_uvw_center = (clipmap_pixel + 0.5) * g_xFrame.vxgi.resolution_rcp;
	float3 voxel_center = g_xFrame.vxgi.clipmap_to_world(clipmap_uvw_center, clipmap);
	AABB voxel_aabb;
	voxel_aabb.c = voxel_center;
	voxel_aabb.e = clipmap.voxelSize;
	AABB triangle_aabb;
	AABBfromMinMax(triangle_aabb, input.aabb_min, input.aabb_max);
	if (!IntersectAABB(voxel_aabb, triangle_aabb))
		return;
#endif // VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED

	float3 viewDir = normalize(P - GetViewerPos());
	float3 shadowCoord = mul(GetModelToShadow(), float4(P, 1.0)).xyz;
	
    float4 baseColor = float4(1, 1, 1, 1);
    float lod_bias = 0;
    baseColor *= float4(texBaseColor.SampleBias(sampler_linear_clamp, uvset, lod_bias).rgb, 1.0f);

    float3 emissiveColor = float3(0, 0, 0);
    float4 emissiveMap = texEmissive.Sample(sampler_linear_clamp, uvset);
    emissiveColor *= emissiveMap.rgb * emissiveMap.a;

    float3 N = normalize(input.N);

    uint2 pixelPos = uint2(input.pos.xy);
    float3 diffuseAlbedo = texBaseColor.Sample(sampler_linear_clamp, uvset);
    float3 colorSum = 0;
    {
        float ao = texSSAO[pixelPos];
        colorSum += ApplyAmbientLight(diffuseAlbedo, ao, GetAmbientColor());
    }

    float gloss = 128.0;
    float3 specularAlbedo = float3(0.56, 0.56, 0.56);
    float specularMask = texSpecular.Sample(sampler_linear_clamp, uvset).g;
    colorSum += ApplyDirectionalLight(diffuseAlbedo, specularAlbedo, specularMask, gloss, N, viewDir, GetSunDirection(), GetSunColor(), shadowCoord, texShadow);

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
    uint3 writecoord = floor(uvw * g_xFrame.vxgi.resolution);
    writecoord.z *= VOXELIZATION_CHANNEL_COUNT; // de-interleaved channels

    float3 aniso_direction = N;

#if 0
	// This voxelization is faster but less accurate:
	uint face_offset = cubemap_to_uv(aniso_direction).z * g_xFrame.vxgi.resolution;
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
		) * g_xFrame.vxgi.resolution;
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
