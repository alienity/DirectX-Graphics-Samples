#ifndef VXGIRENDERER_H
#define VXGIRENDERER_H

#include "VXGICommon.hlsli"

struct FrameVoxelCB
{
    VXGI vxgi;
};

CONSTANTBUFFER(g_xFrameVoxel, FrameVoxelCB, CBSLOT_RENDERER_FRAME);

#endif