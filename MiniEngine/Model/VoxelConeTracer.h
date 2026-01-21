#pragma once

class VolumeBuffer;
class ColorBuffer;
class BoolVar;
class NumVar;
class ComputeContext;

class GraphicsContext;
class ShadowCamera;
class ModelH3D;
class ExpVar;

namespace Math
{
    class Camera;
    class Vector3;
}

namespace VCT
{
    extern bool VXGI_ENABLED;
    extern bool VXGI_REFLECTIONS_ENABLED;
    extern bool VXGI_DEBUG;
    extern int VXGI_DEBUG_CLIPMAP;

    // VXGI: Voxel-based Global Illumination (voxel cone tracing-based)
    struct VXGIResources
    {
        ColorBuffer diffuse;
        ColorBuffer specular;
        mutable bool pre_clear = true;

        bool IsValid() const { return diffuse.GetResource() != nullptr; }
    };

    void RefreshEnvProbes(CommandContext& BaseContext, const Math::Camera& camera);
    
    void VXGI_Voxelize(CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
                       ModelH3D& model, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
    // Resolve VXGI to screen
    void VXGI_Resolve(
        CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
        const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);

    void Startup(Math::Camera& camera, ModelH3D& model);
    void Shutdown(void);
} // namespace VCT
