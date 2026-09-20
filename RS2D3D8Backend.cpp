//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Device lifecycle ownership arrives here in stages; each stage is its own
//	commit so the move stays reviewable.  Until a stage lands, the method below
//	says so rather than pretending to own something it does not.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D8Backend.h"

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

//	--- not yet owned by the backend -------------------------------------------
//	Filled in by the render-pass and viewport commits.

bool CRS2D3D8Backend::BeginRenderPass(unsigned int clearColor, bool clearColorBuffer){
	(void)clearColor;
	(void)clearColorBuffer;
	return false;
}

void CRS2D3D8Backend::EndRenderPass(){
}

void CRS2D3D8Backend::Present(){
}

void CRS2D3D8Backend::SetViewport(
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height,
	float minZ,
	float maxZ
){
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)minZ;
	(void)maxZ;
}

void CRS2D3D8Backend::GetViewportSize(unsigned int *width, unsigned int *height) const{
	if(width) *width = m_ViewportWidth;
	if(height) *height = m_ViewportHeight;
}
