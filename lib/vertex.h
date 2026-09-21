//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21.

typedef LPDIRECT3DVERTEXBUFFER8 LPVB8;
typedef D3DPRIMITIVETYPE PRIMTYPE;

void CalcNormal(VEC3 t[3], VEC3 *n);

/*
 *	頂点フォーマット
 *
 *	※ライティング済み頂点を使用する場合はライティングをOFFする。
 */

//	座標3D変換済み、ライティング済み
#define FVF_TL (D3DFVF_XYZRHW|D3DFVF_DIFFUSE)

struct VTX_TL{
	FLOAT x, y, z;
	FLOAT rhw;
	DWORD d;
};

//	座標3D変換済み、ライティング済み、テクスチャ有り
#define FVF_TLX (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)

struct VTX_TLX{
	FLOAT x, y, z;
	FLOAT rhw;
	DWORD d;
	FLOAT u;
	FLOAT v;
};

//	ライティング済み
#define FVF_L (D3DFVF_XYZ|D3DFVF_DIFFUSE)

struct VTX_L{
	FLOAT x, y, z;
	DWORD d;
};

//	ライティング済み、テクスチャ有り
#define FVF_LX (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)

struct VTX_LX{
	FLOAT x, y, z;
	DWORD d;
	FLOAT u, v;
};

inline void SetVTX_LX(VTX_LX *vt, float x, float y, float z, D3DCOLOR d, float u, float v){
	vt->x = x, vt->y = y, vt->z = z, vt->d = d, vt->u = u, vt->v = v;
}

void SetRectTL_LX(VTX_LX *vt, int x1, int y1, int x2, int y2, int z, D3DCOLOR c);

//	ライティング済み、テクスチャ×２
#define FVF_LX2 (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2)

struct VTX_LX2{
	FLOAT x, y, z;
	DWORD d;
	FLOAT u1, v1;
	FLOAT u2, v2;
};

//	未ライティング
#define FVF_N (D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE)

struct VTX_N{
	FLOAT x, y, z;
	VEC3 n;
	DWORD d;
};

inline void SetVTX_N(
	VTX_N *vt, float x, float y, float z, VEC3 n, D3DCOLOR d){
	vt->x = x, vt->y = y, vt->z = z, vt->n = n, vt->d = d;
}

//	未ライティング、テクスチャ有り
#define FVF_NX (D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1)

struct VTX_NX{
	FLOAT x, y, z;
	VEC3 n;
	DWORD d;
	FLOAT u, v;
};

inline void SetVTX_NX(
	VTX_NX *vt, float x, float y, float z, VEC3 n, D3DCOLOR d, float u, float v){
	vt->x = x, vt->y = y, vt->z = z, vt->n = n, vt->d = d, vt->u = u, vt->v = v;
}

//	未ライティング、テクスチャ×２
#define FVF_NX2 (D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX2)

struct VTX_NX2{
	FLOAT x, y, z;
	VEC3 n;
	DWORD d;
	FLOAT u1, v1;
	FLOAT u2, v2;
};


////////////////////////////////////////////////////////////////////////////////
//	[RS2EX] Layout builders
//
//	Each of the structs above is one of the nine vertex layouts this program
//	draws with.  2.15 paired each with an FVF constant; these describe the same
//	bytes in terms the renderer-neutral draw boundary understands.
//
//	Offsets come from offsetof rather than being written out, because a layout
//	that silently disagrees with its struct is exactly the failure this boundary
//	would otherwise hide.  The D3D8 backend checks the resulting stride against
//	the format it derives, so a mismatch is refused rather than misread.
////////////////////////////////////////////////////////////////////////////////

#include <stddef.h>	//	offsetof

#include "..\RS2MeshData.h"

/*
 *	Shared spine: position, optional normal, optional diffuse, N texture sets
 *	of two floats each.  Every audited layout is this shape.
 */
inline RS2MeshVertexLayout RS2MakeVertexLayout(
	unsigned int stride, RS2PositionSemantic semantic,
	int positionOffset, int normalOffset, int diffuseOffset,
	unsigned int texCoordCount, int texCoord0Offset, int texCoord1Offset
){
	RS2MeshVertexLayout layout;

	layout.stride = stride;
	layout.positionOffset = positionOffset;
	layout.positionSemantic = semantic;
	layout.normalOffset = normalOffset;
	layout.diffuseOffset = diffuseOffset;
	layout.texCoordCount = texCoordCount;

	if(texCoordCount>0){
		layout.texCoord[0].offset = texCoord0Offset;
		layout.texCoord[0].components = 2;
	}
	if(texCoordCount>1){
		layout.texCoord[1].offset = texCoord1Offset;
		layout.texCoord[1].components = 2;
	}
	return layout;
}

#define RS2_NO_ATTRIBUTE RS2MeshVertexLayout::NOT_PRESENT

//	Screen space, colour
inline RS2MeshVertexLayout RS2LayoutTL(){
	return RS2MakeVertexLayout(sizeof(VTX_TL), RS2_POSITION_ALREADY_TRANSFORMED,
		offsetof(VTX_TL, x), RS2_NO_ATTRIBUTE, offsetof(VTX_TL, d),
		0, RS2_NO_ATTRIBUTE, RS2_NO_ATTRIBUTE);
}

//	Screen space, colour, one texture set
inline RS2MeshVertexLayout RS2LayoutTLX(){
	return RS2MakeVertexLayout(sizeof(VTX_TLX), RS2_POSITION_ALREADY_TRANSFORMED,
		offsetof(VTX_TLX, x), RS2_NO_ATTRIBUTE, offsetof(VTX_TLX, d),
		1, offsetof(VTX_TLX, u), RS2_NO_ATTRIBUTE);
}

//	World space, colour
inline RS2MeshVertexLayout RS2LayoutL(){
	return RS2MakeVertexLayout(sizeof(VTX_L), RS2_POSITION_TRANSFORMED_BY_PIPELINE,
		offsetof(VTX_L, x), RS2_NO_ATTRIBUTE, offsetof(VTX_L, d),
		0, RS2_NO_ATTRIBUTE, RS2_NO_ATTRIBUTE);
}

//	World space, colour, one texture set
inline RS2MeshVertexLayout RS2LayoutLX(){
	return RS2MakeVertexLayout(sizeof(VTX_LX), RS2_POSITION_TRANSFORMED_BY_PIPELINE,
		offsetof(VTX_LX, x), RS2_NO_ATTRIBUTE, offsetof(VTX_LX, d),
		1, offsetof(VTX_LX, u), RS2_NO_ATTRIBUTE);
}

//	World space, colour, two texture sets
inline RS2MeshVertexLayout RS2LayoutLX2(){
	return RS2MakeVertexLayout(sizeof(VTX_LX2), RS2_POSITION_TRANSFORMED_BY_PIPELINE,
		offsetof(VTX_LX2, x), RS2_NO_ATTRIBUTE, offsetof(VTX_LX2, d),
		2, offsetof(VTX_LX2, u1), offsetof(VTX_LX2, u2));
}

//	World space, normal, colour
inline RS2MeshVertexLayout RS2LayoutN(){
	return RS2MakeVertexLayout(sizeof(VTX_N), RS2_POSITION_TRANSFORMED_BY_PIPELINE,
		offsetof(VTX_N, x), offsetof(VTX_N, n), offsetof(VTX_N, d),
		0, RS2_NO_ATTRIBUTE, RS2_NO_ATTRIBUTE);
}

//	World space, normal, colour, one texture set
inline RS2MeshVertexLayout RS2LayoutNX(){
	return RS2MakeVertexLayout(sizeof(VTX_NX), RS2_POSITION_TRANSFORMED_BY_PIPELINE,
		offsetof(VTX_NX, x), offsetof(VTX_NX, n), offsetof(VTX_NX, d),
		1, offsetof(VTX_NX, u), RS2_NO_ATTRIBUTE);
}

//	World space, normal, colour, two texture sets
inline RS2MeshVertexLayout RS2LayoutNX2(){
	return RS2MakeVertexLayout(sizeof(VTX_NX2), RS2_POSITION_TRANSFORMED_BY_PIPELINE,
		offsetof(VTX_NX2, x), offsetof(VTX_NX2, n), offsetof(VTX_NX2, d),
		2, offsetof(VTX_NX2, u1), offsetof(VTX_NX2, u2));
}

/*
/*
 *	Position only - three floats, nothing else.
 *
 *	The shadow volume, whose vertex type is already VEC3.  No struct is
 *	declared here: naming a second three-float type would only create the
 *	chance for the two to disagree.
 *
 *	It carries no colour because it never writes colour - the volume pass
 *	blends Zero/One and touches only stencil.
 */
inline RS2MeshVertexLayout RS2LayoutPositionOnly(){
	return RS2MakeVertexLayout(3*sizeof(float), RS2_POSITION_TRANSFORMED_BY_PIPELINE,
		0, RS2_NO_ATTRIBUTE, RS2_NO_ATTRIBUTE,
		0, RS2_NO_ATTRIBUTE, RS2_NO_ATTRIBUTE);
}

//	頂点クラス
class CVertex{
	LPVB8 m_pVB;	//	頂点バッファ
	DWORD m_fvf;	//	頂点フォーマット
	UINT m_stride;	//	次の頂点データまでのバイト数
	UINT m_num;		//	頂点数

	//	コピーコンストラクタ封印
	CVertex& operator = (const CVertex&){return *this;}
public:
	CVertex();
	~CVertex();

	BOOL Create(LPVOID pSrc, DWORD fvf, UINT size);
	void Free();
	BOOL Lock(LPVOID *ppBuf);
	void Unlock();

	void RenderPL();
	void RenderLL();
	void RenderLS(UINT count);
	void RenderTL();
	void RenderTS(UINT count);
	void RenderTF(UINT count);
	void Render(PRIMTYPE type, UINT count);

	/*
	 *	頂点フォーマットの取得
	 */
	DWORD GetFVF(){return m_fvf;}
	/*
	 *	頂点数の取得
	 */
	UINT Count(){return m_num;}
	//	[RS2EX] GetObject() removed.  LPVB8 appeared nowhere outside this header,
	//	so the buffer never escaped the class and no compatibility accessor is
	//	needed - unlike CTexture, whose handle 11 call sites still require.
};
