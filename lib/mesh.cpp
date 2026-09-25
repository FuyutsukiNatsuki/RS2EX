//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21, 2026-09-26.

#include "..\stdafx.h"

#define UDX_MESH_MEASURE (0)

#if UDX_MESH_MEASURE
#include <vector>
#include <string>
#include "..\CJobTimer.h"
#define UDX_MESH_TIMER_RAII(name) TIMER_RAII(name)
#else
#define UDX_MESH_TIMER_RAII(name)
#endif

#include "..\RS2MeshImport.h"
#include "..\RS2MaterialBinding.h"
#include "..\CModelPlugin.h"
#include "..\CEnvPlugin.h"
#include "..\CConfigMode.h"

//	外部グローバル
extern CTexList g_TexList;
extern float g_BlinkAlpha;
extern CEnvPlugin *g_Env;

//	内部グローバル
int g_AncientNightFlag;				//	旧バージョン対応用・夜間発光フラグ
CMeshList g_MeshList;				//	テクスチャリスト
RS2Material *g_AltMaterial = NULL;	//	代替マテリアル


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
 *	コンストラクタ
 */
CMesh::CMesh(){
	m_Geometry = 0;
	m_pMatFlag = NULL;
	m_pMatOrder = NULL;
	m_pMat = NULL;
	m_pCustomMat = NULL;
	m_pTex = NULL;
	m_pCustomTex = NULL;
	m_pTexTrans = NULL;
}

/*
 *	デストラクタ
 */
CMesh::~CMesh(){
	Free();
}

/*
 *	X ファイルの読込み
 */
BOOL CMesh::Load(
	BOOL fRes,			//	リソースか？
	char *strName,		//	ファイル・リソース名
	RS2PackedColor cTrans,	//	テクスチャーの透過色
	int nMipLv			//	ミップマップ LV
){
	//	既存なら解放
	Free();

	Debug("load(%s) ... ", strName);

	/*
	 *	[RS2EX] D3DX imports, RailSim owns.
	 *
	 *	The importer loads and optimises a temporary ID3DXMesh, copies the
	 *	optimised geometry into RS2 memory and releases it before returning.
	 *	Nothing is committed to this object until that has succeeded.
	 */
	CRS2MeshImportResult import;
	if(!RS2ImportMeshFile(fRes, strName, &import)) return FALSE;

	m_strName = strName;
	m_dwNumMat = import.GetMaterialCount();

	//	マテリアルの取得、テクスチャーのロード
	m_pMatFlag = new DWORD[m_dwNumMat];
	m_pMat = new RS2Material[m_dwNumMat];
	m_pCustomMat = new RS2Material[m_dwNumMat];
	m_pTex = new RS2TextureRef[m_dwNumMat];
	m_pCustomTex = new RS2TextureRef[m_dwNumMat];
	m_pTexTrans = new TTMTX[m_dwNumMat];

	DWORD i, j;
	for(i = 0; i<m_dwNumMat; i++){
		const RS2ImportedMaterial &src = import.GetMaterial(i);

		m_pMat[i] = src.material;
		//	[RS2EX] RailSim behaviour, not an import artefact: keep it here.
		m_pMat[i].Ambient = m_pMat[i].Diffuse;
		m_pTex[i].Clear();

		if(!src.textureFileName) continue;

		//	リストにあれば参照、なければロードして追加
		m_pTex[i] = g_TexList.Get(fRes, src.textureFileName, cTrans, nMipLv);
	}

	m_pMatOrder = new DWORD[m_dwNumMat];
	for(i = 0; i<m_dwNumMat; i++) m_pMatOrder[i] = i;
	for(i = 1; i<m_dwNumMat; i++){
		for(j = i; j>0; j--){
			DWORD &o1 = m_pMatOrder[j-1], &o2 = m_pMatOrder[j];
			if(m_pMat[o1].Diffuse.a<m_pMat[o2].Diffuse.a){
				int tmp = o1; o1 = o2; o2 = tmp;
			}else{
				break;
			}
		}
	}

	//	[RS2EX] Adopt the geometry, then upload it.  A failed upload leaves
	//	nothing half-built: Free() clears the materials loaded above too.
	m_Data.AdoptFrom(import.geometry);

	if(!CreateGeometry()){
		Debug("[RS2EX Mesh] GPU upload failed for %s\n", strName);
		Free();
		return FALSE;
	}

	//	境界の計算
	//	[RS2EX] Taken from the importer rather than recomputed: 2.15 measured the
	//	mesh before optimisation, and COMPACT can drop an unreferenced extreme
	//	vertex.  The centre and radius formulas are unchanged.
	m_min = import.boundsMin;
	m_max = import.boundsMax;
	m_center = 0.5f*(m_min+m_max);
	m_radius = 0.5f*V3Len(&(m_max-m_min));
	return TRUE;
}




/*
 *	メッシュの解放
 */
void CMesh::Free(){
	if(m_strName!=""){
		Debug("release(%s)\n", m_strName.c_str());
		m_strName = "";
	}

	if(m_pTex) for(DWORD i = 0;i<m_dwNumMat;i++) g_TexList.Release(m_pTex[i]);
	DELETE_A(m_pMatFlag);
	DELETE_A(m_pMatOrder);
	DELETE_A(m_pMat);
	DELETE_A(m_pCustomMat);
	DELETE_A(m_pTex);
	DELETE_A(m_pCustomTex);
	DELETE_A(m_pTexTrans);

	//	[RS2EX] Geometry last, and safe to repeat.
	if(m_Geometry){
		RS2DestroyGeometry(m_Geometry);
		m_Geometry = 0;
	}
	m_Data.Free();
	m_dwNumMat = 0;
}

/*
 *	境界ボックス／球の計算
 */
void CMesh::ComputeBoundary(){
	//	[RS2EX] Read from RS2 geometry instead of locking a D3DX buffer.
	//	
	//	Only the primitive generators reach this now - imported meshes take their
	//	bounds from the importer, measured before optimisation.  Primitives are
	//	never optimised, so computing from their geometry is exactly equivalent.
	//	
	//	The sphere is still derived from the box rather than fitted: a tighter
	//	sphere would change culling and picking pre-tests everywhere.
	const unsigned int n = m_Data.GetVertexCount();

	if(!n){
		m_min = m_max = m_center = VEC3(0, 0, 0);
		m_radius = 0.0f;
		return;
	}

	VEC3 v;
	m_Data.GetPosition(0, &v);
	m_min = m_max = v;

	unsigned int i;
	for(i = 1; i<n; i++){
		m_Data.GetPosition(i, &v);

		if(v.x<m_min.x) m_min.x = v.x;
		if(v.y<m_min.y) m_min.y = v.y;
		if(v.z<m_min.z) m_min.z = v.z;
		if(v.x>m_max.x) m_max.x = v.x;
		if(v.y>m_max.y) m_max.y = v.y;
		if(v.z>m_max.z) m_max.z = v.z;
	}
	m_center = 0.5f*(m_min+m_max);
	m_radius = 0.5f*V3Len(&(m_max-m_min));
}

/*
 *	[RS2EX] Draw every face belonging to one material.
 *
 *	materialId	: material to draw
 *
 *	Replaces ID3DXMesh::DrawSubset(materialId).  A material owning no faces
 *	draws nothing, which is normal rather than an error.  More than one range
 *	per material is handled because the subset table does not assume that
 *	ATTRSORT made them contiguous.
 */
/*
 *	[RS2EX] Upload the CPU geometry.
 *
 *	CRS2MeshData keeps owning the vertices and indices - picking, bounds and
 *	shadow generation still read them - and the resource is a copy for the
 *	backend, exactly as the mesh resource it replaces was.
 */
BOOL CMesh::CreateGeometry(){
	if(m_Geometry){
		RS2DestroyGeometry(m_Geometry);
		m_Geometry = 0;
	}
	if(!m_Data.IsValid()) return FALSE;

	m_Geometry = RS2CreateIndexedGeometry(
		m_Data.GetLayout(), m_Data.GetVertexBytes(), m_Data.GetVertexCount(),
		m_Data.GetIndices(), m_Data.GetIndexCount());

	return m_Geometry!=0;
}

void CMesh::DrawSubset(DWORD materialId){
	const unsigned int n = m_Data.GetSubsetCount();
	unsigned int i;

	for(i = 0; i<n; i++){
		const RS2MeshSubset &s = m_Data.GetSubset(i);

		if(s.materialId!=materialId) continue;
		//	The subset table stores triangles; the draw boundary takes indices.
		RS2DrawIndexed(m_Geometry, RS2_PRIMITIVE_TRIANGLE_LIST,
			s.firstIndex, s.primitiveCount*3);
	}
}

/*
 *	マテリアルフラグのリセット
 */
void CMesh::ResetMatFlag(
	DWORD def	//	デフォルト値
){
	for(DWORD i = 0; i<m_dwNumMat; i++) m_pMatFlag[i] = def;
}

/*
 *	マテリアルフラグをマスク
 */
void CMesh::MaskMatFlag(
	DWORD mask	//	マスク値
){
	for(DWORD i = 0; i<m_dwNumMat; i++) m_pMatFlag[i] &= mask;
}

/*
 *	カスタムレンダリング
 *
 *	pMtx	: 座標変換行列
 *
 *	更新するデバイスパラメータ	: ワールドマトリクス、マテリアル、テクスチャ
 */
void CMesh::RenderCustom(MTX4 *pMtx, CNamedObject *nobj){
	if(!IsValid()) return;
	UDX_MESH_TIMER_RAII("CMesh::Render");

	devTransform(pMtx);

	/*
	 *	MatFlag
	 *	0x01 ( 1): NoCastShadow
	 *	0x02 ( 2): AfterShadow phase
	 *	0x04 ( 4): AfterShadow enable
	 *	0x08 ( 8): EnvMap
	 *	0x10 (16): TexTrans
	 *	0x20 (32): AlphaZeroTest
	 */

	memcpy(m_pCustomMat, m_pMat, m_dwNumMat*sizeof(RS2Material));
	//	[RS2EX] A loop rather than memcpy(): RS2TextureRef has a constructor,
	//	so copying it bytewise would be relying on its layout.
	for(DWORD c = 0; c<m_dwNumMat; c++) m_pCustomTex[c] = m_pTex[c];
	nobj->SetMaterial(this);
	for(DWORD i = 0; i<m_dwNumMat; i++){
		DWORD order = m_pMatOrder[i];
		float alpha = m_pCustomMat[order].Diffuse.a;
		int flag = m_pMatFlag[order];
		if((flag&6)==2 || !alpha) continue;
		if(g_AltMaterial){
			RS2SetMaterial(*g_AltMaterial);
			RS2BindTexture(0, RS2TextureRef());
			{
				UDX_MESH_TIMER_RAII("DrawSubset");
				DrawSubset(order);
			}
		}else{
			if(flag&8){
				RS2SetEnvironmentMapping(1, true);
				g_Env->SetEnvMapTexture();
				RS2SetSecondaryTextureCombine(1, true);
				//devSetTexAlpha(1, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
			}
			if(flag&16){
				RS2SetUVTransform(0, true);
				RS2SetUVMatrix(0, m_pTexTrans[order].GetMTX4());
			}
			if(flag&32){
				RS2SetTextureFilter(0, RS2_FILTER_POINT);
				RS2SetTextureFilter(1, RS2_FILTER_POINT);
				RS2SetAlphaTest(true);
				RS2SetAlphaRef(0);
				RS2SetAlphaFunc(RS2_COMPARE_GREATER);
			}
			if(g_RenderBlink) m_pCustomMat[order].Diffuse.a *= g_BlinkAlpha;
			RS2SetMaterial(m_pCustomMat[order]);
			RS2BindTexture(0, m_pCustomTex[order]);
			{
				UDX_MESH_TIMER_RAII("DrawSubset");
				DrawSubset(order);
			}
			if(flag&8){
				RS2SetEnvironmentMapping(1, false);
				RS2BindTexture(1, RS2TextureRef());
				RS2SetSecondaryTextureCombine(1, false);
				//devSetTexAlpha(1, D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_CURRENT);
			}
			if(flag&16) RS2SetUVTransform(0, false);
			if(flag&32){
				g_ConfigMode->SetTexFilter();
				RS2SetAlphaTest(false);
			}
		}
	}
}

/*
 *	レンダリング
 *
 *	pMtx	: 座標変換行列
 *
 *	更新するデバイスパラメータ	: ワールドマトリクス、マテリアル、テクスチャ
 */
void CMesh::Render(MTX4 *pMtx){
	if(!IsValid()) return;
	UDX_MESH_TIMER_RAII("CMesh::Render");

	devTransform(pMtx);

	for(DWORD i = 0; i<m_dwNumMat; i++){
		DWORD order = m_pMatOrder[i];
		float alpha = m_pMat[order].Diffuse.a;
		if(g_AltMaterial){
			RS2SetMaterial(*g_AltMaterial);
			RS2BindTexture(0, RS2TextureRef());
			{
				UDX_MESH_TIMER_RAII("DrawSubset");
				DrawSubset(order);
			}
		}else{
			if(g_AncientNightFlag){
				if(!alpha){
					RS2Material matTmp = m_pMat[order];
					matTmp.Diffuse.a = g_RenderBlink ? g_BlinkAlpha : 1.0f;
					RS2SetMaterial(matTmp);
				}else if(alpha==0.5f){
					static RS2Material matLight = {
						{1.0f, 0.8f, 0.5f, 0.7f},
						{1.0f, 0.8f, 0.5f, 0.7f},
						{1.0f, 0.8f, 0.5f, 1.0f},
						{1.0f, 0.8f, 0.5f, 1.0f},
						1.0f};
					matLight.Diffuse.a = g_RenderBlink ? g_BlinkAlpha*0.7f : 0.7f;
					RS2SetMaterial(matLight);
				}else{
					if(g_RenderBlink) m_pMat[order].Diffuse.a *= g_BlinkAlpha;
					RS2SetMaterial(m_pMat[order]);
				}
			}else{
				if(!alpha) continue;
				if(g_RenderBlink) m_pMat[order].Diffuse.a *= g_BlinkAlpha;
				RS2SetMaterial(m_pMat[order]);
			}
			RS2BindTexture(0, m_pTex[order]);
			{
				UDX_MESH_TIMER_RAII("DrawSubset");
				DrawSubset(order);
			}
			m_pMat[order].Diffuse.a = alpha;
		}
	}
}

/*
 *	レンダリング (アンビエントのみ)
 *
 *	pMtx	: 座標変換行列
 */
void CMesh::RenderAmb(MTX4 *pMtx){
	if(!IsValid()) return;
	UDX_MESH_TIMER_RAII("CMesh::Render");

	devTransform(pMtx);

	for(DWORD i = 0;i<m_dwNumMat;i++){
		DWORD order = m_pMatOrder[i];
		RS2Color4 &dif = m_pMat[order].Diffuse, tdif = dif;
		dif.r = dif.g = dif.b = 0.0f;
		RS2SetMaterial(m_pMat[order]);
		RS2BindTexture(0, m_pTex[order]);
		{
			UDX_MESH_TIMER_RAII("DrawSubset");
			DrawSubset(order);
		}
		dif = tdif;
	}
}

/*
 *	レンダリング (テクスチャ指定)
 *
 *	pMtx	: 座標変換行列
 *	pTex	: テクスチャ
 */
void CMesh::RenderT(MTX4 *pMtx, RS2TextureRef pTex){
	if(!IsValid()) return;
	UDX_MESH_TIMER_RAII("CMesh::Render");

	devTransform(pMtx);

	for(DWORD i = 0;i<m_dwNumMat;i++){
		DWORD order = m_pMatOrder[i];
		RS2SetMaterial(m_pMat[order]);
		RS2BindTexture(0, pTex);
		{
			UDX_MESH_TIMER_RAII("DrawSubset");
			DrawSubset(order);
		}
	}
}

/*
 *	レンダリング (アルファ値指定)
 *
 *	pMtx	: 座標変換行列
 *	alpha : アルファ値
 */
void CMesh::RenderA(MTX4 *pMtx, float altalpha){
	if(!IsValid()) return;
	UDX_MESH_TIMER_RAII("CMesh::Render");

	devTransform(pMtx);

	for(DWORD i = 0;i<m_dwNumMat;i++){
		DWORD order = m_pMatOrder[i];
		float alpha = m_pMat[order].Diffuse.a;
		if(g_AncientNightFlag){
			if(!alpha){
				RS2Material matTmp = m_pMat[order];
				matTmp.Diffuse.a = altalpha*(g_RenderBlink ? g_BlinkAlpha : 1.0f);
				RS2SetMaterial(matTmp);
			}else if(alpha==0.5f){
				static RS2Material matLight = {
					{1.0f, 0.8f, 0.5f, 0.7f},
					{1.0f, 0.8f, 0.5f, 0.7f},
					{1.0f, 0.8f, 0.5f, 1.0f},
					{1.0f, 0.8f, 0.5f, 1.0f},
					1.0f};
				matLight.Diffuse.a = altalpha*(g_RenderBlink ? g_BlinkAlpha*0.7f : 0.7f);
				RS2SetMaterial(matLight);
			}else{
				m_pMat[order].Diffuse.a *= altalpha*(g_RenderBlink ? g_BlinkAlpha : 1.0f);
				RS2SetMaterial(m_pMat[order]);
			}
		}else{
			if(!alpha) continue;
			m_pMat[order].Diffuse.a *= altalpha*(g_RenderBlink ? g_BlinkAlpha : 1.0f);
			RS2SetMaterial(m_pMat[order]);
		}
		RS2BindTexture(0, m_pTex[order]);
		{
			UDX_MESH_TIMER_RAII("DrawSubset");
			DrawSubset(order);
		}
		m_pMat[order].Diffuse.a = alpha;
	}
}

/*
 *	レンダリング (α加算)
 *
 *	pMtx	: 座標変換行列
 *	aplus	: α加算値
 */
void CMesh::RenderAP(MTX4 *pMtx, float aplus){
	if(!IsValid()) return;
	UDX_MESH_TIMER_RAII("CMesh::Render");

	devTransform(pMtx);

	RS2Color4 c1, c2;

	for(DWORD i = 0;i<m_dwNumMat;i++){
		DWORD order = m_pMatOrder[i];
		//	α値を変更
		c2 = c1 = m_pMat[0].Ambient;	//	= m_pMat[0].Diffuse
		c2.a = max(-1.0f, min(1.0f, c2.a*aplus));
		m_pMat[order].Diffuse = m_pMat[order].Ambient = c2;

		RS2SetMaterial(m_pMat[order]);
		RS2BindTexture(0, m_pTex[order]);
		{
			UDX_MESH_TIMER_RAII("DrawSubset");
			DrawSubset(order);
		}

		//	α値を復元
		m_pMat[order].Diffuse = m_pMat[order].Ambient = c1;
	}
}

/*
 *	レンダリング (マテリアル指定、影などで使用)
 *
 *	pMtx	: 座標変換行列
 *	mat	: マテリアル
 */
void CMesh::RenderSC(MTX4 *pMtx, RS2Material *pMat){
	if(!IsValid()) return;
	UDX_MESH_TIMER_RAII("CMesh::Render");

	devTransform(pMtx);

	for(DWORD i = 0;i<m_dwNumMat;i++){
		DWORD order = m_pMatOrder[i];
		RS2SetMaterial(*pMat);
		RS2BindTexture(0, RS2TextureRef());
		{
			UDX_MESH_TIMER_RAII("DrawSubset");
			DrawSubset(order);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
 *	コンストラクタ
 */
CMeshList::CMeshList(){
	m_pList = NULL;
}

/*
 *	デストラクタ
 */
CMeshList::~CMeshList(){
	while(m_pList) Release(&m_pList->m_Mesh);
}

/*
 *	メッシュをリストから検索し、なければロード
 */
CMesh *CMeshList::Get(BOOL fRes, LPCSTR strName, RS2PackedColor cTrans, int nMipLv){
	if(!strName || !*strName) return NULL;

	//	リストからテクスチャーを検索
	MESHINFO *p = m_pList;

	//	ファイルならフルパスに変換
	char full[_MAX_PATH];
	if(!fRes){
		_fullpath(full, strName, _MAX_PATH);
		strName = full;
		extern void MoveToFile(char *);
		MoveToFile(full);
	}

	while(p){
		//	見つかれば参照カウンタを増やし、メッシュを返す
		if(!_mbsicmp((PUCHAR)p->strName.c_str(), (PUCHAR)strName)
			&& p->cTrans==cTrans && p->nMipLv==nMipLv){
			//	Debug("[%s] is in mesh-list.\n", strName); /*デバッグ*/
			p->nRef++;
			return &p->m_Mesh;
		}
		p = p->pNext;
	}
	//	見つからなければロード
	Debug("load(%s) ... ", strName);

	p = new MESHINFO;

	if(!p->m_Mesh.Load(fRes, (char *)strName, cTrans, nMipLv)){
		Debug("failed.\n");
		return NULL;
	}
	Debug("ok.\n");

	//	リストの先頭に追加、参照カウンタを設定する
	MESHINFO *q = m_pList;
	m_pList = p;
	p->pNext = q;
	p->strName = strName;
	p->nRef = 1;
	//	[RS2EX] cTrans is part of the lookup key a few lines above but was
	//	never assigned here, so the comparison read whatever the heap left in
	//	the field and cache hits became a coin toss.
	p->cTrans = cTrans;
	p->nMipLv = nMipLv;

	return &p->m_Mesh;
}

/*
 *	可能ならメッシュを解放する
 */
void CMeshList::Release(CMesh *pMesh){
	//	メッシュを検索
	MESHINFO *p = m_pList;
	MESHINFO *q = NULL;

	while(p){
		//	見つかれば参照カウンタを減らす
		if(&p->m_Mesh==pMesh){
			p->nRef--;

			//	参照がなくなればメッシュを解放、リストから外す
			if(p->nRef==0){
				Debug("release(%s)\n", p->strName.c_str());
				p->m_Mesh.Free();

				if(p==m_pList){
					//	リストの先頭
					m_pList = p->pNext;
					delete p;
				}else{
					//	途中
					q->pNext = p->pNext;
					delete p;
				}
			}
			break;
		}
		q = p;
		p = p->pNext;
	}
}
