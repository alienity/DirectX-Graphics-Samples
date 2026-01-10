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
#include "CompiledShaders/VXGIOffsetprevCS.h"
#include "CompiledShaders/VXGIResolveDiffuseCS.h"
#include "CompiledShaders/VXGIResolveSpecularCS.h"


using namespace Graphics;
using namespace Math;

namespace VCT
{
    // 添加VXGI渲染根签名和PSO
    RootSignature m_vxgi_resolve_RootSig;
    ComputePSO m_vxgi_resolve_diffuse_PSO(L"VXGI: Resolve Diffuse PSO");
    ComputePSO m_vxgi_resolve_specular_PSO(L"VXGI: Resolve Specular PSO");

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

    void OrthoVoxelCamera::UpdateMatrix(Vector3 ForwardDirection, Vector3 CameraCenter, Vector3 VoxelBounds, float VoxelSize)
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
        m_VoxelMatrix = Matrix4(AffineTransform(Matrix3::MakeScale(0.5f, -0.5f, 1.0f), Vector3(0.5f, 0.5f, 0.0f))) * m_ViewProjMatrix;
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

    ByteAddressBuffer g_xFrame;
    ByteAddressBuffer g_xVoxelizer;

}

namespace VCT
{
    RootSignature m_vxgi_voxel_RootSig;
    GraphicsPSO m_vxgi_voxel_PSO(L"VXGI: Voxelization PSO");

    RootSignature m_vxgi_offsetprev_RootSig;
    ComputePSO m_vxgi_offsetprev_PSO(L"VXGI: OffsetPrev");

    void Startup(Camera& camera, ModelH3D& model)
    {
        // DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
        // DXGI_FORMAT NormalFormat = g_SceneNormalBuffer.GetFormat();
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

        m_vxgi_voxel_RootSig.Reset(10, 3);
        m_vxgi_voxel_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL, 1);
        m_vxgi_voxel_RootSig[1].InitAsConstantBuffer(3, D3D12_SHADER_VISIBILITY_ALL, 1);
        m_vxgi_voxel_RootSig[2].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
        m_vxgi_voxel_RootSig[3].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig[5].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig[7].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig[8].InitAsConstantBuffer(1);
        m_vxgi_voxel_RootSig[9].InitAsBufferSRV(20, D3D12_SHADER_VISIBILITY_VERTEX);
        m_vxgi_voxel_RootSig.InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig.InitStaticSampler(11, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig.InitStaticSampler(12, CubeMapSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_voxel_RootSig.Finalize(L"VoxelRootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

        m_vxgi_voxel_PSO.SetRootSignature(m_vxgi_voxel_RootSig);
        m_vxgi_voxel_PSO.SetRasterizerState(VoxelizationRasterizer);
        m_vxgi_voxel_PSO.SetBlendState(VoxelizationBlendDesc);
        m_vxgi_voxel_PSO.SetDepthStencilState(DepthStateDisabled);
        m_vxgi_voxel_PSO.SetInputLayout(_countof(vertElem), vertElem);
        m_vxgi_voxel_PSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        m_vxgi_voxel_PSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
        m_vxgi_voxel_PSO.SetVertexShader(g_pVXGIVoxelizationVS, sizeof(g_pVXGIVoxelizationVS));
        m_vxgi_voxel_PSO.SetGeometryShader(g_pVXGIVoxelizationGS, sizeof(g_pVXGIVoxelizationGS));
        m_vxgi_voxel_PSO.SetPixelShader(g_pVXGIVoxelizationPS, sizeof(g_pVXGIVoxelizationPS));
        m_vxgi_voxel_PSO.Finalize();


        m_vxgi_offsetprev_RootSig.Reset(4, 3);
        m_vxgi_offsetprev_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL, 1);
        m_vxgi_offsetprev_RootSig[1].InitAsConstantBuffer(3, D3D12_SHADER_VISIBILITY_ALL, 1);
        m_vxgi_offsetprev_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_vxgi_offsetprev_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1, D3D12_SHADER_VISIBILITY_ALL);
        m_vxgi_offsetprev_RootSig.InitStaticSampler(10, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_offsetprev_RootSig.InitStaticSampler(11, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_offsetprev_RootSig.InitStaticSampler(12, CubeMapSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_vxgi_offsetprev_RootSig.Finalize(L"VXGIOffsetprevRootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        m_vxgi_offsetprev_PSO.SetRootSignature(m_vxgi_offsetprev_RootSig);
        m_vxgi_offsetprev_PSO.SetComputeShader(g_pVXGIOffsetprevCS, sizeof(g_pVXGIOffsetprevCS));
        m_vxgi_offsetprev_PSO.Finalize();

        // 初始化VXGI解析阶段的根签名和PSO
        // 对应VXGIRenderer.hlsli中的Voxel_RootSig
        m_vxgi_resolve_RootSig.Reset(13, 10); // 13个根参数，10个静态采样器
        
        // 根常量: 12个32位常量，寄存器b999，空间0
        m_vxgi_resolve_RootSig[0].InitAsConstants(999, 12, D3D12_SHADER_VISIBILITY_ALL, 0);
        
        // CBV: b0, space = 0, visibility = SHADER_VISIBILITY_PIXEL
        m_vxgi_resolve_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL, 0);
        
        // CBV: b1, space = 0, visibility = SHADER_VISIBILITY_PIXEL
        m_vxgi_resolve_RootSig[2].InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_PIXEL, 0);
        
        // CBV: b0, space = 1, visibility = SHADER_VISIBILITY_ALL
        m_vxgi_resolve_RootSig[3].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL, 1);
        
        // CBV: b1, space = 1, visibility = SHADER_VISIBILITY_ALL
        m_vxgi_resolve_RootSig[4].InitAsConstantBuffer(1, D3D12_SHADER_VISIBILITY_ALL, 1);
        
        // CBV: b2, space = 1, visibility = SHADER_VISIBILITY_ALL
        m_vxgi_resolve_RootSig[5].InitAsConstantBuffer(2, D3D12_SHADER_VISIBILITY_ALL, 1);
        
        // 描述符表: 包含CBV, SRV, UAV
        // CBV(b3, space = 1, numDescriptors = 11)
        m_vxgi_resolve_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 3, 11, D3D12_SHADER_VISIBILITY_ALL, 1);
        
        // SRV(t0, space = 0, numDescriptors = 16)
        m_vxgi_resolve_RootSig[7].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 16, D3D12_SHADER_VISIBILITY_ALL, 0);
        
        // UAV(u0, space = 0, numDescriptors = 16)
        m_vxgi_resolve_RootSig[8].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 16, D3D12_SHADER_VISIBILITY_ALL, 0);
        
        // 添加额外的根参数以匹配Voxel_RootSig的完整定义
        m_vxgi_resolve_RootSig[9].InitAsConstantBuffer(3, D3D12_SHADER_VISIBILITY_ALL, 1);
        m_vxgi_resolve_RootSig[10].InitAsConstantBuffer(4, D3D12_SHADER_VISIBILITY_ALL, 1);
        m_vxgi_resolve_RootSig[11].InitAsConstantBuffer(5, D3D12_SHADER_VISIBILITY_ALL, 1);
        m_vxgi_resolve_RootSig[12].InitAsConstantBuffer(6, D3D12_SHADER_VISIBILITY_ALL, 1);
        
        // 静态采样器
        SamplerDesc linearClampSamplerDesc = {};
        linearClampSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linearClampSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearClampSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        linearClampSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        m_vxgi_resolve_RootSig.InitStaticSampler(100, linearClampSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc linearWrapSamplerDesc = linearClampSamplerDesc;
        linearWrapSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linearWrapSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        m_vxgi_resolve_RootSig.InitStaticSampler(101, linearWrapSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc linearMirrorSamplerDesc = linearClampSamplerDesc;
        linearMirrorSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        linearMirrorSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        linearMirrorSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        m_vxgi_resolve_RootSig.InitStaticSampler(102, linearMirrorSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc pointClampSamplerDesc = {};
        pointClampSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        pointClampSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClampSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClampSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        m_vxgi_resolve_RootSig.InitStaticSampler(103, pointClampSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc pointWrapSamplerDesc = pointClampSamplerDesc;
        pointWrapSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        pointWrapSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        pointWrapSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        m_vxgi_resolve_RootSig.InitStaticSampler(104, pointWrapSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc pointMirrorSamplerDesc = pointClampSamplerDesc;
        pointMirrorSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        pointMirrorSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        pointMirrorSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        m_vxgi_resolve_RootSig.InitStaticSampler(105, pointMirrorSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc anisoClampSamplerDesc = {};
        anisoClampSamplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
        anisoClampSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        anisoClampSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        anisoClampSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        anisoClampSamplerDesc.MaxAnisotropy = 16;
        m_vxgi_resolve_RootSig.InitStaticSampler(106, anisoClampSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc anisoWrapSamplerDesc = anisoClampSamplerDesc;
        anisoWrapSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        anisoWrapSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        anisoWrapSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        m_vxgi_resolve_RootSig.InitStaticSampler(107, anisoWrapSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc anisoMirrorSamplerDesc = anisoClampSamplerDesc;
        anisoMirrorSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        anisoMirrorSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        anisoMirrorSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        m_vxgi_resolve_RootSig.InitStaticSampler(108, anisoMirrorSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        SamplerDesc comparisonSamplerDesc = {};
        comparisonSamplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        comparisonSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        comparisonSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        comparisonSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        comparisonSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        m_vxgi_resolve_RootSig.InitStaticSampler(109, comparisonSamplerDesc, D3D12_SHADER_VISIBILITY_ALL);
        
        m_vxgi_resolve_RootSig.Finalize(L"VXGIResolveRootSig", D3D12_ROOT_SIGNATURE_FLAG_NONE);
        
        // 设置解析着色器的PSO
        m_vxgi_resolve_diffuse_PSO.SetRootSignature(m_vxgi_resolve_RootSig);
        m_vxgi_resolve_diffuse_PSO.SetComputeShader(g_pVXGIResolveDiffuseCS, sizeof(g_pVXGIResolveDiffuseCS));
        m_vxgi_resolve_diffuse_PSO.Finalize();
        
        m_vxgi_resolve_specular_PSO.SetRootSignature(m_vxgi_resolve_RootSig);
        m_vxgi_resolve_specular_PSO.SetComputeShader(g_pVXGIResolveSpecularCS, sizeof(g_pVXGIResolveSpecularCS));
        m_vxgi_resolve_specular_PSO.Finalize();

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

    }

    void Cleanup(void)
    {
        if (vxgi_resources.IsValid())
        {
            vxgi_resources.diffuse.Destroy();
            vxgi_resources.specular.Destroy();
        }
        g_xFrame.Destroy();
        g_xVoxelizer.Destroy();
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


        if (g_xFrame.GetResource() != nullptr)
        {
            g_xFrame.Create(L"g_xFrame", 1, sizeof(FrameCB));
        }
        if (g_xVoxelizer.GetResource() != nullptr)
        {
            g_xVoxelizer.Create(L"g_xVoxelizer", 1, sizeof(VoxelizerCB));
        }
    }

    void Voxelize(GraphicsContext& gfxContext, const Camera& camera, const ShadowCamera& shadowCamera,
                  const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
    {
        // gfxContext.TransitionResource(g_VoxelColorVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        // gfxContext.TransitionResource(g_VoxelNormalVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        // gfxContext.SetRenderTargets(0, nullptr);
        // gfxContext.SetViewportAndScissor(viewport, scissor);
        // {
        //     gfxContext.SetPipelineState(m_VoxelPSO);
        //     VoxelizeObjects(gfxContext, shadowCamera, m_OrthoCamera.GetVoxelMatrix(), camera.GetPosition(), kOpaque);
        //     //gfxContext.SetPipelineState(m_CutoutShadowPSO);
        //     //VoxelizeObjects(gfxContext, m_OrthoCamera, camera.GetPosition(), kCutout);
        // }
        // gfxContext.TransitionResource(g_VoxelColorVolume, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // gfxContext.TransitionResource(g_VoxelNormalVolume, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
            EngineProfiling::EndBlock(&gfxContext);
        }
        else
        {
            {
                gfxContext.TransitionResource(scene_gi.vxgi.render_atomic, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(scene_gi.vxgi.prev_radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(scene_gi.vxgi.sdf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(scene_gi.vxgi.sdf_temp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.FlushResourceBarriers();
            }

            EngineProfiling::BeginBlock(L"Atomic Clear", &gfxContext);
            gfxContext.ClearUAV(scene_gi.vxgi.render_atomic);
            EngineProfiling::EndBlock(&gfxContext);

            EngineProfiling::BeginBlock(L"Offset Previous Voxels", &gfxContext);
            gfxContext.SetRootSignature(m_vxgi_offsetprev_RootSig);
            gfxContext.SetPipelineState(m_vxgi_offsetprev_PSO);
            gfxContext.SetConstantBuffer(0, );
            EngineProfiling::EndBlock(&gfxContext);

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
