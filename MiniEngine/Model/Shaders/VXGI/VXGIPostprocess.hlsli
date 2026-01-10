#ifndef WI_POSTPROCESS_H
#define WI_POSTPROCESS_H

static const uint POSTPROCESS_BLOCKSIZE = 8;
static const uint POSTPROCESS_LINEARDEPTH_BLOCKSIZE = 16;
static const uint POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT = 256;

struct PostProcess
{
    uint2 resolution;
    float2 resolution_rcp;
    float4 params0;
    float4 params1;
};

#endif