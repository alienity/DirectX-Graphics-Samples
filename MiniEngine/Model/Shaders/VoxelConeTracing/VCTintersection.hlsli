#ifndef INTERSECTION_HLSLI
#define INTERSECTION_HLSLI

struct AABBox3D
{
    float3 minPos;
    float3 maxPos;
};

struct Ray
{
    float3 origin;
    float3 direction;
};

// Returns a vector with the first component being tNear (first hit) and the second tFar (second hit)
// If there is no intersection then tNear > tFar
// Note: If the AABB is hit in the opposite direction of the ray from the origin then tNear < 0
float2 rayIntersectsAABB(Ray ray, AABBox3D aabb)
{
    // Since GLSL Spec 4.10.6 - Subsection 4.5.1 "Range and Precision" division by zero produces the appropriate Inf./-Inf. here
    // and thus ensures the correctness of the following operations
    float3 tMin = (aabb.minPos - ray.origin) / ray.direction;
    float3 tMax = (aabb.maxPos - ray.origin) / ray.direction;
    float3 t1 = min(tMin, tMax);
    float3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    return float2(tNear, tFar);
}

bool aabbIntersectsAABB(AABBox3D a, AABBox3D b)
{
    // return all(greaterThanEqual(a.maxPos, b.minPos)) && all(lessThanEqual(a.minPos, b.maxPos));
    return all(a.maxPos >= b.minPos) && all(a.minPos <= b.maxPos);
}

// Algorithm from "Fast Parallel Surface and Solid Voxelization on GPUs" by Michael Schwarz and Hans-Peter Seide
// Note: This is optimal for 1 Triangle 1 AABB intersection test. For intersection tests with 1 triangle and multiple AABBs 
// a lot of values can be precomputed and reused to maximize performance.
bool aabbIntersectsTriangle(AABBox3D b, float3 v0, float3 v1, float3 v2)
{
    // AABB/AABB test
    AABBox3D triangleAABB;
    triangleAABB.minPos = min(v0, min(v1, v2));
    triangleAABB.maxPos = max(v0, max(v1, v2));

    if (!aabbIntersectsAABB(b, triangleAABB))
        return false;

    // Triangle plane/AABB test
    float3 e0 = v1 - v0;
    float3 e1 = v2 - v1;
    float3 e2 = v0 - v2;

    float3 n = normalize(cross(e0, e1));
    float3 dp = b.maxPos - b.minPos;
    float3 c = float3(
        n.x > 0.0 ? dp.x : 0.0,
        n.y > 0.0 ? dp.y : 0.0,
        n.z > 0.0 ? dp.z : 0.0);

    float d1 = dot(n, c - v0);
    float d2 = dot(n, (dp - c) - v0);

    if ((dot(n, b.minPos) + d1) * (dot(n, b.minPos) + d2) > 0.0)
        return false;

    // 2D Projections of Triangle/AABB test

    // XY Plane
    float signZ = sign(n.z);
    float2 n_xy_e0 = float2(-e0.y, e0.x) * signZ;
    float2 n_xy_e1 = float2(-e1.y, e1.x) * signZ;
    float2 n_xy_e2 = float2(-e2.y, e2.x) * signZ;

    float d_xy_e0 = -dot(n_xy_e0, v0.xy) + max(0.0, dp.x * n_xy_e0.x) + max(0.0, dp.y * n_xy_e0.y);
    float d_xy_e1 = -dot(n_xy_e1, v1.xy) + max(0.0, dp.x * n_xy_e1.x) + max(0.0, dp.y * n_xy_e1.y);
    float d_xy_e2 = -dot(n_xy_e2, v2.xy) + max(0.0, dp.x * n_xy_e2.x) + max(0.0, dp.y * n_xy_e2.y);

    float2 p_xy = float2(b.minPos.x, b.minPos.y);

    if ((dot(n_xy_e0, p_xy) + d_xy_e0 < 0.0) ||
        (dot(n_xy_e1, p_xy) + d_xy_e1 < 0.0) ||
        (dot(n_xy_e2, p_xy) + d_xy_e2 < 0.0))
        return false;

    // ZX Plane
    float signY = sign(n.y);
    float2 n_zx_e0 = float2(-e0.x, e0.z) * signY;
    float2 n_zx_e1 = float2(-e1.x, e1.z) * signY;
    float2 n_zx_e2 = float2(-e2.x, e2.z) * signY;

    float d_zx_e0 = -dot(n_zx_e0, v0.zx) + max(0.0, dp.z * n_zx_e0.x) + max(0.0, dp.x * n_zx_e0.y);
    float d_zx_e1 = -dot(n_zx_e1, v1.zx) + max(0.0, dp.z * n_zx_e1.x) + max(0.0, dp.x * n_zx_e1.y);
    float d_zx_e2 = -dot(n_zx_e2, v2.zx) + max(0.0, dp.z * n_zx_e2.x) + max(0.0, dp.x * n_zx_e2.y);

    float2 p_zx = float2(b.minPos.z, b.minPos.x);

    if ((dot(n_zx_e0, p_zx) + d_zx_e0 < 0.0) ||
        (dot(n_zx_e1, p_zx) + d_zx_e1 < 0.0) ||
        (dot(n_zx_e2, p_zx) + d_zx_e2 < 0.0))
        return false;

    // YZ Plane
    float signX = sign(n.x);
    float2 n_yz_e0 = float2(-e0.z, e0.y) * signX;
    float2 n_yz_e1 = float2(-e1.z, e1.y) * signX;
    float2 n_yz_e2 = float2(-e2.z, e2.y) * signX;

    float d_yz_e0 = -dot(n_yz_e0, v0.yz) + max(0.0, dp.y * n_yz_e0.x) + max(0.0, dp.z * n_yz_e0.y);
    float d_yz_e1 = -dot(n_yz_e1, v1.yz) + max(0.0, dp.y * n_yz_e1.x) + max(0.0, dp.z * n_yz_e1.y);
    float d_yz_e2 = -dot(n_yz_e2, v2.yz) + max(0.0, dp.y * n_yz_e2.x) + max(0.0, dp.z * n_yz_e2.y);

    float2 p_yz = float2(b.minPos.y, b.minPos.z);

    if ((dot(n_yz_e0, p_yz) + d_yz_e0 < 0.0) ||
        (dot(n_yz_e1, p_yz) + d_yz_e1 < 0.0) ||
        (dot(n_yz_e2, p_yz) + d_yz_e2 < 0.0))
        return false;

    return true;
}

#endif
