//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	The Direct3D 8 side of draw submission.
//
//	Every SetStreamSource, SetIndices, SetVertexShader and Draw* call the
//	running program makes is in this file.  The values and the call order are
//	inherited from lib/vertex.cpp, lib/draw.cpp, CVertexDump.cpp and
//	RS2D3D8MeshResource.cpp (parts Copyright (c) 2002 Midikyou) - moved, not
//	redesigned.
//
//	The one deliberate change is at the boundary rather than in the calls: the
//	public API takes vertex and index counts, and the primitive count Direct3D
//	wants is derived here.  Callers used to write that arithmetic themselves,
//	differently per topology, at thirty-seven places.

#include "stdafx.h"
#include "RS2Draw.h"
#include "RS2D3D8Draw.h"
#include "RS2D3D8Resources.h"

////////////////////////////////////////////////////////////////////////////////
//	Layout translation
////////////////////////////////////////////////////////////////////////////////

bool RS2D3D8_LayoutToFVF(const RS2MeshVertexLayout &layout, unsigned long *outFvf){
	if(!layout.HasPosition()) return false;

	//	XYZRHW is not XYZ with a flag: it selects the transformed vertex path,
	//	and Direct3D refuses to combine it with a normal.
	DWORD fvf;

	if(layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED){
		if(layout.HasNormal()) return false;
		fvf = D3DFVF_XYZRHW;
	}else{
		fvf = D3DFVF_XYZ;
		if(layout.HasNormal()) fvf |= D3DFVF_NORMAL;
	}

	if(layout.HasDiffuse()) fvf |= D3DFVF_DIFFUSE;

	if(layout.texCoordCount>8) return false;
	fvf |= layout.texCoordCount<<D3DFVF_TEXCOUNT_SHIFT;

	unsigned int i;
	for(i = 0; i<layout.texCoordCount; i++){
		switch(layout.texCoord[i].components){
		case 1:	fvf |= D3DFVF_TEXTUREFORMAT1<<(i*2+16); break;
		case 2:	/* D3DFVF_TEXTUREFORMAT2 is zero */ break;
		case 3:	fvf |= D3DFVF_TEXTUREFORMAT3<<(i*2+16); break;
		case 4:	fvf |= D3DFVF_TEXTUREFORMAT4<<(i*2+16); break;
		default:	return false;
		}
	}

	if(D3DXGetFVFVertexSize(fvf)!=layout.stride){
		Debug("[RS2EX Draw] layout stride %u does not match fvf %08x\n",
			layout.stride, fvf);
		return false;
	}
	*outFvf = fvf;
	return true;
}

static D3DPRIMITIVETYPE RS2ToD3DPrimitive(RS2PrimitiveType primitive){
	switch(primitive){
	case RS2_PRIMITIVE_POINT_LIST:		return D3DPT_POINTLIST;
	case RS2_PRIMITIVE_LINE_LIST:		return D3DPT_LINELIST;
	case RS2_PRIMITIVE_LINE_STRIP:	return D3DPT_LINESTRIP;
	case RS2_PRIMITIVE_TRIANGLE_STRIP:	return D3DPT_TRIANGLESTRIP;
	case RS2_PRIMITIVE_TRIANGLE_FAN:	return D3DPT_TRIANGLEFAN;
	case RS2_PRIMITIVE_TRIANGLE_LIST:
	default:				return D3DPT_TRIANGLELIST;
	}
}

/*
 *	How many primitives a count forms, or 0 if it cannot form whole ones.
 *
 *	Refusing rather than truncating is the point.  A triangle list of eight
 *	vertices is a caller bug; drawing two triangles and ignoring the rest would
 *	hide it behind geometry that is merely slightly wrong.
 */
unsigned int RS2PrimitiveCount(RS2PrimitiveType primitive, unsigned int count){
	switch(primitive){
	case RS2_PRIMITIVE_POINT_LIST:
		return count;
	case RS2_PRIMITIVE_LINE_LIST:
		return (count>=2 && count%2==0) ? count/2 : 0;
	case RS2_PRIMITIVE_LINE_STRIP:
		return count>=2 ? count-1 : 0;
	case RS2_PRIMITIVE_TRIANGLE_LIST:
		return (count>=3 && count%3==0) ? count/3 : 0;
	case RS2_PRIMITIVE_TRIANGLE_STRIP:
	case RS2_PRIMITIVE_TRIANGLE_FAN:
		return count>=3 ? count-2 : 0;
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//	Geometry resource
////////////////////////////////////////////////////////////////////////////////

/*
 *	The opaque type RS2Draw.h forward-declares.
 *
 *	Buffers come from RS2D3D8Resources, so pool choice and the live-resource
 *	counters keep the single owner v0.0.5 gave them.
 */
class CRS2GeometryResource
{
public:
	LPDIRECT3DVERTEXBUFFER8 vb;
	LPDIRECT3DINDEXBUFFER8 ib;
	DWORD fvf;
	unsigned int stride;
	unsigned int vertexCount;
	unsigned int indexCount;

	CRS2GeometryResource()
		: vb(0), ib(0), fvf(0), stride(0), vertexCount(0), indexCount(0){}
};

static void RS2FreeGeometry(CRS2GeometryResource *g){
	if(!g) return;

	if(g->vb) RS2D3D8_ReleaseVertexBuffer(&g->vb);
	if(g->ib) RS2D3D8_ReleaseIndexBuffer(&g->ib);
	delete g;
}

/*
 *	Build the vertex half.  Returns 0 having released nothing partial.
 */
static CRS2GeometryResource *RS2CreateVertexOnly(
	const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount
){
	if(!vertices || vertexCount==0) return 0;

	unsigned long fvf = 0;
	if(!RS2D3D8_LayoutToFVF(layout, &fvf)) return 0;

	const UINT bytes = layout.stride*vertexCount;

	CRS2GeometryResource *g = new CRS2GeometryResource;
	g->fvf = (DWORD)fvf;
	g->stride = layout.stride;
	g->vertexCount = vertexCount;

	if(!RS2D3D8_CreateVertexBuffer(bytes, g->fvf, &g->vb)
		|| !RS2D3D8_UploadVertexBuffer(g->vb, vertices, bytes)){
		RS2FreeGeometry(g);
		return 0;
	}
	return g;
}

CRS2GeometryResource *RS2CreateGeometry(
	const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount
){
	return RS2CreateVertexOnly(layout, vertices, vertexCount);
}

CRS2GeometryResource *RS2CreateIndexedGeometry(
	const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount,
	const unsigned int *indices, unsigned int indexCount
){
	if(!indices || indexCount==0) return 0;

	//	Inherited from CMesh: the index buffers are 16-bit, and a mesh that
	//	needs more is refused rather than silently wrapping.
	if(vertexCount>0xffff){
		Debug("[RS2EX Draw] %u vertices exceeds the 16-bit index range\n", vertexCount);
		return 0;
	}

	CRS2GeometryResource *g = RS2CreateVertexOnly(layout, vertices, vertexCount);
	if(!g) return 0;

	g->indexCount = indexCount;

	if(!RS2D3D8_CreateIndexBuffer(indexCount*sizeof(WORD), &g->ib)){
		RS2FreeGeometry(g);
		return 0;
	}

	void *dst = 0;
	if(!RS2D3D8_LockIndexBuffer(g->ib, &dst)){
		RS2FreeGeometry(g);
		return 0;
	}

	WORD *out = (WORD *)dst;
	unsigned int i;
	for(i = 0; i<indexCount; i++){
		if(indices[i]>=vertexCount){
			RS2D3D8_UnlockIndexBuffer(g->ib);
			Debug("[RS2EX Draw] index %u is outside %u vertices\n",
				indices[i], vertexCount);
			RS2FreeGeometry(g);
			return 0;
		}
		out[i] = (WORD)indices[i];
	}
	RS2D3D8_UnlockIndexBuffer(g->ib);
	return g;
}

bool RS2UpdateGeometry(
	CRS2GeometryResource *geometry, const void *vertices, unsigned int vertexCount
){
	if(!geometry || !geometry->vb || !vertices) return false;
	if(vertexCount>geometry->vertexCount) return false;

	return !!RS2D3D8_UploadVertexBuffer(
		geometry->vb, vertices, geometry->stride*vertexCount);
}

void RS2DestroyGeometry(CRS2GeometryResource *geometry){
	RS2FreeGeometry(geometry);
}

unsigned int RS2GetGeometryVertexCount(const CRS2GeometryResource *geometry){
	return geometry ? geometry->vertexCount : 0;
}

////////////////////////////////////////////////////////////////////////////////
//	Submission
////////////////////////////////////////////////////////////////////////////////

void RS2DrawImmediate(
	const RS2MeshVertexLayout &layout, RS2PrimitiveType primitive,
	const void *vertices, unsigned int vertexCount
){
	const unsigned int prims = RS2PrimitiveCount(primitive, vertexCount);
	if(!prims || !vertices) return;

	unsigned long fvf = 0;
	if(!RS2D3D8_LayoutToFVF(layout, &fvf)) return;

	sv3.pDev->SetVertexShader((DWORD)fvf);
	sv3.pDev->DrawPrimitiveUP(
		RS2ToD3DPrimitive(primitive), prims, vertices, layout.stride);
}

void RS2DrawBuffered(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstVertex, unsigned int vertexCount
){
	if(!geometry || !geometry->vb) return;
	if(firstVertex+vertexCount>geometry->vertexCount) return;

	const unsigned int prims = RS2PrimitiveCount(primitive, vertexCount);
	if(!prims) return;

	sv3.pDev->SetStreamSource(0, geometry->vb, geometry->stride);
	sv3.pDev->SetVertexShader(geometry->fvf);
	sv3.pDev->DrawPrimitive(RS2ToD3DPrimitive(primitive), firstVertex, prims);
}

void RS2DrawIndexed(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstIndex, unsigned int indexCount
){
	if(!geometry || !geometry->vb || !geometry->ib) return;
	if(firstIndex+indexCount>geometry->indexCount) return;

	const unsigned int prims = RS2PrimitiveCount(primitive, indexCount);
	if(!prims) return;

	sv3.pDev->SetStreamSource(0, geometry->vb, geometry->stride);
	sv3.pDev->SetIndices(geometry->ib, 0);
	sv3.pDev->SetVertexShader(geometry->fvf);

	//	The whole vertex buffer stays addressable: a subset draws its own index
	//	range but shares vertices with the others.
	sv3.pDev->DrawIndexedPrimitive(
		RS2ToD3DPrimitive(primitive), 0, geometry->vertexCount, firstIndex, prims);
}

////////////////////////////////////////////////////////////////////////////////
//	Transforms
////////////////////////////////////////////////////////////////////////////////

void RS2SetWorldTransform(const float *matrix){
	if(!matrix) return;
	sv3.pDev->SetTransform(D3DTS_WORLD, (const D3DMATRIX *)matrix);
}

void RS2SetViewTransform(const float *matrix){
	if(!matrix) return;
	sv3.pDev->SetTransform(D3DTS_VIEW, (const D3DMATRIX *)matrix);
}

void RS2SetProjectionTransform(const float *matrix){
	if(!matrix) return;
	sv3.pDev->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX *)matrix);
}
