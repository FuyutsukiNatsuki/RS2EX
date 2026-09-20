//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20.

class CNamedObject;

#include "..\RS2MeshData.h"
#include "..\RS2D3D8MeshResource.h"

//	[RS2EX] CXFile moved to RS2LegacyXMeshImporter.h.  Reading .x files is
//	import work, and after v0.0.6 the importer is the only place that does it.


class TTMTX{
public:
	float a[6];
	TTMTX(){}
	TTMTX(float a0, float a1, float a2, float a3, float a4, float a5){
		a[0] = a0; a[1] = a1; a[2] = a2; a[3] = a3; a[4] = a4; a[5] = a5;
	}
	MTX4 GetMTX4(){
		return MTX4(
			a[0], a[3], 0.0f, 0.0f,
			a[1], a[4], 0.0f, 0.0f,
			a[2], a[5], 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
	}
};

/*
 *	メッシュ
 */
class CMesh{
	//	[RS2EX] Geometry, owned by RailSim rather than by D3DX.
	//	The CPU copy answers picking, shadow silhouettes and bounds without a
	//	device; the GPU resource draws.  Both are geometry only - materials,
	//	textures and flags below stay exactly where the customizers expect them.
	CRS2MeshData m_Data;
	CRS2D3D8MeshResource m_Resource;
	DWORD *m_pMatFlag;		//	マテリアルフラグ (1: rendered)
	DWORD *m_pMatOrder;		//	マテリアル順序
	RS2Material *m_pMat;		//	マテリアルリスト
	RS2Material *m_pCustomMat;	//	代替マテリアルリスト
	LPTEX8 *m_pTex;			//	テクスチャリスト
	LPTEX8 *m_pCustomTex;	//	代替テクスチャリスト
	TTMTX *m_pTexTrans;		//	テクスチャ変換リスト
	DWORD m_dwNumMat;		//	マテリアル数
	string m_strName;		//	ファイル名

	//	コピーコンストラクタ封印
	CMesh &operator=(const CMesh &){ return *this; }
public:
	VEC3 m_min;		//	境界ボックス
	VEC3 m_max;		//	境界ボックス
	VEC3 m_center;	//	境界球の中心
	float m_radius;	//	境界球の半径

	CMesh();
	//	CMesh(CMesh& src);
	~CMesh();

	BOOL Load(BOOL fRes, char *strFile, D3DCOLOR cTrans = 0, int nMipLv = 1);
	BOOL CreateSphere(float r, UINT sl, UINT st, RS2Color4 cv);
	BOOL CreateBox(float x, float y, float z, RS2Color4 cv);
	BOOL CreateTeapot(RS2Color4 cv);
	void Free();
	void ComputeBoundary();

	//	[RS2EX] Replaces "the D3DX pointer is not null".  Means usable geometry
	//	is loaded, which is what every caller actually wanted to know.
	BOOL IsValid() const{ return m_Data.IsValid() && m_Resource.IsValid(); }

	//	[RS2EX] Read-only geometry, for picking and shadow-volume generation.
	const CRS2MeshData &GetMeshData() const{ return m_Data; }

	bool CheckMatNum(DWORD i){ return 0<=i && i<m_dwNumMat; }
	void ResetMatFlag(DWORD);
	void MaskMatFlag(DWORD);
	DWORD GetMatFlag(DWORD i){ return m_pMatFlag[i]; }
	void SetMatFlag(DWORD i, DWORD v){ m_pMatFlag[i] |= v; }
	RS2Material GetDefaultMaterial(DWORD i){ return m_pMat[i]; }
	void SetCustomMaterial(DWORD i, RS2Material &mat){ m_pCustomMat[i] = mat; }
	void SetCustomTexture(DWORD i, LPTEX8 tex){ m_pCustomTex[i] = tex; }
	void SetTexTrans(DWORD i, TTMTX &mtx){ m_pTexTrans[i] = mtx; }

	void RenderCustom(MTX4 *pMtx, CNamedObject *nobj);
	void Render(MTX4 *pMtx);
	void RenderAmb(MTX4 *pMtx);
	void RenderT(MTX4 *pMtx, LPTEX8 pTex);
	void RenderA(MTX4 *pMtx, float alpha);
	void RenderAP(MTX4 *pMtx, float aplus);
	void RenderSC(MTX4 *pMtx, RS2Material *pMat);

private:
	//	[RS2EX] Replaces ID3DXMesh::DrawSubset(materialId): draw every face whose
	//	material ID matches, and nothing if the material owns none.
	void DrawSubset(DWORD materialId);
};

//	メッシュリストの要素
struct MESHINFO{
	CMesh m_Mesh;
	string strName;
	int nRef;
	D3DCOLOR cTrans;
	int nMipLv;
	MESHINFO *pNext;
};

/*
 *	メッシュリスト・クラス
 *	・同じメッシュが多重にロードされないようにする。
 *	・参照数を監視し、使用されていないメッシュを解放する。
 */
class CMeshList{
	MESHINFO *m_pList;	//	リストの先頭
public:
	CMeshList();
	~CMeshList();
	CMesh *Get(BOOL fRes, LPCSTR strName, D3DCOLOR cTrans = 0, int nMipLv = 1);
	void Release(CMesh *);
};

//	外部グローバル
extern bool g_RenderBlink;
