#ifndef COMMON_HLSLI
#define COMMON_HLSLI

int3 computeVoxelFaceIndices(float3 direction)
{
    return int3(direction.x > 0.0 ? 0 : 1,
                direction.y > 0.0 ? 2 : 3,
                direction.z > 0.0 ? 4 : 5);

    // Branchless version - not necessarily faster
    //return int3(1, 3, 5) - (int3((sign(direction) + 1)) / 2);
}

int getDominantAxisIdx(float3 v0, float3 v1, float3 v2)
{
    float3 aN = abs(cross(v1 - v0, v2 - v0));

    if (aN.x > aN.y && aN.x > aN.z)
        return 0;

    if (aN.y > aN.z)
        return 1;

    return 2;
}

static const float3 MAIN_AXES[3] = {
    {1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 1.0}
};

static const float EPSILON = 0.000001;
static const float PI = 3.14159265;

#endif
