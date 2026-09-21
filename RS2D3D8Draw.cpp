//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//	Modified for RS2EX on 2026-09-22.
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
#include "RS2DrawBackend.h"
#include "RS2GeometryResource.h"
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

////////////////////////////////////////////////////////////////////////////////
//	Geometry resource
////////////////////////////////////////////////////////////////////////////////

/*
 *	What this backend keeps behind a geometry resource.
 *
 *	The resource itself is neutral now - see RS2GeometryResource.h - and this
 *	hangs off it as a void pointer that only this file dereferences.  Buffers
 *	still come from RS2D3D8Resources, so pool choice and the live-resource
 *	counters keep the single owner v0.0.5 gave them.
 */
struct RS2D3D8Geometry
{
	LPDIRECT3DVERTEXBUFFER8 vb;
	LPDIRECT3DINDEXBUFFER8 ib;
	DWORD fvf;
};

static RS2D3D8Geometry *RS2D3D8_Payload(const CRS2GeometryResource *geometry){
	return geometry ? (RS2D3D8Geometry *)geometry->payload : 0;
}

/*
 *	Release the buffers and the payload, leaving the neutral resource alone.
 */
void RS2D3D8_DestroyGeometry(CRS2GeometryResource *geometry){
	RS2D3D8Geometry *payload = RS2D3D8_Payload(geometry);

	if(!payload) return;

	if(payload->vb) RS2D3D8_ReleaseVertexBuffer(&payload->vb);
	if(payload->ib) RS2D3D8_ReleaseIndexBuffer(&payload->ib);

	delete payload;
	geometry->payload = 0;
}

/*
 *	Build the vertex half into an already-allocated resource.
 */
static bool RS2D3D8_BuildVertices(
	CRS2GeometryResource *geometry,	//	resource to fill
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices		//	vertex data
){
	unsigned long fvf = 0;

	if(!RS2D3D8_LayoutToFVF(layout, &fvf)) return false;

	RS2D3D8Geometry *payload = new RS2D3D8Geometry;

	payload->vb = 0;
	payload->ib = 0;
	payload->fvf = (DWORD)fvf;
	geometry->payload = payload;

	const UINT bytes = geometry->stride*geometry->vertexCount;

	if(!RS2D3D8_CreateVertexBuffer(bytes, payload->fvf, &payload->vb)) return false;
	return !!RS2D3D8_UploadVertexBuffer(payload->vb, vertices, bytes);
}

bool RS2D3D8_CreateGeometry(
	CRS2GeometryResource *geometry,		//	resource to fill
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices			//	vertex data
){
	return RS2D3D8_BuildVertices(geometry, layout, vertices);
}

bool RS2D3D8_CreateIndexedGeometry(
	CRS2GeometryResource *geometry,		//	resource to fill
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices,			//	vertex data
	const unsigned int *indices		//	index data
){
	//	Inherited from CMesh: the index buffers are 16-bit, and a mesh that
	//	needs more is refused rather than silently wrapping.
	if(geometry->vertexCount>0xffff){
		Debug("[RS2EX Draw] %u vertices exceeds the 16-bit index range\n",
			geometry->vertexCount);
		return false;
	}

	if(!RS2D3D8_BuildVertices(geometry, layout, vertices)) return false;

	RS2D3D8Geometry *payload = RS2D3D8_Payload(geometry);

	if(!RS2D3D8_CreateIndexBuffer(geometry->indexCount*sizeof(WORD), &payload->ib))
		return false;

	void *dst = 0;

	if(!RS2D3D8_LockIndexBuffer(payload->ib, &dst)) return false;

	WORD *out = (WORD *)dst;
	unsigned int i;

	for(i = 0; i<geometry->indexCount; i++){
		if(indices[i]>=geometry->vertexCount){
			RS2D3D8_UnlockIndexBuffer(payload->ib);
			Debug("[RS2EX Draw] index %u is outside %u vertices\n",
				indices[i], geometry->vertexCount);
			return false;
		}
		out[i] = (WORD)indices[i];
	}
	RS2D3D8_UnlockIndexBuffer(payload->ib);
	return true;
}

bool RS2D3D8_UpdateGeometry(
	CRS2GeometryResource *geometry,	//	resource to update
	const void *vertices,		//	new vertex data
	unsigned int vertexCount	//	vertices to write
){
	RS2D3D8Geometry *payload = RS2D3D8_Payload(geometry);

	if(!payload || !payload->vb || !vertices) return false;
	if(vertexCount>geometry->vertexCount) return false;

	return !!RS2D3D8_UploadVertexBuffer(
		payload->vb, vertices, geometry->stride*vertexCount);
}

////////////////////////////////////////////////////////////////////////////////
//	Submission
////////////////////////////////////////////////////////////////////////////////

void RS2D3D8_DrawImmediate(
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

void RS2D3D8_DrawBuffered(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstVertex, unsigned int vertexCount
){
	RS2D3D8Geometry *payload = RS2D3D8_Payload(geometry);

	if(!payload || !payload->vb) return;
	if(firstVertex+vertexCount>geometry->vertexCount) return;

	const unsigned int prims = RS2PrimitiveCount(primitive, vertexCount);
	if(!prims) return;

	sv3.pDev->SetStreamSource(0, payload->vb, geometry->stride);
	sv3.pDev->SetVertexShader(payload->fvf);
	sv3.pDev->DrawPrimitive(RS2ToD3DPrimitive(primitive), firstVertex, prims);
}

void RS2D3D8_DrawIndexed(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstIndex, unsigned int indexCount
){
	RS2D3D8Geometry *payload = RS2D3D8_Payload(geometry);

	if(!payload || !payload->vb || !payload->ib) return;
	if(firstIndex+indexCount>geometry->indexCount) return;

	const unsigned int prims = RS2PrimitiveCount(primitive, indexCount);
	if(!prims) return;

	sv3.pDev->SetStreamSource(0, payload->vb, geometry->stride);
	sv3.pDev->SetIndices(payload->ib, 0);
	sv3.pDev->SetVertexShader(payload->fvf);

	//	The whole vertex buffer stays addressable: a subset draws its own index
	//	range but shares vertices with the others.
	sv3.pDev->DrawIndexedPrimitive(
		RS2ToD3DPrimitive(primitive), 0, geometry->vertexCount, firstIndex, prims);
}

////////////////////////////////////////////////////////////////////////////////
//	Transforms
////////////////////////////////////////////////////////////////////////////////

void RS2D3D8_SetWorldTransform(const float *matrix){
	if(!matrix) return;
	sv3.pDev->SetTransform(D3DTS_WORLD, (const D3DMATRIX *)matrix);
}

void RS2D3D8_SetViewTransform(const float *matrix){
	if(!matrix) return;
	sv3.pDev->SetTransform(D3DTS_VIEW, (const D3DMATRIX *)matrix);
}

void RS2D3D8_SetProjectionTransform(const float *matrix){
	if(!matrix) return;
	sv3.pDev->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX *)matrix);
}
