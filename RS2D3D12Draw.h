//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	The Direct3D 12 side of the draw boundary.
//
//	Everything the public RS2Draw functions need when Direct3D 12 is the active
//	backend.  RS2Draw.cpp chooses between these and the Direct3D 8 ones; this
//	header is internal and no game code includes it.
//
//	No Direct3D type appears in the signatures, because the callers are the
//	neutral dispatch layer, not renderer code.

#ifndef RS2D3D12DRAW_H_INCLUDED
#define RS2D3D12DRAW_H_INCLUDED

#include "RS2Draw.h"
#include "RS2MeshData.h"
#include "RS2RenderState.h"
#include "RS2GeometryResource.h"

class CRS2GeometryResource;

//	Transforms.  The engine owns these matrices; the backend only remembers
//	what it was last told and combines them when a draw needs them.
void RS2D3D12_SetWorldTransform(const float *matrix);
void RS2D3D12_SetViewTransform(const float *matrix);
void RS2D3D12_SetProjectionTransform(const float *matrix);

//	Render state that the pipeline key is built from.  Set by the render-state
//	boundary; read here when a draw picks its pipeline.
void RS2D3D12_SetDepthTest(bool enable);
void RS2D3D12_SetDepthWrite(bool enable);
void RS2D3D12_SetDepthFunc(RS2CompareFunc func);
void RS2D3D12_SetCullMode(RS2CullMode mode);
void RS2D3D12_SetBlend(RS2BlendMode mode);
void RS2D3D12_SetAlphaTest(bool enable);
void RS2D3D12_SetAlphaRef(unsigned int ref);
void RS2D3D12_SetAlphaFunc(RS2CompareFunc func);

/*
 *	Put the state back where the engine expects to find it at start-up.
 *
 *	Direct3D 12 has no runtime defaults to inherit, so this says what they are
 *	rather than leaving them to whatever the struct was initialised with.
 */
void RS2D3D12_ApplyInitialRenderState();

/*
 *	Draw vertices that exist only for the duration of the call.
 *
 *	The data is copied into this frame's scratch memory, because Direct3D 12
 *	has no equivalent of DrawPrimitiveUP and the GPU reads the buffer long
 *	after this function returns.
 */
void RS2D3D12_DrawImmediate(
	const RS2MeshVertexLayout &layout,
	RS2PrimitiveType primitive,
	const void *vertices,
	unsigned int vertexCount);

//	Geometry resources.  The neutral side owns the resource and these fill in
//	and release what is behind it, exactly as the Direct3D 8 side does.
bool RS2D3D12_CreateGeometry(
	CRS2GeometryResource *geometry,
	const RS2MeshVertexLayout &layout,
	const void *vertices);
bool RS2D3D12_CreateIndexedGeometry(
	CRS2GeometryResource *geometry,
	const RS2MeshVertexLayout &layout,
	const void *vertices,
	const unsigned int *indices);
void RS2D3D12_DestroyGeometry(CRS2GeometryResource *geometry);

void RS2D3D12_DrawBuffered(
	const CRS2GeometryResource *geometry,
	RS2PrimitiveType primitive,
	unsigned int firstVertex,
	unsigned int vertexCount);

void RS2D3D12_DrawIndexed(
	const CRS2GeometryResource *geometry,
	RS2PrimitiveType primitive,
	unsigned int firstIndex,
	unsigned int indexCount);

/*
 *	How many draws the Direct3D 12 backend has submitted.
 *
 *	Read by the smoke tests and the validation record.  A renderer that is
 *	running and presenting but has submitted nothing looks exactly like one
 *	that is drawing something invisible, and this tells the two apart.
 */
unsigned int RS2D3D12_GetDrawCount();

/*
 *	How many draws were refused, for any reason.
 */
unsigned int RS2D3D12_GetRefusedDrawCount();

#endif	//	RS2D3D12DRAW_H_INCLUDED
