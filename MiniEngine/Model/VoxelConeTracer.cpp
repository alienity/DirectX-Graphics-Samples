#include "pch.h"
#include "VoxelConeTracer.h"
#include "../Core/GraphicsCore.h"
#include "../Core/BufferManager.h"
#include "../Core/CommandContext.h"

using namespace Graphics;

namespace VCT
{
    void VoxelVolume::Create(uint32_t size, uint32_t mipLevels)
    {
        m_Size = size;
        m_MipLevels = mipLevels;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        texDesc.Width = size;
        texDesc.Height = size;
        texDesc.DepthOrArraySize = size;
        texDesc.MipLevels = mipLevels;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texDesc.SampleDesc.Count = 1;

        CD3DX12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        ASSERT_SUCCEEDED(Graphics::g_Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE,
            &texDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, MY_IID_PPV_ARGS(&m_pResource)));

        m_UsageState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        m_UAVs.resize(mipLevels);
        for (uint32_t i = 0; i < mipLevels; ++i)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            uavDesc.Texture3D.MipSlice = i;
            uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            Graphics::g_Device->CreateUnorderedAccessView(m_pResource.Get(), nullptr, &uavDesc, uavHandle);
            m_UAVs[i] = uavHandle;
        }

        m_SRV = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = mipLevels;
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        Graphics::g_Device->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_SRV);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE VoxelVolume::GetUAV(uint32_t mipLevel) const
    {
        return m_UAVs[mipLevel];
    }

    D3D12_CPU_DESCRIPTOR_HANDLE VoxelVolume::GetSRV() const
    {
        return m_SRV;
    }


    void VCT::Initialize(void)
    {

    }

    void VCT::Shutdown(void)
    {

    }

    void VCT::Render(ComputeContext& Context, bool bUsePreComputedLuma)
    {

    }

    // 检查硬件是否支持 Conservative Rasterization
    bool SupportsConservativeRaster(ID3D12Device* device)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
        HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
        return SUCCEEDED(hr) && options.ConservativeRasterizationTier != D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
    }

    void VoxelConeTracer::Initialize()
    {
        // ... 创建 VoxelVolume ...

        // 编译着色器
        auto vs = g_pVoxelizationVS; // 从 .cso 加载
        auto ps = g_pVoxelizationPS;

        // 创建 Root Signature
        RootSignature rs;
        rs.Reset(2, 1); // 2 CBVs, 1 UAV
        rs[0].InitAsConstantBuffer(0); // VoxelCB for VS
        rs[1].InitAsConstantBuffer(1); // VoxelCB for PS
        rs.SetPixelShaderResource(0, 0); // u0: UAV
        rs.Finalize(L"Voxelization RS");

        // 创建 PSO
        GraphicsPSO pso;
        pso.SetRootSignature(rs);
        pso.SetRasterizerState(RasterizerDefault);
        pso.SetBlendState(BlendDisable);
        pso.SetDepthStencilState(DepthStateDisabled); // 不需要深度测试
        pso.SetInputLayout(_countof(Model::Vertex::s_InputLayout), Model::Vertex::s_InputLayout);
        pso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        pso.SetVertexShader(vs);
        pso.SetPixelShader(ps);

        // === 关键：启用 Conservative Rasterization ===
        if (SupportsConservativeRaster(Graphics::g_Device))
        {
            D3D12_RASTERIZER_DESC& raster = pso.m_PSODesc.RasterizerState;
            raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
        }

        // 注意：不设置 RenderTargetFormat！因为我们只写 UAV
        pso.m_PSODesc.NumRenderTargets = 0; // ← 非常重要！

        pso.Finalize();
        m_VoxelizationPSO = pso;
    }

    void VoxelConeTracer::Voxelize(CommandContext& context, const Model& model)
    {
        // 清空体素体积（RGBA=0）
        float clearColor[4] = { 0, 0, 0, 0 };
        context.ClearColor(m_Volume.GetUAV(), clearColor);

        struct VoxelView
        {
            Vector3 dir, up;
        } views[6] = {
            {Vector3(1,0,0), Vector3(0,1,0)},
            {Vector3(-1,0,0), Vector3(0,1,0)},
            {Vector3(0,1,0), Vector3(0,0,-1)},
            {Vector3(0,-1,0), Vector3(0,0,1)},
            {Vector3(0,0,1), Vector3(0,1,0)},
            {Vector3(0,0,-1), Vector3(0,1,0)}
        };

        for (int i = 0; i < 6; ++i)
        {
            Vector3 eye = views[i].dir * 15.0f; // 相机位置
            Matrix4 view = Matrix4::LookAt(eye, Vector3::Zero, views[i].up);
            Matrix4 proj = Matrix4::Orthographic(20.0f, 20.0f, 0.1f, 30.0f);
            Matrix4 vp = proj * view;

            VoxelCB cb;
            cb.ViewProj = vp;
            cb.VoxelWorldMin = Vector3(-10.0f, -10.0f, -10.0f);
            cb.VoxelSize = 20.0f / 128.0f;
            cb.CameraPos = eye;          // ← 关键：传入当前相机位置
            cb.VoxelRes = 128;

            context.SetPipelineState(m_VoxelizationPSO);
            context.SetRootSignature(m_VoxelizationRS);

            // b0 给 VS, b1 给 PS
            context.SetDynamicConstantBufferView(0, sizeof(cb), &cb);
            context.SetDynamicConstantBufferView(1, sizeof(cb), &cb);

            context.TransitionResource(m_Volume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.SetRenderTargets(0, nullptr); // 无 RTV
            context.SetDynamicDescriptor(0, m_Volume.GetUAV()); // u0

            model.Draw(context);
        }
    }

    void VCT::GenerateMipmaps(CommandContext& context)
    {
        context.SetPipelineState(m_MipmapCS);
        context.SetRootSignature(m_MipmapRS);

        for (uint32_t level = 0; level < m_Volume.GetMipLevels() - 1; ++level)
        {
            uint32_t srcSize = m_Volume.GetSize() >> level;
            uint32_t dstSize = srcSize / 2;

            context.TransitionResource(m_Volume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            context.SetConstants(0, level); // 传入当前 level
            context.SetDynamicDescriptor(1, m_Volume.GetUAV(level));     // src
            context.SetDynamicDescriptor(2, m_Volume.GetUAV(level + 1));   // dst

            context.Dispatch(Math::DivideByMultiple(dstSize, 4),
                Math::DivideByMultiple(dstSize, 4),
                Math::DivideByMultiple(dstSize, 4));
        }
    }

}


