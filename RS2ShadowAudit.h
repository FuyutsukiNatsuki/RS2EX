//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	Developer-only stencil-shadow contract instrumentation (-shadowaudit).
//
//	v0.1.5 moves the stencil shadow of CShadowVolume to Direct3D 12.  The
//	states that path asks for - stencil function, reference, masks and ops,
//	with the depth, cull, blend and shade state around them, in order - are
//	written down here from the running program before anything is
//	reproduced.
//
//	Hooks sit in the renderer-neutral public functions, see the same calls on
//	either backend, and change nothing.  Without -shadowaudit each hook is one
//	cached flag test.

#ifndef RS2SHADOWAUDIT_H_INCLUDED
#define RS2SHADOWAUDIT_H_INCLUDED

#include "RS2MeshData.h"

class CRS2GeometryResource;

enum RS2ShadowAuditField
{
	RS2_SA_DEPTH_TEST,
	RS2_SA_DEPTH_WRITE,
	RS2_SA_DEPTH_FUNC,
	RS2_SA_CULL,
	RS2_SA_BLEND,
	RS2_SA_SHADE,
	RS2_SA_STENCIL_TEST,
	RS2_SA_STENCIL_FUNC,
	RS2_SA_STENCIL_REF,
	RS2_SA_STENCIL_READ_MASK,
	RS2_SA_STENCIL_WRITE_MASK,
	RS2_SA_STENCIL_FAIL,
	RS2_SA_STENCIL_DEPTH_FAIL,
	RS2_SA_STENCIL_PASS,
	RS2_SA_FOG_DISABLE,		//	value ignored
	RS2_SA_BASE_COMBINE,	//	value ignored
	RS2_SA_CLEAR_DEPTH,		//	value ignored
	RS2_SA_FIELDS
};

bool RS2ShadowAuditEnabled();

void RS2ShadowAuditSet(RS2ShadowAuditField field, unsigned int value);
void RS2ShadowAuditDrawImmediate(const RS2MeshVertexLayout &layout, const void *vertices,
	unsigned int vertexCount);
void RS2ShadowAuditDrawGeometry(const CRS2GeometryResource *geometry, unsigned int count,
	bool indexed);
void RS2ShadowAuditRenderPass();
void RS2ShadowAuditPresent();

//	True exactly once, on the first stencil enable, so the backend can record
//	the stencil state it holds before the shadow pass sets any of it.
bool RS2ShadowAuditTakeFirstEnable();

//	Called before renderer shutdown.  Idempotent.
void RS2ShadowAuditDump();

#endif	//	RS2SHADOWAUDIT_H_INCLUDED
