#include "GraphicsCore.h"
#include "BufferManager.h"
#include "Camera.h"
#include "ShadowCamera.h"
#include "CommandContext.h"
#include "TemporalEffects.h"
#include "VoxelConeTracer.h"
#include "Renderer.h"
#include "DirectXCollision.h"

#include <cmath>
#include "ModelH3D.h"

#include "CompiledShaders/VXGIVoxelizationVS.h"
#include "CompiledShaders/VXGIVoxelizationGS.h"
#include "CompiledShaders/VXGIVoxelizationPS.h"
#include "CompiledShaders/VXGITemporalCS.h"
#include "CompiledShaders/VXGISDFJumpfloodCS.h"
#include "CompiledShaders/VXGIOffsetprevCS.h"
#include "CompiledShaders/VXGIResolveDiffuseCS.h"
#include "CompiledShaders/VXGIResolveSpecularCS.h"


using namespace Graphics;
using namespace Math;


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

namespace VCT
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
        if (vxgi.radiance.GetResource() == nullptr)
        {
            vxgi.radiance.Create(L"vxgi.radiance", vxgi.res * (6 + DIFFUSE_CONE_COUNT), vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
            vxgi.prev_radiance.Create(L"vxgi.prev_radiance", vxgi.res * (6 + DIFFUSE_CONE_COUNT), vxgi.res * VXGI_CLIPMAP_COUNT, vxgi.res, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
            vxgi.pre_clear = true;
        }
        if (vxgi.render_atomic.GetResource() == nullptr)
        {
            vxgi.render_atomic.Create(L"vxgi.render_atomic", vxgi.res * 6, vxgi.res, vxgi.res * VOXELIZATION_CHANNEL_COUNT, 1, DXGI_FORMAT_R32_UINT);
        }
        if (vxgi.sdf.GetResource() == nullptr)
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
    //OrthoVoxelCamera m_OrthoCamera;

    BoolVar Enable("Graphics/VCT/Enable", true);
    BoolVar DebugDraw("Graphics/VCT/DebugDraw", false);

    SceneGI g_scene_gi;
    VXGIResources g_vxgi_resources;

    UploadBuffer g_xVoxelizerCPU;
    ByteAddressBuffer g_xVoxelizer;

    UploadBuffer m_xFrameCPU;
    ByteAddressBuffer g_xFrame;

    UploadBuffer m_xCameraCPU;
    ByteAddressBuffer m_xCamera;

}

namespace VCT
{
    ComputePSO m_vxgi_temporal_PSO(L"VXGI: Temporal PSO");
    ComputePSO m_vxgi_jumpflood_PSO(L"VXGI: JumpFlood PSO");
    ComputePSO m_vxgi_offsetprev_PSO(L"VXGI: OffsetPrev PSO");
    ComputePSO m_vxgi_resolve_diffuse_PSO(L"VXGI: Resolve Diffuse PSO");
    ComputePSO m_vxgi_resolve_specular_PSO(L"VXGI: Resolve Specular PSO");

    void UpdateBuffers(CommandContext& BaseContext, const Camera& camera, ModelH3D& model)
    {
        if (!g_vxgi_resources.IsValid())
        {
            uint32_t sceneWidth = g_SceneColorBuffer.GetWidth();
            uint32_t sceneHeight = g_SceneColorBuffer.GetHeight();

            g_vxgi_resources.diffuse.Create(L"vxgi.diffuse", sceneWidth, sceneHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
            g_vxgi_resources.specular.Create(L"vxgi.specular", sceneWidth, sceneHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
            g_vxgi_resources.pre_clear = true;
        }

        g_scene_gi.Update(camera);

        if (g_xVoxelizerCPU.GetResource() == nullptr)
        {
            g_xVoxelizerCPU.Create(L"g_xVoxelizerCPU", sizeof(VoxelizerCB));
        }
        if (g_xVoxelizer.GetResource() == nullptr)
        {
            g_xVoxelizer.Create(L"g_xVoxelizer", 1, sizeof(VoxelizerCB));
        }

        if (m_xFrameCPU.GetResource() == nullptr)
        {
            m_xFrameCPU.Create(L"m_xFrameCPU", sizeof(FrameCB));
        }
        if (g_xFrame.GetResource() == nullptr)
        {
            g_xFrame.Create(L"g_xFrame", 1, sizeof(FrameCB));
        }

        if (m_xCameraCPU.GetResource() == nullptr)
        {
            m_xCameraCPU.Create(L"m_xCameraCPU", sizeof(CameraCB));
        }
        if (m_xCamera.GetResource() == nullptr)
        {
            m_xCamera.Create(L"m_xCamera", 1, sizeof(CameraCB));
        }

        {
            const SceneGI::VXGI::ClipMap& clipmap = g_scene_gi.vxgi.clipmaps[g_scene_gi.vxgi.clipmap_to_update];

            VoxelizerCB* cb = (VoxelizerCB*)g_xVoxelizerCPU.Map();

            cb->offsetfromPrevFrame = clipmap.offsetfromPrevFrame;
            cb->clipmap_index = g_scene_gi.vxgi.clipmap_to_update;

            g_xVoxelizerCPU.Unmap();

            BaseContext.TransitionResource(g_xVoxelizer, D3D12_RESOURCE_STATE_COPY_DEST, true);
            BaseContext.GetCommandList()->CopyBufferRegion(g_xVoxelizer.GetResource(), 0, g_xVoxelizerCPU.GetResource(), 0, g_xVoxelizerCPU.GetBufferSize());
            BaseContext.TransitionResource(g_xVoxelizer, D3D12_RESOURCE_STATE_GENERIC_READ);
        }

        {
            FrameCB* cb = (FrameCB*)m_xFrameCPU.Map();

            cb->options = 0;
            cb->time = 0;
            cb->time_previous = 0;
            cb->delta_time = 0;

            cb->frame_count = 0;
            cb->temporalaa_samplerotation = 0;
            cb->texture_shadowatlas_index = 0;
            cb->texture_shadowatlas_transparent_index = 0;

            cb->vxgi.resolution = g_scene_gi.vxgi.res;
            cb->vxgi.resolution_rcp = 1.0f / g_scene_gi.vxgi.res;
            cb->vxgi.stepsize = g_scene_gi.vxgi.rayStepSize;
            cb->vxgi.max_distance = g_scene_gi.vxgi.maxDistance;

            for (uint i = 0; i < VXGI_CLIPMAP_COUNT; i++)
            {
                cb->vxgi.clipmaps[i].center = g_scene_gi.vxgi.clipmaps[i].center;
                cb->vxgi.clipmaps[i].voxelSize = g_scene_gi.vxgi.clipmaps[i].voxelsize;
            }

            m_xFrameCPU.Unmap();

            BaseContext.TransitionResource(g_xFrame, D3D12_RESOURCE_STATE_COPY_DEST, true);
            BaseContext.GetCommandList()->CopyBufferRegion(g_xFrame.GetResource(), 0, m_xFrameCPU.GetResource(), 0, m_xFrameCPU.GetBufferSize());
            BaseContext.TransitionResource(g_xFrame, D3D12_RESOURCE_STATE_GENERIC_READ);
        }

        {
            CameraCB* cb = (CameraCB*)m_xCameraCPU.Map();
            cb->init();

            XMMATRIX viewProjection = camera.GetViewProjMatrix();
            XMMATRIX invVP = XMMatrixInverse(nullptr, viewProjection);

            ShaderFrustumCorners frustum_corners;
            
            // 设置近平面的四个角点
            XMStoreFloat4(&frustum_corners.cornersNEAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), invVP)); // near topleft
            XMStoreFloat4(&frustum_corners.cornersNEAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), invVP));  // near topright
            XMStoreFloat4(&frustum_corners.cornersNEAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), invVP)); // near bottomleft
            XMStoreFloat4(&frustum_corners.cornersNEAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), invVP));  // near bottomright
            
            // 设置远平面的四个角点
            XMStoreFloat4(&frustum_corners.cornersFAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 0, 1), invVP)); // far topleft
            XMStoreFloat4(&frustum_corners.cornersFAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 0, 1), invVP));  // far topright
            XMStoreFloat4(&frustum_corners.cornersFAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 0, 1), invVP)); // far bottomleft
            XMStoreFloat4(&frustum_corners.cornersFAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 0, 1), invVP));  // far bottomright

            XMFLOAT4 frustumPlanes[6];
            
            // 从视图投影矩阵提取视锥体平面
            XMFLOAT4X4 vp;
            XMStoreFloat4x4(&vp, viewProjection);
            
            // Left plane
            frustumPlanes[0] = XMFLOAT4(
                vp._14 + vp._11,
                vp._24 + vp._21,
                vp._34 + vp._31,
                vp._44 + vp._41);
            
            // Right plane
            frustumPlanes[1] = XMFLOAT4(
                vp._14 - vp._11,
                vp._24 - vp._21,
                vp._34 - vp._31,
                vp._44 - vp._41);
            
            // Bottom plane
            frustumPlanes[2] = XMFLOAT4(
                vp._14 + vp._12,
                vp._24 + vp._22,
                vp._34 + vp._32,
                vp._44 + vp._42);
            
            // Top plane
            frustumPlanes[3] = XMFLOAT4(
                vp._14 - vp._12,
                vp._24 - vp._22,
                vp._34 - vp._32,
                vp._44 - vp._42);
            
            // Near plane
            frustumPlanes[4] = XMFLOAT4(
                vp._13,
                vp._23,
                vp._33,
                vp._43);
            
            // Far plane
            frustumPlanes[5] = XMFLOAT4(
                vp._14 - vp._13,
                vp._24 - vp._23,
                vp._34 - vp._33,
                vp._44 - vp._43);
            
            // 归一化所有平面
            for (int i = 0; i < 6; i++) {
                XMVECTOR planeVec = XMLoadFloat4(&frustumPlanes[i]);
                planeVec = XMPlaneNormalize(planeVec);
                XMStoreFloat4(&frustumPlanes[i], planeVec);
            }
            
            ShaderCamera scamera;
            XMStoreFloat4x4(&scamera.inverse_view_projection, invVP);
            
            // 将提取的平面复制到scamera的frustum
            for (int i = 0; i < 6; i++) {
                scamera.frustum.planes[i] = frustumPlanes[i];
            }
            
            scamera.frustum_corners = frustum_corners;

            // 填充相机的其他属性
            XMStoreFloat4x4(&scamera.view_projection, viewProjection);
            
            XMVECTOR camPos = camera.GetPosition();
            XMStoreFloat3(&scamera.position, camPos);
            scamera.output_index = 0;
            
            XMVECTOR camForwardVec = camera.GetForwardVec();
            XMVECTOR camUpVec = camera.GetUpVec();
            XMStoreFloat3(&scamera.forward, camForwardVec);
            XMStoreFloat3(&scamera.up, camUpVec);
            
            scamera.z_near = camera.GetNearClip();
            scamera.z_far = camera.GetFarClip();
            scamera.z_near_rcp = 1.0f / camera.GetNearClip();
            scamera.z_far_rcp = 1.0f / camera.GetFarClip();
            scamera.z_range = abs(scamera.z_far - scamera.z_near);
            scamera.z_range_rcp = 1.0f / std::max(0.0001f, scamera.z_range);
            
            XMMATRIX view = camera.GetViewMatrix();
            XMMATRIX proj = camera.GetProjMatrix();
            XMMATRIX invView = XMMatrixInverse(nullptr, view);
            XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
            
            XMStoreFloat4x4(&scamera.view, view);
            XMStoreFloat4x4(&scamera.projection, proj);
            XMStoreFloat4x4(&scamera.inverse_view, invView);
            XMStoreFloat4x4(&scamera.inverse_projection, invProj);
            
            scamera.clip_plane = float4(0, 0, 0, 0); // 默认值
            scamera.reflection_plane = float4(0, 0, 0, 0); // 默认值
            scamera.temporalaa_jitter = float2(0, 0);
            scamera.temporalaa_jitter_prev = float2(0, 0);
            
            XMMATRIX prevViewProj = camera.GetPreviousViewProjMatrix();
            XMStoreFloat4x4(&scamera.previous_view_projection, prevViewProj);
            XMMATRIX tempView = camera.GetViewMatrix(); // 使用当前视图矩阵作为近似值
            XMMATRIX tempProj = camera.GetProjMatrix(); // 使用当前投影矩阵作为近似值
            XMMATRIX tempInvPrevVP = XMMatrixInverse(nullptr, prevViewProj);
            XMStoreFloat4x4(&scamera.previous_view, tempView);
            XMStoreFloat4x4(&scamera.previous_projection, tempProj);
            XMStoreFloat4x4(&scamera.previous_inverse_view_projection, tempInvPrevVP);
            XMStoreFloat4x4(&scamera.reflection_view_projection, viewProjection);
            XMStoreFloat4x4(&scamera.reflection_inverse_view_projection, invVP);
            XMMATRIX tempReproj = prevViewProj * XMMatrixInverse(nullptr, viewProjection);
            XMStoreFloat4x4(&scamera.reprojection, tempReproj);
            
            scamera.aperture_shape = float2(1, 1);
            scamera.aperture_size = 0.0f;
            scamera.focal_length = 1.0f;
            
            // 从模型获取缓冲区尺寸信息，如果可能的话
            // 如果不能获取，使用默认值
            uint32_t width = g_SceneColorBuffer.GetWidth();
            uint32_t height = g_SceneColorBuffer.GetHeight();
            
            scamera.canvas_size = float2((float)width, (float)height);
            scamera.canvas_size_rcp.x = 1.0f / std::max(0.0001f, scamera.canvas_size.x);
            scamera.canvas_size_rcp.y = 1.0f / std::max(0.0001f, scamera.canvas_size.y);
            
            scamera.internal_resolution = uint2(width, height);
            scamera.internal_resolution_rcp.x = 1.0f / scamera.internal_resolution.x;
            scamera.internal_resolution_rcp.y = 1.0f / scamera.internal_resolution.y;
            
            scamera.scissor = uint4(0, 0, width, height);
            scamera.scissor_uv = float4(0.0f, 0.0f, 1.0f, 1.0f);

            cb->cameras[0] = scamera;

            m_xCameraCPU.Unmap();

            BaseContext.TransitionResource(m_xCamera, D3D12_RESOURCE_STATE_COPY_DEST, true);
            BaseContext.GetCommandList()->CopyBufferRegion(m_xCamera.GetResource(), 0, m_xCameraCPU.GetResource(), 0, m_xCameraCPU.GetBufferSize());
            BaseContext.TransitionResource(m_xCamera, D3D12_RESOURCE_STATE_GENERIC_READ);
        }

    }

    void Startup()
    {
        m_vxgi_offsetprev_PSO.SetRootSignature(Renderer::m_VoxelRootSig);
        m_vxgi_offsetprev_PSO.SetComputeShader(g_pVXGIOffsetprevCS, sizeof(g_pVXGIOffsetprevCS));
        m_vxgi_offsetprev_PSO.Finalize();

        m_vxgi_temporal_PSO.SetRootSignature(Renderer::m_VoxelRootSig);
        m_vxgi_temporal_PSO.SetComputeShader(g_pVXGITemporalCS, sizeof(g_pVXGITemporalCS));
        m_vxgi_temporal_PSO.Finalize();

        m_vxgi_jumpflood_PSO.SetRootSignature(Renderer::m_VoxelRootSig);
        m_vxgi_jumpflood_PSO.SetComputeShader(g_pVXGISDFJumpfloodCS, sizeof(g_pVXGISDFJumpfloodCS));
        m_vxgi_jumpflood_PSO.Finalize();

        m_vxgi_resolve_diffuse_PSO.SetRootSignature(Renderer::m_VoxelRootSig);
        m_vxgi_resolve_diffuse_PSO.SetComputeShader(g_pVXGIResolveDiffuseCS, sizeof(g_pVXGIResolveDiffuseCS));
        m_vxgi_resolve_diffuse_PSO.Finalize();
        
        m_vxgi_resolve_specular_PSO.SetRootSignature(Renderer::m_VoxelRootSig);
        m_vxgi_resolve_specular_PSO.SetComputeShader(g_pVXGIResolveSpecularCS, sizeof(g_pVXGIResolveSpecularCS));
        m_vxgi_resolve_specular_PSO.Finalize();
    }

    void Shutdown(void)
    {
        g_scene_gi.Destroy();
        g_vxgi_resources.diffuse.Destroy();
        g_vxgi_resources.specular.Destroy();
        g_xVoxelizerCPU.Destroy();
        g_xVoxelizer.Destroy();
        m_xFrameCPU.Destroy();
        g_xFrame.Destroy();
        m_xCameraCPU.Destroy();
        m_xCamera.Destroy();
    }

    ByteAddressBuffer& GetVoxelBuffer()
    {
        return g_xVoxelizer;
    }

    ByteAddressBuffer& GetFrameBuffer()
    {
        return g_xFrame;
    }

    ByteAddressBuffer& GetCameraBuffer()
    {
        return m_xCamera;
    }

    void VXGI_Voxelize(CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
        ModelH3D& model, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
    {
        EngineProfiling::BeginBlock(L"VXGI", &BaseContext);

        UpdateBuffers(BaseContext, camera, model);

        const SceneGI::VXGI::ClipMap& clipmap = g_scene_gi.vxgi.clipmaps[g_scene_gi.vxgi.clipmap_to_update];

        //Primitive::AABB bbox;
        //bbox.createFromHalfWidth(clipmap.center, clipmap.extents);

        VoxelizerCB cb;
        cb.offsetfromPrevFrame = clipmap.offsetfromPrevFrame;
        cb.clipmap_index = g_scene_gi.vxgi.clipmap_to_update;

        GraphicsContext& gfxContext = BaseContext.GetGraphicsContext();

    	//gfxContext.SetDynamicConstantBufferView(CBSLOT_RENDERER_VOXELIZER, sizeof(VoxelizerCB), &cb);

        if (g_scene_gi.vxgi.pre_clear)
        {
            EngineProfiling::BeginBlock(L"Pre Clear", &gfxContext);
            {
                gfxContext.TransitionResource(g_scene_gi.vxgi.render_atomic, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(g_scene_gi.vxgi.prev_radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(g_scene_gi.vxgi.radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(g_scene_gi.vxgi.sdf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(g_scene_gi.vxgi.sdf_temp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.FlushResourceBarriers();
            }
            gfxContext.ClearUAV(g_scene_gi.vxgi.prev_radiance);
            gfxContext.ClearUAV(g_scene_gi.vxgi.radiance);
            gfxContext.ClearUAV(g_scene_gi.vxgi.sdf);
            gfxContext.ClearUAV(g_scene_gi.vxgi.sdf_temp);
            gfxContext.ClearUAV(g_scene_gi.vxgi.render_atomic);
            g_scene_gi.vxgi.pre_clear = false;;
            EngineProfiling::EndBlock(&gfxContext);
        }
        else
        {
            {
                gfxContext.TransitionResource(g_scene_gi.vxgi.render_atomic, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(g_scene_gi.vxgi.prev_radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(g_scene_gi.vxgi.sdf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.TransitionResource(g_scene_gi.vxgi.sdf_temp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                gfxContext.FlushResourceBarriers();
            }

            {
                EngineProfiling::BeginBlock(L"Atomic Clear", &gfxContext);
                gfxContext.ClearUAV(g_scene_gi.vxgi.render_atomic);
                EngineProfiling::EndBlock(&gfxContext);
            }

            //{
            //    EngineProfiling::BeginBlock(L"Offset Previous Voxels", &gfxContext);
            //    ComputeContext& Context = BaseContext.GetComputeContext();
            //    Context.SetRootSignature(m_vxgi_RootSig);
            //    Context.SetPipelineState(m_vxgi_offsetprev_PSO);
            //    Context.SetConstantBuffer(3, g_xFrame.GetGpuVirtualAddress());
            //    Context.SetDynamicConstantBufferView(5, sizeof(VoxelizerCB), &cb);
            //    Context.SetDynamicDescriptor(7, 0, g_scene_gi.vxgi.radiance.GetSRV());
            //    Context.SetDynamicDescriptor(8, 0, g_scene_gi.vxgi.prev_radiance.GetUAV());
            //    uint32_t dispatchSize = g_scene_gi.vxgi.res / 8;
            //    Context.Dispatch(dispatchSize, dispatchSize, dispatchSize);
            //    EngineProfiling::EndBlock(&gfxContext);
            //}
        }

        /*
        {
            EngineProfiling::BeginBlock(L"Voxelize", &gfxContext);
            gfxContext.SetViewport(0, 0, scene_gi.vxgi.res, scene_gi.vxgi.res);


            EngineProfiling::EndBlock(&gfxContext);


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
		*/

        EngineProfiling::EndBlock(&BaseContext);
    }

    void VXGI_Resolve(
        CommandContext& BaseContext, const Math::Camera& camera, const ShadowCamera& shadowCamera,
        const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
    {
        /*
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
		*/
    }

}
