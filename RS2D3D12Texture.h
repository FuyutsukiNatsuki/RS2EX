//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	Direct3D 12 sampled-texture allocation and upload lifetime.
//
//	This is renderer-internal.  The neutral texture boundary sees only the
//	opaque payload and its operations; native resources stay here.

#ifndef RS2D3D12TEXTURE_H_INCLUDED
#define RS2D3D12TEXTURE_H_INCLUDED

#include "RS2D3D12.h"
#include "RS2DecodedImage.h"

#include <list>
#include <string>

struct RS2TexturePayloadOps;
struct RS2D3D12PendingTextureUpload;

class CRS2D3D12TextureUpload
{
private:
	ID3D12Device *m_Device;
	ID3D12CommandQueue *m_Queue;
	ID3D12Fence *m_Fence;
	HANDLE m_FenceEvent;
	UINT64 m_NextFenceValue;
	UINT64 m_LastSubmittedFence;
	std::list<RS2D3D12PendingTextureUpload *> m_Pending;
	unsigned int m_PendingPeak;
	unsigned int m_WaitCount;
	UINT64 m_SubmittedBytes;

	CRS2D3D12TextureUpload(const CRS2D3D12TextureUpload &);
	CRS2D3D12TextureUpload &operator=(const CRS2D3D12TextureUpload &);

	void ReleasePending(RS2D3D12PendingTextureUpload *pending);

public:
	CRS2D3D12TextureUpload();
	~CRS2D3D12TextureUpload();

	bool Create(ID3D12Device *device, ID3D12CommandQueue *queue);
	void Destroy();

	bool CreateTexture(
		const CRS2DecodedImage &image,
		void **payload,
		std::string *error);

	void CollectCompleted();
	bool WaitForAll();

	unsigned int GetPendingCount() const{
		return (unsigned int)m_Pending.size(); }
	unsigned int GetPendingPeak() const{ return m_PendingPeak; }
	unsigned int GetWaitCount() const{ return m_WaitCount; }
	UINT64 GetSubmittedBytes() const{ return m_SubmittedBytes; }
};

const RS2TexturePayloadOps *RS2D3D12_GetTexturePayloadOps();
unsigned int RS2D3D12_GetLiveTextureCount();
unsigned int RS2D3D12_GetPeakTextureCount();

//	WP3 validation.  Repeatedly decodes, submits and destroys textures without
//	a descriptor or draw, then performs one batch wait and verifies baselines.
bool RS2D3D12TextureUploadSmoke(CRS2D3D12TextureUpload *upload);

#endif	// RS2D3D12TEXTURE_H_INCLUDED
