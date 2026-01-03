#include "VoxelRegion.h"

using namespace Math;

namespace VCT
{
    Math::Vector3 VoxelRegion::getMinPosImage(const Math::Vector3& imageSize) const
    {
        int minPosX = (int)minPos.GetX();
        int minPosY = (int)minPos.GetY();
        int minPosZ = (int)minPos.GetZ();

        int imageSizeX = (int)imageSize.GetX();
        int imageSizeY = (int)imageSize.GetY();
        int imageSizeZ = (int)imageSize.GetZ();

        Vector3 outPos = Vector3(
            ((minPosX % imageSizeX) + imageSizeX) % imageSizeX,
            ((minPosY % imageSizeY) + imageSizeY) % imageSizeY,
            ((minPosZ % imageSizeZ) + imageSizeZ) % imageSizeZ);

        return outPos;
    }

    Math::Vector3 VoxelRegion::getMaxPosImage(const Math::Vector3& imageSize) const
    {
        Math::Vector3 maxPos = getMaxPos();

        int maxPosX = (int)maxPos.GetX();
        int maxPosY = (int)maxPos.GetY();
        int maxPosZ = (int)maxPos.GetZ();

        int imageSizeX = (int)imageSize.GetX();
        int imageSizeY = (int)imageSize.GetY();
        int imageSizeZ = (int)imageSize.GetZ();

        Vector3 outPos = Vector3(
            ((maxPosX % imageSizeX) + imageSizeX) % imageSizeX,
            ((maxPosY % imageSizeY) + imageSizeY) % imageSizeY,
            ((maxPosZ % imageSizeZ) + imageSizeZ) % imageSizeZ);

        return outPos;
    }

    VoxelRegion VoxelRegion::toPrevLevelRegion() const
    {
        return VoxelRegion(minPos * 2, extent * 2, voxelSize * 2.0f);
    }

    VoxelRegion VoxelRegion::toNextLevelRegion() const
    {
        // extent + 1 is used to make sure that the upper bound is computed
        return VoxelRegion(minPos / 2, (extent + Math::Vector3(1)) / 2, voxelSize / 2.0f);
    }
}
