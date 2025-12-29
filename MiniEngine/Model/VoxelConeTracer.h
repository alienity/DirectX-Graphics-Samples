#pragma once
#include "../Core/pch.h"
#include "../Core/GpuResource.h"
#include "../Core/VectorMath.h"
#include "Model.h"
#include <vector>

class ColorBuffer;
class BoolVar;
class NumVar;
class ComputeContext;

namespace VCT
{
	extern BoolVar Enable;
	extern BoolVar DebugDraw;

	extern NumVar VoxelSize; // = 20 / 128
	extern NumVar VoxelRes; // 128

	void Initialize(void);
	void Shutdown(void);
	void Render(ComputeContext& Context);

	void Voxelize(CommandContext& context);
	void LightInjection(CommandContext& context);
	void GenerateMipmaps(CommandContext& context);

	//==========================================================================
	class VoxelVolume : public GpuResource
	{
	public:
		void Create(uint32_t size, uint32_t mipLevels = 1);
		D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(uint32_t mipLevel = 0) const;
		D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const;

	private:
		uint32_t m_Size;
		uint32_t m_MipLevels;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_UAVs;
		D3D12_CPU_DESCRIPTOR_HANDLE m_SRV;
	};
	//==========================================================================


} // namespace VCT