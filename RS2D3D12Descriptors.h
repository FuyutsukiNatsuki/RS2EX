// RS2EX - RailSim II development fork
// Created for RS2EX on 2026-09-23.
// D3D12-only sampled-resource descriptors and Stage 0 samplers.

#ifndef RS2D3D12DESCRIPTORS_H_INCLUDED
#define RS2D3D12DESCRIPTORS_H_INCLUDED

#include "RS2D3D12.h"
#include "RS2RenderState.h"

// v0.1.2 WP0 measured 94 simultaneous immutable textures and chose 128.
// v0.1.3 WP0B measured 178 in a layout with DDS trains, past that limit.
// The installed content holds 907 image files in all (PNG 132, BMP 643,
// DDS 132), so 1024 slots hold every installed image at once with room to
// spare.  A slot is one descriptor, a few tens of bytes; the heap is about
// 32 KB, far below the shader-visible limit.  Exhaustion still fails
// explicitly - the capacity is a measured bound, not a promise.
#define RS2D3D12_SRV_CAPACITY 1024

struct RS2D3D12SrvSlot
{
	unsigned int index;
	unsigned int serial;
	RS2D3D12SrvSlot() : index(0xffffffffu), serial(0){}
};

class CRS2D3D12Descriptors
{
private:
	ID3D12DescriptorHeap *m_SrvHeap;
	ID3D12DescriptorHeap *m_SamplerHeap;
	UINT m_SrvStride;
	UINT m_SamplerStride;
	unsigned int m_Next;
	unsigned int m_FreeCount;
	unsigned int m_Free[RS2D3D12_SRV_CAPACITY];
	unsigned int m_Serial[RS2D3D12_SRV_CAPACITY];
	bool m_Used[RS2D3D12_SRV_CAPACITY];
	unsigned int m_NextSerial;
	unsigned int m_Live;
	unsigned int m_Peak;

	CRS2D3D12Descriptors(const CRS2D3D12Descriptors &);
	CRS2D3D12Descriptors &operator=(const CRS2D3D12Descriptors &);

public:
	CRS2D3D12Descriptors();
	~CRS2D3D12Descriptors();

	bool Create(ID3D12Device *device);
	void Destroy();
	bool Allocate(RS2D3D12SrvSlot *out);
	bool Free(const RS2D3D12SrvSlot &slot);
	bool IsLive(const RS2D3D12SrvSlot &slot) const;
	bool WriteTexture(
		ID3D12Device *device,
		const RS2D3D12SrvSlot &slot,
		ID3D12Resource *texture,
		unsigned int mipCount);
	bool GetSrvHandles(
		const RS2D3D12SrvSlot &slot,
		D3D12_CPU_DESCRIPTOR_HANDLE *cpu,
		D3D12_GPU_DESCRIPTOR_HANDLE *gpu) const;
	bool GetSamplerHandle(
		RS2TextureFilter filter,
		D3D12_GPU_DESCRIPTOR_HANDLE *gpu) const;
	void BindHeaps(ID3D12GraphicsCommandList *list) const;

	ID3D12DescriptorHeap *GetSrvHeap() const{ return m_SrvHeap; }
	ID3D12DescriptorHeap *GetSamplerHeap() const{ return m_SamplerHeap; }
	unsigned int GetCapacity() const{ return RS2D3D12_SRV_CAPACITY; }
	unsigned int GetLive() const{ return m_Live; }
	unsigned int GetPeak() const{ return m_Peak; }
};

bool RS2D3D12DescriptorsSmoke(CRS2D3D12Descriptors *descriptors, ID3D12Device *device);

#endif // RS2D3D12DESCRIPTORS_H_INCLUDED
