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

// From Model
#include "ModelH3D.h"

// From ModelViewer
#include "LightManager.h"

// VXGI
#include "Math/XPrimitive.h"

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

namespace VXGI
{
    DescriptorHandle m_CommonBuffers;
    DescriptorHandle m_CommonUAVs;

    class InnerScene
    {
    public:
        // Voxel GI resources:
        struct VXGI
        {
            uint32_t res = 64;
            float rayStepSize = 1;
            float maxDistance = 100.0f;

            struct ClipMap
            {
                float voxelsize = 0.125;
                XMFLOAT3 center = XMFLOAT3(0, 0, 0);
                XMINT3 offsetfromPrevFrame = XMINT3(0, 0, 0);
                XMFLOAT3 extents = XMFLOAT3(0, 0, 0);
            } clipmaps[VXGI_CLIPMAP_COUNT];

            uint32_t clipmap_to_update = 0;

            ByteAddressBuffer m_xVoxelizer;

            VolumeBuffer radiance;
            VolumeBuffer prev_radiance;
            VolumeBuffer render_atomic;
            VolumeBuffer sdf;
            VolumeBuffer sdf_temp;
            mutable bool pre_clear = true;
        } vxgi;

        ByteAddressBuffer m_xFrame;
        ByteAddressBuffer m_xCamera;

        ModelH3D m_Model;
        std::vector<bool> m_pMaterialIsCutout;

        Vector3 m_SunDirection;
        ShadowCamera m_SunShadow;

    public:
        void Startup()
        {
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

            if (vxgi.m_xVoxelizer.GetResource() == nullptr)
            {
                vxgi.m_xVoxelizer = ByteAddressBuffer::CreateCBVReady(L"g_xVoxelizerCPU", Math::AlignUp(sizeof(VoxelizerCB), 256), nullptr);
            }
            if (m_xFrame.GetResource() == nullptr)
            {
                m_xFrame = ByteAddressBuffer::CreateCBVReady(L"g_xFrame", Math::AlignUp(sizeof(FrameCB), 256), nullptr);
            }
            if (m_xCamera.GetResource() == nullptr)
            {
                m_xCamera = ByteAddressBuffer::CreateCBVReady(L"g_xCamera", Math::AlignUp(sizeof(CameraCB), 256), nullptr);
            }

            if (vxgi.radiance.GetResource() == nullptr)
            {
                vxgi.radiance.Create(L"vxgi.radiance", vxgi.res * (6 + DIFFUSE_CONE_COUNT), vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
                vxgi.prev_radiance.Create(L"vxgi.prev_radiance", vxgi.res * (6 + DIFFUSE_CONE_COUNT), vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
                vxgi.pre_clear = true;
            }
            if (vxgi.render_atomic.GetResource() == nullptr)
            {
                vxgi.render_atomic.Create(L"vxgi.render_atomic", vxgi.res * 6, vxgi.res, vxgi.res * VOXELIZATION_CHANNEL_COUNT, 1, DXGI_FORMAT_R32_UINT);
            }
            if (vxgi.sdf.GetResource() == nullptr)
            {
                vxgi.sdf.Create(L"vxgi.sdf", vxgi.res, vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16_FLOAT);
                vxgi.sdf_temp.Create(L"vxgi.sdf_temp", vxgi.res, vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16_FLOAT);
            }

            if (m_CommonBuffers.IsNull())
            {
                m_CommonBuffers = Renderer::s_TextureHeap.Alloc(10);

                uint32_t DestCount = 3;
                uint32_t SourceCounts[] = { 1, 1, 1 };
                D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
                {
                    vxgi.m_xVoxelizer.GetCBV(),
                    m_xFrame.GetCBV(),
                    m_xCamera.GetCBV()
                };
                g_Device->CopyDescriptors(1, &m_CommonBuffers, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            if (m_CommonUAVs.IsNull())
            {
                m_CommonUAVs = Renderer::s_TextureHeap.Alloc(10);

                uint32_t DestCount = 1;
                uint32_t SourceCounts[] = { 1 };
                D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
                {
                    vxgi.render_atomic.GetUAV()
                };
                g_Device->CopyDescriptors(1, &m_CommonUAVs, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }

        }

        void Destroy()
        {
            m_Model.Clear();

            m_xFrame.Destroy();
            m_xCamera.Destroy();

            vxgi.m_xVoxelizer.Destroy();
            vxgi.radiance.Destroy();
            vxgi.prev_radiance.Destroy();
            vxgi.render_atomic.Destroy();
            vxgi.sdf.Destroy();
            vxgi.sdf_temp.Destroy();
        }

        void Update(CommandContext& context, const Camera& camera)
        {
            vxgi.clipmap_to_update = (vxgi.clipmap_to_update + 1) % VXGI_CLIPMAP_COUNT;

            // VXGI volume update:
            {
                Vector3 Eye = camera.GetPosition();

                VXGI::ClipMap& clipmap = vxgi.clipmaps[vxgi.clipmap_to_update];
                clipmap.voxelsize = vxgi.clipmaps[0].voxelsize * (1u << vxgi.clipmap_to_update);
                const float texelSize = clipmap.voxelsize * 2;
                XMFLOAT3 center = XMFLOAT3(std::floor(Eye.GetX() / texelSize) * texelSize,
                    std::floor(Eye.GetY() / texelSize) * texelSize,
                    std::floor(Eye.GetZ() / texelSize) * texelSize);
                clipmap.offsetfromPrevFrame.x = int((clipmap.center.x - center.x) / texelSize);
                clipmap.offsetfromPrevFrame.y = -int((clipmap.center.y - center.y) / texelSize);
                clipmap.offsetfromPrevFrame.z = int((clipmap.center.z - center.z) / texelSize);
                clipmap.center = center;
                XMFLOAT3 extents = XMFLOAT3(vxgi.res * clipmap.voxelsize, vxgi.res * clipmap.voxelsize,
                    vxgi.res * clipmap.voxelsize);
                if (extents.x != clipmap.extents.x || extents.y != clipmap.extents.y || extents.z != clipmap.extents.z)
                {
                    vxgi.pre_clear = true;
                }
                clipmap.extents = extents;
            }

            // Update voxelizer constant buffer
            {
                const VXGI::ClipMap& clipmap = vxgi.clipmaps[vxgi.clipmap_to_update];

                VoxelizerCB cb;
                cb.offsetfromPrevFrame = clipmap.offsetfromPrevFrame;
                cb.clipmap_index = vxgi.clipmap_to_update;

                context.WriteBuffer(vxgi.m_xVoxelizer, 0, &cb, sizeof(cb));
                context.TransitionResource(vxgi.m_xVoxelizer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            }

            // Update frame constant buffer
            {
                FrameCB cb;

                cb.options = 0;
                cb.time = 0;
                cb.time_previous = 0;
                cb.delta_time = 0;

                cb.frame_count = 0;
                cb.temporalaa_samplerotation = 0;
                cb.texture_shadowatlas_index = 0;
                cb.texture_shadowatlas_transparent_index = 0;

                cb.vxgi.resolution = vxgi.res;
                cb.vxgi.resolution_rcp = 1.0f / vxgi.res;
                cb.vxgi.stepsize = vxgi.rayStepSize;
                cb.vxgi.max_distance = vxgi.maxDistance;

                for (uint32_t i = 0; i < VXGI_CLIPMAP_COUNT; i++)
                {
                    cb.vxgi.clipmaps[i].center = vxgi.clipmaps[i].center;
                    cb.vxgi.clipmaps[i].voxelSize = vxgi.clipmaps[i].voxelsize;
                }

                context.WriteBuffer(m_xFrame, 0, &cb, sizeof(cb));
                context.TransitionResource(m_xFrame, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            }

            // Update camera constant buffer
            {
                CameraCB cb;
                cb.init();

                XMMATRIX viewProjection = camera.GetViewProjMatrix();
                XMMATRIX invVP = XMMatrixInverse(nullptr, viewProjection);

                ShaderFrustumCorners frustum_corners;

                XMStoreFloat4(&frustum_corners.cornersNEAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), invVP)); // near topleft
                XMStoreFloat4(&frustum_corners.cornersNEAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), invVP));  // near topright
                XMStoreFloat4(&frustum_corners.cornersNEAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), invVP)); // near bottomleft
                XMStoreFloat4(&frustum_corners.cornersNEAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), invVP));  // near bottomright

                XMStoreFloat4(&frustum_corners.cornersFAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 0, 1), invVP)); // far topleft
                XMStoreFloat4(&frustum_corners.cornersFAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 0, 1), invVP));  // far topright
                XMStoreFloat4(&frustum_corners.cornersFAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 0, 1), invVP)); // far bottomleft
                XMStoreFloat4(&frustum_corners.cornersFAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 0, 1), invVP));  // far bottomright

                XMFLOAT4 frustumPlanes[6];

                XMFLOAT4X4 vp;
                XMStoreFloat4x4(&vp, viewProjection);

                // Left plane
                frustumPlanes[0] = XMFLOAT4(
                    vp._14 + vp._11,
                    vp._24 + vp._21,
                    vp._34 + vp._31,
                    vp._44 + vp._41);

                // Right plane
                frustumPlanes[1] = XMFLOAT4(
                    vp._14 - vp._11,
                    vp._24 - vp._21,
                    vp._34 - vp._31,
                    vp._44 - vp._41);

                // Bottom plane
                frustumPlanes[2] = XMFLOAT4(
                    vp._14 + vp._12,
                    vp._24 + vp._22,
                    vp._34 + vp._32,
                    vp._44 + vp._42);

                // Top plane
                frustumPlanes[3] = XMFLOAT4(
                    vp._14 - vp._12,
                    vp._24 - vp._22,
                    vp._34 - vp._32,
                    vp._44 - vp._42);

                // Near plane
                frustumPlanes[4] = XMFLOAT4(
                    vp._13,
                    vp._23,
                    vp._33,
                    vp._43);

                // Far plane
                frustumPlanes[5] = XMFLOAT4(
                    vp._14 - vp._13,
                    vp._24 - vp._23,
                    vp._34 - vp._33,
                    vp._44 - vp._43);

                for (int i = 0; i < 6; i++) {
                    XMVECTOR planeVec = XMLoadFloat4(&frustumPlanes[i]);
                    planeVec = XMPlaneNormalize(planeVec);
                    XMStoreFloat4(&frustumPlanes[i], planeVec);
                }

                ShaderCamera scamera;
                XMStoreFloat4x4(&scamera.inverse_view_projection, invVP);

                for (int i = 0; i < 6; i++) {
                    scamera.frustum.planes[i] = frustumPlanes[i];
                }

                scamera.frustum_corners = frustum_corners;

                XMStoreFloat4x4(&scamera.view_projection, viewProjection);

                XMVECTOR camPos = camera.GetPosition();
                XMStoreFloat3(&scamera.position, camPos);
                scamera.output_index = 0;

                XMVECTOR camForwardVec = camera.GetForwardVec();
                XMVECTOR camUpVec = camera.GetUpVec();
                XMStoreFloat3(&scamera.forward, camForwardVec);
                XMStoreFloat3(&scamera.up, camUpVec);

                scamera.z_near = camera.GetNearClip();
                scamera.z_far = camera.GetFarClip();
                scamera.z_near_rcp = 1.0f / camera.GetNearClip();
                scamera.z_far_rcp = 1.0f / camera.GetFarClip();
                scamera.z_range = abs(scamera.z_far - scamera.z_near);
                scamera.z_range_rcp = 1.0f / std::max(0.0001f, scamera.z_range);

                XMMATRIX view = camera.GetViewMatrix();
                XMMATRIX proj = camera.GetProjMatrix();
                XMMATRIX invView = XMMatrixInverse(nullptr, view);
                XMMATRIX invProj = XMMatrixInverse(nullptr, proj);

                XMStoreFloat4x4(&scamera.view, view);
                XMStoreFloat4x4(&scamera.projection, proj);
                XMStoreFloat4x4(&scamera.inverse_view, invView);
                XMStoreFloat4x4(&scamera.inverse_projection, invProj);

                scamera.clip_plane = XMFLOAT4(0, 0, 0, 0); // Ĭ��ֵ
                scamera.reflection_plane = XMFLOAT4(0, 0, 0, 0); // Ĭ��ֵ
                scamera.temporalaa_jitter = XMFLOAT2(0, 0);
                scamera.temporalaa_jitter_prev = XMFLOAT2(0, 0);

                XMMATRIX prevViewProj = camera.GetPreviousViewProjMatrix();
                XMStoreFloat4x4(&scamera.previous_view_projection, prevViewProj);
                XMMATRIX tempView = camera.GetViewMatrix();
                XMMATRIX tempProj = camera.GetProjMatrix();
                XMMATRIX tempInvPrevVP = XMMatrixInverse(nullptr, prevViewProj);
                XMStoreFloat4x4(&scamera.previous_view, tempView);
                XMStoreFloat4x4(&scamera.previous_projection, tempProj);
                XMStoreFloat4x4(&scamera.previous_inverse_view_projection, tempInvPrevVP);
                XMStoreFloat4x4(&scamera.reflection_view_projection, viewProjection);
                XMStoreFloat4x4(&scamera.reflection_inverse_view_projection, invVP);
                XMMATRIX tempReproj = prevViewProj * XMMatrixInverse(nullptr, viewProjection);
                XMStoreFloat4x4(&scamera.reprojection, tempReproj);

                scamera.aperture_shape = XMFLOAT2(1, 1);
                scamera.aperture_size = 0.0f;
                scamera.focal_length = 1.0f;

                uint32_t width = g_SceneColorBuffer.GetWidth();
                uint32_t height = g_SceneColorBuffer.GetHeight();

                scamera.canvas_size = XMFLOAT2((float)width, (float)height);
                scamera.canvas_size_rcp.x = 1.0f / std::max(0.0001f, scamera.canvas_size.x);
                scamera.canvas_size_rcp.y = 1.0f / std::max(0.0001f, scamera.canvas_size.y);

                scamera.internal_resolution = XMUINT2(width, height);
                scamera.internal_resolution_rcp.x = 1.0f / scamera.internal_resolution.x;
                scamera.internal_resolution_rcp.y = 1.0f / scamera.internal_resolution.y;

                scamera.scissor = XMUINT4(0, 0, width, height);
                scamera.scissor_uv = XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);

                cb.cameras[0] = scamera;

                context.WriteBuffer(m_xCamera, 0, &cb, sizeof(cb));
                context.TransitionResource(m_xCamera, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            }

            if (vxgi.pre_clear)
            {
                context.TransitionResource(vxgi.render_atomic, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                context.TransitionResource(vxgi.prev_radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                context.TransitionResource(vxgi.radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                context.TransitionResource(vxgi.sdf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                context.TransitionResource(vxgi.sdf_temp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                {
                    GraphicsContext& gfxContext = context.GetGraphicsContext();
                    gfxContext.ClearUAV(vxgi.render_atomic);
                    gfxContext.ClearUAV(vxgi.prev_radiance);
                    gfxContext.ClearUAV(vxgi.radiance);
                    gfxContext.ClearUAV(vxgi.sdf);
                    gfxContext.ClearUAV(vxgi.sdf_temp);
                }

                vxgi.pre_clear = false;
            }
        }
    };
}

namespace Sponza
{
    void RenderLightShadows(GraphicsContext& gfxContext, const Camera& camera);

    enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };
    void RenderObjects( GraphicsContext& Context, const Matrix4& ViewProjMat, const Vector3& viewerPos, eObjectFilter Filter = kAll );

    void VoxelizeObjects( GraphicsContext& Context, const Matrix4& ViewProjMat, const Vector3& viewerPos, eObjectFilter Filter = kAll );

    GraphicsPSO m_DepthPSO = { (L"Sponza: Depth PSO") };
    GraphicsPSO m_CutoutDepthPSO = { (L"Sponza: Cutout Depth PSO") };
    GraphicsPSO m_ModelPSO = { (L"Sponza: Color PSO") };
    GraphicsPSO m_CutoutModelPSO = { (L"Sponza: Cutout Color PSO") };
    GraphicsPSO m_ShadowPSO(L"Sponza: Shadow PSO");
    GraphicsPSO m_CutoutShadowPSO(L"Sponza: Cutout Shadow PSO");
    GraphicsPSO m_VoxelPSO(L"Sponza: Voxel PSO");
    GraphicsPSO m_CutoutVoxelPSO(L"Sponza: Cutout Voxel PSO");

    VXGI::InnerScene m_Scene;

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

    m_Scene.Startup();

    ParticleEffects::InitFromJSON(L"Sponza/particles.json");

    float modelRadius = Length(m_Scene.m_Model.GetBoundingBox().GetDimensions()) * 0.5f;
    const Vector3 eye = m_Scene.m_Model.GetBoundingBox().GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
    Camera.SetEyeAtUp( eye, Vector3(kZero), Vector3(kYUnitVector) );

    Lighting::CreateRandomLights(m_Scene.m_Model.GetBoundingBox().GetMin(), m_Scene.m_Model.GetBoundingBox().GetMax());
}

const ModelH3D& Sponza::GetModel()
{
    return m_Scene.m_Model;
}

void Sponza::Cleanup( void )
{
    m_Scene.Destroy();
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
    vsConstants.modelToShadow = m_Scene.m_SunShadow.GetShadowMatrix();
    XMStoreFloat3(&vsConstants.viewerPos, viewerPos);

    gfxContext.SetDynamicConstantBufferView(Renderer::kMeshConstants, sizeof(vsConstants), &vsConstants);

    __declspec(align(16)) uint32_t materialIdx = 0xFFFFFFFFul;

    uint32_t VertexStride = m_Scene.m_Model.GetVertexStride();

    for (uint32_t meshIndex = 0; meshIndex < m_Scene.m_Model.GetMeshCount(); meshIndex++)
    {
        const ModelH3D::Mesh& mesh = m_Scene.m_Model.GetMesh(meshIndex);

        uint32_t indexCount = mesh.indexCount;
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

        if (mesh.materialIndex != materialIdx)
        {
            if (m_Scene.m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kCutout) ||
                !m_Scene.m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kOpaque) )
                continue;

            materialIdx = mesh.materialIndex;
            gfxContext.SetDescriptorTable(Renderer::kMaterialSRVs, m_Scene.m_Model.GetSRVs(materialIdx));

            gfxContext.SetDynamicConstantBufferView(Renderer::kCommonCBV, sizeof(uint32_t), &materialIdx);
        }

        gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
    }
}

void Sponza::VoxelizeObjects(GraphicsContext& gfxContext, const Matrix4& ViewProjMat, const Vector3& viewerPos, eObjectFilter Filter)
{
    __declspec(align(16)) uint32_t materialIdx = 0xFFFFFFFFul;

    uint32_t VertexStride = m_Scene.m_Model.GetVertexStride();

    for (uint32_t meshIndex = 0; meshIndex < m_Scene.m_Model.GetMeshCount(); meshIndex++)
    {
        const ModelH3D::Mesh& mesh = m_Scene.m_Model.GetMesh(meshIndex);

        uint32_t indexCount = mesh.indexCount;
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

        if (mesh.materialIndex != materialIdx)
        {
            if (m_Scene.m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kCutout) ||
                !m_Scene.m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kOpaque))
                continue;

            materialIdx = mesh.materialIndex;
            gfxContext.SetDescriptorTable(4, m_Scene.m_Model.GetSRVs(materialIdx));

            //gfxContext.SetDynamicConstantBufferView(Renderer::kCommonCBV, sizeof(uint32_t), &materialIdx);
        }

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

	m_Scene.Update(gfxContext, camera);

    uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();

    float costheta = cosf(m_SunOrientation);
    float sintheta = sinf(m_SunOrientation);
    float cosphi = cosf(m_SunInclination * 3.14159f * 0.5f);
    float sinphi = sinf(m_SunInclination * 3.14159f * 0.5f);
    m_Scene.m_SunDirection = Normalize(Vector3( costheta * cosphi, sinphi, sintheta * cosphi ));

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

    psConstants.sunDirection = m_Scene.m_SunDirection;
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
        gfxContext.SetIndexBuffer(m_Scene.m_Model.GetIndexBuffer());
        gfxContext.SetVertexBuffer(0, m_Scene.m_Model.GetVertexBuffer());
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

                m_Scene.m_SunShadow.UpdateMatrix(-m_Scene.m_SunDirection, Vector3(0, -500.0f, 0), Vector3(ShadowDimX, ShadowDimY, ShadowDimZ),
                    (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

                g_ShadowBuffer.BeginRendering(gfxContext);
                gfxContext.SetPipelineState(m_ShadowPSO);
                RenderObjects(gfxContext, m_Scene.m_SunShadow.GetViewProjMatrix(), camera.GetPosition(), kOpaque);
                gfxContext.SetPipelineState(m_CutoutShadowPSO);
                RenderObjects(gfxContext, m_Scene.m_SunShadow.GetViewProjMatrix(), camera.GetPosition(), kCutout);
                g_ShadowBuffer.EndRendering(gfxContext);
            }
        }
    }

    {
        gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        
        {
            D3D12_VIEWPORT m_VoxelViewport;
            m_VoxelViewport.Width = (float)m_Scene.vxgi.res;
            m_VoxelViewport.Height = (float)m_Scene.vxgi.res;
            m_VoxelViewport.MinDepth = 0.0f;
            m_VoxelViewport.MaxDepth = 1.0f;

            D3D12_RECT m_VoxelScissor;
            m_VoxelScissor.left = 0;
            m_VoxelScissor.top = 0;
            m_VoxelScissor.right = (LONG)m_Scene.vxgi.res;
            m_VoxelScissor.bottom = (LONG)m_Scene.vxgi.res;

            __declspec(align(16)) struct VSConstants
            {
                XMFLOAT4X4 modelMatrix;
                XMFLOAT4X4 modelMatrixIT;
                int cameraIndex;
            } mVSConstants;
            // For VXGI voxelization, we typically use identity matrix for static models like Sponza
            mVSConstants.modelMatrix = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            mVSConstants.modelMatrixIT = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            mVSConstants.cameraIndex = 0;

            uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();

            __declspec(align(16)) struct PSConstants
            {
                Vector3 SunDirection;
                Vector3 SunColor;
                Vector3 AmbientColor;
                float ShadowTexelSize[4];
                float InvTileDim[4];
                uint32_t TileCount[4];
                uint32_t FirstLightIndex[4];
                uint32_t FrameIndexMod2;
                Matrix4 modelToShadow;
                XMFLOAT3 viewerPos;
            } mPSConstants;

            mPSConstants.SunDirection = m_Scene.m_SunDirection;
            mPSConstants.SunColor = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
            mPSConstants.AmbientColor = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
            mPSConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
            mPSConstants.InvTileDim[0] = 1.0f / Lighting::LightGridDim;
            mPSConstants.InvTileDim[1] = 1.0f / Lighting::LightGridDim;
            mPSConstants.TileCount[0] = Math::DivideByMultiple(g_SceneColorBuffer.GetWidth(), Lighting::LightGridDim);
            mPSConstants.TileCount[1] = Math::DivideByMultiple(g_SceneColorBuffer.GetHeight(), Lighting::LightGridDim);
            mPSConstants.FirstLightIndex[0] = Lighting::m_FirstConeLight;
            mPSConstants.FirstLightIndex[1] = Lighting::m_FirstConeShadowedLight;
            mPSConstants.FrameIndexMod2 = FrameIndex;
            mPSConstants.modelToShadow = m_Scene.m_SunShadow.GetShadowMatrix();
            XMStoreFloat3(&mPSConstants.viewerPos, camera.GetPosition());

            gfxContext.SetRootSignature(Renderer::m_VoxelRootSig);
            gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());
            gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            gfxContext.SetIndexBuffer(m_Scene.m_Model.GetIndexBuffer());
            gfxContext.SetVertexBuffer(0, m_Scene.m_Model.GetVertexBuffer());

            ScopedTimer _prof(L"VoxelPass", gfxContext);

            gfxContext.SetDynamicConstantBufferView(1, sizeof(mVSConstants), &mVSConstants);
            gfxContext.SetDynamicConstantBufferView(2, sizeof(mPSConstants), &mPSConstants);

            gfxContext.SetDescriptorTable(3, VXGI::m_CommonBuffers);

            gfxContext.SetDescriptorTable(5, Renderer::m_CommonTextures);

            gfxContext.SetDescriptorTable(6, VXGI::m_CommonUAVs);

            {
                ScopedTimer _prof2(L"OpaqueVoxel", gfxContext);
                {
                    gfxContext.SetPipelineState(m_VoxelPSO);
                    gfxContext.SetRenderTargets(0, nullptr);
                    gfxContext.SetViewportAndScissor(m_VoxelViewport, m_VoxelScissor);
                }
                VoxelizeObjects(gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), kOpaque);
            }

            {
                ScopedTimer _prof2(L"CutoutVoxel", gfxContext);
                {
                    gfxContext.SetPipelineState(m_CutoutVoxelPSO);
                    gfxContext.SetRenderTargets(0, nullptr);
                    gfxContext.SetViewportAndScissor(m_VoxelViewport, m_VoxelScissor);
                }
                VoxelizeObjects(gfxContext, camera.GetViewProjMatrix(), camera.GetPosition(), kCutout);
            }

            gfxContext.TransitionResource(m_Scene.vxgi.render_atomic, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            gfxContext.TransitionResource(m_Scene.vxgi.prev_radiance, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        }

        {
            ScopedTimer _prof(L"Temporal Blend Voxels", gfxContext);




        }

        {
            ScopedTimer _prof(L"SDF Jump Flood", gfxContext);


        }

        {
            ScopedTimer _prof(L"Resolve", gfxContext);


            {
                ScopedTimer _prof2(L"Diffuse", gfxContext);


            }


            {
                ScopedTimer _prof2(L"Specular", gfxContext);


            }
        }

    };

    pfnSetupGraphicsState();

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
