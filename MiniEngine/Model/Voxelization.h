#pragma once

#include "VoxelRegion.h"

namespace Math { class Camera;  }
class GraphicsContext;
class ComputeContext;
class BoolVar;

namespace VCT
{
    namespace VOXELIZATION
    {
        struct DebugInfo
        {
            std::vector<VoxelRegion> lastRevoxelizationRegions;
        };

        void Initialize( void );
        void Shutdown( void );
        void Render(GraphicsContext& Context, const float* ProjMat, float NearClipDist, float FarClipDist );
        void Render(GraphicsContext& Context, const Math::Camera& camera );

        // extern BoolVar Enable;
    }
}
