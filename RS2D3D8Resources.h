//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Direct3D 8 GPU resource creation and destruction.
//
//	This is the one place allowed to create or destroy a Direct3D 8 GPU resource.
//	The legacy UDX classes - CVertex, CTexture, CTexList, COffScreen - ask for
//	resources here instead of reaching sv3.pDev themselves, so pool choice, format
//	choice and release order have a single owner.  Replacing the backend means
//	replacing this file, not auditing the whole tree again.
//
//	Unlike RS2Renderer.h and RS2RenderResource.h this header is Direct3D 8
//	specific by design, and says so in its name.  The handles are the same ones
//	the callers already use: v0.0.5 moves ownership, not the handle type.
//	Changing the handle type would drag CMesh and the fixed-function state code
//	into this release - see docs/v0.0.5-resource-inventory.md section 3.
//
//	The vertex-buffer alias LPVB8 belongs to lib/vertex.h, which not every
//	caller of this header includes, so its underlying type is spelled out.

#ifndef RS2D3D8RESOURCES_H_INCLUDED
#define RS2D3D8RESOURCES_H_INCLUDED

/*
 *	Vertex buffers
 *
 *	D3DPOOL_MANAGED, so the runtime restores them across a device reset and they
 *	are not reset participants.
 */
BOOL RS2D3D8_CreateVertexBuffer(UINT bytes, DWORD fvf, LPDIRECT3DVERTEXBUFFER8 *ppOut);
BOOL RS2D3D8_UploadVertexBuffer(LPDIRECT3DVERTEXBUFFER8 pVB, const void *pSrc, UINT bytes);
BOOL RS2D3D8_LockVertexBuffer(LPDIRECT3DVERTEXBUFFER8 pVB, void **ppData);
void RS2D3D8_UnlockVertexBuffer(LPDIRECT3DVERTEXBUFFER8 pVB);
void RS2D3D8_ReleaseVertexBuffer(LPDIRECT3DVERTEXBUFFER8 *ppVB);

/*
 *	Textures
 *
 *	All D3DPOOL_MANAGED.  The loaders keep D3DX8 internally on purpose: the
 *	supported input formats, colour-key handling and mip behaviour are part of
 *	the content contract with existing plugins.
 */
HRESULT RS2D3D8_CreateTextureFromFile(
	LPTEX8 *ppOut, LPCSTR strFile, D3DCOLOR cTrans, int nMipLv);
HRESULT RS2D3D8_CreateTextureFromResource(
	LPTEX8 *ppOut, LPCSTR strRes, D3DCOLOR cTrans, int nMipLv);

//	Mutable texture used as a GDI text target.  A4R4G4B4, one mip level, sizes
//	rounded up to a power of two - all three are load-bearing for DrawInText().
HRESULT RS2D3D8_CreateMutableTexture(LPTEX8 *ppOut, int w, int h);

void RS2D3D8_ReleaseTexture(LPTEX8 *ppTex);

/*
 *	Offscreen render target
 *
 *	The only default-pool resources in the program.  Their owner must be a reset
 *	participant; see docs/v0.0.5-resource-inventory.md section 2.
 */
BOOL RS2D3D8_CreateRenderTargetTexture(LPTEX8 *ppOut, int w, int h);
BOOL RS2D3D8_CreateDepthStencilSurface(LPSURF8 *ppOut, int w, int h);
void RS2D3D8_ReleaseSurface(LPSURF8 *ppSurf);

/*
 *	Live-resource counters, for validation rather than gameplay.
 */
unsigned int RS2D3D8_GetLiveVertexBufferCount();
unsigned int RS2D3D8_GetLiveTextureCount();
unsigned int RS2D3D8_GetLiveSurfaceCount();

#endif	//	RS2D3D8RESOURCES_H_INCLUDED
