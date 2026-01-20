#pragma once

#include "PixelBuffer.h"
#include "Color.h"
#include "GpuBuffer.h"

class EsramAllocator;

class VolumeBuffer : public PixelBuffer
{
public:
	VolumeBuffer(Color ClearColor = Color(0.0f, 0.0f, 0.0f, 0.0f))
		: m_ClearColor(ClearColor), m_NumMipMaps(0), m_FragmentCount(1)
	{
		m_SRVHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		for (int i = 0; i < _countof(m_UAVHandle); ++i)
			m_UAVHandle[i].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
	}

	// Create a color buffer.  If an address is supplied, memory will not be allocated.
	// The vmem address allows you to alias buffers (which can be especially useful for
	// reusing ESRAM across a frame.)
	void Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t NumMips,
		DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

	// Create a color buffer.  Memory will be allocated in ESRAM (on Xbox One).  On Windows,
	// this functions the same as Create() without a video address.
	void Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t NumMips,
		DXGI_FORMAT Format, EsramAllocator& Allocator);

	// Get pre-created CPU-visible descriptor handles
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV(void) const { return m_SRVHandle; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV(void) const { return m_UAVHandle[0]; }

	void SetClearColor(Color ClearColor) { m_ClearColor = ClearColor; }

	Color GetClearColor(void) const { return m_ClearColor; }

	// This will work for all texture sizes, but it's recommended for speed and quality
	// that you use dimensions with powers of two (but not necessarily square.)  Pass
	// 0 for ArrayCount to reserve space for mips at creation time.
	void GenerateMipMaps(CommandContext& Context);

protected:

	D3D12_RESOURCE_FLAGS CombineResourceFlags(void) const
	{
		D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE;

		if (Flags == D3D12_RESOURCE_FLAG_NONE && m_FragmentCount == 1)
			Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		return Flags;
	}

	void CreateTextureResource(ID3D12Device* Device, const std::wstring& Name, const D3D12_RESOURCE_DESC& ResourceDesc,
		D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

	void CreateTextureResource(ID3D12Device* Device, const std::wstring& Name, const D3D12_RESOURCE_DESC& ResourceDesc,
		EsramAllocator& Allocator);

	// Compute the number of texture levels needed to reduce to 1x1.  This uses
	// _BitScanReverse to find the highest set bit.  Each dimension reduces by
	// half and truncates bits.  The dimension 256 (0x100) has 9 mip levels, same
	// as the dimension 511 (0x1FF).
	static inline uint32_t ComputeNumMips(uint32_t Width, uint32_t Height, uint32_t Depth)
	{
		uint32_t HighBit;
		_BitScanReverse((unsigned long*)&HighBit, Width | Height | Depth);
		return HighBit + 1;
	}

	void CreateDerivedViews(ID3D12Device* Device, DXGI_FORMAT Format, uint32_t DepthSize, uint32_t NumMips = 1);

	D3D12_RESOURCE_DESC DescribeTex3D(uint32_t Width, uint32_t Height, uint32_t DepthOrArraySize, 
		uint32_t NumMips, DXGI_FORMAT Format, UINT Flags);

	Color m_ClearColor;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle[12];
	uint32_t m_NumMipMaps; // number of texture sublevels
	uint32_t m_FragmentCount;
};
