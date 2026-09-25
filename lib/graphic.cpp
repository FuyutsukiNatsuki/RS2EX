//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21, 2026-09-26.

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
extern const float FOV_DEF = 0.25f*RS2_PI;		//	デフォルト視野角

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
 *	renderer backend - see RS2D3D12Backend.cpp.
 */
BOOL InitDirect3D(){
	DebugHL();
	Debug("InitDirect3D\n");

	return GetRS2Renderer().Initialize(g_DispWidth, g_DispHeight) ? TRUE : FALSE;
}

/*
 *	Direct3Dの解放
 *
 *	[RS2EX] Compatibility entry point - see RS2D3D12Backend.cpp.
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

	RS2MatrixPerspectiveFovLH(
		&sv3.mtxProj,
		RS2_PI/4,
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

	//	各座標変換行列の指定
	sv3.mtxWorld = MTX4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	RS2SetWorldTransform(sv3.mtxWorld);

	RS2MatrixLookAtLH(
		&sv3.mtxView,
		&VEC3(0.0f, 0.0f, -1.0f),
		&VEC3(0.0f, 0.0f, 0.0f),
		&VEC3(0.0f, 1.0f, 0.0f));
	RS2SetViewTransform(sv3.mtxView);

	//	[RS2EX] The clip status moved into the renderer backend in v0.0.9: it
	//	is a device setting, and InitMetrics is about matrices.

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
BOOL BeginScene(RS2PackedColor c){
	if(!GetRS2Renderer().BeginRenderPass(c, c!=0)) return FALSE;

	//	各種変換行列の計算
	float tmp;
	RS2MatrixInverse(&sv3.mtxViewInv, &tmp, &sv3.mtxView);

	sv3.mtxWtoS = /*sv3.mtxWorld**/ sv3.mtxView*sv3.mtxProj*sv3.mtxVPort;
	RS2MatrixInverse(&sv3.mtxStoW, &tmp, &sv3.mtxWtoS);

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

//	[RS2EX] v0.2.0: GetXRGB32 (Direct3D 8 surface formats) removed with its
//	only caller, the Direct3D 8 pixel read in lib/effect.cpp.

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
