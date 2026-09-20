//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20.

#include "..\RS2RenderResource.h"

/*
 *	[RS2EX] The only owner of D3DPOOL_DEFAULT resources in the program.
 *
 *	A render-target texture and a depth surface do not survive a device
 *	reset, and the two surfaces borrowed from the swap chain in Begin()
 *	stop Reset() from succeeding at all while they are held.  So this class
 *	participates in the reset lifecycle rather than being reset underneath.
 */
class COffScreen : public IRS2RendererResetParticipant{
	LPTEX8	m_pTex;		//	テクスチャー
	LPSURF8	m_pRT;		//	レンダリングターゲット
	LPSURF8	m_pZB;		//	Ｚバッファ
	LPSURF8 m_pOldRT;	//	レンダリングターゲット（退避用）
	LPSURF8 m_pOldZB;	//	Ｚバッファ（退避用）
	BOOL	m_fRender;	//	レンダリング可能か？

	//	[RS2EX] Logical size, kept across a reset so the resources can be
	//	rebuilt.  Never ask a released resource how big it was.
	int		m_Width;
	int		m_Height;

	//	[RS2EX] True between Begin() and End(), i.e. while the offscreen
	//	target is bound and the saved swap-chain surfaces are held.
	bool	m_Active;

	BOOL CreateNative(int w, int h);

public:
	COffScreen();
	~COffScreen();

	BOOL Create(int w, int h);
	void Free();
	BOOL Begin(D3DCOLOR c = 0xff000000);
	void End();

	//	[RS2EX] Reset lifecycle - see RS2RenderResource.h.
	virtual void OnRendererBeforeReset();
	virtual bool OnRendererAfterReset();

	/*
	 *	テクスチャーの取得
	 *
	 *	[RS2EX] Legacy compatibility exposure, not the ownership path.  One
	 *	caller: HidefCapture() takes surface level 0 for the CopyRects readback.
	 *	Readback is category H and deferred.  Returns NULL between a device reset
	 *	and a successful recreate, so callers must not assume it is valid.
	 */
	LPTEX8 GetTexture(){return m_pTex;}
};
