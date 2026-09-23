//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23, 2026-09-24.
//
//	See RS2D3D12Texture.h.

#include "stdafx.h"
#include "RS2D3D12Texture.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12Unsupported.h"
#include "RS2TextureResource.h"
#include "RS2DDS.h"

struct RS2D3D12TexturePayload
{
	ID3D12Resource *texture;
	unsigned int mipCount;
	D3D12_RESOURCE_STATES finalState;
	CRS2D3D12Backend *owner;
	RS2D3D12SrvSlot slot;
	bool dds;
};

struct RS2D3D12PendingTextureUpload
{
	ID3D12CommandAllocator *allocator;
	ID3D12GraphicsCommandList *list;
	ID3D12Resource *upload;
	ID3D12Resource *texture;
	UINT64 fenceValue;
};

static unsigned int s_LiveTextures = 0;
static unsigned int s_PeakTextures = 0;
static unsigned int s_LiveDDSTextures = 0;
static unsigned int s_PeakDDSTextures = 0;
//	Stages 0 and 1, the only ones RailSim uses (v0.1.4 audit).  Kept apart
//	so each stage has its own texture and its own filter at the same time.
static RS2D3D12TexturePayload *s_Bound[2] = { 0, 0 };
static RS2TextureFilter s_Filter[2] = { RS2_FILTER_POINT, RS2_FILTER_POINT };
static RS2D3D12TextureRuntimeStats s_RuntimeStats;

static bool RS2D3D12TextureError(std::string *error, const char *what, HRESULT hr){
	char text[256];

	if(hr==S_OK) wsprintfA(text, "%s", what);
	else wsprintfA(text, "%s (0x%08lx)", what, (unsigned long)hr);
	if(error) *error = text;
	Debug("[RS2EX D3D12 Texture] %s\n", text);
	return false;
}

static void RS2D3D12DestroyTexturePayload(void *opaque){
	RS2D3D12TexturePayload *payload = (RS2D3D12TexturePayload *)opaque;
	if(!payload) return;

	if(s_Bound[0]==payload) s_Bound[0] = 0;
	if(s_Bound[1]==payload) s_Bound[1] = 0;
	if(payload->owner && RS2D3D12GetActiveBackend()==payload->owner){
		payload->owner->RetireTexture(payload->texture, payload->slot);
		payload->texture = 0;
	}else RELEASE(payload->texture);
	if(payload->dds && s_LiveDDSTextures) s_LiveDDSTextures--;
	delete payload;
	if(s_LiveTextures) s_LiveTextures--;
}

static const RS2TexturePayloadOps s_TextureOps = {
	RS2D3D12DestroyTexturePayload,
	0,
	0
};

const RS2TexturePayloadOps *RS2D3D12_GetTexturePayloadOps(){ return &s_TextureOps; }
unsigned int RS2D3D12_GetLiveTextureCount(){ return s_LiveTextures; }
unsigned int RS2D3D12_GetPeakTextureCount(){ return s_PeakTextures; }
unsigned int RS2D3D12_GetLiveDDSTextureCount(){ return s_LiveDDSTextures; }
unsigned int RS2D3D12_GetPeakDDSTextureCount(){ return s_PeakDDSTextures; }
bool RS2D3D12_BoundTextureIsDDS(){ return s_Bound[0] && s_Bound[0]->dds; }
void RS2D3D12_ResetTextureRuntimeStats(){
	ZeroMemory(&s_RuntimeStats, sizeof(s_RuntimeStats));
}
const RS2D3D12TextureRuntimeStats &RS2D3D12_GetTextureRuntimeStats(){
	return s_RuntimeStats;
}

void RS2D3D12_ResetTextureBinding(){
	s_Bound[0] = s_Bound[1] = 0;
	s_Filter[0] = s_Filter[1] = RS2_FILTER_POINT;
}

/*
 *	Bind a texture to stage 0 or 1.
 *
 *	The same resource and the same shader view serve either stage: a texture
 *	used on stage 1 gets no second descriptor.  A texture that is not a live
 *	Direct3D 12 texture of this backend leaves the stage empty rather than
 *	holding on to something it cannot sample.  Stages past 1 are refused and
 *	say so - RailSim never reaches them (v0.1.4 audit).
 */
void RS2D3D12_BindTexture(unsigned int stage, const RS2TextureRef &texture){
	if(stage>1){
		s_RuntimeStats.otherStageBinds++;
		RS2D3D12Unsupported("RS2BindTexture(stage>1)");
		return;
	}
	const CRS2TextureResource *resource = texture.GetResource();
	if(!resource){
		if(stage==0) s_RuntimeStats.stage0Unbinds++;
		else s_RuntimeStats.stage1Unbinds++;
		s_Bound[stage] = 0;
		return;
	}
	if(!resource->IsOwnedByBackend(RS2_RENDERER_D3D12)){
		s_RuntimeStats.rejectedBinds++;
		Debug("[RS2EX D3D12 Texture] stage %u bind rejected a foreign texture\n", stage);
		s_Bound[stage] = 0;
		return;
	}
	RS2D3D12TexturePayload *payload =
		(RS2D3D12TexturePayload *)resource->GetPayloadForBackend();
	s_Bound[stage] = payload && payload->owner==RS2D3D12GetActiveBackend()
		&& payload->owner && payload->owner->GetDescriptors()->IsLive(payload->slot)
		? payload : 0;
	if(!s_Bound[stage]) s_RuntimeStats.rejectedBinds++;
	else if(stage==0) s_RuntimeStats.stage0Binds++;
	else s_RuntimeStats.stage1Binds++;
}

void RS2D3D12_SetTextureFilter(unsigned int stage, RS2TextureFilter filter){
	if(stage>1){
		s_RuntimeStats.rejectedFilters++;
		RS2D3D12Unsupported("RS2SetTextureFilter(stage>1)");
		return;
	}
	if(filter!=RS2_FILTER_POINT && filter!=RS2_FILTER_LINEAR){
		s_RuntimeStats.rejectedFilters++;
		RS2D3D12Unsupported("RS2SetTextureFilter(invalid filter)");
		return;
	}
	if(stage==1){
		if(filter==RS2_FILTER_LINEAR) s_RuntimeStats.stage1LinearFilters++;
		else s_RuntimeStats.stage1PointFilters++;
	}else if(filter==RS2_FILTER_LINEAR) s_RuntimeStats.linearFilters++;
	else s_RuntimeStats.pointFilters++;
	s_Filter[stage] = filter;
}

bool RS2D3D12_GetBoundTexture(
	CRS2D3D12Backend *backend, RS2D3D12SrvSlot *slot, RS2TextureFilter *filter
){
	return RS2D3D12_GetBoundStageTexture(backend, 0, slot, filter);
}

bool RS2D3D12_GetBoundStageTexture(
	CRS2D3D12Backend *backend, unsigned int stage,
	RS2D3D12SrvSlot *slot, RS2TextureFilter *filter
){
	if(stage>1) return false;

	const RS2D3D12TexturePayload *bound = s_Bound[stage];

	//	Checked at every draw, not only at bind: a texture destroyed since
	//	its bind has had its slot retired, and a retired slot is never read.
	if(!backend || !bound || bound->owner!=backend
			|| !backend->GetDescriptors()->IsLive(bound->slot)) return false;
	if(slot) *slot = bound->slot;
	if(filter) *filter = s_Filter[stage];
	return true;
}

/*
 *	Give an uploaded texture its shader view and hand it to the caller.
 *
 *	returns	: false having destroyed the payload
 */
static bool RS2D3D12_PublishTexture(
	CRS2D3D12Backend *backend, void *opaque, void **outPayload
){
	RS2D3D12TexturePayload *payload = (RS2D3D12TexturePayload *)opaque;
	RS2D3D12SrvSlot slot;
	if(!backend->GetDescriptors()->Allocate(&slot)){
		RS2D3D12DestroyTexturePayload(payload);
		return false;
	}
	if(!backend->GetDescriptors()->WriteTexture(
			backend->GetDevice(), slot, payload->texture, payload->mipCount)){
		backend->GetDescriptors()->Free(slot);
		RS2D3D12DestroyTexturePayload(payload);
		return false;
	}
	payload->owner = backend;
	payload->slot = slot;
	*outPayload = payload;
	return true;
}

/*
 *	A DDS file as an ordinary Stage 0 texture.
 *
 *	The blocks go to the GPU as stored.  When RailSim asks for more mips than
 *	the file holds, the file's levels are used and nothing is generated - the
 *	v0.1.3 decision, because making BC mips means decompressing and
 *	recompressing.  Direct3D 8 generated them, so this is a real difference
 *	and it is said once, with the counter in the scene audit carrying the
 *	rest.
 */
static bool RS2D3D12_CreateDDSTexture(
	CRS2D3D12Backend *backend, void **outPayload, int *width, int *height,
	const char *path, unsigned long colourKey, int mipArgument
){
	static bool s_ShortfallReported = false;

	s_RuntimeStats.ddsAttempts++;

	CRS2TextureSource source;
	RS2DDSInfo info;
	std::string error;

	if(!RS2LoadDDSFile(path, colourKey, mipArgument, &source, &info, &error)){
		s_RuntimeStats.ddsFailures++;
		Debug("[RS2EX D3D12 Texture] DDS refused: %s: %s\n", error.c_str(), path);
		return false;
	}

	if(info.usedMips<info.requestedMips){
		s_RuntimeStats.ddsMipShortfalls++;
		if(!s_ShortfallReported){
			s_ShortfallReported = true;
			Debug("[RS2EX D3D12 Texture] compatibility difference: a DDS asks for "
				"%u mip levels and stores %u; only stored levels are used "
				"(Direct3D 8 generated the rest).  Reported once.  First: %s\n",
				info.requestedMips, info.storedMips, path);
		}
	}

	backend->CollectRetiredTextures();

	const unsigned long long before = backend->GetTextureUpload()->GetSubmittedBytes();
	void *opaque = 0;

	if(!backend->GetTextureUpload()->CreateTexture(source, &opaque, &error)){
		s_RuntimeStats.ddsFailures++;
		Debug("[RS2EX D3D12 Texture] DDS upload failed: %s: %s\n", error.c_str(), path);
		return false;
	}
	((RS2D3D12TexturePayload *)opaque)->dds = true;
	s_LiveDDSTextures++;
	if(s_LiveDDSTextures>s_PeakDDSTextures) s_PeakDDSTextures = s_LiveDDSTextures;

	if(!RS2D3D12_PublishTexture(backend, opaque, outPayload)){
		s_RuntimeStats.ddsFailures++;
		return false;
	}

	s_RuntimeStats.ddsSuccesses++;
	if(info.format==RS2_TEXTURE_SOURCE_BC1) s_RuntimeStats.ddsBC1++;
	else s_RuntimeStats.ddsBC3++;
	s_RuntimeStats.ddsUploadBytes +=
		backend->GetTextureUpload()->GetSubmittedBytes()-before;
	*width = (int)info.width;
	*height = (int)info.height;
	return true;
}

static bool RS2D3D12_CreatePublicTexture(
	void **outPayload, int *width, int *height,
	const char *source, unsigned long colourKey, int mipArgument, bool fromResource
){
	if(outPayload) *outPayload = 0;
	if(width) *width = 0;
	if(height) *height = 0;
	if(!outPayload || !width || !height) return false;
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();
	if(!backend) return false;
	if(fromResource) s_RuntimeStats.resourceAttempts++;
	else s_RuntimeStats.fileAttempts++;

	if(!fromResource && RS2IsDDSFile(source)){
		if(!RS2D3D12_CreateDDSTexture(backend, outPayload, width, height,
				source, colourKey, mipArgument)) return false;
		s_RuntimeStats.fileSuccesses++;
		return true;
	}

	CRS2DecodedImage image;
	std::string error;
	const bool decoded = fromResource
		? RS2DecodeImageResource(source, colourKey, mipArgument, &image, &error)
		: RS2DecodeImageFile(source, colourKey, mipArgument, &image, &error);
	if(!decoded){
		Debug("[RS2EX D3D12 Texture] decode failed: %s\n", error.c_str());
		return false;
	}
	backend->CollectRetiredTextures();
	void *opaque = 0;
	if(!backend->GetTextureUpload()->CreateTexture(image, &opaque, &error)) return false;
	if(!RS2D3D12_PublishTexture(backend, opaque, outPayload)) return false;
	*width = (int)image.GetWidth();
	*height = (int)image.GetHeight();
	if(fromResource) s_RuntimeStats.resourceSuccesses++;
	else s_RuntimeStats.fileSuccesses++;
	return true;
}

bool RS2D3D12_CreateTexturePayloadFromFile(
	void **payload, int *width, int *height,
	const char *path, unsigned long colourKey, int mipArgument
){
	return RS2D3D12_CreatePublicTexture(
		payload, width, height, path, colourKey, mipArgument, false);
}

bool RS2D3D12_CreateTexturePayloadFromResource(
	void **payload, int *width, int *height,
	const char *name, unsigned long colourKey, int mipArgument
){
	return RS2D3D12_CreatePublicTexture(
		payload, width, height, name, colourKey, mipArgument, true);
}

CRS2D3D12TextureUpload::CRS2D3D12TextureUpload()
	: m_Device(0),
	  m_Queue(0),
	  m_Fence(0),
	  m_FenceEvent(NULL),
	  m_NextFenceValue(1),
	  m_LastSubmittedFence(0),
	  m_PendingPeak(0),
	  m_WaitCount(0),
	  m_SubmittedBytes(0)
{
}

CRS2D3D12TextureUpload::~CRS2D3D12TextureUpload(){ Destroy(); }

bool CRS2D3D12TextureUpload::Create(
	ID3D12Device *device,
	ID3D12CommandQueue *queue
){
	Destroy();
	if(!device || !queue) return false;

	HRESULT hr = device->CreateFence(
		0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
	if(FAILED(hr)) return RS2D3D12TextureError(0, "CreateFence failed", hr);

	m_FenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!m_FenceEvent){
		RS2D3D12TextureError(0, "CreateEvent failed", HRESULT_FROM_WIN32(GetLastError()));
		Destroy();
		return false;
	}

	m_Device = device;
	m_Queue = queue;
	m_NextFenceValue = 1;
	m_LastSubmittedFence = 0;
	m_PendingPeak = 0;
	m_WaitCount = 0;
	m_SubmittedBytes = 0;
	return true;
}

void CRS2D3D12TextureUpload::ReleasePending(
	RS2D3D12PendingTextureUpload *pending
){
	if(!pending) return;
	RELEASE(pending->list);
	RELEASE(pending->allocator);
	RELEASE(pending->upload);
	RELEASE(pending->texture);
	delete pending;
}

void CRS2D3D12TextureUpload::CollectCompleted(){
	if(!m_Fence) return;
	const UINT64 completed = m_Fence->GetCompletedValue();

	while(!m_Pending.empty()){
		RS2D3D12PendingTextureUpload *pending = m_Pending.front();
		if(pending->fenceValue>completed) break;
		m_Pending.pop_front();
		ReleasePending(pending);
	}
}

bool CRS2D3D12TextureUpload::WaitForAll(){
	if(!m_Fence || !m_FenceEvent || !m_LastSubmittedFence){
		CollectCompleted();
		return m_Pending.empty();
	}

	m_WaitCount++;
	if(m_Fence->GetCompletedValue()<m_LastSubmittedFence){
		HRESULT hr = m_Fence->SetEventOnCompletion(
			m_LastSubmittedFence, m_FenceEvent);
		if(FAILED(hr)) return RS2D3D12TextureError(0,
			"SetEventOnCompletion failed", hr);
		WaitForSingleObject(m_FenceEvent, INFINITE);
	}
	CollectCompleted();
	return m_Pending.empty();
}

void CRS2D3D12TextureUpload::Destroy(){
	if(m_Fence && !m_Pending.empty()) WaitForAll();

	while(!m_Pending.empty()){
		RS2D3D12PendingTextureUpload *pending = m_Pending.front();
		m_Pending.pop_front();
		ReleasePending(pending);
	}

	if(m_SubmittedBytes)
		Debug("[RS2EX D3D12 Texture] uploaded %I64u bytes, pending peak %u, "
			"payload live %u peak %u, DDS live %u peak %u\n",
			m_SubmittedBytes, m_PendingPeak,
			s_LiveTextures, s_PeakTextures, s_LiveDDSTextures, s_PeakDDSTextures);

	if(m_FenceEvent){
		CloseHandle(m_FenceEvent);
		m_FenceEvent = NULL;
	}
	RELEASE(m_Fence);
	m_Device = 0;
	m_Queue = 0;
	m_NextFenceValue = 1;
	m_LastSubmittedFence = 0;
	m_PendingPeak = 0;
	m_WaitCount = 0;
	m_SubmittedBytes = 0;
}

/*
 *	The DXGI format that holds a source's bytes unchanged.
 *
 *	One-to-one on purpose: a format is only listed here when the GPU can read
 *	the stored bytes as they are.  UNORM, not SRGB - Direct3D 8 never applied
 *	gamma to these, and a renderer-wide change of colour space is not this
 *	release.
 */
static DXGI_FORMAT RS2D3D12_SourceFormat(RS2TextureSourceFormat format){
	switch(format){
	case RS2_TEXTURE_SOURCE_RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case RS2_TEXTURE_SOURCE_BC1: return DXGI_FORMAT_BC1_UNORM;
	case RS2_TEXTURE_SOURCE_BC3: return DXGI_FORMAT_BC3_UNORM;
	default: return DXGI_FORMAT_UNKNOWN;
	}
}

bool CRS2D3D12TextureUpload::CreateTexture(
	const CRS2DecodedImage &image,
	void **outPayload,
	std::string *error
){
	CRS2TextureSource source;

	if(!image.IsValid() || !image.GetWidth() || !image.GetHeight()){
		if(outPayload) *outPayload = 0;
		return RS2D3D12TextureError(error, "decoded image is empty", S_OK);
	}
	if(!source.ViewDecoded(image)){
		if(outPayload) *outPayload = 0;
		return RS2D3D12TextureError(error, "decoded image cannot be described", S_OK);
	}
	return CreateTexture(source, outPayload, error);
}

bool CRS2D3D12TextureUpload::CreateTexture(
	const CRS2TextureSource &image,
	void **outPayload,
	std::string *error
){
	if(outPayload) *outPayload = 0;
	if(error) error->clear();
	if(!outPayload) return RS2D3D12TextureError(error, "null texture output", S_OK);
	if(!m_Device || !m_Queue || !m_Fence)
		return RS2D3D12TextureError(error, "texture uploader is not initialized", S_OK);
	if(!image.IsValid() || !image.GetWidth() || !image.GetHeight())
		return RS2D3D12TextureError(error, "texture source is empty", S_OK);
	if(image.GetMipCount()>0xffff)
		return RS2D3D12TextureError(error, "texture source has too many mips", S_OK);

	const DXGI_FORMAT format = RS2D3D12_SourceFormat(image.GetFormat());
	const bool blocks = RS2TextureSourceIsBlockCompressed(image.GetFormat());

	if(format==DXGI_FORMAT_UNKNOWN)
		return RS2D3D12TextureError(error, "texture source format has no DXGI equivalent", S_OK);

	//	Block-compressed resources must start at a whole number of blocks.
	//	The parser refuses anything else; this keeps the rule next to the
	//	resource that needs it.
	if(blocks && (image.GetWidth()%4 || image.GetHeight()%4))
		return RS2D3D12TextureError(error, "block-compressed size is not a multiple of 4", S_OK);

	CollectCompleted();

	D3D12_RESOURCE_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Width = image.GetWidth();
	textureDesc.Height = image.GetHeight();
	textureDesc.DepthOrArraySize = 1;
	textureDesc.MipLevels = (UINT16)image.GetMipCount();
	textureDesc.Format = format;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	D3D12_HEAP_PROPERTIES defaultHeap;
	ZeroMemory(&defaultHeap, sizeof(defaultHeap));
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

	ID3D12Resource *texture = 0;
	HRESULT hr = m_Device->CreateCommittedResource(
		&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&texture));
	if(FAILED(hr)) return RS2D3D12TextureError(error,
		"DEFAULT Texture2D creation failed", hr);

	const UINT mipCount = image.GetMipCount();
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(mipCount);
	std::vector<UINT> rows(mipCount);
	std::vector<UINT64> rowBytes(mipCount);
	UINT64 uploadBytes = 0;

	m_Device->GetCopyableFootprints(&textureDesc, 0, mipCount, 0,
		&layouts[0], &rows[0], &rowBytes[0], &uploadBytes);
	if(!uploadBytes){
		texture->Release();
		return RS2D3D12TextureError(error, "copy footprint is empty", S_OK);
	}
	if(uploadBytes>(UINT64)(SIZE_T)-1){
		texture->Release();
		return RS2D3D12TextureError(error,
			"texture upload is too large for this process", S_OK);
	}

	D3D12_HEAP_PROPERTIES uploadHeap;
	ZeroMemory(&uploadHeap, sizeof(uploadHeap));
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC uploadDesc;
	ZeroMemory(&uploadDesc, sizeof(uploadDesc));
	uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadDesc.Width = uploadBytes;
	uploadDesc.Height = 1;
	uploadDesc.DepthOrArraySize = 1;
	uploadDesc.MipLevels = 1;
	uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadDesc.SampleDesc.Count = 1;
	uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *upload = 0;
	hr = m_Device->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&upload));
	if(FAILED(hr)){
		texture->Release();
		return RS2D3D12TextureError(error, "texture upload buffer creation failed", hr);
	}

	unsigned char *mapped = 0;
	D3D12_RANGE noRead = { 0, 0 };
	hr = upload->Map(0, &noRead, (void **)&mapped);
	if(FAILED(hr)){
		upload->Release();
		texture->Release();
		return RS2D3D12TextureError(error, "texture upload buffer map failed", hr);
	}
	ZeroMemory(mapped, (SIZE_T)uploadBytes);

	//	Every level must agree with the footprint in the units it is stored
	//	in - texel rows for RGBA8, block rows for BC - or nothing is copied.
	//	A footprint of a small BC level is padded to a whole block, which is
	//	what the stored block row already is.
	bool copyValid = true;
	UINT mip;
	for(mip = 0; mip<mipCount; mip++){
		const RS2TextureSourceMip *source = image.GetMip(mip);
		const UINT expectWidth = source
			? (blocks ? (source->width+3)&~3u : source->width) : 0;
		const UINT expectHeight = source
			? (blocks ? (source->height+3)&~3u : source->height) : 0;

		if(!source || !source->data || rows[mip]!=source->rows
				|| rowBytes[mip]!=source->rowBytes
				|| source->rowPitch<source->rowBytes
				|| layouts[mip].Footprint.Width!=expectWidth
				|| layouts[mip].Footprint.Height!=expectHeight){
			copyValid = false;
			break;
		}

		UINT row;
		for(row = 0; row<rows[mip]; row++){
			memcpy(mapped+layouts[mip].Offset
					+row*layouts[mip].Footprint.RowPitch,
				source->data+row*source->rowPitch,
				(SIZE_T)rowBytes[mip]);
		}
	}
	D3D12_RANGE written = { 0, (SIZE_T)uploadBytes };
	upload->Unmap(0, &written);

	if(!copyValid){
		upload->Release();
		texture->Release();
		return RS2D3D12TextureError(error, "texture source level does not match its copy footprint", S_OK);
	}

	ID3D12CommandAllocator *allocator = 0;
	ID3D12GraphicsCommandList *list = 0;
	hr = m_Device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
	if(SUCCEEDED(hr)) hr = m_Device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL, IID_PPV_ARGS(&list));
	if(FAILED(hr)){
		RELEASE(list);
		RELEASE(allocator);
		upload->Release();
		texture->Release();
		return RS2D3D12TextureError(error, "texture copy command creation failed", hr);
	}

	for(mip = 0; mip<mipCount; mip++){
		D3D12_TEXTURE_COPY_LOCATION destination;
		D3D12_TEXTURE_COPY_LOCATION source;
		ZeroMemory(&destination, sizeof(destination));
		ZeroMemory(&source, sizeof(source));
		destination.pResource = texture;
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = mip;
		source.pResource = upload;
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = layouts[mip];
		list->CopyTextureRegion(&destination, 0, 0, 0, &source, NULL);
	}

	D3D12_RESOURCE_BARRIER barrier;
	ZeroMemory(&barrier, sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	list->ResourceBarrier(1, &barrier);

	hr = list->Close();
	if(FAILED(hr)){
		list->Release();
		allocator->Release();
		upload->Release();
		texture->Release();
		return RS2D3D12TextureError(error, "texture copy command close failed", hr);
	}

	RS2D3D12TexturePayload *payload = new RS2D3D12TexturePayload;
	payload->texture = texture;
	payload->mipCount = mipCount;
	payload->finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	payload->owner = 0;
	payload->dds = false;
	s_LiveTextures++;
	if(s_LiveTextures>s_PeakTextures) s_PeakTextures = s_LiveTextures;

	RS2D3D12PendingTextureUpload *pending = new RS2D3D12PendingTextureUpload;
	pending->allocator = allocator;
	pending->list = list;
	pending->upload = upload;
	pending->texture = texture;
	pending->texture->AddRef();
	pending->fenceValue = m_NextFenceValue++;

	ID3D12CommandList *lists[1] = { list };
	m_Queue->ExecuteCommandLists(1, lists);
	hr = m_Queue->Signal(m_Fence, pending->fenceValue);
	if(FAILED(hr)){
		//	The queue accepted the copy.  Keep every referenced object alive in
		//	the pending list; device removal/shutdown is the only safe recovery.
		m_Pending.push_back(pending);
		RS2D3D12DestroyTexturePayload(payload);
		return RS2D3D12TextureError(error, "texture upload fence signal failed", hr);
	}

	m_LastSubmittedFence = pending->fenceValue;
	m_Pending.push_back(pending);
	if(m_Pending.size()>m_PendingPeak) m_PendingPeak = (unsigned int)m_Pending.size();
	m_SubmittedBytes += uploadBytes;
	*outPayload = payload;
	return true;
}

bool RS2D3D12TextureUploadSmoke(CRS2D3D12TextureUpload *upload){
	if(!upload) return false;

	const unsigned int liveBefore = RS2D3D12_GetLiveTextureCount();
	const unsigned int waitsBefore = upload->GetWaitCount();
	const UINT64 bytesBefore = upload->GetSubmittedBytes();
	const int cycles = 16;
	int cycle;

	for(cycle = 0; cycle<cycles; cycle++){
		CRS2DecodedImage image;
		std::string error;
		if(!RS2DecodeImageResource("OPENING", 0, 0, &image, &error)) return false;

		void *payload = 0;
		if(!upload->CreateTexture(image, &payload, &error) || !payload) return false;

		RS2D3D12TexturePayload *native = (RS2D3D12TexturePayload *)payload;
		D3D12_HEAP_PROPERTIES heap;
		D3D12_HEAP_FLAGS flags;
		if(FAILED(native->texture->GetHeapProperties(&heap, &flags))
				|| heap.Type!=D3D12_HEAP_TYPE_DEFAULT
				|| native->mipCount!=image.GetMipCount()
				|| native->finalState!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE){
			RS2D3D12DestroyTexturePayload(payload);
			return false;
		}

		CRS2TextureResource *resource = new CRS2TextureResource;
		resource->AdoptPayloadFromBackend(
			RS2_RENDERER_D3D12, payload,
			(int)image.GetWidth(), (int)image.GetHeight(),
			RS2D3D12_GetTexturePayloadOps());
		RS2TextureLock lock;
		const bool neutral = resource->IsOwnedByBackend(RS2_RENDERER_D3D12)
			&& resource->GetWidth()==(int)image.GetWidth()
			&& resource->GetHeight()==(int)image.GetHeight()
			&& !resource->Lock(&lock);
		RS2DestroyTexture(resource);
		if(!neutral) return false;
	}

	//	No CreateTexture call may wait.  All sixteen uploads are owned by their
	//	fence values; this single batch wait is the accepted validation drain.
	if(upload->GetWaitCount()!=waitsBefore) return false;
	if(upload->GetSubmittedBytes()<=bytesBefore) return false;
	if(!upload->WaitForAll()) return false;
	if(upload->GetWaitCount()!=waitsBefore+1) return false;
	if(upload->GetPendingCount()!=0) return false;
	const bool baseline = RS2D3D12_GetLiveTextureCount()==liveBefore;
	Debug("RS2D3D12TEXTURE|cycles=%d bytes=%I64u pendingPeak=%u waits=1 live=%u|%s\n",
		cycles, upload->GetSubmittedBytes()-bytesBefore,
		upload->GetPendingPeak(), RS2D3D12_GetLiveTextureCount(),
		baseline ? "pass" : "FAIL");
	return baseline;
}
