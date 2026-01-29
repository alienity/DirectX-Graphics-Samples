//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  Alex Nankervis
//             James Stanard
//

// From Core
#include "GraphicsCore.h"
#include "BufferManager.h"
#include "Camera.h"
#include "CommandContext.h"
#include "TemporalEffects.h"
#include "SSAO.h"
#include "SystemTime.h"
#include "ShadowCamera.h"
#include "ParticleEffects.h"
#include "SponzaRenderer.h"
#include "Renderer.h"
#include "VoxelConeTracer.h"

// From Model
#include "ModelH3D.h"

// From ModelViewer
#include "LightManager.h"

#include "CompiledShaders/DepthViewerVS.h"
#include "CompiledShaders/DepthViewerPS.h"
#include "CompiledShaders/ModelViewerVS.h"
#include "CompiledShaders/ModelViewerPS.h"

#include "CompiledShaders/VXGIVoxelizationVS.h"
#include "CompiledShaders/VXGIVoxelizationGS.h"
#include "CompiledShaders/VXGIVoxelizationPS.h"
#include "CompiledShaders/VXGITemporalCS.h"
#include "CompiledShaders/VXGISDFJumpfloodCS.h"
#include "CompiledShaders/VXGIOffsetprevCS.h"
#include "CompiledShaders/VXGIResolveDiffuseCS.h"
#include "CompiledShaders/VXGIResolveSpecularCS.h"

using namespace Math;
using namespace Graphics;

namespace VXGI
{
#define CBSLOT_RENDERER_FRAME					0
#define CBSLOT_RENDERER_CAMERA					1
#define CBSLOT_RENDERER_VOXELIZER				3

    // If enabled, geometry shader will be used to voxelize, and axis will be selected by geometry shader
    //	If disabled, vertex shader with instance replication will be used for each axis
#define VOXELIZATION_GEOMETRY_SHADER_ENABLED

    // Number of clipmaps, each doubling in size:
    static const uint32_t VXGI_CLIPMAP_COUNT = 6;

    struct alignas(16) VoxelClipMap
    {
        XMFLOAT3 center; // center of clipmap volume in world space
        float voxelSize; // half-extent of one voxel
    };

    struct alignas(16) VXGI
    {
        uint32_t resolution; // voxel grid resolution
        float resolution_rcp; // 1.0 / voxel grid resolution
        float stepsize; // raymarch step size in voxel space units
        float max_distance; // maximum raymarch distance for voxel GI in world-space

        //int texture_radiance;
        //int texture_sdf;
        //int padding0;
        //int padding1;

        VoxelClipMap clipmaps[VXGI_CLIPMAP_COUNT];
    };

    struct alignas(16) VoxelizerCB
    {
        XMINT3 offsetfromPrevFrame;
        int clipmap_index;
    };

    enum VOXELIZATION_CHANNEL
    {
        VOXELIZATION_CHANNEL_BASECOLOR_R,
        VOXELIZATION_CHANNEL_BASECOLOR_G,
        VOXELIZATION_CHANNEL_BASECOLOR_B,
        VOXELIZATION_CHANNEL_BASECOLOR_A,
        VOXELIZATION_CHANNEL_EMISSIVE_R,
        VOXELIZATION_CHANNEL_EMISSIVE_G,
        VOXELIZATION_CHANNEL_EMISSIVE_B,
        VOXELIZATION_CHANNEL_DIRECTLIGHT_R,
        VOXELIZATION_CHANNEL_DIRECTLIGHT_G,
        VOXELIZATION_CHANNEL_DIRECTLIGHT_B,
        VOXELIZATION_CHANNEL_NORMAL_R,
        VOXELIZATION_CHANNEL_NORMAL_G,
        VOXELIZATION_CHANNEL_FRAGMENT_COUNTER,

        VOXELIZATION_CHANNEL_COUNT,
    };


    // Cones from: https://github.com/compix/VoxelConeTracingGI/blob/master/assets/shaders/voxelConeTracing/finalLightingPass.frag

    //#define USE_32_CONES
#ifdef USE_32_CONES
    // 32 Cones for higher quality (16 on average per hemisphere)
    static const int DIFFUSE_CONE_COUNT = 32;
    static const float DIFFUSE_CONE_APERTURE = 0.628319f;

    static const XMFLOAT3 DIFFUSE_CONE_DIRECTIONS[32] = {
        XMFLOAT3(0.898904f, 0.435512f, 0.0479745f),
        XMFLOAT3(0.898904f, -0.435512f, -0.0479745f),
        XMFLOAT3(0.898904f, 0.0479745f, -0.435512f),
        XMFLOAT3(0.898904f, -0.0479745f, 0.435512f),
        XMFLOAT3(-0.898904f, 0.435512f, -0.0479745f),
        XMFLOAT3(-0.898904f, -0.435512f, 0.0479745f),
        XMFLOAT3(-0.898904f, 0.0479745f, 0.435512f),
        XMFLOAT3(-0.898904f, -0.0479745f, -0.435512f),
        XMFLOAT3(0.0479745f, 0.898904f, 0.435512f),
        XMFLOAT3(-0.0479745f, 0.898904f, -0.435512f),
        XMFLOAT3(-0.435512f, 0.898904f, 0.0479745f),
        XMFLOAT3(0.435512f, 0.898904f, -0.0479745f),
        XMFLOAT3(-0.0479745f, -0.898904f, 0.435512f),
        XMFLOAT3(0.0479745f, -0.898904f, -0.435512f),
        XMFLOAT3(0.435512f, -0.898904f, 0.0479745f),
        XMFLOAT3(-0.435512f, -0.898904f, -0.0479745f),
        XMFLOAT3(0.435512f, 0.0479745f, 0.898904f),
        XMFLOAT3(-0.435512f, -0.0479745f, 0.898904f),
        XMFLOAT3(0.0479745f, -0.435512f, 0.898904f),
        XMFLOAT3(-0.0479745f, 0.435512f, 0.898904f),
        XMFLOAT3(0.435512f, -0.0479745f, -0.898904f),
        XMFLOAT3(-0.435512f, 0.0479745f, -0.898904f),
        XMFLOAT3(0.0479745f, 0.435512f, -0.898904f),
        XMFLOAT3(-0.0479745f, -0.435512f, -0.898904f),
        XMFLOAT3(0.57735f, 0.57735f, 0.57735f),
        XMFLOAT3(0.57735f, 0.57735f, -0.57735f),
        XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(0.57735f, -0.57735f, -0.57735f),
        XMFLOAT3(-0.57735f, 0.57735f, 0.57735f),
        XMFLOAT3(-0.57735f, 0.57735f, -0.57735f),
        XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(-0.57735f, -0.57735f, -0.57735f)
    };
#else // 16 cones for lower quality (8 on average per hemisphere)
    static const int DIFFUSE_CONE_COUNT = 16;
    static const float DIFFUSE_CONE_APERTURE = 0.872665f;

    static const XMFLOAT3 DIFFUSE_CONE_DIRECTIONS[16] = {
        XMFLOAT3(0.57735f, 0.57735f, 0.57735f),
        XMFLOAT3(0.57735f, -0.57735f, -0.57735f),
        XMFLOAT3(-0.57735f, 0.57735f, -0.57735f),
        XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(-0.903007f, -0.182696f, -0.388844f),
        XMFLOAT3(-0.903007f, 0.182696f, 0.388844f),
        XMFLOAT3(0.903007f, -0.182696f, 0.388844f),
        XMFLOAT3(0.903007f, 0.182696f, -0.388844f),
        XMFLOAT3(-0.388844f, -0.903007f, -0.182696f),
        XMFLOAT3(0.388844f, -0.903007f, 0.182696f),
        XMFLOAT3(0.388844f, 0.903007f, -0.182696f),
        XMFLOAT3(-0.388844f, 0.903007f, 0.182696f),
        XMFLOAT3(-0.182696f, -0.388844f, -0.903007f),
        XMFLOAT3(0.182696f, 0.388844f, -0.903007f),
        XMFLOAT3(-0.182696f, 0.388844f, 0.903007f),
        XMFLOAT3(0.182696f, -0.388844f, 0.903007f)
    };
#endif

    struct alignas(16) FrameCB
    {
        uint32_t options; // wi::renderer bool options packed into bitmask (OPTION_BIT_ values)
        float time;
        float time_previous;
        float delta_time;

        uint32_t frame_count;
        uint32_t temporalaa_samplerotation;
        int texture_shadowatlas_index;
        int texture_shadowatlas_transparent_index;

        VXGI vxgi;
    };

    struct alignas(16) ShaderSphere
    {
        XMFLOAT3 center;
        float radius;
    };

    struct alignas(16) ShaderClusterBounds
    {
        ShaderSphere sphere;

        XMFLOAT3 cone_axis;
        float cone_cutoff;
    };

    struct alignas(16) ShaderFrustum
    {
        // Frustum planes:
        //	0 : near
        //	1 : far
        //	2 : left
        //	3 : right
        //	4 : top
        //	5 : bottom
        XMFLOAT4 planes[6];
    };

    struct alignas(16) ShaderFrustumCorners
    {
        // topleft, topright, bottomleft, bottomright
        XMFLOAT4 cornersNEAR[4];
        XMFLOAT4 cornersFAR[4];
    };

    struct alignas(16) ShaderCamera
    {
        XMFLOAT4X4	view_projection;

        XMFLOAT3		position;
        uint32_t		output_index; // viewport or rendertarget array index

        XMFLOAT4		clip_plane;
        XMFLOAT4		reflection_plane; // not clip plane (not reversed when camera is under), but the original plane

        XMFLOAT3		forward;
        float		z_near;

        XMFLOAT3		up;
        float		z_far;

        float		z_near_rcp;
        float		z_far_rcp;
        float		z_range;
        float		z_range_rcp;

        XMFLOAT4X4	view;
        XMFLOAT4X4	projection;
        XMFLOAT4X4	inverse_view;
        XMFLOAT4X4	inverse_projection;
        XMFLOAT4X4	inverse_view_projection;

        ShaderFrustum frustum;
        ShaderFrustumCorners frustum_corners;

        XMFLOAT2		temporalaa_jitter;
        XMFLOAT2		temporalaa_jitter_prev;

        XMFLOAT4X4	previous_view;
        XMFLOAT4X4	previous_projection;
        XMFLOAT4X4	previous_view_projection;
        XMFLOAT4X4	previous_inverse_view_projection;
        XMFLOAT4X4	reflection_view_projection;
        XMFLOAT4X4	reflection_inverse_view_projection;
        XMFLOAT4X4	reprojection; // view_projection_inverse_matrix * previous_view_projection_matrix

        XMFLOAT2		aperture_shape;
        float		aperture_size;
        float		focal_length;

        XMFLOAT2 canvas_size;
        XMFLOAT2 canvas_size_rcp;

        XMUINT2 internal_resolution;
        XMFLOAT2 internal_resolution_rcp;

        XMUINT4 scissor; // scissor in physical coordinates (left,top,right,bottom) range: [0, internal_resolution]
        XMFLOAT4 scissor_uv; // scissor in screen UV coordinates (left,top,right,bottom) range: [0, 1]

        inline void init()
        {
            view_projection = {};
            position = {};
            output_index = 0;
            clip_plane = {};
            forward = {};
            z_near = {};
            up = {};
            z_far = {};
            z_near_rcp = {};
            z_far_rcp = {};
            z_range = {};
            z_range_rcp = {};
            view = {};
            projection = {};
            inverse_view = {};
            inverse_projection = {};
            inverse_view_projection = {};
        }
    };

    struct alignas(16) CameraCB
    {
        ShaderCamera cameras[16];

        inline void init()
        {
            for (int i = 0; i < 16; ++i)
            {
                cameras[i].init();
            }
        }
    };
}


namespace Sponza
{
    void RenderLightShadows(GraphicsContext& gfxContext, const Camera& camera);

    enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };
    void RenderObjects( GraphicsContext& Context, const Matrix4& ViewProjMat, const Vector3& viewerPos, eObjectFilter Filter = kAll );

    void VXGIVoxelize(GraphicsContext& gfxContext, const Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
    void VXGIResolve(GraphicsContext& gfxContext, const Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
    void VoxelizeObjects( GraphicsContext& Context, const Matrix4& ViewProjMat, const Vector3& viewerPos, eObjectFilter Filter = kAll );

    GraphicsPSO m_DepthPSO = { (L"Sponza: Depth PSO") };
    GraphicsPSO m_CutoutDepthPSO = { (L"Sponza: Cutout Depth PSO") };
    GraphicsPSO m_ModelPSO = { (L"Sponza: Color PSO") };
    GraphicsPSO m_CutoutModelPSO = { (L"Sponza: Cutout Color PSO") };
    GraphicsPSO m_ShadowPSO(L"Sponza: Shadow PSO");
    GraphicsPSO m_CutoutShadowPSO(L"Sponza: Cutout Shadow PSO");
    GraphicsPSO m_VoxelPSO(L"Sponza: Voxel PSO");
    GraphicsPSO m_CutoutVoxelPSO(L"Sponza: Cutout Voxel PSO");

    ModelH3D m_Model;
    std::vector<bool> m_pMaterialIsCutout;

    Vector3 m_SunDirection;
    ShadowCamera m_SunShadow;

    ExpVar m_AmbientIntensity("Sponza/Lighting/Ambient Intensity", 0.1f, -16.0f, 16.0f, 0.1f);
    ExpVar m_SunLightIntensity("Sponza/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
    NumVar m_SunOrientation("Sponza/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f );
    NumVar m_SunInclination("Sponza/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f );
    NumVar ShadowDimX("Sponza/Lighting/Shadow Dim X", 5000, 1000, 10000, 100 );
    NumVar ShadowDimY("Sponza/Lighting/Shadow Dim Y", 3000, 1000, 10000, 100 );
    NumVar ShadowDimZ("Sponza/Lighting/Shadow Dim Z", 3000, 1000, 10000, 100 );
}

void Sponza::Startup( Camera& Camera )
{
    DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
    DXGI_FORMAT NormalFormat = g_SceneNormalBuffer.GetFormat();
    DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
    //DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();

    D3D12_INPUT_ELEMENT_DESC vertElem[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Depth-only (2x rate)
    m_DepthPSO.SetRootSignature(Renderer::m_RootSig);
    m_DepthPSO.SetRasterizerState(RasterizerDefault);
    m_DepthPSO.SetBlendState(BlendNoColorWrite);
    m_DepthPSO.SetDepthStencilState(DepthStateReadWrite);
    m_DepthPSO.SetInputLayout(_countof(vertElem), vertElem);
    m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_DepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
    m_DepthPSO.SetVertexShader(g_pDepthViewerVS, sizeof(g_pDepthViewerVS));
    m_DepthPSO.Finalize();

    // Depth-only shading but with alpha testing
    m_CutoutDepthPSO = m_DepthPSO;
    m_CutoutDepthPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
    m_CutoutDepthPSO.SetRasterizerState(RasterizerTwoSided);
    m_CutoutDepthPSO.Finalize();

    // Depth-only but with a depth bias and/or render only backfaces
    m_ShadowPSO = m_DepthPSO;
    m_ShadowPSO.SetRasterizerState(RasterizerShadow);
    m_ShadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
    m_ShadowPSO.Finalize();

    // Shadows with alpha testing
    m_CutoutShadowPSO = m_ShadowPSO;
    m_CutoutShadowPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
    m_CutoutShadowPSO.SetRasterizerState(RasterizerShadowTwoSided);
    m_CutoutShadowPSO.Finalize();

    DXGI_FORMAT formats[2] = { ColorFormat, NormalFormat };

    // Full color pass
    m_ModelPSO = m_DepthPSO;
    m_ModelPSO.SetBlendState(BlendDisable);
    m_ModelPSO.SetDepthStencilState(DepthStateTestEqual);
    m_ModelPSO.SetRenderTargetFormats(2, formats, DepthFormat);
    m_ModelPSO.SetVertexShader( g_pModelViewerVS, sizeof(g_pModelViewerVS) );
    m_ModelPSO.SetPixelShader( g_pModelViewerPS, sizeof(g_pModelViewerPS) );
    m_ModelPSO.Finalize();

    m_CutoutModelPSO = m_ModelPSO;
    m_CutoutModelPSO.SetRasterizerState(RasterizerTwoSided);
    m_CutoutModelPSO.Finalize();

    // Voxel pass
    D3D12_RASTERIZER_DESC VoxelizationRasterizer;
    VoxelizationRasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    VoxelizationRasterizer.CullMode = D3D12_CULL_MODE_NONE;
    VoxelizationRasterizer.FrontCounterClockwise = TRUE;
    VoxelizationRasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    VoxelizationRasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    VoxelizationRasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    VoxelizationRasterizer.DepthClipEnable = FALSE;
    VoxelizationRasterizer.MultisampleEnable = FALSE;
    VoxelizationRasterizer.AntialiasedLineEnable = FALSE;
    VoxelizationRasterizer.ForcedSampleCount = 0;
    VoxelizationRasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;

    D3D12_BLEND_DESC VoxelizationBlendDesc;
    VoxelizationBlendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    VoxelizationBlendDesc.RenderTarget[0].BlendEnable = FALSE;
    VoxelizationBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    VoxelizationBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    VoxelizationBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_MAX;
    VoxelizationBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    VoxelizationBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    VoxelizationBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_MAX;
    VoxelizationBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    m_VoxelPSO.SetRootSignature(Renderer::m_VoxelRootSig);
    m_VoxelPSO.SetRasterizerState(VoxelizationRasterizer);
    m_VoxelPSO.SetBlendState(VoxelizationBlendDesc);
    m_VoxelPSO.SetDepthStencilState(DepthStateDisabled);
    m_VoxelPSO.SetRenderTargetFormats(0, nullptr, DXGI_FORMAT_UNKNOWN);
    m_VoxelPSO.SetSampleMask(0xFFFFFFFF);
    m_VoxelPSO.SetInputLayout(_countof(vertElem), vertElem);
    m_VoxelPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_VoxelPSO.SetVertexShader(g_pVXGIVoxelizationVS, sizeof(g_pVXGIVoxelizationVS));
    m_VoxelPSO.SetGeometryShader(g_pVXGIVoxelizationGS, sizeof(g_pVXGIVoxelizationGS));
    m_VoxelPSO.SetPixelShader(g_pVXGIVoxelizationPS, sizeof(g_pVXGIVoxelizationPS));
    m_VoxelPSO.Finalize();

    m_CutoutVoxelPSO = m_VoxelPSO;
    m_CutoutModelPSO.SetRasterizerState(RasterizerTwoSided);
    m_CutoutModelPSO.Finalize();

    ASSERT(m_Model.Load(L"Sponza/sponza.h3d"), "Failed to load model");
    ASSERT(m_Model.GetMeshCount() > 0, "Model contains no meshes");

    // The caller of this function can override which materials are considered cutouts
    m_pMaterialIsCutout.resize(m_Model.GetMaterialCount());
    for (uint32_t i = 0; i < m_Model.GetMaterialCount(); ++i)
    {
        const ModelH3D::Material& mat = m_Model.GetMaterial(i);
        if (std::string(mat.texDiffusePath).find("thorn") != std::string::npos ||
            std::string(mat.texDiffusePath).find("plant") != std::string::npos ||
            std::string(mat.texDiffusePath).find("chain") != std::string::npos)
        {
            m_pMaterialIsCutout[i] = true;
        }
        else
        {
            m_pMaterialIsCutout[i] = false;
        }
    }

    VCT::Startup();

    ParticleEffects::InitFromJSON(L"Sponza/particles.json");

    float modelRadius = Length(m_Model.GetBoundingBox().GetDimensions()) * 0.5f;
    const Vector3 eye = m_Model.GetBoundingBox().GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
    Camera.SetEyeAtUp( eye, Vector3(kZero), Vector3(kYUnitVector) );

    Lighting::CreateRandomLights(m_Model.GetBoundingBox().GetMin(), m_Model.GetBoundingBox().GetMax());
}

const ModelH3D& Sponza::GetModel()
{
    return Sponza::m_Model;
}

void Sponza::Cleanup( void )
{
    VCT::Shutdown();

    m_Model.Clear();
    Lighting::Shutdown();
    TextureManager::Shutdown();
}

void Sponza::RenderObjects( GraphicsContext& gfxContext, const Matrix4& ViewProjMat, const Vector3& viewerPos, eObjectFilter Filter )
{
    struct VSConstants
    {
        Matrix4 modelToProjection;
        Matrix4 modelToShadow;
        XMFLOAT3 viewerPos;
    } vsConstants;
    vsConstants.modelToProjection = ViewProjMat;
    vsConstants.modelToShadow = m_SunShadow.GetShadowMatrix();
    XMStoreFloat3(&vsConstants.viewerPos, viewerPos);

    gfxContext.SetDynamicConstantBufferView(Renderer::kMeshConstants, sizeof(vsConstants), &vsConstants);

    __declspec(align(16)) uint32_t materialIdx = 0xFFFFFFFFul;

    uint32_t VertexStride = m_Model.GetVertexStride();

    for (uint32_t meshIndex = 0; meshIndex < m_Model.GetMeshCount(); meshIndex++)
    {
        const ModelH3D::Mesh& mesh = m_Model.GetMesh(meshIndex);

        uint32_t indexCount = mesh.indexCount;
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

        if (mesh.materialIndex != materialIdx)
        {
            if ( m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kCutout) ||
                !m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kOpaque) )
                continue;

            materialIdx = mesh.materialIndex;
            gfxContext.SetDescriptorTable(Renderer::kMaterialSRVs, m_Model.GetSRVs(materialIdx));

            gfxContext.SetDynamicConstantBufferView(Renderer::kCommonCBV, sizeof(uint32_t), &materialIdx);
        }

        gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
    }
}

void Sponza::VXGIVoxelize(GraphicsContext& gfxContext, const Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
{

    {
        gfxContext.SetRootSignature(Renderer::m_VoxelRootSig);
        gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());
        gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gfxContext.SetIndexBuffer(m_Model.GetIndexBuffer());
        gfxContext.SetVertexBuffer(0, m_Model.GetVertexBuffer());

        ScopedTimer _prof(L"VoxelPass", gfxContext);

        gfxContext.SetDynamicDescriptor(2, sizeof(psConstants), &psConstants);

        {
            ScopedTimer _prof2(L"OpaqueVoxel", gfxContext);
            {
                //gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
                //gfxContext.ClearDepth(g_SceneDepthBuffer);
                gfxContext.SetPipelineState(m_VoxelPSO);
                //gfxContext.SetDepthStencilTarget(g_SceneDepthBuffer.GetDSV());
                gfxContext.SetViewportAndScissor(viewport, scissor);
            }
            VoxelizeObjects(gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), kOpaque);
        }

        {
            ScopedTimer _prof2(L"CutoutVoxel", gfxContext);
            {
                gfxContext.SetPipelineState(m_CutoutVoxelPSO);
            }
            VoxelizeObjects(gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), kCutout);
        }
    }

}

void Sponza::VXGIResolve(GraphicsContext& gfxContext, const Camera& camera, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
{

}

void Sponza::VoxelizeObjects(GraphicsContext& gfxContext, const Matrix4& ViewProjMat, const Vector3& viewerPos, eObjectFilter Filter)
{

    //gfxContext.SetDescriptorTable(1, m_Model.GetSRVs(materialIdx));

    uint32_t VertexStride = m_Model.GetVertexStride();

    for (uint32_t meshIndex = 0; meshIndex < m_Model.GetMeshCount(); meshIndex++)
    {
        const ModelH3D::Mesh& mesh = m_Model.GetMesh(meshIndex);

        __declspec(align(16)) struct VSConstants
        {
            XMFLOAT4X4 modelMatrix;
            XMFLOAT4X4 modelMatrixIT;
            int cameraIndex;
        } vsConstants;
        vsConstants.modelMatrix = ;
        vsConstants.modelMatrixIT = ;
        vsConstants.cameraIndex = 0;

        gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);


        uint32_t indexCount = mesh.indexCount;
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

        __declspec(align(16)) struct PSConstants
        {
            XMFLOAT3 SunDirection;
            XMFLOAT3 SunColor;
            XMFLOAT3 AmbientColor;
            XMFLOAT4 ShadowTexelSize;
            XMFLOAT4 InvTileDim;
            XMUINT4 TileCount;
            XMUINT4 FirstLightIndex;
            uint32_t FrameIndexMod2;
            XMFLOAT4X4 modelToShadow;
            XMFLOAT3 viewerPos;
        } psConstants;

        gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

        gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
    }
}

void Sponza::RenderLightShadows(GraphicsContext& gfxContext, const Camera& camera)
{
    using namespace Lighting;

    ScopedTimer _prof(L"RenderLightShadows", gfxContext);

    static uint32_t LightIndex = 0;
    if (LightIndex >= MaxLights)
        return;

    m_LightShadowTempBuffer.BeginRendering(gfxContext);
    {
        gfxContext.SetPipelineState(m_ShadowPSO);
        RenderObjects(gfxContext, m_LightShadowMatrix[LightIndex], camera.GetPosition(), kOpaque);
        gfxContext.SetPipelineState(m_CutoutShadowPSO);
        RenderObjects(gfxContext, m_LightShadowMatrix[LightIndex], camera.GetPosition(), kCutout);
    }
    //m_LightShadowTempBuffer.EndRendering(gfxContext);

    gfxContext.TransitionResource(m_LightShadowTempBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gfxContext.TransitionResource(m_LightShadowArray, D3D12_RESOURCE_STATE_COPY_DEST);

    gfxContext.CopySubresource(m_LightShadowArray, LightIndex, m_LightShadowTempBuffer, 0);

    gfxContext.TransitionResource(m_LightShadowArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ++LightIndex;
}

void Sponza::RenderScene(
    GraphicsContext& gfxContext,
    const Camera& camera,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissor,
    bool skipDiffusePass,
    bool skipShadowMap)
{
    Renderer::UpdateGlobalDescriptors();

    uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();

    float costheta = cosf(m_SunOrientation);
    float sintheta = sinf(m_SunOrientation);
    float cosphi = cosf(m_SunInclination * 3.14159f * 0.5f);
    float sinphi = sinf(m_SunInclination * 3.14159f * 0.5f);
    m_SunDirection = Normalize(Vector3( costheta * cosphi, sinphi, sintheta * cosphi ));

    // Voxelization
    VXGIVoxelize(gfxContext, camera, viewport, scissor);
    VXGIResolve(gfxContext, camera, viewport, scissor);

    __declspec(align(16)) struct
    {
        Vector3 sunDirection;
        Vector3 sunLight;
        Vector3 ambientLight;
        float ShadowTexelSize[4];

        float InvTileDim[4];
        uint32_t TileCount[4];
        uint32_t FirstLightIndex[4];

		uint32_t FrameIndexMod2;
    } psConstants;

    psConstants.sunDirection = m_SunDirection;
    psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
    psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
    psConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
    psConstants.InvTileDim[0] = 1.0f / Lighting::LightGridDim;
    psConstants.InvTileDim[1] = 1.0f / Lighting::LightGridDim;
    psConstants.TileCount[0] = Math::DivideByMultiple(g_SceneColorBuffer.GetWidth(), Lighting::LightGridDim);
    psConstants.TileCount[1] = Math::DivideByMultiple(g_SceneColorBuffer.GetHeight(), Lighting::LightGridDim);
    psConstants.FirstLightIndex[0] = Lighting::m_FirstConeLight;
    psConstants.FirstLightIndex[1] = Lighting::m_FirstConeShadowedLight;
	psConstants.FrameIndexMod2 = FrameIndex;

    // Set the default state for command lists
    auto& pfnSetupGraphicsState = [&](void)
    {
        gfxContext.SetRootSignature(Renderer::m_RootSig);
        gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());
        gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gfxContext.SetIndexBuffer(m_Model.GetIndexBuffer());
        gfxContext.SetVertexBuffer(0, m_Model.GetVertexBuffer());
    };
	
	pfnSetupGraphicsState();

    RenderLightShadows(gfxContext, camera);

    {
        ScopedTimer _prof(L"Z PrePass", gfxContext);

        gfxContext.SetDynamicConstantBufferView(Renderer::kMaterialConstants, sizeof(psConstants), &psConstants);

        {
            ScopedTimer _prof2(L"Opaque", gfxContext);
            {
                gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
                gfxContext.ClearDepth(g_SceneDepthBuffer);
                gfxContext.SetPipelineState(m_DepthPSO);
                gfxContext.SetDepthStencilTarget(g_SceneDepthBuffer.GetDSV());
                gfxContext.SetViewportAndScissor(viewport, scissor);
            }
            RenderObjects(gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), kOpaque );
        }

        {
            ScopedTimer _prof2(L"Cutout", gfxContext);
            {
                gfxContext.SetPipelineState(m_CutoutDepthPSO);
            }
            RenderObjects(gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), kCutout );
        }
    }

    SSAO::Render(gfxContext, camera);

    if (!skipDiffusePass)
    {
        Lighting::FillLightGrid(gfxContext, camera);

        if (!SSAO::DebugDraw)
        {
            ScopedTimer _prof(L"Main Render", gfxContext);
            {
                gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
                gfxContext.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
                gfxContext.ClearColor(g_SceneColorBuffer);
            }
        }
    }

    if (!skipShadowMap)
    {
        if (!SSAO::DebugDraw)
        {
            pfnSetupGraphicsState();
            {
                ScopedTimer _prof2(L"Render Shadow Map", gfxContext);

                m_SunShadow.UpdateMatrix(-m_SunDirection, Vector3(0, -500.0f, 0), Vector3(ShadowDimX, ShadowDimY, ShadowDimZ),
                    (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

                g_ShadowBuffer.BeginRendering(gfxContext);
                gfxContext.SetPipelineState(m_ShadowPSO);
                RenderObjects(gfxContext, m_SunShadow.GetViewProjMatrix(), camera.GetPosition(), kOpaque);
                gfxContext.SetPipelineState(m_CutoutShadowPSO);
                RenderObjects(gfxContext, m_SunShadow.GetViewProjMatrix(), camera.GetPosition(), kCutout);
                g_ShadowBuffer.EndRendering(gfxContext);
            }
        }
    }

    if (!skipDiffusePass)
    {
        if (!SSAO::DebugDraw)
        {
            if (SSAO::AsyncCompute)
            {
                gfxContext.Flush();
                pfnSetupGraphicsState();

                // Make the 3D queue wait for the Compute queue to finish SSAO
                g_CommandManager.GetGraphicsQueue().StallForProducer(g_CommandManager.GetComputeQueue());
            }

            {
                ScopedTimer _prof2(L"Render Color", gfxContext);

                gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

                gfxContext.SetDescriptorTable(Renderer::kCommonSRVs, Renderer::m_CommonTextures);
                gfxContext.SetDynamicConstantBufferView(Renderer::kMaterialConstants, sizeof(psConstants), &psConstants);

                {
                    gfxContext.SetPipelineState(m_ModelPSO);
                    gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[]{ g_SceneColorBuffer.GetRTV(), g_SceneNormalBuffer.GetRTV() };
                    gfxContext.SetRenderTargets(ARRAYSIZE(rtvs), rtvs, g_SceneDepthBuffer.GetDSV_DepthReadOnly());
                    gfxContext.SetViewportAndScissor(viewport, scissor);
                }
                RenderObjects( gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), Sponza::kOpaque );

                gfxContext.SetPipelineState(m_CutoutModelPSO);
                RenderObjects( gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), Sponza::kCutout );
            }
        }
    }
}
