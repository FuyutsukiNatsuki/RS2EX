//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21, 2026-09-26.

#include "headers.h"
#include "debug.h"
#include "graphic.h"
#include "vertex.h"
#include "..\RS2Draw.h"

/*
 *	３角形の法線ベクトルを計算
 */
void CalcNormal(VEC3 t[3], VEC3 *n){
	VEC3 u = t[1]-t[0];
	VEC3 v = t[2]-t[0];
	VEC3 tmp;

	RS2Vec3Cross(&tmp, &u, &v); 
	RS2Vec3Normalize(n, &tmp);
}

/*
 *	VTX_LXに矩形情報トライアングルリストでセットする
 *
 *	vt		: 頂点テーブルのアドレス
 *	x1, y1	: 始点
 *	x2, y2	: 終点
 */
void SetRectTL_LX(VTX_LX *vt, int x1, int y1, int x2, int y2, int z, RS2PackedColor c){
	SetVTX_LX(vt , x1, y1, z, c, 0, 1);
	SetVTX_LX(vt+1, x1, y2, z, c, 0, 0);
	SetVTX_LX(vt+2, x2, y1, z, c, 1, 1);
	SetVTX_LX(vt+3, x2, y2, z, c, 1, 0);
	SetVTX_LX(vt+4, x2, y1, z, c, 1, 1);
	SetVTX_LX(vt+5, x1, y2, z, c, 0, 0);
}

/*
 *	コンストラクタ
 */
CVertex::CVertex(){
	m_Geometry = 0;
	m_num = 0;
}

/*
 *	デストラクタ
 */
CVertex::~CVertex(){
	Free();
}

/*
 *	頂点バッファ作成
 *
 *	pSrc			: 頂点が格納された配列
 *	layout		: 頂点フォーマット
 *	vertexCount	: 頂点数
 */
BOOL CVertex::Create(
	const void *pSrc, const RS2MeshVertexLayout &layout, UINT vertexCount
){
	Free();	//	すでにあるバッファは解放

	m_Geometry = RS2CreateGeometry(layout, pSrc, vertexCount);
	if(!m_Geometry) return FALSE;

	m_num = vertexCount;
	return TRUE;
}

/*
 *	頂点バッファ解放
 */
void CVertex::Free(){
	if(!m_Geometry) return;

	RS2DestroyGeometry(m_Geometry);
	m_Geometry = 0;
	m_num = 0;
}

/*
 *	以下のメソッドはライト、マテリアル、テクスチャ、ワールドマトリクスの
 *	影響を受けます。
 *
 *	[RS2EX] The draw layer sets none of them - that is the v0.0.7 and v0.0.8
 *	boundaries' job, and it stays the caller's responsibility here.
 */
void CVertex::RenderPL(){
	RS2DrawBuffered(m_Geometry, RS2_PRIMITIVE_POINT_LIST, 0, m_num);
}

void CVertex::RenderLL(){
	RS2DrawBuffered(m_Geometry, RS2_PRIMITIVE_LINE_LIST, 0, m_num);
}

void CVertex::RenderLS(){
	RS2DrawBuffered(m_Geometry, RS2_PRIMITIVE_LINE_STRIP, 0, m_num);
}

void CVertex::RenderTL(){
	RS2DrawBuffered(m_Geometry, RS2_PRIMITIVE_TRIANGLE_LIST, 0, m_num);
}

void CVertex::RenderTS(){
	RS2DrawBuffered(m_Geometry, RS2_PRIMITIVE_TRIANGLE_STRIP, 0, m_num);
}

void CVertex::RenderTF(){
	RS2DrawBuffered(m_Geometry, RS2_PRIMITIVE_TRIANGLE_FAN, 0, m_num);
}
