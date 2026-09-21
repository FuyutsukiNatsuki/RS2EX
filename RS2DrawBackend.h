//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	The Direct3D 8 implementations behind the Draw boundary.
//
//	Internal to the renderer.  Game code includes RS2Draw.h and never this:
//	the whole point of the boundary is that it does not know which of these
//	exists.
//
//	The public names live in RS2Draw.cpp, which picks an implementation from
//	the active backend.  Before v0.1.0 the public names were compiled straight
//	out of the Direct3D 8 file, which meant a Direct3D 12 renderer would still
//	have reached sv3.pDev through every one of them.

#ifndef RS2DRAWBACKEND_H_INCLUDED
#define RS2DRAWBACKEND_H_INCLUDED

#include "RS2Draw.h"
#include "RS2MeshData.h"

CRS2GeometryResource * RS2D3D8_CreateGeometry(const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount);
CRS2GeometryResource * RS2D3D8_CreateIndexedGeometry(const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount,
	const unsigned int *indices, unsigned int indexCount);
bool RS2D3D8_UpdateGeometry(CRS2GeometryResource *geometry, const void *vertices, unsigned int vertexCount);
void RS2D3D8_DestroyGeometry(CRS2GeometryResource *geometry);
unsigned int RS2D3D8_GetGeometryVertexCount(const CRS2GeometryResource *geometry);
unsigned int RS2D3D8_GetLiveGeometryCount();
unsigned int RS2D3D8_GetGeometryVertexBytes();
unsigned int RS2D3D8_GetGeometryIndexBytes();
void RS2D3D8_DrawImmediate(const RS2MeshVertexLayout &layout, RS2PrimitiveType primitive,
	const void *vertices, unsigned int vertexCount);
void RS2D3D8_DrawBuffered(const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstVertex, unsigned int vertexCount);
void RS2D3D8_DrawIndexed(const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstIndex, unsigned int indexCount);
void RS2D3D8_SetWorldTransform(const float *matrix);
void RS2D3D8_SetViewTransform(const float *matrix);
void RS2D3D8_SetProjectionTransform(const float *matrix);

#endif	//	RS2DRAWBACKEND_H_INCLUDED
