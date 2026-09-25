//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21, 2026-09-26.

//	[RS2EX] LPVB8 and PRIMTYPE removed in v0.0.9.  The buffer is a
//	CRS2GeometryResource and the primitive is an RS2PrimitiveType; neither
//	name had a use outside this header once CVertex stopped drawing directly.

#include "..\RS2Draw.h"

void CalcNormal(VEC3 t[3], VEC3 *n);

/*
 *	頂点フォーマット
 *
 *	※ライティング済み頂点を使用する場合はライティングをOFFする。
 */

//	座標3D変換済み、ライティング済み

struct VTX_TL{
	FLOAT x, y, z;
	FLOAT rhw;
	RS2PackedColor d;
};

//	座標3D変換済み、ライティング済み、テクスチャ有り

struct VTX_TLX{
	FLOAT x, y, z;
	FLOAT rhw;
	RS2PackedColor d;
	FLOAT u;
	FLOAT v;
};

//	ライティング済み

struct VTX_L{
	FLOAT x, y, z;
	RS2PackedColor d;
};

//	ライティング済み、テクスチャ有り

struct VTX_LX{
	FLOAT x, y, z;
	RS2PackedColor d;
	FLOAT u, v;
};

inline void SetVTX_LX(VTX_LX *vt, float x, float y, float z, RS2PackedColor d, float u, float v){
	vt->x = x, vt->y = y, vt->z = z, vt->d = d, vt->u = u, vt->v = v;
}

void SetRectTL_LX(VTX_LX *vt, int x1, int y1, int x2, int y2, int z, RS2PackedColor c);

//	ライティング済み、テクスチャ×２

struct VTX_LX2{
	FLOAT x, y, z;
	RS2PackedColor d;
	FLOAT u1, v1;
	FLOAT u2, v2;
};

//	未ライティング

struct VTX_N{
	FLOAT x, y, z;
	VEC3 n;
	RS2PackedColor d;
};

inline void SetVTX_N(
	VTX_N *vt, float x, float y, float z, VEC3 n, RS2PackedColor d){
	vt->x = x, vt->y = y, vt->z = z, vt->n = n, vt->d = d;
}

//	未ライティング、テクスチャ有り

struct VTX_NX{
	FLOAT x, y, z;
	VEC3 n;
	RS2PackedColor d;
	FLOAT u, v;
};

inline void SetVTX_NX(
	VTX_NX *vt, float x, float y, float z, VEC3 n, RS2PackedColor d, float u, float v){
	vt->x = x, vt->y = y, vt->z = z, vt->n = n, vt->d = d, vt->u = u, vt->v = v;
}

//	未ライティング、テクスチャ×２

struct VTX_NX2{
	FLOAT x, y, z;
	VEC3 n;
	RS2PackedColor d;
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
//	would otherwise hide.  The backend checks the resulting stride against
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
/*
 *	Vertices uploaded once and drawn repeatedly.
 *
 *	[RS2EX] The Direct3D vertex buffer, the FVF and the six direct draw calls
 *	moved behind CRS2GeometryResource in v0.0.9.  What the class is for did
 *	not change: the detail grid and the prepared dump batches still build
 *	their vertices once and draw the whole buffer each frame.
 *
 *	Lock(), Unlock(), GetFVF() and the generic Render(type, count) are gone.
 *	None had a caller in the compiled tree, and each would have had to become
 *	a mapped-buffer or native-primitive escape in the new API to survive.
 *
 *	The strip and fan methods no longer take a count.  They took a primitive
 *	count, which is the convention the draw boundary removed, and every
 *	draw here covers the whole buffer anyway.
 */
class CVertex{
	CRS2GeometryResource *m_Geometry;
	UINT m_num;		//	vertices, not primitives

	//	コピーコンストラクタ禁止
	CVertex& operator = (const CVertex&){return *this;}
public:
	CVertex();
	~CVertex();

	/*
	 *	Upload vertices.
	 *
	 *	pSrc		: vertexCount * layout.stride bytes
	 *	layout	: one of the RS2Layout*() builders above
	 *
	 *	2.15 took a byte size and derived the count from the FVF.  The count is
	 *	now explicit, because the byte size was always count * stride at every
	 *	call site and the division was a chance to be wrong.
	 */
	BOOL Create(const void *pSrc, const RS2MeshVertexLayout &layout, UINT vertexCount);
	void Free();

	//	Each draws the whole buffer.
	void RenderPL();
	void RenderLL();
	void RenderLS();
	void RenderTL();
	void RenderTS();
	void RenderTF();

	/*
	 *	頂点数の取得
	 */
	UINT Count(){return m_num;}
};
