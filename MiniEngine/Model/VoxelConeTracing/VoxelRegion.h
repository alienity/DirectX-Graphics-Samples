#pragma once
#include "../Core/pch.h"
#include "../Core/GpuResource.h"
#include "../Core/VectorMath.h"
#include "../Core/Math/BoundingBox.h"
#include <vector>

namespace VCT
{
    struct VoxelRegion
    {
        VoxelRegion()
        {
        }

        VoxelRegion(const Math::Vector3& minPos, const Math::Vector3& extent, float voxelSize)
            : minPos(minPos), extent(extent), voxelSize(voxelSize)
        {
        }

        /**
         * Returns the minimum position of the clip region in world coordinates.
         */
        Math::Vector3 getMinPosWorld() const { return Math::Vector3(minPos) * voxelSize; }

        /**
        * Returns the maximum position of the clip region in world coordinates.
        */
        Math::Vector3 getMaxPosWorld() const { return Math::Vector3(getMaxPos()) * voxelSize; }

        /**
        * Returns the minimum position of the clip region in image coordinates using toroidal addressing.
        * Note: The % operator is expected to be defined by the C++11 standard.
        */
        Math::Vector3 getMinPosImage(const Math::Vector3& imageSize) const;
        //  { return ((minPos % imageSize) + imageSize) % imageSize; }

        /**
        * Returns the maximum position of the clip region in image coordinates using toroidal addressing.
        * Note: The % operator is expected to be defined by the C++11 standard.
        */
        Math::Vector3 getMaxPosImage(const Math::Vector3& imageSize) const;
        // { return ((getMaxPos() % imageSize) + imageSize) % imageSize; }

        /**
         * Returns the maximum position in local voxel coordinates.
         */
        Math::Vector3 getMaxPos() const { return minPos + extent; }

        /**
         * Returns the extent in world coordinates.
         */
        Math::Vector3 getExtentWorld() const { return Math::Vector3(extent) * voxelSize; }

        Math::Vector3 getCenterPosWorld() const { return getMinPosWorld() + getExtentWorld() * 0.5f; }

        VoxelRegion toPrevLevelRegion() const;
        //{
        //    return VoxelRegion(minPos * 2, extent * 2, voxelSize / 2.0f);
        //}

        VoxelRegion toNextLevelRegion() const;
        //{
        //    // extent + 1 is used to make sure that the upper bound is computed
        //    return VoxelRegion(minPos / 2, (extent + Math::Vector3(1)) / 2, voxelSize * 2.0f);
        //}

        Math::Vector3 minPos; // The minimum position in local voxel coordinates
        Math::Vector3 extent{0}; // The extent of the region in local voxel coordinates 
        float voxelSize{0.0f}; // Voxel size in world coordinates

        // Note: the voxel size in local voxel coordinates is always 1
    };
} // namespace VCT
