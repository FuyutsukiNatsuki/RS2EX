//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	The pool, format and mip choices below are inherited from the UDX library
//	(Copyright (c) 2002 Midikyou) - lib/vertex.cpp, lib/texture.h, lib/texture.cpp
//	and lib/offscreen.cpp.  They are moved here, not redesigned: every one of them
//	is observable, either as content compatibility or as memory use.

#include "stdafx.h"
#include "RS2D3D8Resources.h"

//	Live-resource counters.  Incremented on successful creation, decremented on
//	release, so a leak or a double release shows up as drift rather than silence.
static unsigned int s_LiveVertexBuffers = 0;
static unsigned int s_LiveTextures = 0;
static unsigned int s_LiveSurfaces = 0;
static unsigned int s_LiveIndexBuffers = 0;

unsigned int RS2D3D8_GetLiveVertexBufferCount(){ return s_LiveVertexBuffers; }
unsigned int RS2D3D8_GetLiveTextureCount(){ return s_LiveTextures; }
unsigned int RS2D3D8_GetLiveSurfaceCount(){ return s_LiveSurfaces; }
unsigned int RS2D3D8_GetLiveIndexBufferCount(){ return s_LiveIndexBuffers; }

////////////////////////////////////////////////////////////////////////////////
//	Vertex buffers
////////////////////////////////////////////////////////////////////////////////

/*
 *	Create a vertex buffer
 *
 *	bytes	: buffer size
 *	fvf		: vertex format
 *	ppOut	: receives the buffer, left NULL on failure
 *
 *	[RS2EX] Moved from CVertex::Create().  D3DPOOL_MANAGED and usage 0 are the
 *	original choices and are what keeps vertex buffers out of reset handling.
 */
BOOL RS2D3D8_CreateVertexBuffer(UINT bytes, DWORD fvf, LPDIRECT3DVERTEXBUFFER8 *ppOut){
	if(!ppOut) return FALSE;
	*ppOut = NULL;

	if(!sv3.pDev) return FALSE;

	HRESULT hr = sv3.pDev->CreateVertexBuffer(
		bytes, 0, fvf, D3DPOOL_MANAGED, ppOut);

	if(FAILED(hr)){
		Debug("[RS2EX Resource] vertex buffer %u bytes failed\n", bytes);
		*ppOut = NULL;
		return FALSE;
	}
	s_LiveVertexBuffers++;
	return TRUE;
}

/*
 *	Fill a vertex buffer from system memory
 */
BOOL RS2D3D8_UploadVertexBuffer(LPDIRECT3DVERTEXBUFFER8 pVB, const void *pSrc, UINT bytes){
	if(!pVB || !pSrc) return FALSE;

	void *pDst;
	if(FAILED(pVB->Lock(0, bytes, (BYTE **)&pDst, 0))) return FALSE;

	memcpy(pDst, pSrc, bytes);
	pVB->Unlock();
	return TRUE;
}

/*
 *	Lock a whole vertex buffer
 */
BOOL RS2D3D8_LockVertexBuffer(LPDIRECT3DVERTEXBUFFER8 pVB, void **ppData){
	if(!pVB || !ppData) return FALSE;
	return SUCCEEDED(pVB->Lock(0, 0/*whole buffer*/, (BYTE **)ppData, 0));
}

void RS2D3D8_UnlockVertexBuffer(LPDIRECT3DVERTEXBUFFER8 pVB){
	if(pVB) pVB->Unlock();
}

void RS2D3D8_ReleaseVertexBuffer(LPDIRECT3DVERTEXBUFFER8 *ppVB){
	if(!ppVB || !*ppVB) return;

	(*ppVB)->Release();
	*ppVB = NULL;
	if(s_LiveVertexBuffers) s_LiveVertexBuffers--;
}

////////////////////////////////////////////////////////////////////////////////
//	Index buffers
////////////////////////////////////////////////////////////////////////////////

/*
 *	Create an index buffer
 *
 *	bytes	: buffer size
 *	ppOut	: receives the buffer, left NULL on failure
 *
 *	[RS2EX] New in v0.0.6.  Managed pool and 16-bit indices, matching the vertex
 *	buffers: mesh geometry is static, so it stays out of reset handling.
 */
BOOL RS2D3D8_CreateIndexBuffer(UINT bytes, LPDIRECT3DINDEXBUFFER8 *ppOut){
	if(!ppOut) return FALSE;
	*ppOut = NULL;

	if(!sv3.pDev) return FALSE;

	HRESULT hr = sv3.pDev->CreateIndexBuffer(
		bytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, ppOut);

	if(FAILED(hr)){
		Debug("[RS2EX Resource] index buffer %u bytes failed\n", bytes);
		*ppOut = NULL;
		return FALSE;
	}
	s_LiveIndexBuffers++;
	return TRUE;
}

BOOL RS2D3D8_LockIndexBuffer(LPDIRECT3DINDEXBUFFER8 pIB, void **ppData){
	if(!pIB || !ppData) return FALSE;
	return SUCCEEDED(pIB->Lock(0, 0/*whole buffer*/, (BYTE **)ppData, 0));
}

void RS2D3D8_UnlockIndexBuffer(LPDIRECT3DINDEXBUFFER8 pIB){
	if(pIB) pIB->Unlock();
}

void RS2D3D8_ReleaseIndexBuffer(LPDIRECT3DINDEXBUFFER8 *ppIB){
	if(!ppIB || !*ppIB) return;

	(*ppIB)->Release();
	*ppIB = NULL;
	if(s_LiveIndexBuffers) s_LiveIndexBuffers--;
}

////////////////////////////////////////////////////////////////////////////////
//	Textures
////////////////////////////////////////////////////////////////////////////////

/*
 *	Load a texture from a file
 *
 *	[RS2EX] Moved from the LOAD_TEXTURE() inline in lib/texture.h, which put a
 *	D3DX call in a header included by almost every translation unit.  Every
 *	argument is unchanged: A8R8G8B8, managed pool, D3DX_DEFAULT filters, and the
 *	colour key the caller supplies.
 */
HRESULT RS2D3D8_CreateTextureFromFile(
	LPTEX8 *ppOut, LPCSTR strFile, D3DCOLOR cTrans, int nMipLv
){
	if(!ppOut) return E_POINTER;
	*ppOut = NULL;

	HRESULT hr = D3DXCreateTextureFromFileExA(
		sv3.pDev, strFile, 0, 0, nMipLv, 0,
		D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
		D3DX_DEFAULT, D3DX_DEFAULT,
		cTrans, NULL, NULL, ppOut);

	if(SUCCEEDED(hr)) s_LiveTextures++;
	else *ppOut = NULL;
	return hr;
}

/*
 *	Load a texture from a Win32 resource (bitmap only)
 *
 *	[RS2EX] Moved from LOAD_TEXTURE_RES().  Note the point filters here differ
 *	from the file loader above; that is original behaviour, not an oversight.
 */
HRESULT RS2D3D8_CreateTextureFromResource(
	LPTEX8 *ppOut, LPCSTR strRes, D3DCOLOR cTrans, int nMipLv
){
	if(!ppOut) return E_POINTER;
	*ppOut = NULL;

	HRESULT hr = D3DXCreateTextureFromResourceExA(
		sv3.pDev, NULL, strRes, 0, 0, nMipLv, 0,
		D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
		D3DTEXF_POINT, D3DTEXF_POINT,
		cTrans, NULL, NULL, ppOut);

	if(SUCCEEDED(hr)) s_LiveTextures++;
	else *ppOut = NULL;
	return hr;
}

/*
 *	Create a writable texture
 *
 *	w, h	: requested size, rounded up to a power of two
 *
 *	[RS2EX] Moved from CTexture::Create().  A4R4G4B4 with one mip level is not
 *	an arbitrary choice - DrawInText() writes 16-bit pixels into the locked
 *	surface by hand, so widening the format would silently corrupt every string
 *	texture in the game.
 */
HRESULT RS2D3D8_CreateMutableTexture(LPTEX8 *ppOut, int w, int h){
	if(!ppOut) return E_POINTER;
	*ppOut = NULL;

	const int texW = (int)powf(2, ceilf(logf((float)w)/logf(2.0f)));
	const int texH = (int)powf(2, ceilf(logf((float)h)/logf(2.0f)));

	HRESULT hr = sv3.pDev->CreateTexture(
		texW, texH, 1, 0, D3DFMT_A4R4G4B4,
		D3DPOOL_MANAGED, ppOut);

	if(SUCCEEDED(hr)) s_LiveTextures++;
	else *ppOut = NULL;
	return hr;
}

void RS2D3D8_ReleaseTexture(LPTEX8 *ppTex){
	if(!ppTex || !*ppTex) return;

	(*ppTex)->Release();
	*ppTex = NULL;
	if(s_LiveTextures) s_LiveTextures--;
}

////////////////////////////////////////////////////////////////////////////////
//	Offscreen render target
////////////////////////////////////////////////////////////////////////////////

/*
 *	Create a render-target texture
 *
 *	[RS2EX] Moved from COffScreen::Create().  D3DUSAGE_RENDERTARGET forces
 *	D3DPOOL_DEFAULT, which is exactly why the owner has to be a reset
 *	participant.  The format follows the back buffer so CopyRects() into the
 *	capture staging texture keeps working.
 */
BOOL RS2D3D8_CreateRenderTargetTexture(LPTEX8 *ppOut, int w, int h){
	if(!ppOut) return FALSE;
	*ppOut = NULL;

	if(!sv3.pDev) return FALSE;

	HRESULT hr = sv3.pDev->CreateTexture(
		w, h, 1, D3DUSAGE_RENDERTARGET,
		sv3.d3dpp.BackBufferFormat,
		D3DPOOL_DEFAULT, ppOut);

	if(FAILED(hr)){
		Debug("[RS2EX Resource] render target %d x %d failed\n", w, h);
		*ppOut = NULL;
		return FALSE;
	}
	s_LiveTextures++;
	return TRUE;
}

/*
 *	Create a depth/stencil surface
 *
 *	[RS2EX] Moved from COffScreen::Create(), including the original comment that
 *	a D3DUSAGE_DEPTHSTENCIL texture could not be created, which is why this is a
 *	plain surface.  Always default pool.
 */
BOOL RS2D3D8_CreateDepthStencilSurface(LPSURF8 *ppOut, int w, int h){
	if(!ppOut) return FALSE;
	*ppOut = NULL;

	if(!sv3.pDev) return FALSE;

	HRESULT hr = sv3.pDev->CreateDepthStencilSurface(
		w, h,
		sv3.d3dpp.AutoDepthStencilFormat,
		sv3.d3dpp.MultiSampleType,
		ppOut);

	if(FAILED(hr)){
		Debug("[RS2EX Resource] depth surface %d x %d failed\n", w, h);
		*ppOut = NULL;
		return FALSE;
	}
	s_LiveSurfaces++;
	return TRUE;
}

void RS2D3D8_ReleaseSurface(LPSURF8 *ppSurf){
	if(!ppSurf || !*ppSurf) return;

	(*ppSurf)->Release();
	*ppSurf = NULL;
	if(s_LiveSurfaces) s_LiveSurfaces--;
}
