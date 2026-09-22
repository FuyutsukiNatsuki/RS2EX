//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//	Modified for RS2EX on 2026-09-22.
//
//	This file owns neutral lifetime and identity.  It never interprets a payload:
//	the backend that created one supplies the operations that destroy or lock it.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D12Unsupported.h"
#include "RS2TextureResource.h"
#include "RS2D3D8Resources.h"
#include "RS2D3D12TextureBackend.h"

bool RS2TextureRef::GetSize(int *width, int *height) const{
	if(!m_Resource) return false;

	if(width) *width = m_Resource->GetWidth();
	if(height) *height = m_Resource->GetHeight();
	return true;
}

CRS2TextureResource::CRS2TextureResource()
	: m_Backend(RS2_RENDERER_D3D8),
	  m_Payload(0),
	  m_Ops(0),
	  m_Width(0),
	  m_Height(0)
{
}

CRS2TextureResource::~CRS2TextureResource(){
	Free();
}

void CRS2TextureResource::Free(){
	if(m_Payload && m_Ops && m_Ops->destroy) m_Ops->destroy(m_Payload);

	m_Backend = RS2_RENDERER_D3D8;
	m_Payload = 0;
	m_Ops = 0;
	m_Width = 0;
	m_Height = 0;
}

/*
 *	Take ownership of a freshly created backend texture.
 *
 *	The size is read once, here, so callers never have to ask the device how big
 *	a texture is.
 */
void CRS2TextureResource::AdoptPayloadFromBackend(
	RS2RendererBackendType backend,
	void *payload,
	int width,
	int height,
	const RS2TexturePayloadOps *ops
){
	Free();

	m_Backend = backend;
	m_Payload = payload;
	m_Ops = ops;
	m_Width = width;
	m_Height = height;
}

/*
 *	Lock the whole texture for writing.
 *
 *	out		: receives the pixel pointer and the row pitch
 *
 *	returns	: false if there is nothing to lock
 */
bool CRS2TextureResource::Lock(RS2TextureLock *out){
	if(!out) return false;

	out->bits = 0;
	out->pitch = 0;

	if(!m_Payload || !m_Ops || !m_Ops->lock) return false;
	return m_Ops->lock(m_Payload, out);
}

void CRS2TextureResource::Unlock(){
	if(m_Payload && m_Ops && m_Ops->unlock) m_Ops->unlock(m_Payload);
}

////////////////////////////////////////////////////////////////////////////////
//	Creation
////////////////////////////////////////////////////////////////////////////////

/*
 *	Wrap a created D3D8 payload. D3D12 uses the same neutral owner below,
 *	with its own operations and no native handle crossing this boundary.
 */
static CRS2TextureResource *RS2AdoptD3D8Texture(
	void *payload,
	int width,
	int height
){
	if(!payload) return 0;

	CRS2TextureResource *resource = new CRS2TextureResource;
	resource->AdoptPayloadFromBackend(
		RS2_RENDERER_D3D8,
		payload,
		width,
		height,
		RS2D3D8_GetTexturePayloadOps());

	return resource;
}

CRS2TextureResource *RS2CreateTextureFromFile(
	const char *strFile, unsigned long cTrans, int nMipLv
){
	void *payload = 0;
	int width = 0, height = 0;
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12){
		if(!RS2D3D12_CreateTexturePayloadFromFile(
				&payload, &width, &height, strFile, cTrans, nMipLv)) return 0;
		CRS2TextureResource *resource = new CRS2TextureResource;
		resource->AdoptPayloadFromBackend(RS2_RENDERER_D3D12,
			payload, width, height, RS2D3D12_GetTexturePayloadOps());
		return resource;
	}

	if(!RS2D3D8_CreateTexturePayloadFromFile(
		&payload, &width, &height, strFile, cTrans, nMipLv)) return 0;
	return RS2AdoptD3D8Texture(payload, width, height);
}

CRS2TextureResource *RS2CreateTextureFromResource(
	const char *strRes, unsigned long cTrans, int nMipLv
){
	void *payload = 0;
	int width = 0, height = 0;
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12){
		if(!RS2D3D12_CreateTexturePayloadFromResource(
				&payload, &width, &height, strRes, cTrans, nMipLv)) return 0;
		CRS2TextureResource *resource = new CRS2TextureResource;
		resource->AdoptPayloadFromBackend(RS2_RENDERER_D3D12,
			payload, width, height, RS2D3D12_GetTexturePayloadOps());
		return resource;
	}

	if(!RS2D3D8_CreateTexturePayloadFromResource(
		&payload, &width, &height, strRes, cTrans, nMipLv)) return 0;
	return RS2AdoptD3D8Texture(payload, width, height);
}

CRS2TextureResource *RS2CreateMutableTexture(int w, int h){
	//	[RS2EX] Direct3D 12 has no texture creation in v0.1.0, and the
	//	Direct3D 8 helper below would use a device this backend does not
	//	own.  Failing is the honest answer; the caller already handles a
	//	texture that would not load.
	if(GetRS2Renderer().GetBackendType()!=RS2_RENDERER_D3D8){
		RS2D3D12Unsupported("RS2CreateMutableTexture");
		return 0;
	}

	void *payload = 0;
	int width = 0, height = 0;

	if(!RS2D3D8_CreateMutableTexturePayload(
		&payload, &width, &height, w, h)) return 0;
	return RS2AdoptD3D8Texture(payload, width, height);
}

void RS2DestroyTexture(CRS2TextureResource *resource){
	if(!resource) return;

	delete resource;
}

////////////////////////////////////////////////////////////////////////////////
//	Neutral ownership smoke
////////////////////////////////////////////////////////////////////////////////

struct RS2TextureOwnershipProbe
{
	int *destroyed;
	int *locked;
	int *unlocked;
};

static void RS2TextureOwnershipProbeDestroy(void *payload){
	RS2TextureOwnershipProbe *probe = (RS2TextureOwnershipProbe *)payload;
	if(!probe) return;
	(*probe->destroyed)++;
	delete probe;
}

static bool RS2TextureOwnershipProbeLock(void *payload, RS2TextureLock *out){
	RS2TextureOwnershipProbe *probe = (RS2TextureOwnershipProbe *)payload;
	if(!probe || !out) return false;

	(*probe->locked)++;
	out->bits = probe;
	out->pitch = 128;
	return true;
}

static void RS2TextureOwnershipProbeUnlock(void *payload){
	RS2TextureOwnershipProbe *probe = (RS2TextureOwnershipProbe *)payload;
	if(probe) (*probe->unlocked)++;
}

bool RS2TextureOwnershipSmoke(){
	static const RS2TexturePayloadOps ops = {
		RS2TextureOwnershipProbeDestroy,
		RS2TextureOwnershipProbeLock,
		RS2TextureOwnershipProbeUnlock
	};

	int destroyed = 0;
	int locked = 0;
	int unlocked = 0;
	int cycle;

	for(cycle = 0; cycle<3; cycle++){
		RS2TextureOwnershipProbe *probe = new RS2TextureOwnershipProbe;
		probe->destroyed = &destroyed;
		probe->locked = &locked;
		probe->unlocked = &unlocked;

		CRS2TextureResource *resource = new CRS2TextureResource;
		resource->AdoptPayloadFromBackend(
			RS2_RENDERER_D3D12, probe, 32, 16, &ops);

		RS2TextureRef ref = resource->GetRef();
		RS2TextureRef copy = ref;
		int width = 0, height = 0;
		RS2TextureLock lock;

		const bool valid =
			resource->IsValid() &&
			resource->GetBackendForBackend()==RS2_RENDERER_D3D12 &&
			resource->GetPayloadForBackend()==probe &&
			resource->IsOwnedByBackend(RS2_RENDERER_D3D12) &&
			!resource->IsOwnedByBackend(RS2_RENDERER_D3D8) &&
			ref==copy &&
			ref.GetSize(&width, &height) &&
			width==32 && height==16 &&
			resource->Lock(&lock) && lock.bits==probe && lock.pitch==128;

		resource->Unlock();
		resource->Free();
		const bool released = !resource->IsValid() && destroyed==cycle+1;
		RS2DestroyTexture(resource); // destructor must make a second Free harmless

		if(!valid || !released || locked!=cycle+1 || unlocked!=cycle+1)
			return false;
	}

	return destroyed==3 && locked==3 && unlocked==3;
}
