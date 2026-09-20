//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	The pool and format choices below are not made here - they live in
//	RS2D3D8Resources, where v0.0.5 centralised them.  This file owns lifetime and
//	identity; that file owns how a texture is actually created.

#include "stdafx.h"
#include "RS2TextureResource.h"
#include "RS2D3D8Resources.h"

bool RS2TextureRef::GetSize(int *width, int *height) const{
	if(!m_Resource) return false;

	if(width) *width = m_Resource->GetWidth();
	if(height) *height = m_Resource->GetHeight();
	return true;
}

CRS2TextureResource::CRS2TextureResource()
	: m_Native(0), m_Width(0), m_Height(0)
{
}

CRS2TextureResource::~CRS2TextureResource(){
	Free();
}

void CRS2TextureResource::Free(){
	if(!m_Native) return;

	LPTEX8 tex = (LPTEX8)m_Native;
	RS2D3D8_ReleaseTexture(&tex);

	m_Native = 0;
	m_Width = 0;
	m_Height = 0;
}

/*
 *	Take ownership of a freshly created backend texture.
 *
 *	The size is read once, here, so callers never have to ask the device how big
 *	a texture is.
 */
void CRS2TextureResource::AdoptNativeFromBackend(void *native, int width, int height){
	Free();

	m_Native = native;
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

	if(!m_Native) return false;

	D3DLOCKED_RECT rect;
	if(FAILED(((LPTEX8)m_Native)->LockRect(0, &rect, NULL, 0))) return false;

	out->bits = rect.pBits;
	out->pitch = rect.Pitch;
	return true;
}

void CRS2TextureResource::Unlock(){
	if(!m_Native) return;

	((LPTEX8)m_Native)->UnlockRect(0);
}

////////////////////////////////////////////////////////////////////////////////
//	Creation
////////////////////////////////////////////////////////////////////////////////

/*
 *	Wrap a created backend texture, reading its real size.
 *
 *	D3DX rounds non-power-of-two images up, so the requested size and the actual
 *	size are not the same thing - callers have always wanted the actual one.
 */
static CRS2TextureResource *RS2AdoptTexture(LPTEX8 tex){
	if(!tex) return 0;

	D3DSURFACE_DESC desc;
	int w = 0, h = 0;

	if(SUCCEEDED(tex->GetLevelDesc(0, &desc))){
		w = (int)desc.Width;
		h = (int)desc.Height;
	}

	CRS2TextureResource *resource = new CRS2TextureResource;
	resource->AdoptNativeFromBackend(tex, w, h);

	return resource;
}

CRS2TextureResource *RS2CreateTextureFromFile(
	const char *strFile, unsigned long cTrans, int nMipLv
){
	LPTEX8 tex = 0;

	if(FAILED(RS2D3D8_CreateTextureFromFile(&tex, strFile, cTrans, nMipLv))) return 0;
	return RS2AdoptTexture(tex);
}

CRS2TextureResource *RS2CreateTextureFromResource(
	const char *strRes, unsigned long cTrans, int nMipLv
){
	LPTEX8 tex = 0;

	if(FAILED(RS2D3D8_CreateTextureFromResource(&tex, strRes, cTrans, nMipLv))) return 0;
	return RS2AdoptTexture(tex);
}

CRS2TextureResource *RS2CreateMutableTexture(int w, int h){
	LPTEX8 tex = 0;

	if(FAILED(RS2D3D8_CreateMutableTexture(&tex, w, h))) return 0;
	return RS2AdoptTexture(tex);
}

void RS2DestroyTexture(CRS2TextureResource *resource){
	if(!resource) return;

	delete resource;
}
