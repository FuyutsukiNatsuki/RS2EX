//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	RS2-owned draw submission.
//
//	RailSim II 2.15 drew by handing Direct3D 8 an FVF and a primitive type:
//	twenty-nine DrawPrimitiveUP statements in UI, editor, effect, shadow and
//	opening code, seven DrawPrimitive statements in the vertex-buffer class, and
//	one DrawIndexedPrimitive for meshes.  Each one named a D3D vertex format and
//	a D3D primitive constant at the call site.
//
//	This header says what to draw.  Three operations cover every one of those
//	paths:
//
//	  RS2DrawImmediate	CPU vertices used once and forgotten
//	  RS2DrawBuffered	vertices uploaded ahead of time, no indices
//	  RS2DrawIndexed		uploaded vertices plus indices
//
//	Counts are vertex and index counts, not Direct3D primitive counts.  The
//	backend derives the primitive count and rejects a count that cannot form
//	whole primitives, so the "minus one for a strip" arithmetic stops being
//	every caller's problem.
//
//	The draw layer sets no render state, material, texture or transform.  Those
//	are the v0.0.8 and v0.0.7 boundaries and stay exactly where they are.
//
//	This header deliberately does not include d3d8.h.

#ifndef RS2DRAW_H_INCLUDED
#define RS2DRAW_H_INCLUDED

#include "RS2Color.h"		//	RS2PackedColor, for vertex diffuse
#include "RS2MeshData.h"	//	RS2MeshVertexLayout

/*
 *	What the vertices form.
 */
enum RS2PrimitiveType
{
	RS2_PRIMITIVE_POINT_LIST,
	RS2_PRIMITIVE_LINE_LIST,
	RS2_PRIMITIVE_LINE_STRIP,
	RS2_PRIMITIVE_TRIANGLE_LIST,
	RS2_PRIMITIVE_TRIANGLE_STRIP,
	RS2_PRIMITIVE_TRIANGLE_FAN
};

/*
 *	How many primitives a vertex or index count forms.
 *
 *	Returns 0 when the count cannot form whole primitives - two vertices for a
 *	triangle list, one for a line list, and so on.  Callers do not need to use
 *	this; the backend refuses the same cases.  It is public because the layout
 *	builders and tests want the same rule rather than a second copy of it.
 */
unsigned int RS2PrimitiveCount(RS2PrimitiveType primitive, unsigned int count);

////////////////////////////////////////////////////////////////////////////////
//	Geometry resources
////////////////////////////////////////////////////////////////////////////////

class CRS2GeometryResource;

/*
 *	Upload vertices for repeated drawing.
 *
 *	layout		: describes the bytes, and is validated against them
 *	vertices	: stride * vertexCount bytes
 *
 *	Returns 0 on failure, which callers must tolerate: a mesh that fails to
 *	upload has always produced no drawing rather than a crash.
 */
CRS2GeometryResource *RS2CreateGeometry(
	const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount);

/*
 *	The same, plus indices.
 *
 *	indices		: 32-bit on the way in.  The backend narrows them and fails if
 *			  a vertex index does not fit - the behaviour CMesh already had.
 */
CRS2GeometryResource *RS2CreateIndexedGeometry(
	const RS2MeshVertexLayout &layout,
	const void *vertices, unsigned int vertexCount,
	const unsigned int *indices, unsigned int indexCount);

/*
 *	Replace the vertex bytes of an existing resource.
 *
 *	The layout and the vertex count do not change.  This exists because the
 *	detail grid and the prepared profile geometry rebuild their contents
 *	in place; it is not a general mapped-buffer API.
 */
bool RS2UpdateGeometry(
	CRS2GeometryResource *geometry, const void *vertices, unsigned int vertexCount);

void RS2DestroyGeometry(CRS2GeometryResource *geometry);

unsigned int RS2GetGeometryVertexCount(const CRS2GeometryResource *geometry);

////////////////////////////////////////////////////////////////////////////////
//	Submission
////////////////////////////////////////////////////////////////////////////////

/*
 *	Draw CPU vertices directly.
 *
 *	For data built per call and not kept - UI rectangles, editor guides, effect
 *	quads, shadow volume faces.  Nothing is retained afterwards.
 */
void RS2DrawImmediate(
	const RS2MeshVertexLayout &layout, RS2PrimitiveType primitive,
	const void *vertices, unsigned int vertexCount);

/*
 *	Draw from an uploaded resource.
 */
void RS2DrawBuffered(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstVertex, unsigned int vertexCount);

/*
 *	Draw an indexed range.
 *
 *	firstIndex/indexCount select the range; the whole vertex buffer stays
 *	addressable, as it must for a mesh whose subsets share vertices.
 */
void RS2DrawIndexed(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int firstIndex, unsigned int indexCount);

////////////////////////////////////////////////////////////////////////////////
//	Transforms
////////////////////////////////////////////////////////////////////////////////

/*
 *	World, view and projection, as 16 row-major floats.
 *
 *	Engine matrices stay authoritative and stay MTX4; these submit a copy.
 *	There is no getter and no save/restore stack - nothing in the program reads
 *	a transform back from the device, and adding the ability to would invite it.
 *
 *	Texture-coordinate transforms are not here.  They belong to the texture
 *	stage and stayed in RS2RenderState in v0.0.8.
 */
void RS2SetWorldTransform(const float *matrix);
void RS2SetViewTransform(const float *matrix);
void RS2SetProjectionTransform(const float *matrix);

#endif	//	RS2DRAW_H_INCLUDED
