#pragma once
#include "DirectXCollision.h"
#include "../Core/pch.h"
#include "../Core/GpuResource.h"
#include "../Core/VectorMath.h"
#include "VolumeBuffer.h"
#include "Model.h"

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

using float3x3 = XMFLOAT3X3;
using float4x4 = XMFLOAT4X4;
using float2 = XMFLOAT2;
using float3 = XMFLOAT3;
using float4 = XMFLOAT4;
using uint = uint32_t;
using uint2 = XMUINT2;
using uint3 = XMUINT3;
using uint4 = XMUINT4;
using int2 = XMINT2;
using int3 = XMINT3;
using int4 = XMINT4;

#define CBSLOT_RENDERER_FRAME					0
#define CBSLOT_RENDERER_CAMERA					1
#define CBSLOT_RENDERER_VOXELIZER				3

namespace VCT
{
    class OrthoVoxelCamera : public Math::BaseCamera
    {
    public:
        OrthoVoxelCamera()
        {
        }

        void UpdateMatrix(Math::Vector3 ForwardDirection, Math::Vector3 CameraCenter, Math::Vector3 VoxelBounds,
                          float VoxelSize);

        // Used to transform world space to texture space for voxel sampling
        const Math::Matrix4& GetVoxelMatrix() const { return m_VoxelMatrix; }

    private:
        Math::Matrix4 m_VoxelMatrix;
    };
}

namespace VCT
{
    // If enabled, geometry shader will be used to voxelize, and axis will be selected by geometry shader
    //	If disabled, vertex shader with instance replication will be used for each axis
#define VOXELIZATION_GEOMETRY_SHADER_ENABLED

    // Number of clipmaps, each doubling in size:
    static const uint VXGI_CLIPMAP_COUNT = 6;

    struct alignas(16) VoxelClipMap
    {
        float3 center; // center of clipmap volume in world space
        float voxelSize; // half-extent of one voxel
    };

    struct alignas(16) VXGI
    {
        uint resolution; // voxel grid resolution
        float resolution_rcp; // 1.0 / voxel grid resolution
        float stepsize; // raymarch step size in voxel space units
        float max_distance; // maximum raymarch distance for voxel GI in world-space

        //int texture_radiance;
        //int texture_sdf;
        //int padding0;
        //int padding1;

        VoxelClipMap clipmaps[VXGI_CLIPMAP_COUNT];

#ifndef __cplusplus
        float3 world_to_clipmap(in float3 P, in VoxelClipMap clipmap)
        {
            float3 diff = (P - clipmap.center) * resolution_rcp / clipmap.voxelSize;
            float3 uvw = diff * float3(0.5f, -0.5f, 0.5f) + 0.5f;
            return uvw;
        }
        float3 clipmap_to_world(in float3 uvw, in VoxelClipMap clipmap)
        {
            float3 P = uvw * 2 - 1;
            P.y *= -1;
            P *= clipmap.voxelSize;
            P *= resolution;
            P += clipmap.center;
            return P;
        }
#endif // __cplusplus
    };

    struct alignas(16) VoxelizerCB
    {
        int3 offsetfromPrevFrame;
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

    static const float3 DIFFUSE_CONE_DIRECTIONS[32] = {
        float3(0.898904f, 0.435512f, 0.0479745f),
        float3(0.898904f, -0.435512f, -0.0479745f),
        float3(0.898904f, 0.0479745f, -0.435512f),
        float3(0.898904f, -0.0479745f, 0.435512f),
        float3(-0.898904f, 0.435512f, -0.0479745f),
        float3(-0.898904f, -0.435512f, 0.0479745f),
        float3(-0.898904f, 0.0479745f, 0.435512f),
        float3(-0.898904f, -0.0479745f, -0.435512f),
        float3(0.0479745f, 0.898904f, 0.435512f),
        float3(-0.0479745f, 0.898904f, -0.435512f),
        float3(-0.435512f, 0.898904f, 0.0479745f),
        float3(0.435512f, 0.898904f, -0.0479745f),
        float3(-0.0479745f, -0.898904f, 0.435512f),
        float3(0.0479745f, -0.898904f, -0.435512f),
        float3(0.435512f, -0.898904f, 0.0479745f),
        float3(-0.435512f, -0.898904f, -0.0479745f),
        float3(0.435512f, 0.0479745f, 0.898904f),
        float3(-0.435512f, -0.0479745f, 0.898904f),
        float3(0.0479745f, -0.435512f, 0.898904f),
        float3(-0.0479745f, 0.435512f, 0.898904f),
        float3(0.435512f, -0.0479745f, -0.898904f),
        float3(-0.435512f, 0.0479745f, -0.898904f),
        float3(0.0479745f, 0.435512f, -0.898904f),
        float3(-0.0479745f, -0.435512f, -0.898904f),
        float3(0.57735f, 0.57735f, 0.57735f),
        float3(0.57735f, 0.57735f, -0.57735f),
        float3(0.57735f, -0.57735f, 0.57735f),
        float3(0.57735f, -0.57735f, -0.57735f),
        float3(-0.57735f, 0.57735f, 0.57735f),
        float3(-0.57735f, 0.57735f, -0.57735f),
        float3(-0.57735f, -0.57735f, 0.57735f),
        float3(-0.57735f, -0.57735f, -0.57735f)
    };
#else // 16 cones for lower quality (8 on average per hemisphere)
    static const int DIFFUSE_CONE_COUNT = 16;
    static const float DIFFUSE_CONE_APERTURE = 0.872665f;

    static const float3 DIFFUSE_CONE_DIRECTIONS[16] = {
        float3(0.57735f, 0.57735f, 0.57735f),
        float3(0.57735f, -0.57735f, -0.57735f),
        float3(-0.57735f, 0.57735f, -0.57735f),
        float3(-0.57735f, -0.57735f, 0.57735f),
        float3(-0.903007f, -0.182696f, -0.388844f),
        float3(-0.903007f, 0.182696f, 0.388844f),
        float3(0.903007f, -0.182696f, 0.388844f),
        float3(0.903007f, 0.182696f, -0.388844f),
        float3(-0.388844f, -0.903007f, -0.182696f),
        float3(0.388844f, -0.903007f, 0.182696f),
        float3(0.388844f, 0.903007f, -0.182696f),
        float3(-0.388844f, 0.903007f, 0.182696f),
        float3(-0.182696f, -0.388844f, -0.903007f),
        float3(0.182696f, 0.388844f, -0.903007f),
        float3(-0.182696f, 0.388844f, 0.903007f),
        float3(0.182696f, -0.388844f, 0.903007f)
    };
#endif

    struct alignas(16) FrameCB
    {
        uint options; // wi::renderer bool options packed into bitmask (OPTION_BIT_ values)
        float time;
        float time_previous;
        float delta_time;

        uint frame_count;
        uint temporalaa_samplerotation;
        int texture_shadowatlas_index;
        int texture_shadowatlas_transparent_index;
        
        VXGI vxgi;
    };

    struct alignas(16) ShaderSphere
    {
        float3 center;
        float radius;
    };

    struct alignas(16) ShaderClusterBounds
    {
        ShaderSphere sphere;

        float3 cone_axis;
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
        float4 planes[6];
    };

    struct alignas(16) ShaderFrustumCorners
    {
        // topleft, topright, bottomleft, bottomright
        float4 cornersNEAR[4];
        float4 cornersFAR[4];
    };
    
    struct alignas(16) ShaderCamera
    {
        float4x4	view_projection;

        float3		position;
        uint		output_index; // viewport or rendertarget array index

        float4		clip_plane;
        float4		reflection_plane; // not clip plane (not reversed when camera is under), but the original plane

        float3		forward;
        float		z_near;

        float3		up;
        float		z_far;

        float		z_near_rcp;
        float		z_far_rcp;
        float		z_range;
        float		z_range_rcp;

        float4x4	view;
        float4x4	projection;
        float4x4	inverse_view;
        float4x4	inverse_projection;
        float4x4	inverse_view_projection;

        ShaderFrustum frustum;
        ShaderFrustumCorners frustum_corners;

        float2		temporalaa_jitter;
        float2		temporalaa_jitter_prev;

        float4x4	previous_view;
        float4x4	previous_projection;
        float4x4	previous_view_projection;
        float4x4	previous_inverse_view_projection;
        float4x4	reflection_view_projection;
        float4x4	reflection_inverse_view_projection;
        float4x4	reprojection; // view_projection_inverse_matrix * previous_view_projection_matrix

        float2		aperture_shape;
        float		aperture_size;
        float		focal_length;

        float2 canvas_size;
        float2 canvas_size_rcp;
		   
        uint2 internal_resolution;
        float2 internal_resolution_rcp;

        uint4 scissor; // scissor in physical coordinates (left,top,right,bottom) range: [0, internal_resolution]
        float4 scissor_uv; // scissor in screen UV coordinates (left,top,right,bottom) range: [0, 1]
        
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

namespace VCT::Primitive
{
    struct Sphere;
    struct Ray;
    struct AABB;
    struct Capsule;
    struct Plane;

    struct alignas(16) AABB
    {
        enum INTERSECTION_TYPE
        {
            OUTSIDE,
            INTERSECTS,
            INSIDE,
        };

        XMFLOAT3 _min;
        uint32_t layerMask = ~0u;
        XMFLOAT3 _max;
        uint32_t userdata = 0;

        AABB(
            const XMFLOAT3& _min = XMFLOAT3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                            std::numeric_limits<float>::max()),
            const XMFLOAT3& _max = XMFLOAT3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                            std::numeric_limits<float>::lowest())
        ) : _min(_min), _max(_max)
        {
        }

        void createFromHalfWidth(const XMFLOAT3& center, const XMFLOAT3& halfwidth);
        AABB transform(const XMMATRIX& mat) const;
        AABB transform(const XMFLOAT4X4& mat) const;
        XMFLOAT3 getCenter() const;
        XMFLOAT3 getHalfWidth() const;
        XMMATRIX getAsBoxMatrix() const;
        XMMATRIX getUnormRemapMatrix() const;
        float getArea() const;
        float getRadius() const;
        INTERSECTION_TYPE intersects2D(const AABB& b) const;
        INTERSECTION_TYPE intersects(const AABB& b) const;
        bool intersects(const XMFLOAT3& p) const;
        bool intersects(const XMVECTOR& P) const;
        bool intersects(const Ray& ray) const;
        bool intersects(const Sphere& sphere) const;
        bool intersects(const BoundingFrustum& frustum) const;
        bool intersects(const BoundingBox& other) const;
        bool intersects(const BoundingOrientedBox& other) const;
        AABB operator*(float a);
        static AABB Merge(const AABB& a, const AABB& b);
        void AddPoint(const XMFLOAT3& pos);
        void AddPoint(const XMVECTOR& P);

        // projects the AABB to the screen, returns a 2D rectangle in UV-space as Vector(topleftX, topleftY, bottomrightX, bottomrightY), each value is in range [0, 1]
        XMFLOAT4 ProjectToScreen(const XMMATRIX& ViewProjection) const;

        constexpr XMFLOAT3 getMin() const { return _min; }
        constexpr XMFLOAT3 getMax() const { return _max; }

        constexpr XMFLOAT3 corner(int index) const
        {
            switch (index)
            {
            case 0: return _min;
            case 1: return XMFLOAT3(_min.x, _max.y, _min.z);
            case 2: return XMFLOAT3(_min.x, _max.y, _max.z);
            case 3: return XMFLOAT3(_min.x, _min.y, _max.z);
            case 4: return XMFLOAT3(_max.x, _min.y, _min.z);
            case 5: return XMFLOAT3(_max.x, _max.y, _min.z);
            case 6: return _max;
            case 7: return XMFLOAT3(_max.x, _min.y, _max.z);
            }
            assert(0);
            return XMFLOAT3(0, 0, 0);
        }

        constexpr bool IsValid() const
        {
            if (_min.x > _max.x || _min.y > _max.y || _min.z > _max.z)
                return false;
            return true;
        }
    };

    struct Sphere
    {
        XMFLOAT3 center;
        float radius;

        Sphere() : center(XMFLOAT3(0, 0, 0)), radius(0)
        {
        }

        Sphere(const XMFLOAT3& c, float r) : center(c), radius(r)
        {
            assert(radius >= 0);
        }

        bool intersects(const XMVECTOR& P) const;
        bool intersects(const XMFLOAT3& P) const;
        bool intersects(const AABB& b) const;
        bool intersects(const Sphere& b) const;
        bool intersects(const Sphere& b, float& dist) const;
        bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Capsule& b) const;
        bool intersects(const Capsule& b, float& dist) const;
        bool intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Plane& b) const;
        bool intersects(const Plane& b, float& dist) const;
        bool intersects(const Plane& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Ray& b) const;
        bool intersects(const Ray& b, float& dist) const;
        bool intersects(const Ray& b, float& dist, XMFLOAT3& direction) const;

        // Construct a matrix that will orient to position according to surface normal:
        XMFLOAT4X4 GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const;
    };

    struct Capsule
    {
        XMFLOAT3 base = XMFLOAT3(0, 0, 0);
        XMFLOAT3 tip = XMFLOAT3(0, 0, 0);
        float radius = 0;
        Capsule() = default;

        Capsule(const XMFLOAT3& base, const XMFLOAT3& tip, float radius) : base(base), tip(tip), radius(radius)
        {
            assert(radius >= 0);
        }

        Capsule(XMVECTOR base, XMVECTOR tip, float radius) : radius(radius)
        {
            assert(radius >= 0);
            XMStoreFloat3(&this->base, base);
            XMStoreFloat3(&this->tip, tip);
        }

        Capsule(const Sphere& sphere, float height) :
            base(XMFLOAT3(sphere.center.x, sphere.center.y - sphere.radius, sphere.center.z)),
            tip(XMFLOAT3(base.x, base.y + height, base.z)),
            radius(sphere.radius)
        {
            assert(radius >= 0);
        }

        inline Sphere getSphere() const
        {
            XMVECTOR B = XMLoadFloat3(&base);
            XMVECTOR T = XMLoadFloat3(&tip);
            Sphere ret;
            XMStoreFloat3(&ret.center, XMVectorLerp(B, T, 0.5f));
            XMStoreFloat(&ret.radius, XMVector3Length(B - T) * 0.5f);
            return ret;
        }

        inline AABB getAABB() const
        {
            XMFLOAT3 halfWidth = XMFLOAT3(radius, radius, radius);
            AABB base_aabb;
            base_aabb.createFromHalfWidth(base, halfWidth);
            AABB tip_aabb;
            tip_aabb.createFromHalfWidth(tip, halfWidth);
            AABB result = AABB::Merge(base_aabb, tip_aabb);
            assert(result.IsValid());
            return result;
        }

        bool intersects(const Capsule& b, XMFLOAT3& position, XMFLOAT3& incident_normal,
                        float& penetration_depth) const;
        bool intersects(const Sphere& b) const;
        bool intersects(const Sphere& b, float& dist) const;
        bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Plane& b) const;
        bool intersects(const Plane& b, float& dist) const;
        bool intersects(const Plane& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Ray& b) const;
        bool intersects(const Ray& b, float& dist) const;
        bool intersects(const Ray& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const XMFLOAT3& point) const;

        // Construct a matrix that will orient to position according to surface normal:
        XMFLOAT4X4 GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const;
    };

    struct Plane
    {
        XMFLOAT3 origin = {};
        XMFLOAT3 normal = {};
        XMFLOAT4X4 projection = XMFLOAT4X4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

        bool intersects(const Sphere& b) const;
        bool intersects(const Sphere& b, float& dist) const;
        bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Capsule& b) const;
        bool intersects(const Capsule& b, float& dist) const;
        bool intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Ray& b) const;
        bool intersects(const Ray& b, float& dist) const;
        bool intersects(const Ray& b, float& dist, XMFLOAT3& direction) const;
    };

    struct Ray
    {
        XMFLOAT3 origin;
        float TMin = 0;
        XMFLOAT3 direction;
        float TMax = std::numeric_limits<float>::max();
        XMFLOAT3 direction_inverse;

        Ray(const XMFLOAT3& newOrigin = XMFLOAT3(0, 0, 0), const XMFLOAT3& newDirection = XMFLOAT3(0, 0, 1),
            float newTMin = 0, float newTMax = std::numeric_limits<float>::max()) :
            Ray(XMLoadFloat3(&newOrigin), XMLoadFloat3(&newDirection), newTMin, newTMax)
        {
        }

        Ray(const XMVECTOR& newOrigin, const XMVECTOR& newDirection, float newTMin = 0,
            float newTMax = std::numeric_limits<float>::max())
        {
            XMStoreFloat3(&origin, newOrigin);
            XMStoreFloat3(&direction, newDirection);
            XMStoreFloat3(&direction_inverse, XMVectorReciprocal(newDirection));
            TMin = newTMin;
            TMax = newTMax;
        }

        bool intersects(const AABB& b) const;
        bool intersects(const Sphere& b) const;
        bool intersects(const Sphere& b, float& dist) const;
        bool intersects(const Sphere& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Capsule& b) const;
        bool intersects(const Capsule& b, float& dist) const;
        bool intersects(const Capsule& b, float& dist, XMFLOAT3& direction) const;
        bool intersects(const Plane& b) const;
        bool intersects(const Plane& b, float& dist) const;
        bool intersects(const Plane& b, float& dist, XMFLOAT3& direction) const;

        void CreateFromPoints(const XMFLOAT3& a, const XMFLOAT3& b);

        // Construct a matrix that will orient to position according to surface normal:
        XMFLOAT4X4 GetPlacementOrientation(const XMFLOAT3& position, const XMFLOAT3& normal) const;
    };

    struct Frustum
    {
        XMFLOAT4 planes[6];

        void Create(const XMMATRIX& viewProjection);

        bool CheckPoint(const XMFLOAT3&) const;
        bool CheckSphere(const XMFLOAT3&, float) const;

        enum BoxFrustumIntersect
        {
            BOX_FRUSTUM_OUTSIDE,
            BOX_FRUSTUM_INTERSECTS,
            BOX_FRUSTUM_INSIDE,
        };

        BoxFrustumIntersect CheckBox(const AABB& box) const;
        bool CheckBoxFast(const AABB& box) const;

        const XMFLOAT4& getNearPlane() const;
        const XMFLOAT4& getFarPlane() const;
        const XMFLOAT4& getLeftPlane() const;
        const XMFLOAT4& getRightPlane() const;
        const XMFLOAT4& getTopPlane() const;
        const XMFLOAT4& getBottomPlane() const;
    };

    class Hitbox2D
    {
    public:
        XMFLOAT2 pos;
        XMFLOAT2 siz;

        Hitbox2D() : pos(XMFLOAT2(0, 0)), siz(XMFLOAT2(0, 0))
        {
        }

        Hitbox2D(const XMFLOAT2& newPos, const XMFLOAT2 newSiz) : pos(newPos), siz(newSiz)
        {
        }

        ~Hitbox2D()
        {
        };

        bool intersects(const XMFLOAT2& b) const;
        bool intersects(const Hitbox2D& b) const;
    };
}

namespace VCT
{
    class SceneGI
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

            VolumeBuffer radiance;
            VolumeBuffer prev_radiance;
            VolumeBuffer render_atomic;
            VolumeBuffer sdf;
            VolumeBuffer sdf_temp;
            mutable bool pre_clear = true;
        } vxgi;

        void Destroy();
        void Update(const Math::Camera& camera);
    };
}

namespace VCT
{
    extern bool VXGI_ENABLED;              // VXGI����״̬
    extern bool VXGI_REFLECTIONS_ENABLED;  // VXGI��������״̬
    extern bool VXGI_DEBUG;                // VXGI����ģʽ
    extern int VXGI_DEBUG_CLIPMAP;         // VXGI����������ͼ����

    // VXGI: Voxel-based Global Illumination (voxel cone tracing-based)
    struct VXGIResources
    {
        ColorBuffer diffuse;
        ColorBuffer specular;
        mutable bool pre_clear = true;

        bool IsValid() const { return diffuse.GetResource() != nullptr; }
    };

    void CreateVXGIResources(VXGIResources& res, XMUINT2 resolution);

    void RefreshEnvProbes(CommandContext& BaseContext, const Math::Camera& camera);
    
    void VXGI_Voxelize(CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
                       ModelH3D& model, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
    // Resolve VXGI to screen
    void VXGI_Resolve(
        CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
        const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);

    void Startup(Math::Camera& camera, ModelH3D& model);
    void Cleanup(void);
} // namespace VCT
