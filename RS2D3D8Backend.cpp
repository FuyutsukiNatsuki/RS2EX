//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Device lifecycle ownership arrives here in stages; each stage is its own
//	commit so the move stays reviewable.  Until a stage lands, the method below
//	says so rather than pretending to own something it does not.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D8Backend.h"

//	Owned by lib/graphic.cpp; the clear mask depends on the depth/stencil
//	format the backend picked at start-up.
extern DWORD g_BufferClearMode;

CRS2D3D8Backend::CRS2D3D8Backend()
	: m_Initialized(false),
	  m_ViewportWidth(1),
	  m_ViewportHeight(1)
{
}

CRS2D3D8Backend::~CRS2D3D8Backend(){
	Shutdown();
}

const char *CRS2D3D8Backend::GetName() const{
	return "Direct3D 8 (Legacy)";
}

/*
 *	Re-seed the tracked viewport from the current back buffer
 *
 *	Direct3D 8 silently resets the viewport to the full render target on both
 *	CreateDevice() and Reset(), so the tracked size has to follow.
 */
void CRS2D3D8Backend::SyncViewportToBackBuffer(){
	m_ViewportWidth = sv3.d3dpp.BackBufferWidth;
	m_ViewportHeight = sv3.d3dpp.BackBufferHeight;
}

/*
 *	3Dデバイスの作成
 *
 *	width	: ビューポート横幅
 *	height: ビューポート縦幅
 *
 *	[RS2EX] Moved verbatim from lib/graphic.cpp Create3DDevice().
 */
bool CRS2D3D8Backend::CreateDevice(int width, int height){
	sv3.width = width;
	sv3.height = height;

	SelectDisplayAdapter();	//	ディスプレイアダプタの選択(sv3.iAdapter)
	SetPresentParam();		//	デバイスパラメータの指定(sv3.d3dpp)

	//	ウインドウモードならウインドウを表示する
	if(sv3.fWindowed){
		ShowWindow(svw.hWnd, SW_SHOW);
		UpdateWindow(svw.hWnd);
	}
	HRESULT hr;

	//	TnL HAL Device
	strcpy(sv3.type, "TnLHAL");

	hr = sv3.pD3D->CreateDevice(
		sv3.iAdapter, D3DDEVTYPE_HAL, svw.hWnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING, &sv3.d3dpp, &sv3.pDev);

	if(FAILED(hr)){
		//	HAL Device
		strcpy(sv3.type, "HAL");

		hr = sv3.pD3D->CreateDevice(
			sv3.iAdapter, D3DDEVTYPE_HAL, svw.hWnd,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING, &sv3.d3dpp, &sv3.pDev);

		if(FAILED(hr)){
			//	REF Device
			strcpy(sv3.type, "REF");

			hr = sv3.pD3D->CreateDevice(
				sv3.iAdapter, D3DDEVTYPE_REF, svw.hWnd,
				D3DCREATE_SOFTWARE_VERTEXPROCESSING, &sv3.d3dpp, &sv3.pDev);
			FAILED_ASSERT("3Dデバイスが作成できません.", hr);
		}
	}
	Debug("デバイスタイプ = %s\n", sv3.type);
	return TRUE;
}

/*
 *	Direct3Dの初期化
 *
 *	[RS2EX] Moved from lib/graphic.cpp InitDirect3D().  The debug banner and
 *	the g_DispWidth/g_DispHeight lookup stay behind in the compatibility
 *	wrapper; everything that touches the device belongs here.
 *
 *	width	: back buffer width
 *	height	: back buffer height
 */
bool CRS2D3D8Backend::Initialize(int width, int height){
	//	Direct3Dの作成
	sv3.pD3D = Direct3DCreate8(D3D_SDK_VERSION);

	ASSERT("Direct3Dの初期化に失敗しました.", sv3.pD3D); 

	//	デバイスの作成
	if(!CreateDevice(width, height)) return false;

	GetDeviceCaps();	//	デバイス能力の取得

	//	環境設定
	InitMetrics();
	InitRenderState();

	//	関連オブジェクトの作成
	SetDirLight(VEC3(1, -1, 1), MAKE_CV(0.5f, 0.5f, 0.5f, 0.0f));
	D3DXCreateSprite(sv3.pDev, &sv3.pSpr);
	CreateFont(FONT_HEIGHT, 0xffffffff, FW_NORMAL);

	Debug("[RS2EX Renderer] device = %s\n", sv3.type);

	SyncViewportToBackBuffer();
	m_Initialized = true;
	return true;
}

/*
 *	Direct3Dの解放
 *
 *	[RS2EX] Moved from lib/graphic.cpp FreeDirect3D().  The release order is
 *	unchanged and stays safe on a partially initialised device.
 */
void CRS2D3D8Backend::Shutdown(){
	FreeFont();
	RELEASE(sv3.pSpr);
	RELEASE(sv3.pDev);
	RELEASE(sv3.pD3D);

	m_Initialized = false;
}

/*
 *	reset
 *
 *	[RS2EX] Moved from lib/graphic.cpp ResetD3DDevice().  The sequence is
 *	unchanged: the D3DX font holds default-pool resources and has to be
 *	released before Reset() and rebuilt after it.  That coupling is why a
 *	renderer backend still calls into the font module; the clean fix is a
 *	default-pool resource registry, which does not exist yet.
 */
bool CRS2D3D8Backend::Reset()
{
	FreeFont();
	HRESULT hr = sv3.pDev->Reset(&sv3.d3dpp);
	if(FAILED(hr))
	{
		Debug("error D3DDevice Reset\n", hr);
		SendWM_CLOSE();
		return false;
	}
	CreateFont();
	InitMetrics();
	InitRenderState();

	//	Reset() leaves the viewport covering the whole back buffer.
	SyncViewportToBackBuffer();

	Debug("[RS2EX Renderer] reset completed\n");
	return true;
}

/*
 *	シーンの開始
 *
 *	[RS2EX] Moved from lib/graphic.cpp BeginScene().  Device health, the
 *	resize-triggered reset, the D3D scene begin, the buffer clear and the view
 *	transform are all backend work.  The inverse/world-to-screen matrices are
 *	not - they are engine math and stay in the compatibility wrapper.
 *
 *	The checks run once per pass, not once per frame, because stereo and
 *	window division begin several passes per frame.  That is what 2.15 did.
 *
 *	clearColor		: clear colour, used only when clearColorBuffer is true
 *	clearColorBuffer: false to keep the colour buffer and clear depth only
 */
bool CRS2D3D8Backend::BeginRenderPass(
	unsigned int clearColor,
	bool clearColorBuffer
){
	//	デバイスのテスト
	HRESULT hr = sv3.pDev->TestCooperativeLevel();

	if(hr==D3DERR_DEVICELOST){
		Debug("3Dデバイスがロストしています.\n");
		return false;
	}else if(hr==D3DERR_DEVICENOTRESET){
		Debug("3Dデバイスのリセットが必要です.\n");
		/*
		 *	本来ならここで sv3.pDev->Reset()を呼び出してデバイスの再設定
		 *	を試みるべきだが、その前にデバイスに関連するオブジェクトを解放
		 *	しておかなくてはならない。
		 *
		 *	そのタイミングをプログラマに通知したり、解放の義務をおしつける
		 *	のは当ライブラリのコンセプトに反する。　
		 *
		 *	従って、ここでプログラム終了させることにする。
		 */
	//	SendWM_CLOSE();
		if(!Reset()) return false;
	}
	else if(sv3.d3dpp.BackBufferWidth!=svw.winW || sv3.d3dpp.BackBufferHeight!=svw.winH)
	{
		Debug("バッファサイズを変更します.\n");
		sv3.d3dpp.BackBufferWidth	= svw.winW;
		sv3.d3dpp.BackBufferHeight	= svw.winH;
		sv3.width  = svw.winW;
		sv3.height = svw.winH;
		AffectWindowSize();
		if(!Reset()) return false;
	}
	//	シーン開始
	if(FAILED(sv3.pDev->BeginScene())) return false;

	//	クリア
	sv3.pDev->Clear(0, NULL,
		(clearColorBuffer ? D3DCLEAR_TARGET : 0)|g_BufferClearMode, clearColor, 1.0f, 0);
	//	ビュートランスフォーム
	sv3.pDev->SetTransform(D3DTS_VIEW, &sv3.mtxView);

	return true;
}

/*
 *	シーンの終了
 *
 *	[RS2EX] Ending a pass no longer presents it.  Stereo, window division and
 *	offscreen rendering all finish a D3D scene without putting it on screen,
 *	and offscreen must never reach the swap chain at all.
 */
void CRS2D3D8Backend::EndRenderPass(){
	sv3.pDev->EndScene();
}

void CRS2D3D8Backend::Present(){
	sv3.pDev->Present(NULL, NULL, NULL, NULL);
}

/*
 *	GPU viewport submission
 *
 *	[RS2EX] The D3DVIEWPORT8 half of lib/view.cpp SetViewport().  The
 *	engine-side viewport matrix used by WorldToScreen/ScreenToWorld is a
 *	different job and stays there.
 */
void CRS2D3D8Backend::SetViewport(
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height,
	float minZ,
	float maxZ
){
	D3DVIEWPORT8 vp;
	vp.X = x;
	vp.Y = y;
	vp.Width = width;
	vp.Height = height;
	vp.MinZ = minZ;
	vp.MaxZ = maxZ;
	sv3.pDev->SetViewport(&vp);

	m_ViewportWidth = width;
	m_ViewportHeight = height;
}

void CRS2D3D8Backend::GetViewportSize(unsigned int *width, unsigned int *height) const{
	if(width) *width = m_ViewportWidth;
	if(height) *height = m_ViewportHeight;
}
