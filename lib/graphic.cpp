//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21.

#include "headers.h"
#include "debug.h"
#include "window.h"
#include "graphic.h"
#include "render.h"
#include "texture.h"
#include "..\RS2Text.h"
#include "frame.h"
#include "..\Const.h"
#include "..\RS2Renderer.h"
#include "..\RS2MaterialBinding.h"

//	内部定数
extern const float CLIP_PLANE_NEAR = 0.5f;		//	前方クリップ面
extern const float CLIP_PLANE_FAR = 10000.0f;	//	後方クリップ面
extern const float FOV_DEF = 0.25f*D3DX_PI;		//	デフォルト視野角

//	外部グローバル
extern int g_DispWidth;
extern int g_DispHeight;

//	内部グローバル
//	[RS2EX] g_StencilEnabled is written by the renderer backend, which picks
//	the depth/stencil format, and read by the shadow code.
bool g_StencilEnabled = false;
DWORD g_BufferClearMode;

/*
 *	Direct3Dの初期化
 *
 *	[RS2EX] Compatibility entry point.  Device lifecycle now belongs to the
 *	renderer backend - see RS2D3D8Backend.cpp.
 */
BOOL InitDirect3D(){
	DebugHL();
	Debug("InitDirect3D\n");

	return GetRS2Renderer().Initialize(g_DispWidth, g_DispHeight) ? TRUE : FALSE;
}

/*
 *	Direct3Dの解放
 *
 *	[RS2EX] Compatibility entry point - see RS2D3D8Backend.cpp.
 */
void FreeDirect3D(){
	DebugHL();
	Debug("FreeDirect3D\n");

	GetRS2Renderer().Shutdown();
}

/*
 *	バッファサイズ適用
 */
void AffectWindowSize()
{
	if(!GetRS2Renderer().IsReady()) return;

	//int width = sv3.width, height = sv3.height;
	int width = svw.winW, height = svw.winH;

	D3DXMatrixPerspectiveFovLH(
		&sv3.mtxProj,
		D3DX_PI/4,
		(float)width / height,
		CLIP_PLANE_NEAR,
		CLIP_PLANE_FAR
	);
	RS2SetProjectionTransform(sv3.mtxProj);

	//ビューポート行列の作成(ワールド→スクリーン座標変換用)
	sv3.mtxVPort = MTX4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
	sv3.mtxVPort._11 =  width *0.5f;
	sv3.mtxVPort._22 = -height*0.5f;
	sv3.mtxVPort._41 =  width *0.5f;
	sv3.mtxVPort._42 =  height*0.5f;
}

/*
 *	座標系の初期化
 */
void InitMetrics(){
#if 0
	// viewport
	D3DVIEWPORT8 vp;
    vp.X = sv3.width / 2;
    vp.Y = 0;
    vp.Width = sv3.width / 2;
    vp.Height = sv3.height;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
	sv3.pDev->SetViewport(&vp);
#endif

	//	各座標変換行列の指定
	sv3.mtxWorld = MTX4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	RS2SetWorldTransform(sv3.mtxWorld);

	D3DXMatrixLookAtLH(
		&sv3.mtxView,
		&D3DXVECTOR3(0.0f, 0.0f, -1.0f),
		&D3DXVECTOR3(0.0f, 0.0f, 0.0f),
		&D3DXVECTOR3(0.0f, 1.0f, 0.0f));
	RS2SetViewTransform(sv3.mtxView);

	//	[RS2EX] The clip status moved into the D3D8 backend in v0.0.9: it is a
	//	device setting, and InitMetrics is about matrices.

	//	向き行列の作成
	sv3.mtxFront = MTX4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	sv3.mtxRear = MTX4(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);
	sv3.mtxLeft = MTX4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1);
	sv3.mtxRight = MTX4(0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1);
	sv3.mtxTop = MTX4(1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1);
	sv3.mtxBottom = MTX4(1, 0, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, 0, 1);

	//	UV座標の初期化
	sv3.u[0] = 0, sv3.u[1] = 1;
	sv3.v[0] = 0, sv3.v[1] = 1;

	AffectWindowSize();
}

/*
 *	レンダリング・ステートの初期化
 */
void InitRenderState(){
	//	[RS2EX] The state itself moved to RS2ApplyInitialRenderState(), which
	//	sets the same things in the same order.  The buffer clear below stays
	//	here: it is not render state, and it depends on g_StencilEnabled.
	RS2ApplyInitialRenderState();

	//	[RS2EX] The startup clear moved behind the renderer in v0.0.9, along
	//	with g_BufferClearMode, which is the backend's own answer to whether
	//	this device has a stencil buffer.
	GetRS2Renderer().ClearTarget(0);
}


/*
 *	シーンの開始
 *
 *	c	: クリアカラー、c = 0でクリアしない。
 *
 *	[RS2EX] Compatibility wrapper.  The device work moved to the renderer
 *	backend; the view-derived matrices below are engine math and stay here.
 */
BOOL BeginScene(D3DCOLOR c){
	if(!GetRS2Renderer().BeginRenderPass(c, c!=0)) return FALSE;

	//	各種変換行列の計算
	float tmp;
	D3DXMatrixInverse(&sv3.mtxViewInv, &tmp, &sv3.mtxView);

	sv3.mtxWtoS = /*sv3.mtxWorld**/ sv3.mtxView*sv3.mtxProj*sv3.mtxVPort;
	D3DXMatrixInverse(&sv3.mtxStoW, &tmp, &sv3.mtxWtoS);

	return TRUE;
}

/*
 *	シーンの終了
 *
 *	[RS2EX] Still means "end the pass and present it", so the call sites do not
 *	have to change.  The two halves are separate operations underneath.
 */
void EndScene(){
	GetRS2Renderer().EndRenderPass();
	GetRS2Renderer().Present();
}

/*
 *	カラー値をX8R8G8B8フォーマットに変換する
 *
 *	d		: 任意フォーマットの色
 *	fmt	: フォーマット
 */
D3DCOLOR GetXRGB32(DWORD d, D3DFORMAT fmt){
	D3DCOLOR c;
	DWORD r, g, b;

	switch(fmt){
	case D3DFMT_R8G8B8:
		c = 0xff000000|(d>>8);
		break;

	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		c = 0xff000000|d;
		break;

	case D3DFMT_R5G6B5:
		r = min((DWORD)0xff, ((d&0xf800)>>11)*8);
		g = min((DWORD)0xff, ((d&0x07e0)>> 5)*4);
		b = min((DWORD)0xff, (d&0x001f)*8);
		c = 0xff000000|r|g|b;
		break;

	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
		r = min((DWORD)0xff, ((d&0x7e00)>>11)*8);
		g = min((DWORD)0xff, ((d&0x03e0)>>5)*8);
		b = min((DWORD)0xff, (d&0x001f)*8);
		c = 0xff000000|r|g|b;
		break;

	case D3DFMT_A4R4G4B4:
		r = min((DWORD)0xff, ((d&0x0f00)>>8)*16);
		g = min((DWORD)0xff, ((d&0x00f0)>>4)*16);
		b = min((DWORD)0xff, (d&0x000f)*16);
		c = 0xff000000|r|g|b;
		break;

	default:
		c = 0;
	}
	return c;
}

/*
 *	マテリアルの初期化
 */
void devResetMaterial(){
	RS2Material mat;

	mat.Diffuse = RS2MakeColor4(0.8f, 0.8f, 0.8f, 1.0f);
	mat.Ambient = RS2MakeColor4(0.8f, 0.8f, 0.8f, 1.0f);
	mat.Specular = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	mat.Emissive = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	mat.Power = 0.0f;

	RS2SetMaterial(mat);
}

/*
 *	ワイヤフレーム用マテリアル
 */
void devSetLineMaterial(){
	RS2Material mat;

	mat.Diffuse = RS2MakeColor4(1.0f, 1.0f, 1.0f, 1.0f);
	mat.Ambient = RS2MakeColor4(0.4f, 0.4f, 0.4f, 1.0f);
	mat.Specular = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	mat.Emissive = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	mat.Power = 0.0f;

	RS2SetMaterial(mat);
}
