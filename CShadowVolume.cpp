//	Modified for RS2EX on 2026-09-20.
#include "stdafx.h"
#include "CJobTimer.h"
#include "CShadowVolume.h"
#include "CScene.h"
#include "CEnvPlugin.h"

//	内部定数
const int TRI_DUMP_MAX_MAX = 21845;	//	三角形ダンプ最大値の最大値
const float SHADOW_INF_DIST = 1.0e10f;	//	シャドウボリューム無限遠点

//	外部グローバル
extern bool g_HidefCaptureFlag;
extern int g_HidefBufferSize;

/*
 *	コンストラクタ
 */
CTriDumpS::CTriDumpS(
	int trinum	//	四角形数
){
	m_TriNum = trinum;
	if(m_TriNum>TRI_DUMP_MAX_MAX) m_TriNum = TRI_DUMP_MAX_MAX;
	m_Count = 0;
	m_Buffer = new VTX_S[m_TriNum*3];
	m_Next = NULL;
}

/*
 *	コンストラクタ
 */
CTriDumpS::CTriDumpS(
	CTriDumpS *src	//	引き継ぎ元
){
	m_TriNum = src->m_TriNum;
	m_Count = src->m_Count;
	m_Buffer = src->m_Buffer;
	m_Next = src->m_Next;
}

/*
 *	デストラクタ
 */
CTriDumpS::~CTriDumpS(){
	DELETE_A(m_Buffer);
	DELETE_V(m_Next);
}

/*
 *	バッファ追加
 */
void CTriDumpS::Feed(){
	m_Next = new CTriDumpS(this);
	m_Count = 0;
	m_Buffer = new VTX_S[m_TriNum*3];
}

/*
 *	プリミティブ追加
 */
void CTriDumpS::Add(
	VEC3 p1, VEC3 p2, VEC3 p3	//	頂点
){
	if(m_Count==m_TriNum) Feed();
	VTX_S *buf = &m_Buffer[m_Count*3];
	*buf = p1; buf++;
	*buf = p2; buf++;
	*buf = p3; buf++;
	m_Count++;
}

/*
 *	即描画
 */
void CTriDumpS::Preview(
	VEC3 p1, VEC3 p2, VEC3 p3	//	頂点
){
	VTX_S prev[3], *buf = prev;
	*buf = p1; buf++;
	*buf = p2; buf++;
	*buf = p3; buf++;
	sv3.pDev->SetVertexShader(FVF_S);
	sv3.pDev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, prev, sizeof(VTX_S));
}

/*
 *	バーテックス準備
 */
void CTriDumpS::PrepareVertex(){
	m_Vertex.Create(m_Buffer, FVF_S, m_Count*3*sizeof(VTX_S));
	if(m_Next) m_Next->PrepareVertex();
}

/*
 *	レンダリング
 */
void CTriDumpS::Render(
	bool drawup	//	DrawPrimitiveUp を使用
){
	RS2BindTexture(0, RS2TextureRef());
	if(drawup){
		sv3.pDev->SetVertexShader(FVF_S);
		sv3.pDev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, m_Count, m_Buffer, sizeof(VTX_S));
	}else{
		m_Vertex.RenderTL();
	}
	if(m_Next) m_Next->Render(drawup);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
 *	コンストラクタ
 */
CShadowVolume::CShadowVolume(){
	m_FaceVolume = NULL;
}

/*
 *	デストラクタ
 */
CShadowVolume::~CShadowVolume(){
	DELETE_V(m_FaceVolume);
}

/*
 *	リセット
 */
void CShadowVolume::Reset(){
	DELETE_V(m_FaceVolume);
	m_FaceVolume = new CTriDumpS(TRI_DUMP_MAX);
}

/*
 *	メッシュから生成
 */
void CShadowVolume::BuildFromMesh(
	CObject *obj,	//	オブジェクト
	VEC3 vLight		//	ライト方向
){
	TIMER_RAII("CShadowVolume::BuildFromMesh()");

	CMesh *udxMesh = obj->GetMesh();
	if(!udxMesh) return;
	udxMesh->MaskMatFlag(1);
	/*
	 *	[RS2EX] Read geometry from the mesh, not from Direct3D.
	 *
	 *	The silhouette logic is unchanged: same light-facing test, same three
	 *	edges per lit face, same sort and cancellation afterwards.
	 *
	 *	The face-to-material mapping matters as much as the positions here - it
	 *	is what MaskMatFlag(1) above filters on, so NoCastShadow keeps working
	 *	only because one material ID per face survived the migration intact.
	 */
	const CRS2MeshData &mesh = udxMesh->GetMeshData();

	VEC3 vLocal = V3WorldToLocal(&vLight, obj);
	MTX4 mtxWorld = obj->GetWMatrix();

	const unsigned int dwNumFaces = mesh.GetFaceCount();

	unsigned int i, j;
	m_TempIndex.clear();
	for(i = 0; i<dwNumFaces; i++){
		if(udxMesh->GetMatFlag(mesh.GetFaceMaterialId(i))) continue;

		WORD wFace0 = (WORD)mesh.GetIndex(i, 0);
		WORD wFace1 = (WORD)mesh.GetIndex(i, 1);
		WORD wFace2 = (WORD)mesh.GetIndex(i, 2);

		VEC3 v0, v1, v2;
		mesh.GetPosition(wFace0, &v0);
		mesh.GetPosition(wFace1, &v1);
		mesh.GetPosition(wFace2, &v2);

		VEC3 vNormal;
		V3Cross(&vNormal, &(v2-v1), &(v1-v0));
		if(V3Dot(&vNormal, &vLocal)>=0.0f){
			m_TempIndex.push_back(CEdgeIndex(wFace0, wFace1));
			m_TempIndex.push_back(CEdgeIndex(wFace1, wFace2));
			m_TempIndex.push_back(CEdgeIndex(wFace2, wFace0));
		}
	}
	sort(m_TempIndex.begin(), m_TempIndex.end());

	DWORD dwNumEdges = m_TempIndex.size();
	for(i = 0; i<dwNumEdges; i++){
		CEdgeIndex edge = m_TempIndex[i];
		int cnt = 1;
		for(; i<dwNumEdges-1; i++){
			int cmp = edge.Compare(m_TempIndex[i+1]);
			if(!cmp) break;
			cnt += cmp;
		}
		if(!cnt) continue;
		VEC3 v1, tv1, v2, tv2;
		mesh.GetPosition(edge.m_Index1, &tv1);
		mesh.GetPosition(edge.m_Index2, &tv2);
		if(cnt<0){
			cnt = -cnt;
			D3DXVec3TransformCoord(&v2, &tv1, &mtxWorld);
			D3DXVec3TransformCoord(&v1, &tv2, &mtxWorld);
		}else{
			D3DXVec3TransformCoord(&v1, &tv1, &mtxWorld);
			D3DXVec3TransformCoord(&v2, &tv2, &mtxWorld);
		}
		for(j = 0; j<cnt; j++) m_FaceVolume->Add(v1, v2, vLight*SHADOW_INF_DIST);
	}

}

/*
 *	境界辺を追加
 */
void CShadowVolume::AddFaceEdge(
	VEC3 &v1, VEC3 &v2,	//	頂点
	VEC3 &vLight		//	光源方向
){
	m_FaceVolume->Add(v1, v2, vLight*SHADOW_INF_DIST);
}

/*
 *	レンダリング
 */
void CShadowVolume::Render(){
	//	いろいろ設定
	RS2SetDepthTest(true);
	RS2SetDepthWrite(false);
	RS2SetStencilTest(true);
	RS2SetShadeMode(RS2_SHADE_FLAT);

	//	ステンシルテストは常にパス
	RS2SetStencilFunc(RS2_COMPARE_ALWAYS);
	RS2SetStencilDepthFailOp(RS2_STENCIL_KEEP);
	RS2SetStencilFailOp(RS2_STENCIL_KEEP);

	//	Z テストがパスするところだけインクリメント
	RS2SetStencilRef(1);
	RS2SetStencilReadMask(0xffffffff);
	RS2SetStencilWriteMask(0xffffffff);
	RS2SetStencilPassOp(RS2_STENCIL_INCREMENT);

	//	フレームバッファには描かない（ステンシルのみ描く）
	RS2SetBlend(RS2_BLEND_COLOR_PRESERVE);

	//	シャドウボリュームの手前面を描画
	devTransform( &sv3.mtxFront );
	m_FaceVolume->Render( true );

	//	Z テストがパスするところだけデクリメント
	RS2SetStencilPassOp(RS2_STENCIL_DECREMENT);

	//	カリングを逆にして奥面を描画
	RS2SetCullMode(RS2_CULL_CLOCKWISE);
	devTransform( &sv3.mtxFront );
	m_FaceVolume->Render( true );

	//	設定を戻す
	RS2SetShadeMode(RS2_SHADE_GOURAUD);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2SetDepthWrite(true);
	RS2SetStencilTest(false);
	RS2SetBlend(RS2_BLEND_DISABLED);
}

/*
 *	描画
 */
void CShadowVolume::Draw(
	D3DCOLOR color	//	shadow color
){
	//	いろいろ設定
	RS2SetDepthTest(false);
	RS2SetStencilTest(true);
	RS2DisableFog();
	RS2SetBlend(RS2_BLEND_ALPHA);

	RS2SetBaseTextureCombine();

	//	ステンシルバッファの値が 1 以上のところは影
	RS2SetStencilRef(1);
	RS2SetStencilFunc(RS2_COMPARE_LESS_EQUAL);
	RS2SetStencilPassOp(RS2_STENCIL_KEEP);

	//	影部分を暗くする
	if(g_HidefCaptureFlag) Fill2DRect(0, 0, g_HidefBufferSize, g_HidefBufferSize, color);
	else Fill2DRect(0, 0, g_DispWidth, g_DispHeight, color);

	//	設定を戻す
	RS2SetDepthTest(true);
	RS2SetStencilTest(false);
	//devSetState( D3DRS_FOGENABLE, TRUE );
	RS2SetBlend(RS2_BLEND_DISABLED);
}
