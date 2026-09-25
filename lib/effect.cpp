//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-21, 2026-09-26.

#include "headers.h"
#include "debug.h"
#include "frame.h"
#include "graphic.h"
#include "view.h"
#include "render.h"
#include "texture.h"
#include "vertex.h"
#include "draw.h"
#include "effect.h"
#include "..\RS2Renderer.h"
#include "..\RS2MaterialBinding.h"

//	外部グローバル
extern int g_DispWidth;
extern int g_DispHeight;

/*
 *	バックバッファ上のピクセル色を取得
 *
 *	x, y	: ピクセル位置
 *	戻り値: X8R8G8B8フォーマットの色
 *
 *	※負荷が高いため頻繁に使用しない事
 */
RS2PackedColor GetPixelColor(int x, int y){
	//	[RS2EX] Readback is a deferred capability (v0.0.9 WP9); the Direct3D 8
	//	surface copy that was here went with the Direct3D 8 renderer (v0.2.0).
	(void)x; (void)y;
	return 0;
}

/*
 *	レンズフレアを描画
 *
 *	pos	: 光源の位置
 *	size	: フレアサイズ
 *	fWhite: ホワイトアウトのON/OFF
 *
 *	更新するデバイスパラメータ：ワールド変換行列、ブレンドモード、テクスチャー
 */
void RenderLensFlare(VEC3 pos, float size, BOOL fWhite){
	RS2SetBlend(RS2_BLEND_ALPHA_ADD);	//	加算モード

	//	光源のレンダリング
	//devTransBillboard(pos);
	//TexMap3DRect(VEC3(0, 0, 0), size, size, 0xc0ffe080+(Rand(0x40)<<24));

	VEC3 vLight = pos-GetVPos();		//	カメラから光源へのベクトル
	VEC3 vCamera = GetVDir();			//	カメラの向き
	VEC3 vDist = vLight/5-vCamera*5;	//	フレアの間隔（適当です）

	//	光の入射角を計算
	RS2Vec3Normalize(&vCamera, &vCamera);
	RS2Vec3Normalize(&vLight, &vLight);

	float angle = RS2Vec3Dot(&vLight, &vCamera);

	if(angle>0.9f){
		//	フレアの描画
		DWORD aplus = (DWORD)(max(0.0f, (angle-0.9f)*FRand2(1400.0f, 2200.0f)))<<24;
		RS2BindTexture(0, RS2TextureRef());

		pos -= vDist;
		devTransBillboard(pos);
		Fill3DCircle(VEC3(0, 0, 0), size/10, 0x00000000+aplus, 0x00804000+aplus);

		pos -= vDist;
		devTransBillboard(pos);
		Fill3DHex( VEC3(0, 0, 0), size/8, 0x00000000+aplus, 0x00806000+aplus);

		pos -= vDist;
		devTransBillboard(pos);
		Fill3DCircle(VEC3(0, 0, 0), size/13, 0x00000000+aplus, 0x00001040+aplus);

		pos -= vDist;
		devTransBillboard(pos);
		Fill3DCircle(VEC3(0, 0, 0), size/7, 0x00000000+aplus, 0x00006020+aplus);

		pos -= vDist;
		devTransBillboard(pos);
		Fill3DCircle(VEC3(0, 0, 0), size/20, 0x00008040+aplus, 0x00000000+aplus);

		pos -= vDist;
		devTransBillboard(pos);
		Fill3DCircle(VEC3(0, 0, 0), size/5, 0x00000000, 0x00002000+aplus);

		RS2SetBlend(RS2_BLEND_ALPHA);	//	半透明モード

		//	ホワイトアウト
		if(fWhite && angle>0.9f){
			aplus = (DWORD)(max(0.0f, (angle-0.9f)*1000.0f))<<24;
			Fill2DRect(0, 0, g_DispWidth, g_DispHeight, aplus|0x00ffffff);
		}
	}else{
		RS2SetBlend(RS2_BLEND_ALPHA);	//	半透明モード
	}
}
