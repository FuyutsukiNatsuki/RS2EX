//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Device lifecycle ownership arrives here in stages; each stage is its own
//	commit so the move stays reviewable.  Until a stage lands, the method below
//	says so rather than pretending to own something it does not.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D8Backend.h"
#include "RS2RenderResource.h"

//	Owned by lib/graphic.cpp; the clear mask depends on the depth/stencil
//	format the backend picked at start-up.
extern DWORD g_BufferClearMode;

//	Set by the depth/stencil format search below and read by the shadow code
//	in CConfigMode/GraphicCover.  Defined in lib/graphic.cpp.
extern bool g_StencilEnabled;

//	Start-up mode selection, unchanged: -win, the plugin viewer and the
//	fullscreen preference all still decide windowed mode the same way.
extern char *g_PluginViewArg;
extern bool g_FullScreen;

//	--- Direct3D 8 device-creation helpers --------------------------------------
//
//	[RS2EX] Moved verbatim from lib/graphic.cpp, where they were declared in
//	graphic.h but never called from outside it.  Adapter enumeration, present
//	parameters, the depth/stencil format search and the capability query are
//	as backend-specific as code gets; nothing outside a Direct3D 8 backend can
//	use them, so they are file-private here.

static void SelectDisplayAdapter();
static BOOL SetPresentParam();
static D3DFORMAT FindDepthStencilFormat(D3DFORMAT form);
static const char *FormatToString(D3DFORMAT f);
static void GetDeviceCaps();

/*
 *	ディスプレイアダプタの選択
 */
static void SelectDisplayAdapter(){
	//	アダプタ数の取得
	int num = sv3.pD3D->GetAdapterCount();
	Debug("アダプタ数 = %d\n", num);

	//	アダプタの選択
	Debug("アダプタID = ");

	if(num>=2 && CheckArguments("-2nd")){
		Debug("1\n");
		sv3.iAdapter = 1;
	}else{
		Debug("D3DADAPTER_DEFAULT\n");
		sv3.iAdapter = D3DADAPTER_DEFAULT;
	}
	//	アダプタ情報の取得
	D3DADAPTER_IDENTIFIER8 id;

	sv3.pD3D->GetAdapterIdentifier(sv3.iAdapter, 0, &id);
	Debug("アダプタ名 = %s\n", id.Description);
}

/*
 *	デバイスパラメータの初期化
 */
static BOOL SetPresentParam(){
	//	ディスプレイモードをカウント(リフレッシュレートの違いも考慮)
	UINT num = sv3.pD3D->GetAdapterModeCount(sv3.iAdapter);
	Debug("アダプタのモード数 = %d\n", num);

	//	現在のサーフェイスフォーマットを取得
	D3DDISPLAYMODE mode;
	D3DFORMAT formatAlt = D3DFMT_UNKNOWN;

	sv3.pD3D->GetAdapterDisplayMode(sv3.iAdapter, &mode);
	sv3.format = mode.Format;

	Debug("現在のモード = %d x %d %s\n",
		mode.Width, mode.Height, FormatToString(mode.Format));

	//	ディスプレイモードを列挙
	for(int i = 0; i<num; i++){
		sv3.pD3D->EnumAdapterModes(sv3.iAdapter, i, &mode);

		//	解像度が一致するか？
		if(mode.Width==sv3.width && mode.Height==sv3.height){
			//	フォーマットが一致するか？
			if(mode.Format==sv3.format)
				break;	//	列挙終了
			else
				formatAlt = mode.Format;	//	代替フォーマットを保存
		}
	}
	Debug("ウインドウ化 = ");

	if(mode.Format!=sv3.format){
		//	一致するモードがないので代替フォーマットを使用する
		sv3.format = formatAlt;

		Debug("OFF(フォーマット変更)\n");
		sv3.fWindowed = FALSE;
	}else{
		//	スクリーンモードの決定
		if(!g_FullScreen || g_PluginViewArg || CheckArguments("-win")){
			Debug("ON\n");
			sv3.fWindowed = TRUE;
		}else{
			Debug("OFF\n");
			sv3.fWindowed = FALSE;
		}
	}
	Debug("選択されたモード = %d x %d %s ",
		sv3.width, sv3.height, FormatToString(sv3.format));

	//	パラメータ設定
	ZeroMemory(&sv3.d3dpp, sizeof(sv3.d3dpp));

	sv3.d3dpp.BackBufferCount = 1;
	sv3.d3dpp.BackBufferWidth = sv3.width;
	sv3.d3dpp.BackBufferHeight = sv3.height;
	sv3.d3dpp.BackBufferFormat = sv3.format;

	sv3.d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
	sv3.d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

	sv3.d3dpp.Windowed = sv3.fWindowed;
	sv3.d3dpp.hDeviceWindow = svw.hWnd;

	sv3.d3dpp.EnableAutoDepthStencil = TRUE;
	sv3.d3dpp.AutoDepthStencilFormat = FindDepthStencilFormat(sv3.format);

	Debug("(%s)\n", FormatToString(sv3.d3dpp.AutoDepthStencilFormat));

	return TRUE;
}

/*
 *	使用可能なデプス／ステンシルバッファのフォーマットを検索
 */
static D3DFORMAT FindDepthStencilFormat(D3DFORMAT form){
	HRESULT hr;

#define TEST_DSFORMAT(dev, dsf) \
	hr = sv3.pD3D->CheckDeviceFormat( /* サポートしているか？ */ \
		sv3.iAdapter, dev, \
		form, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, dsf); \
	if(SUCCEEDED(hr)) { \
		hr = sv3.pD3D->CheckDepthStencilMatch( /* 互換性テスト */ \
			sv3.iAdapter, dev, \
			form, form, dsf); \
		if(SUCCEEDED(hr)) return dsf; \
	}

	g_StencilEnabled = true;
	TEST_DSFORMAT( D3DDEVTYPE_HAL, D3DFMT_D24S8 );
	g_StencilEnabled = true;
	TEST_DSFORMAT( D3DDEVTYPE_HAL, D3DFMT_D24X4S4 );
	g_StencilEnabled = false;
	TEST_DSFORMAT( D3DDEVTYPE_HAL, D3DFMT_D32 );
	g_StencilEnabled = false;
	TEST_DSFORMAT( D3DDEVTYPE_HAL, D3DFMT_D24X8 );
	g_StencilEnabled = false;
	TEST_DSFORMAT( D3DDEVTYPE_HAL, D3DFMT_D16 );
	g_StencilEnabled = true;
	TEST_DSFORMAT( D3DDEVTYPE_HAL, D3DFMT_D15S1 );
	g_StencilEnabled = false;
	return D3DFMT_UNKNOWN;
}

/*
 *	サーフェイスフォーマットを文字列に変換
 */
static const char *FormatToString(D3DFORMAT f){
	switch(f){
	case D3DFMT_R8G8B8:			return "D3DFMT_R8G8B8";
	case D3DFMT_A8R8G8B8:		return "D3DFMT_A8R8G8B8";
	case D3DFMT_X8R8G8B8:		return "D3DFMT_X8R8G8B8";
	case D3DFMT_R5G6B5:			return "D3DFMT_R5G6B5";
	case D3DFMT_X1R5G5B5:		return "D3DFMT_X1R5G5B5";
	case D3DFMT_A1R5G5B5:		return "D3DFMT_A1R5G5B5";
	case D3DFMT_A4R4G4B4:		return "D3DFMT_A4R4G4B4";
	case D3DFMT_R3G3B2:			return "D3DFMT_R3G3B2";
	case D3DFMT_A8:				return "D3DFMT_A8";
	case D3DFMT_A8R3G3B2:		return "D3DFMT_A8R3G3B2";
	case D3DFMT_X4R4G4B4:		return "D3DFMT_X4R4G4B4";
	case D3DFMT_D16_LOCKABLE:	return "D3DFMT_D16_LOCKABLE";
	case D3DFMT_D32:			return "D3DFMT_D32";
	case D3DFMT_D15S1:			return "D3DFMT_D15S1";
	case D3DFMT_D24S8:			return "D3DFMT_D24S8";
	case D3DFMT_D16:			return "D3DFMT_D16";
	case D3DFMT_D24X8:			return "D3DFMT_D24X8";
	case D3DFMT_D24X4S4:		return "D3DFMT_D24X4S4";
	default:					return "D3DFMT_UNKNOWN";
	}
}

/*
 *	デバイス能力の取得
 */
static void GetDeviceCaps(){
	D3DCAPS8 caps;

	sv3.pDev->GetDeviceCaps(&caps);

	sv3.capsMaxPrim = caps.MaxPrimitiveCount;
	sv3.capsMaxLight = caps.MaxActiveLights;
	sv3.capsFogVertex = (caps.RasterCaps&D3DPRASTERCAPS_FOGVERTEX)!=0;
	sv3.capsFogPixel = (caps.RasterCaps&D3DPRASTERCAPS_FOGTABLE)!=0;
	sv3.capsFogRange = (caps.RasterCaps&D3DPRASTERCAPS_FOGRANGE)!=0;
	sv3.capsTexMem = sv3.pDev->GetAvailableTextureMem()/1024;
	sv3.capsTexWidth = caps.MaxTextureWidth;
	sv3.capsTexHeight = caps.MaxTextureHeight;
	sv3.capsTexStage = caps.MaxSimultaneousTextures;
	sv3.capsTexAlpha = (caps.TextureCaps&D3DPTEXTURECAPS_ALPHA)!=0;
	sv3.capsTexMipMap = (caps.TextureCaps&D3DPTEXTURECAPS_MIPMAP)!=0;
	sv3.capsTexBump = (caps.TextureOpCaps&D3DTEXOPCAPS_BUMPENVMAP)!=0;
	Debug(
		"最大プリミティブ = %d\n" 
		"最大ライト = %d\n"
		"フォグ{\n"
		"\t頂点 = %d\n"
		"\tピクセル = %d\n"
		"\t範囲 = %d\n"
		"}\n"
		"テクスチャー{\n"
		"\t利用可能メモリ = %d KB\n"
		"\t最大サイズ = %d x %d\n"
		"\t最大ステージ = %d\n"
		"\tアルファブレンド = %d\n"
		"\tミップマップ = %d\n"
		"\tバンプマップ = %d\n"
		"}\n",
		sv3.capsMaxPrim,
		sv3.capsMaxLight,
		sv3.capsFogVertex,
		sv3.capsFogPixel,
		sv3.capsFogRange,
		sv3.capsTexMem,
		sv3.capsTexWidth, sv3.capsTexHeight,
		sv3.capsTexStage,
		sv3.capsTexAlpha,
		sv3.capsTexMipMap,
		sv3.capsTexBump);
}

//	----------------------------------------------------------------------------

CRS2D3D8Backend::CRS2D3D8Backend()
	: m_Initialized(false),
	  m_ViewportWidth(1),
	  m_ViewportHeight(1),
	  m_ZeroSizeLogged(false)
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
	//	[RS2EX] Default-pool resources must be gone before Reset() - and so must
	//	any borrowed swap-chain surface, or Reset() fails outright.
	GetRS2ResetRegistry().NotifyBeforeReset();

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

	//	[RS2EX] Rebuilt only now, because a participant may need renderer state
	//	(back-buffer format, depth format) to recreate itself.  Not called at all
	//	when Reset() failed: there is no healthy device to build on.
	GetRS2ResetRegistry().NotifyAfterReset();

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
	else if(svw.winW<=0 || svw.winH<=0)
	{
		//	[RS2EX] Zero-size guard - an intentional behaviour change.
		//
		//	OnSize() runs for every WM_SIZE including minimise, and
		//	GetClientRect() on a minimised window reports 0 x 0.  The resize
		//	branch below would then set a 0 x 0 back buffer, and
		//	AffectWindowSize() would divide by zero building the projection.
		//
		//	Skipping is all this does.  d3dpp keeps the size it had, so the
		//	frame renders into the existing back buffer and the ordinary
		//	mismatch test below resumes on the first frame with a real size.
		//	No state is latched and no resize can be lost.
		if(!m_ZeroSizeLogged){
			Debug("[RS2EX Renderer] window has zero size: skipping resize\n");
			m_ZeroSizeLogged = true;
		}
	}
	else if(sv3.d3dpp.BackBufferWidth!=svw.winW || sv3.d3dpp.BackBufferHeight!=svw.winH)
	{
		m_ZeroSizeLogged = false;
		Debug("バッファサイズを変更します.\n");
		sv3.d3dpp.BackBufferWidth	= svw.winW;
		sv3.d3dpp.BackBufferHeight	= svw.winH;
		sv3.width  = svw.winW;
		sv3.height = svw.winH;
		AffectWindowSize();
		if(!Reset()) return false;
	}
	else m_ZeroSizeLogged = false;

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
