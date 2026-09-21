//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	Draw: the public boundary, routed to the active backend.
//
//	Each function here is the name game code calls.  It chooses an
//	implementation and nothing else - no logic, no state, no reordering - so
//	that the Direct3D 8 path is exactly what it was before the routing existed.
//
//	The Direct3D 12 side is not implemented in v0.1.0.  It says so, once per
//	call site, and returns safely.  What it must never do is fall through to
//	the Direct3D 8 implementation: that would reach a device this backend does
//	not own, and it is the failure the whole release is built to prevent.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2DrawBackend.h"
#include "RS2D3D12Unsupported.h"
#include "RS2D3D12Draw.h"

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

/*
 *	Create a geometry resource.
 *
 *	The resource is neutral and this code owns it; only its contents belong to
 *	a backend.  That is what lets a resource say which backend built it, which
 *	matters because game code holds these across frames and a resource handed
 *	to the wrong backend would be an interesting kind of crash to debug.
 */
CRS2GeometryResource *RS2CreateGeometry(
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices,			//	vertex data
	unsigned int vertexCount		//	vertices
){
	if(!vertices || !vertexCount) return 0;

	const RS2RendererBackendType backend = GetRS2Renderer().GetBackendType();
	CRS2GeometryResource *geometry = RS2GeometryAllocate(
		backend, layout.stride, vertexCount, 0);
	bool built = false;

	if(backend==RS2_RENDERER_D3D8)
		built = RS2D3D8_CreateGeometry(geometry, layout, vertices);
	else
		built = RS2D3D12_CreateGeometry(geometry, layout, vertices);

	if(!built){
		//	Whatever the backend managed to build is released before the
		//	resource goes, so a failure leaves nothing behind.
		if(backend==RS2_RENDERER_D3D8) RS2D3D8_DestroyGeometry(geometry);
		else RS2D3D12_DestroyGeometry(geometry);
		RS2GeometryFree(geometry);
		return 0;
	}

	RS2GeometryCount(geometry);
	return geometry;
}

CRS2GeometryResource *RS2CreateIndexedGeometry(
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices,			//	vertex data
	unsigned int vertexCount,		//	vertices
	const unsigned int *indices,		//	index data
	unsigned int indexCount			//	indices
){
	if(!vertices || !vertexCount || !indices || !indexCount) return 0;

	const RS2RendererBackendType backend = GetRS2Renderer().GetBackendType();
	CRS2GeometryResource *geometry = RS2GeometryAllocate(
		backend, layout.stride, vertexCount, indexCount);
	bool built = false;

	if(backend==RS2_RENDERER_D3D8)
		built = RS2D3D8_CreateIndexedGeometry(geometry, layout, vertices, indices);
	else
		built = RS2D3D12_CreateIndexedGeometry(geometry, layout, vertices, indices);

	if(!built){
		if(backend==RS2_RENDERER_D3D8) RS2D3D8_DestroyGeometry(geometry);
		else RS2D3D12_DestroyGeometry(geometry);
		RS2GeometryFree(geometry);
		return 0;
	}

	RS2GeometryCount(geometry);
	return geometry;
}

bool RS2UpdateGeometry(
	CRS2GeometryResource *geometry,	//	resource to update
	const void *vertices,		//	new vertex data
	unsigned int vertexCount	//	vertices to write
){
	if(!RS2GeometryUsable(geometry, "RS2UpdateGeometry")) return false;

	if(geometry->backend==RS2_RENDERER_D3D8)
		return RS2D3D8_UpdateGeometry(geometry, vertices, vertexCount);

	RS2D3D12Unsupported("RS2UpdateGeometry");
	return false;
}

/*
 *	Destroy a geometry resource.
 *
 *	Deliberately not routed through RS2GeometryUsable: a resource has to be
 *	destroyable by the backend that built it even if that is no longer the
 *	active one, or shutting one backend down would leak everything the other
 *	had made.
 */
void RS2DestroyGeometry(
	CRS2GeometryResource *geometry	//	resource to destroy
){
	if(!geometry) return;

	if(geometry->backend==RS2_RENDERER_D3D8) RS2D3D8_DestroyGeometry(geometry);
	else RS2D3D12_DestroyGeometry(geometry);

	RS2GeometryFree(geometry);
}

//	Neutral: these describe the resource, not the backend behind it.
unsigned int RS2GetGeometryVertexCount(const CRS2GeometryResource *geometry){
	return geometry ? geometry->vertexCount : 0;
}

unsigned int RS2GetLiveGeometryCount(){ return RS2GeometryLiveCount(); }
unsigned int RS2GetGeometryVertexBytes(){ return RS2GeometryTotalVertexBytes(); }
unsigned int RS2GetGeometryIndexBytes(){ return RS2GeometryTotalIndexBytes(); }

void RS2DrawImmediate(const RS2MeshVertexLayout &layout, RS2PrimitiveType primitive,
	const void *vertices, unsigned int vertexCount){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DrawImmediate(layout, primitive, vertices, vertexCount);
	else RS2D3D12_DrawImmediate(layout, primitive, vertices, vertexCount);
}

void RS2DrawBuffered(const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstVertex, unsigned int vertexCount){
	if(!RS2GeometryUsable(geometry, "RS2DrawBuffered")) return;

	if(geometry->backend==RS2_RENDERER_D3D8)
		RS2D3D8_DrawBuffered(geometry, primitive, firstVertex, vertexCount);
	else RS2D3D12_DrawBuffered(geometry, primitive, firstVertex, vertexCount);
}

void RS2DrawIndexed(const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstIndex, unsigned int indexCount){
	if(!RS2GeometryUsable(geometry, "RS2DrawIndexed")) return;

	if(geometry->backend==RS2_RENDERER_D3D8)
		RS2D3D8_DrawIndexed(geometry, primitive, firstIndex, indexCount);
	else RS2D3D12_DrawIndexed(geometry, primitive, firstIndex, indexCount);
}

void RS2SetWorldTransform(const float *matrix){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetWorldTransform(matrix);
	else RS2D3D12_SetWorldTransform(matrix);
}

void RS2SetViewTransform(const float *matrix){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetViewTransform(matrix);
	else RS2D3D12_SetViewTransform(matrix);
}

void RS2SetProjectionTransform(const float *matrix){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetProjectionTransform(matrix);
	else RS2D3D12_SetProjectionTransform(matrix);
}
