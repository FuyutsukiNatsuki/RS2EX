//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23, 2026-09-24, 2026-09-25.
//
//	Direct3D 12 sampled-texture allocation and upload lifetime.
//
//	This is renderer-internal.  The neutral texture boundary sees only the
//	opaque payload and its operations; native resources stay here.

#ifndef RS2D3D12TEXTURE_H_INCLUDED
#define RS2D3D12TEXTURE_H_INCLUDED

#include "RS2D3D12.h"
#include "RS2D3D12Descriptors.h"
#include "RS2D3D12TextureBackend.h"
#include "RS2DecodedImage.h"
#include "RS2TextureSource.h"

#include <list>
#include <string>

struct RS2TexturePayloadOps;
struct RS2D3D12PendingTextureUpload;
class CRS2D3D12Backend;
class RS2TextureRef;

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

	//	The general path: RGBA8 rows or native BC blocks, described by the
	//	source in the units it stores them in.
	bool CreateTexture(
		const CRS2TextureSource &source,
		void **payload,
		std::string *error);

	//	The accepted v0.1.2 path, now a view onto the general one.
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
unsigned int RS2D3D12_GetLiveDDSTextureCount();
unsigned int RS2D3D12_GetPeakDDSTextureCount();

//	Whether the texture bound to Stage 0 came from a DDS.  Diagnostics only.
bool RS2D3D12_BoundTextureIsDDS();

// Per-backend observations for WP9's real-scene re-inventory. These counters
// do not change texture policy and are reset when a new backend starts.
struct RS2D3D12TextureRuntimeStats
{
	unsigned int fileAttempts, fileSuccesses;
	unsigned int resourceAttempts, resourceSuccesses;
	unsigned int stage0Binds, stage0Unbinds, rejectedBinds, otherStageBinds;
	unsigned int pointFilters, linearFilters, rejectedFilters;

	//	Stage 1 (v0.1.4).  otherStageBinds above now counts stages past 1.
	unsigned int stage1Binds, stage1Unbinds;
	unsigned int stage1PointFilters, stage1LinearFilters;

	//	DDS, a subset of the file counts above.
	unsigned int ddsAttempts, ddsSuccesses, ddsFailures;
	unsigned int ddsBC1, ddsBC3;
	unsigned int ddsMipShortfalls;	//	fewer mips stored than asked for
	unsigned long long ddsUploadBytes;
};
void RS2D3D12_ResetTextureRuntimeStats();
const RS2D3D12TextureRuntimeStats &RS2D3D12_GetTextureRuntimeStats();

//	Mutable textures (v0.1.6): the string texture's A4R4G4B4 atlas and the
//	live-text line.  The caller writes a CPU copy between Lock and Unlock; the
//	changed rectangle reaches the GPU texture immediately before the next draw
//	that has the texture bound, recorded in that draw's command list.
struct RS2D3D12MutableStats
{
	unsigned int creates, createFailures, live, peak;
	unsigned int locks, unlocks;
	unsigned int refusedLocks;		//	Lock while already locked: refused, as on Direct3D 8
	unsigned int unlocksWithoutLock;	//	ignored
	unsigned int destroyedLocked;
	unsigned int uploads;			//	copies recorded
	unsigned int coalescedUnlocks;		//	Unlocks folded into a later copy (no draw between)
	unsigned int unchangedUnlocks;		//	nothing differed from what the GPU holds
	unsigned int refusedUploads;		//	frame scratch full: left pending
	unsigned long long uploadBytes;		//	RGBA8 bytes copied
	unsigned int largestUpload;		//	bytes, one copy
	unsigned int draws;			//	draws with a mutable texture bound
	unsigned int liveTextDraws;		//	RS2DrawText lines drawn
};
const RS2D3D12MutableStats &RS2D3D12_GetMutableStats();
void RS2D3D12_CountLiveTextDraw();

//	Record the pending upload of any mutable texture bound to stage 0 or 1.
//	Called for every draw, before it is recorded.
void RS2D3D12_PrepareBoundTextures(CRS2D3D12Backend *backend, ID3D12GraphicsCommandList *list);

//	Save / restore the stage bindings and filters around a draw the renderer
//	makes on its own behalf (the live-text line).  One level deep.
void RS2D3D12_PushTextureBinding();
void RS2D3D12_PopTextureBinding();

bool RS2D3D12_GetBoundTexture(
	CRS2D3D12Backend *backend, RS2D3D12SrvSlot *slot, RS2TextureFilter *filter);

//	Stage 0 or 1.  False when the stage has no live texture of this backend.
bool RS2D3D12_GetBoundStageTexture(
	CRS2D3D12Backend *backend, unsigned int stage,
	RS2D3D12SrvSlot *slot, RS2TextureFilter *filter);

//	WP3 validation.  Repeatedly decodes, submits and destroys textures without
//	a descriptor or draw, then performs one batch wait and verifies baselines.
bool RS2D3D12TextureUploadSmoke(CRS2D3D12TextureUpload *upload);

#endif	// RS2D3D12TEXTURE_H_INCLUDED
