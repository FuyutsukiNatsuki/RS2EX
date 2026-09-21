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

CRS2GeometryResource * RS2CreateGeometry(const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_CreateGeometry(layout, vertices, vertexCount);

	RS2D3D12Unsupported("RS2CreateGeometry");
	return 0;
}

CRS2GeometryResource * RS2CreateIndexedGeometry(const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount,
	const unsigned int *indices, unsigned int indexCount){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_CreateIndexedGeometry(layout, vertices, vertexCount, indices, indexCount);

	RS2D3D12Unsupported("RS2CreateIndexedGeometry");
	return 0;
}

bool RS2UpdateGeometry(CRS2GeometryResource *geometry, const void *vertices, unsigned int vertexCount){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_UpdateGeometry(geometry, vertices, vertexCount);

	RS2D3D12Unsupported("RS2UpdateGeometry");
	return false;
}

void RS2DestroyGeometry(CRS2GeometryResource *geometry){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DestroyGeometry(geometry);
	else RS2D3D12Unsupported("RS2DestroyGeometry");
}

unsigned int RS2GetGeometryVertexCount(const CRS2GeometryResource *geometry){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_GetGeometryVertexCount(geometry);

	RS2D3D12Unsupported("RS2GetGeometryVertexCount");
	return 0;
}

unsigned int RS2GetLiveGeometryCount(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_GetLiveGeometryCount();

	RS2D3D12Unsupported("RS2GetLiveGeometryCount");
	return 0;
}

unsigned int RS2GetGeometryVertexBytes(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_GetGeometryVertexBytes();

	RS2D3D12Unsupported("RS2GetGeometryVertexBytes");
	return 0;
}

unsigned int RS2GetGeometryIndexBytes(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_GetGeometryIndexBytes();

	RS2D3D12Unsupported("RS2GetGeometryIndexBytes");
	return 0;
}

void RS2DrawImmediate(const RS2MeshVertexLayout &layout, RS2PrimitiveType primitive,
	const void *vertices, unsigned int vertexCount){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DrawImmediate(layout, primitive, vertices, vertexCount);
	else RS2D3D12Unsupported("RS2DrawImmediate");
}

void RS2DrawBuffered(const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstVertex, unsigned int vertexCount){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DrawBuffered(geometry, primitive, firstVertex, vertexCount);
	else RS2D3D12Unsupported("RS2DrawBuffered");
}

void RS2DrawIndexed(const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstIndex, unsigned int indexCount){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DrawIndexed(geometry, primitive, firstIndex, indexCount);
	else RS2D3D12Unsupported("RS2DrawIndexed");
}

void RS2SetWorldTransform(const float *matrix){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetWorldTransform(matrix);
	else RS2D3D12Unsupported("RS2SetWorldTransform");
}

void RS2SetViewTransform(const float *matrix){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetViewTransform(matrix);
	else RS2D3D12Unsupported("RS2SetViewTransform");
}

void RS2SetProjectionTransform(const float *matrix){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetProjectionTransform(matrix);
	else RS2D3D12Unsupported("RS2SetProjectionTransform");
}
