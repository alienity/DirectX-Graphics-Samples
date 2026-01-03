#include "GraphicsCore.h"
#include "BufferManager.h"
#include "Camera.h"
#include "ShadowCamera.h"
#include "CommandContext.h"
#include "TemporalEffects.h"
#include "VoxelConeTracer.h"
#include "Renderer.h"

// From Model
#include <cmath>

#include "ModelH3D.h"

#include "CompiledShaders/VXGIVoxelizationVS.h"
#include "CompiledShaders/VXGIVoxelizationGS.h"
#include "CompiledShaders/VXGIVoxelizationPS.h"


using namespace Graphics;
using namespace Math;

namespace VCT
{
    LPCWSTR StringToLPCWSTR(const std::string& str)
    {
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
        static std::vector<wchar_t> buffer(size_needed + 1);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), buffer.data(), size_needed);
        buffer[size_needed] = 0;
        return buffer.data();
    }

    std::wstring ConvertToWString(const std::string& str)
    {
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }

    void VolumeBuffer::Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t NumMips, DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMem)
    {
        NumMips = (NumMips == 0 ? ComputeNumMips(Width, Height) : NumMips);
        D3D12_RESOURCE_FLAGS Flags = CombineResourceFlags();
        D3D12_RESOURCE_DESC ResourceDesc = DescribeTex3D(Width, Height, Depth, NumMips, Format, Flags);

        ResourceDesc.SampleDesc.Count = m_FragmentCount;
        ResourceDesc.SampleDesc.Quality = 0;

        D3D12_CLEAR_VALUE ClearValue = {};
        ClearValue.Format = Format;
        ClearValue.Color[0] = m_ClearColor.R();
        ClearValue.Color[1] = m_ClearColor.G();
        ClearValue.Color[2] = m_ClearColor.B();
        ClearValue.Color[3] = m_ClearColor.A();

        CreateTextureResource(Graphics::g_Device, Name, ResourceDesc, ClearValue, VidMem);
        CreateDerived3DViews(Graphics::g_Device, Format, Depth, NumMips);
    }

    void VolumeBuffer::CreateDerived3DViews(ID3D12Device* Device, DXGI_FORMAT Format, uint32_t DpethOrArraySize, uint32_t NumMips = 1)
    {
        ASSERT(DpethOrArraySize == 1 || NumMips == 1, "We don't support auto-mips on texture arrays");

        m_NumMipMaps = NumMips - 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};

        UAVDesc.Format = GetUAVFormat(Format);
        SRVDesc.Format = Format;
        SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        UAVDesc.Texture3D.MipSlice = 0;
        UAVDesc.Texture3D.FirstWSlice = 0;
        UAVDesc.Texture3D.WSize = UINT_MAX;

        SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        SRVDesc.Texture3D.MipLevels = NumMips;
        SRVDesc.Texture3D.MostDetailedMip = 0;
        SRVDesc.Texture3D.ResourceMinLODClamp = 0;

        if (m_SRVHandle.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
        {
            m_RTVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            m_SRVHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        ID3D12Resource* Resource = m_pResource.Get();

        // Create the shader resource view
        Device->CreateShaderResourceView(Resource, &SRVDesc, m_SRVHandle);

        if (m_FragmentCount > 1)
            return;

        // Create the UAVs for each mip level (RWTexture2D)
        for (uint32_t i = 0; i < NumMips; ++i)
        {
            if (m_UAVHandle[i].ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
                m_UAVHandle[i] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            Device->CreateUnorderedAccessView(Resource, nullptr, &UAVDesc, m_UAVHandle[i]);

            UAVDesc.Texture2D.MipSlice++;
        }
    }

    D3D12_RESOURCE_DESC VolumeBuffer::DescribeTex3D(uint32_t Width, uint32_t Height, uint32_t DepthOrArraySize, uint32_t NumMips, DXGI_FORMAT Format, UINT Flags)
    {
        m_Width = Width;
        m_Height = Height;
        m_ArraySize = DepthOrArraySize;
        m_Format = Format;

        D3D12_RESOURCE_DESC Desc = {};
        Desc.Alignment = 0;
        Desc.DepthOrArraySize = (UINT16)DepthOrArraySize;
        Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        Desc.Flags = (D3D12_RESOURCE_FLAGS)Flags;
        Desc.Format = GetBaseFormat(Format);
        Desc.Height = (UINT)Height;
        Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        Desc.MipLevels = (UINT16)NumMips;
        Desc.SampleDesc.Count = 1;
        Desc.SampleDesc.Quality = 0;
        Desc.Width = (UINT64)Width;
        return Desc;
    }

    void OrthoVoxelCamera::UpdateMatrix(Vector3 ForwardDirection, Vector3 CameraCenter, Vector3 VoxelBounds,
                                        float VoxelSize)
    {
        SetLookDirection(ForwardDirection, Vector3(kZUnitVector));

        // Converts world units to texel units so we can quantize the camera position to whole texel units
        Vector3 RcpDimensions = Recip(VoxelBounds);
        Vector3 QuantizeScale = Vector3(VoxelSize, VoxelSize, VoxelSize);

        //
        // Recenter the camera at the quantized position
        //

        // Transform to view space
        CameraCenter = ~GetRotation() * CameraCenter;
        // Scale to texel units, truncate fractional part, and scale back to world units
        CameraCenter = Floor(CameraCenter * QuantizeScale) / QuantizeScale;
        // Transform back into world space
        CameraCenter = GetRotation() * CameraCenter;

        SetPosition(CameraCenter);

        SetProjMatrix(Matrix4::MakeScale(Vector3(2.0f, 2.0f, 1.0f) * RcpDimensions));

        Update();

        // Transform from clip space to texture space
        m_VoxelMatrix = Matrix4(AffineTransform(Matrix3::MakeScale(0.5f, -0.5f, 1.0f), Vector3(0.5f, 0.5f, 0.0f))) *
            m_ViewProjMatrix;
    }
}

namespace VCT
{
    void SceneGI::Destroy()
    {
        vxgi.radiance.Destroy();
        vxgi.prev_radiance.Destroy();
        vxgi.render_atomic.Destroy();
        vxgi.sdf.Destroy();
        vxgi.sdf_temp.Destroy();
    }

    void SceneGI::Update(const Camera& camera)
    {
        if (vxgi.radiance.GetResource() != nullptr)
        {
            vxgi.radiance.Create(L"vxgi.radiance", vxgi.res * (6 + DIFFUSE_CONE_COUNT), vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
            vxgi.prev_radiance.Create(L"vxgi.prev_radiance", vxgi.res * (6 + DIFFUSE_CONE_COUNT), vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
            vxgi.pre_clear = true;
        }
        if (vxgi.render_atomic.GetResource() != nullptr)
        {
            vxgi.render_atomic.Create(L"vxgi.render_atomic", vxgi.res * 6, vxgi.res, vxgi.res * VOXELIZATION_CHANNEL_COUNT, 1, DXGI_FORMAT_R32_UINT);
        }
        if (vxgi.sdf.GetResource() != nullptr)
        {
            vxgi.sdf.Create(L"vxgi.sdf", vxgi.res, vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16_FLOAT);
            vxgi.sdf_temp.Create(L"vxgi.sdf_temp", vxgi.res, vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16_FLOAT);
        }
        vxgi.clipmap_to_update = (vxgi.clipmap_to_update + 1) % VXGI_CLIPMAP_COUNT;

        // VXGI volume update:
        //	Note: this is using camera that the scene is associated with
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
    }
}

namespace VCT
{
    enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };

    ModelH3D m_Model;
    std::vector<bool> m_pMaterialIsCutout;

    OrthoVoxelCamera m_OrthoCamera;

    BoolVar Enable("Graphics/VCT/Enable", true);
    BoolVar DebugDraw("Graphics/VCT/DebugDraw", false);

    SceneGI scene_gi;
    VXGIResources vxgi_resources;

    void CreateVXGIResources(VXGIResources& res, XMUINT2 resolution)
    {
        res.diffuse.Create(L"vxgi.diffuse", resolution.x, resolution.y, 1, DXGI_FORMAT_R11G11B10_FLOAT);
        res.diffuse.Create(L"vxgi.specular", resolution.x, resolution.y, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
        res.pre_clear = true;
    }

    void VXGI_Voxelize(CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
                       ModelH3D& model, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
    {
        scene_gi.Update(camera);

        const SceneGI::VXGI::ClipMap& clipmap = scene_gi.vxgi.clipmaps[scene_gi.vxgi.clipmap_to_update];

        //Primitive::AABB bbox;
        //bbox.createFromHalfWidth(clipmap.center, clipmap.extents);

        VoxelizerCB cb;
        cb.offsetfromPrevFrame = clipmap.offsetfromPrevFrame;
        cb.clipmap_index = scene_gi.vxgi.clipmap_to_update;

        GraphicsContext& gfxContext = BaseContext.GetGraphicsContext();
        //Context.SetRootSignature();

        gfxContext.SetDynamicConstantBufferView(CBSLOT_RENDERER_VOXELIZER, sizeof(VoxelizerCB), &cb);
        if (scene_gi.vxgi.pre_clear)
        {
            EngineProfiling::BeginBlock(L"Pre Clear", &gfxContext);
            {
                gfxContext.TransitionResource(scene_gi.vxgi.render_atomic, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(scene_gi.vxgi.prev_radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(scene_gi.vxgi.radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(scene_gi.vxgi.sdf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(scene_gi.vxgi.sdf_temp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.FlushResourceBarriers();
            }
            gfxContext.ClearUAV(scene_gi.vxgi.prev_radiance);
            gfxContext.ClearUAV(scene_gi.vxgi.radiance);
            gfxContext.ClearUAV(scene_gi.vxgi.sdf);
            gfxContext.ClearUAV(scene_gi.vxgi.sdf_temp);
            gfxContext.ClearUAV(scene_gi.vxgi.render_atomic);
            scene_gi.vxgi.pre_clear = false;;
        }
        else
        {
            {
                GPUBarrier barriers[] = {
                    GPUBarrier::Image(&scene.vxgi.render_atomic, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                    GPUBarrier::Image(&scene.vxgi.prev_radiance, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                    GPUBarrier::Image(&scene.vxgi.sdf, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                    GPUBarrier::Image(&scene.vxgi.sdf_temp, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                };
                device->Barrier(barriers, arraysize(barriers), cmd);
            }
            device->EventBegin("Atomic Clear", cmd);
            device->ClearUAV(&scene.vxgi.render_atomic, 0, cmd);
            device->EventEnd(cmd);

            device->EventBegin("Offset Previous Voxels", cmd);
            device->BindComputeShader(&shaders[CSTYPE_VXGI_OFFSETPREV], cmd);
            device->BindResource(&scene.vxgi.radiance, 0, cmd);
            device->BindUAV(&scene.vxgi.prev_radiance, 0, cmd);

            device->Dispatch(scene.vxgi.res / 8, scene.vxgi.res / 8, scene.vxgi.res / 8, cmd);

            device->EventEnd(cmd);

            {
                GPUBarrier barriers[] = {
                    GPUBarrier::Memory(&scene.vxgi.render_atomic),
                    GPUBarrier::Image(&scene.vxgi.radiance, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                };
                device->Barrier(barriers, arraysize(barriers), cmd);
            }
        }

        {
            device->EventBegin("Voxelize", cmd);

            Viewport vp;
            vp.width = (float)scene.vxgi.res;
            vp.height = (float)scene.vxgi.res;
            device->BindViewports(1, &vp, cmd);

            device->BindResource(&scene.vxgi.prev_radiance, 0, cmd);
            device->BindUAV(&scene.vxgi.render_atomic, 0, cmd);

            device->RenderPassBegin(nullptr, 0, cmd, RenderPassFlags::ALLOW_UAV_WRITES);
#ifdef VOXELIZATION_GEOMETRY_SHADER_ENABLED
            const uint32_t frustum_count = 1; // axis will be selected by geometry shader
#else
            const uint32_t frustum_count = 3; // just used to replicate 3 times for main axes, but not with real frustums
#endif // VOXELIZATION_GEOMETRY_SHADER_ENABLED
            RenderMeshes(vis, renderQueue, RENDERPASS_VOXELIZE, FILTER_OPAQUE, cmd, 0, frustum_count);
            device->RenderPassEnd(cmd);

            {
                GPUBarrier barriers[] = {
                    GPUBarrier::Image(&scene.vxgi.render_atomic, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                    GPUBarrier::Image(&scene.vxgi.prev_radiance, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                };
                device->Barrier(barriers, arraysize(barriers), cmd);
            }

            device->EventEnd(cmd);
        }

        {
            device->EventBegin("Temporal Blend Voxels", cmd);
            device->BindComputeShader(&shaders[CSTYPE_VXGI_TEMPORAL], cmd);
            device->BindResource(&scene.vxgi.prev_radiance, 0, cmd);
            device->BindResource(&scene.vxgi.render_atomic, 1, cmd);
            device->BindUAV(&scene.vxgi.radiance, 0, cmd);
            device->BindUAV(&scene.vxgi.sdf, 1, cmd);

            device->Dispatch(scene.vxgi.res / 8, scene.vxgi.res / 8, scene.vxgi.res / 8, cmd);

            {
                GPUBarrier barriers[] = {
                    GPUBarrier::Image(&scene.vxgi.sdf, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                    GPUBarrier::Image(&scene.vxgi.radiance, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                };
                device->Barrier(barriers, arraysize(barriers), cmd);
            }
            device->EventEnd(cmd);
        }

        {
            device->EventBegin("SDF Jump Flood", cmd);
            device->BindComputeShader(&shaders[CSTYPE_VXGI_SDF_JUMPFLOOD], cmd);

            const Texture* _write = &scene.vxgi.sdf_temp;
            const Texture* _read = &scene.vxgi.sdf;

            int passcount = (int)std::ceil(std::log2((float)scene.vxgi.res));
            for (int i = 0; i < passcount; ++i)
            {
                float jump_size = std::pow(2.0f, float(passcount - i - 1));
                device->PushConstants(&jump_size, sizeof(jump_size), cmd);

                device->BindUAV(_write, 0, cmd);
                device->BindResource(_read, 0, cmd);

                device->Dispatch(scene.vxgi.res / 8, scene.vxgi.res / 8, scene.vxgi.res / 8, cmd);

                if (i < (passcount - 1))
                {
                    {
                        GPUBarrier barriers[] = {
                            GPUBarrier::Image(_write, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                            GPUBarrier::Image(_read, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                        };
                        device->Barrier(barriers, arraysize(barriers), cmd);
                    }
                    std::swap(_read, _write);
                }
            }

            {
                GPUBarrier barriers[] = {
                    GPUBarrier::Image(_write, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                };
                device->Barrier(barriers, arraysize(barriers), cmd);
            }

            device->EventEnd(cmd);
        }
    }

    void VXGI_Resolve(
        CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
        const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
    {
        if (!GetVXGIEnabled() || !scene.vxgi.radiance.IsValid())
        {
            return;
        }

        device->EventBegin("VXGI - Resolve", cmd);
        auto range = wi::profiler::BeginRangeGPU("VXGI - Resolve", cmd);

        BindCommonResources(cmd);

        if (res.pre_clear)
        {
            res.pre_clear = false;
            {
                GPUBarrier barriers[] = {
                    GPUBarrier::Image(&res.diffuse, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                    GPUBarrier::Image(&res.specular, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                };
                device->Barrier(barriers, arraysize(barriers), cmd);
            }
            device->ClearUAV(&res.diffuse, 0, cmd);
            device->ClearUAV(&res.specular, 0, cmd);
            {
                GPUBarrier barriers[] = {
                    GPUBarrier::Image(&res.diffuse, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                    GPUBarrier::Image(&res.specular, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                };
                device->Barrier(barriers, arraysize(barriers), cmd);
            }
        }

        {
            GPUBarrier barriers[] = {
                GPUBarrier::Image(&res.diffuse, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
                GPUBarrier::Image(&res.specular, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
            };
            device->Barrier(barriers, arraysize(barriers), cmd);
        }

        {
            device->EventBegin("Diffuse", cmd);
            device->BindComputeShader(&shaders[CSTYPE_VXGI_RESOLVE_DIFFUSE], cmd);

            PostProcess postprocess;
            device->BindUAV(&res.diffuse, 0, cmd);
            postprocess.resolution.x = res.diffuse.desc.width;
            postprocess.resolution.y = res.diffuse.desc.height;
            postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
            postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
            device->PushConstants(&postprocess, sizeof(postprocess), cmd);

            uint2 dispatch_dim;
            dispatch_dim.x = postprocess.resolution.x;
            dispatch_dim.y = postprocess.resolution.y;

            device->Dispatch((dispatch_dim.x + 7u) / 8u, (dispatch_dim.y + 7u) / 8u, 1, cmd);

            device->EventEnd(cmd);
        }

        if (VXGI_REFLECTIONS_ENABLED)
        {
            device->EventBegin("Specular", cmd);
            device->BindComputeShader(&shaders[CSTYPE_VXGI_RESOLVE_SPECULAR], cmd);

            PostProcess postprocess;
            device->BindUAV(&res.specular, 0, cmd);
            postprocess.resolution.x = res.specular.desc.width;
            postprocess.resolution.y = res.specular.desc.height;
            postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
            postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
            device->PushConstants(&postprocess, sizeof(postprocess), cmd);

            uint2 dispatch_dim;
            dispatch_dim.x = postprocess.resolution.x;
            dispatch_dim.y = postprocess.resolution.y;

            device->Dispatch((dispatch_dim.x + 7u) / 8u, (dispatch_dim.y + 7u) / 8u, 1, cmd);

            device->EventEnd(cmd);
        }

        {
            GPUBarrier barriers[] = {
                GPUBarrier::Image(&res.diffuse, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
                GPUBarrier::Image(&res.specular, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
            };
            device->Barrier(barriers, arraysize(barriers), cmd);
        }

        wi::profiler::EndRange(range);
        device->EventEnd(cmd);
    }


}

namespace VCT
{
    RootSignature m_RootSig;
    GraphicsPSO m_VoxelPSO(L"Renderer: Voxelization PSO"); // Not finalized.  Used as a template.

    GraphicsPSO s_VCTVoxelizationVS(L"VCT: Voxelize VS");
    GraphicsPSO s_VCTVoxelizationPS(L"VCT: Voxelize PS");

    ComputePSO s_VCTLightInjectionCS(L"DOF: Pass 1 CS");
    ComputePSO s_VCTGenMipmapCS(L"DOF: Pass 1 CS");

    D3D12_RASTERIZER_DESC VoxelizationRasterizer;
    D3D12_BLEND_DESC VoxelizationBlendDesc;

    enum RootBindings
    {
        kMeshConstants,
        kMaterialConstants,
        kMaterialSRVs,
        kMaterialSamplers,
        kCommonSRVs,
        kCommonCBV,
        kCommoUAV,
        kSkinMatrices,

        kNumRootBindings
    };

    void Startup(Camera& camera, ModelH3D& model)
    {
        DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
        DXGI_FORMAT NormalFormat = g_SceneNormalBuffer.GetFormat();
        DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();

        D3D12_INPUT_ELEMENT_DESC vertElem[] =
        {
            {
                "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            {
                "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            {
                "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            {
                "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            {
                "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            }
        };

        SamplerDesc DefaultSamplerDesc;
        DefaultSamplerDesc.MaxAnisotropy = 8;

        SamplerDesc CubeMapSamplerDesc = DefaultSamplerDesc;
        //CubeMapSamplerDesc.MaxLOD = 6.0f;

        m_RootSig.Reset(kNumRootBindings, 3);
        m_RootSig.InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig.InitStaticSampler(11, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig.InitStaticSampler(12, CubeMapSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig[kMeshConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
        m_RootSig[kMaterialConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig[kMaterialSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10,
                                                       D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig[kMaterialSamplers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 10,
                                                           D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig[kCommonSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10,
                                                     D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig[kCommonCBV].InitAsConstantBuffer(1);
        m_RootSig[kCommoUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 10,
                                                   D3D12_SHADER_VISIBILITY_PIXEL);
        m_RootSig[kSkinMatrices].InitAsBufferSRV(20, D3D12_SHADER_VISIBILITY_VERTEX);
        m_RootSig.Finalize(L"VoxelRootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

        VoxelizationBlendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        VoxelizationBlendDesc.RenderTarget[0].BlendEnable = FALSE;
        VoxelizationBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        VoxelizationBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        VoxelizationBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_MAX;
        VoxelizationBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        VoxelizationBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        VoxelizationBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_MAX;
        VoxelizationBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        m_VoxelPSO.SetRootSignature(m_RootSig);
        m_VoxelPSO.SetRasterizerState(VoxelizationRasterizer);
        m_VoxelPSO.SetBlendState(VoxelizationBlendDesc);
        m_VoxelPSO.SetDepthStencilState(DepthStateDisabled);
        m_VoxelPSO.SetInputLayout(_countof(vertElem), vertElem);
        m_VoxelPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        m_VoxelPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
        m_VoxelPSO.SetVertexShader(g_pVCTVoxelizationVS, sizeof(g_pVCTVoxelizationVS));
        m_VoxelPSO.SetPixelShader(g_pVCTVoxelizationPS, sizeof(g_pVCTVoxelizationPS));


        // The caller of this function can override which materials are considered cutouts
        m_pMaterialIsCutout.resize(model.GetMaterialCount());
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

        if (!m_bInitialized)
        {
            m_bInitialized = true;

            // Each anisotropic voxel needs multiple values (per face = 6). Furthermore multiple clipmap regions are required.
            // Per attribute everything is stored in one 3D texture:
            // In X Direction -> voxelFace0,voxelFace1,...,voxelFace5
            // In Y Direction -> clipmap L0,L1,...,Ln
            // So the position in [0, resolution - 1]^3 of a specific inner texture is accessed by:
            // samplePos(x, y, z, faceIndex, clipmapLevel) = (x, y, z) + (faceIndex, clipmapLevel, 0) * resolution
            // To ensure correct interpolation at borders we thus need to extend the resolution of each inner texture
            // by 2 in each dimension and copy in a dedicated render pass (WrapBorderPass) the border of the other side
            // to get GL_REPEAT as the texture wrapping mode.
            int resolutionWithBorder = VoxelResolution + 2;
            g_voxelOpacity.Create(resolutionWithBorder * FaceCount, ClipRegionCount * resolutionWithBorder,
                                  resolutionWithBorder, DXGI_FORMAT_R8G8B8A8_UNORM);
            g_voxelRadiance.Create(resolutionWithBorder * FaceCount, ClipRegionCount * resolutionWithBorder,
                                   resolutionWithBorder, DXGI_FORMAT_R8G8B8A8_UNORM);
        }
    }

    void Cleanup(void)
    {
        if (m_bInitialized)
        {
            m_bInitialized = false;
            g_voxelOpacity.Destroy();
            g_voxelRadiance.Destroy();
        }
    }

    void VoxelizeObjects(GraphicsContext& gfxContext, const ShadowCamera& SunShadow, const Matrix4& ViewProjMat,
                         const Vector3& viewerPos, eObjectFilter Filter)
    {
        struct VSConstants
        {
            Matrix4 modelToProjection;
            Matrix4 modelToShadow;
            XMFLOAT3 viewerPos;
        } vsConstants;
        vsConstants.modelToProjection = ViewProjMat;
        vsConstants.modelToShadow = SunShadow.GetShadowMatrix();
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
                if (m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kCutout) ||
                    !m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kOpaque))
                    continue;

                materialIdx = mesh.materialIndex;
                gfxContext.SetDescriptorTable(Renderer::kMaterialSRVs, m_Model.GetSRVs(materialIdx));

                gfxContext.SetDynamicConstantBufferView(Renderer::kCommonCBV, sizeof(uint32_t), &materialIdx);
            }

            gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
        }
    }

    void Voxelize(GraphicsContext& gfxContext, const Camera& camera, const ShadowCamera& shadowCamera,
                  const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
    {
        gfxContext.TransitionResource(g_VoxelColorVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        gfxContext.TransitionResource(g_VoxelNormalVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        gfxContext.SetRenderTargets(0, nullptr);
        gfxContext.SetViewportAndScissor(viewport, scissor);
        {
            gfxContext.SetPipelineState(m_VoxelPSO);
            VoxelizeObjects(gfxContext, shadowCamera, m_OrthoCamera.GetVoxelMatrix(), camera.GetPosition(), kOpaque);
            //gfxContext.SetPipelineState(m_CutoutShadowPSO);
            //VoxelizeObjects(gfxContext, m_OrthoCamera, camera.GetPosition(), kCutout);
        }
        gfxContext.TransitionResource(g_VoxelColorVolume, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gfxContext.TransitionResource(g_VoxelNormalVolume, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void GenerateMipMaps(CommandContext& context)
    {
        //ComputeContext& Context = BaseContext.GetComputeContext();

        //Context.SetRootSignature(Graphics::g_CommonRS);

        //Context.TransitionResource(*this, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        //Context.SetDynamicDescriptor(1, 0, m_SRVHandle);

        //for (uint32_t TopMip = 0; TopMip < m_NumMipMaps; )
        //{
        //    uint32_t SrcWidth = m_Width >> TopMip;
        //    uint32_t SrcHeight = m_Height >> TopMip;
        //    uint32_t DstWidth = SrcWidth >> 1;
        //    uint32_t DstHeight = SrcHeight >> 1;

        //    // Determine if the first downsample is more than 2:1.  This happens whenever
        //    // the source width or height is odd.
        //    uint32_t NonPowerOfTwo = (SrcWidth & 1) | (SrcHeight & 1) << 1;
        //    if (m_Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        //        Context.SetPipelineState(Graphics::g_GenerateMipsGammaPSO[NonPowerOfTwo]);
        //    else
        //        Context.SetPipelineState(Graphics::g_GenerateMipsLinearPSO[NonPowerOfTwo]);

        //    // We can downsample up to four times, but if the ratio between levels is not
        //    // exactly 2:1, we have to shift our blend weights, which gets complicated or
        //    // expensive.  Maybe we can update the code later to compute sample weights for
        //    // each successive downsample.  We use _BitScanForward to count number of zeros
        //    // in the low bits.  Zeros indicate we can divide by two without truncating.
        //    uint32_t AdditionalMips;
        //    _BitScanForward((unsigned long*)&AdditionalMips,
        //        (DstWidth == 1 ? DstHeight : DstWidth) | (DstHeight == 1 ? DstWidth : DstHeight));
        //    uint32_t NumMips = 1 + (AdditionalMips > 3 ? 3 : AdditionalMips);
        //    if (TopMip + NumMips > m_NumMipMaps)
        //        NumMips = m_NumMipMaps - TopMip;

        //    // These are clamped to 1 after computing additional mips because clamped
        //    // dimensions should not limit us from downsampling multiple times.  (E.g.
        //    // 16x1 -> 8x1 -> 4x1 -> 2x1 -> 1x1.)
        //    if (DstWidth == 0)
        //        DstWidth = 1;
        //    if (DstHeight == 0)
        //        DstHeight = 1;

        //    Context.SetConstants(0, TopMip, NumMips, 1.0f / DstWidth, 1.0f / DstHeight);
        //    Context.SetDynamicDescriptors(2, 0, NumMips, m_UAVHandle + TopMip + 1);
        //    Context.Dispatch2D(DstWidth, DstHeight);

        //    Context.InsertUAVBarrier(*this);

        //    TopMip += NumMips;
        //}

        //Context.TransitionResource(*this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        //    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}
