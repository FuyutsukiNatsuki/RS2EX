//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21.

#include "headers.h"
#include "debug.h"
#include "graphic.h"
#include "texture.h"
#include "offscreen.h"
#include "..\RS2Renderer.h"
#include "..\RS2RenderResource.h"
#include "..\RS2D3D8Resources.h"

/*
 *	コンストラクタ
 */
COffScreen::COffScreen(){
	m_pTex = NULL;
	m_pRT = NULL;
	m_pZB = NULL;
	m_fRender = FALSE;

	//	[RS2EX] 2.15 left these two uninitialised.  They are released
	//	unconditionally before a device reset now, so they have to start NULL.
	m_pOldRT = NULL;
	m_pOldZB = NULL;

	m_Width = 0;
	m_Height = 0;
	m_Active = false;

	//	Registered for its whole lifetime, not just while resources exist: the
	//	registry is cheap and this removes a create/free state machine.
	GetRS2ResetRegistry().Register(this);
}

/*
 *	デストラクタ
 */
COffScreen::~COffScreen(){
	//	Unregister first: being destroyed is not the same event as a device	
	//	reset, and the registry must never hold a dangling participant.
	GetRS2ResetRegistry().Unregister(this);
	Free();
}

/*
 *	サーフェイスの作成
 *
 *	w, h	: サイズ　※2の乗数のみ
 */
BOOL COffScreen::Create(int w, int h){
	//	[RS2EX] Readback is a deferred capability (v0.0.9 WP9).  A backend
	//	that cannot read rendered pixels answers false here, and this path
	//	stops rather than reaching for a device it does not have.
	if(!GetRS2Renderer().SupportsReadback()) return FALSE;

	Free();	//	既存なら解放

	//	[RS2EX] Remember the request before touching the device, so a reset can
	//	rebuild the same thing later.
	m_Width = w;
	m_Height = h;

	return CreateNative(w, h);
}

/*
 *	[RS2EX] Build the GPU resources for the current logical size.
 *
 *	Shared by Create() and by reset recovery.  Creation itself lives in the
 *	D3D8 resource module; what stays here is the ownership and the rule that a
 *	half-built offscreen buffer is never left usable.
 */
BOOL COffScreen::CreateNative(int w, int h){
	if(!RS2D3D8_CreateRenderTargetTexture(&m_pTex, w, h)) return FALSE;

	if(!RS2D3D8_CreateDepthStencilSurface(&m_pZB, w, h)){
		RS2D3D8_ReleaseTexture(&m_pTex);
		return FALSE;
	}
	m_fRender = TRUE;
	return TRUE;
}

/*
 *	サーフェイスの解放
 */
void COffScreen::Free(){
	m_fRender = FALSE;
	m_Active = false;

	RS2D3D8_ReleaseTexture(&m_pTex);
	RS2D3D8_ReleaseSurface(&m_pZB);

	//	[RS2EX] Logically destroyed, so a later reset must not resurrect it.
	m_Width = 0;
	m_Height = 0;
}


/*
 *	[RS2EX] The device is about to be reset
 *
 *	Everything here is released, owned or borrowed.  The owned render target
 *	and depth surface are default pool and would be destroyed anyway; the two
 *	surfaces borrowed from the swap chain are worse than that, because Reset()
 *	fails outright while they are outstanding.
 *
 *	An offscreen pass in flight is abandoned rather than rescued.  Rescuing it
 *	means editing HidefCapture(), which is capture work and out of scope; the
 *	screenshot is lost but the process is not.
 */
void COffScreen::OnRendererBeforeReset(){
	if(m_Active){
		Debug("[RS2EX Resource] offscreen pass abandoned by device reset\n");
		m_Active = false;
	}

	//	Borrowed from the swap chain, so released plainly - they were never
	//	counted as resources this program created.
	RELEASE(m_pOldRT);
	RELEASE(m_pOldZB);
	RELEASE(m_pRT);

	RS2D3D8_ReleaseTexture(&m_pTex);
	RS2D3D8_ReleaseSurface(&m_pZB);

	//	Native resources are gone; the logical size is deliberately kept.
	m_fRender = FALSE;
}

/*
 *	[RS2EX] The device has been reset successfully
 *
 *	returns	: false if the resources could not be rebuilt
 *
 *	Only rebuilds what was logically created.  A failure leaves the object
 *	disabled rather than half-built, so Begin() simply returns FALSE and
 *	capture degrades instead of crashing.  The size is kept so a later reset
 *	can still recover.
 */
bool COffScreen::OnRendererAfterReset(){
	if(!m_Width || !m_Height) return true;

	if(!CreateNative(m_Width, m_Height)){
		Debug("[RS2EX Resource] offscreen %d x %d not recreated after reset\n",
			m_Width, m_Height);
		return false;
	}
	return true;
}
/*
 *	[RS2EX] Tell the renderer which viewport the device is really on.
 *
 *	SetRenderTarget() silently resets the viewport to the full size of the
 *	new target, so an offscreen pass changes it without anyone asking.
 *	Re-submitting the same values is a no-op for the device and keeps the
 *	renderer's tracked viewport true - which is what CCamera reads.
 *
 *	Deliberately not the global SetViewport(): that would also rewrite
 *	sv3.mtxVPort, which 2.15 leaves alone across an offscreen pass.
 */
static void SyncRendererViewport(LPSURF8 surface){
	if(!surface) return;

	D3DSURFACE_DESC desc;
	if(FAILED(surface->GetDesc(&desc))) return;

	GetRS2Renderer().SetViewport(0, 0, desc.Width, desc.Height);
}

/*
 *	レンダリング開始
 */
BOOL COffScreen::Begin(D3DCOLOR c){
	if(!m_fRender) return FALSE;

	//	現在のスクリーン設定を退避
	sv3.pDev->GetRenderTarget(&m_pOldRT);
	sv3.pDev->GetDepthStencilSurface(&m_pOldZB);

	//	[RS2EX] Active from the moment the swap-chain surfaces are borrowed,
	//	not from the moment the target is bound: those references are what a
	//	device reset trips over, and what End() has to give back even if the
	//	rest of Begin() fails.
	m_Active = true;

	//	新しいスクリーン設定をセット
	m_pTex->GetSurfaceLevel(0, &m_pRT);

	if(FAILED(sv3.pDev->SetRenderTarget(m_pRT, m_pZB)))
		return FALSE;

	SyncRendererViewport(m_pRT);

	//	シーンの開始
	if(!BeginScene(c)) return FALSE;
	return TRUE;
}

/*
 *	レンダリング終了
 *
 *	[RS2EX] Ends the pass through the renderer and never presents it: an
 *	offscreen target must not reach the swap chain.  The render-target and
 *	depth-surface resources below stay native for now.
 */
void COffScreen::End(){
	//	[RS2EX] Was m_fRender.  A device reset inside the pass clears m_Active
	//	and has already rebound the back buffer, so there is nothing to restore
	//	and the saved surfaces are gone.  This also makes End() without Begin()
	//	harmless instead of undefined.
	if(!m_Active) return;

	//	シーンの終了、スクリーン設定の復元
	GetRS2Renderer().EndRenderPass();
	RELEASE(m_pRT);

	sv3.pDev->SetRenderTarget(m_pOldRT, m_pOldZB);
	SyncRendererViewport(m_pOldRT);
	RELEASE(m_pOldRT);
	RELEASE(m_pOldZB);

	m_Active = false;
}
