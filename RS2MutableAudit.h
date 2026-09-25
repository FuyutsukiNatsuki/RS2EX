//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	Developer-only mutable-texture / text contract instrumentation
//	(-mutableaudit).
//
//	v0.1.6 moves the CPU-written string texture and the live edit-box text to
//	Direct3D 12.  What the program asks of them - how many textures, what
//	size, what pitch, which part each Lock actually changes, how often, and
//	how updates and the draws that read them are ordered within a frame - is
//	written down here from the running program before anything is reproduced.
//
//	The region a Lock changed is found by comparing the locked surface with a
//	copy taken at Lock time, so the text code itself carries no hook.  Hooks
//	sit in the renderer-neutral functions and change nothing.  Without
//	-mutableaudit each hook is one cached flag test.

#ifndef RS2MUTABLEAUDIT_H_INCLUDED
#define RS2MUTABLEAUDIT_H_INCLUDED

#include "RS2MeshData.h"
#include "RS2RenderState.h"

class CRS2TextureResource;
class CRS2GeometryResource;

bool RS2MutableAuditEnabled();

void RS2MutableAuditCreated(const CRS2TextureResource *texture, int requestedW, int requestedH);
void RS2MutableAuditDestroyed(const CRS2TextureResource *texture);
bool RS2MutableAuditIsMutable(const CRS2TextureResource *texture);

//	Around a successful Lock / before Unlock, with what the lock returned.
void RS2MutableAuditLocked(const CRS2TextureResource *texture, bool ok, const void *bits, int pitch);
void RS2MutableAuditUnlocking(const CRS2TextureResource *texture);

void RS2MutableAuditBind(unsigned int stage, const CRS2TextureResource *texture);
void RS2MutableAuditFilter(unsigned int stage, RS2TextureFilter filter);
void RS2MutableAuditBlend(RS2BlendMode mode);
void RS2MutableAuditAlphaTest(bool enable);
void RS2MutableAuditLighting(bool enable);

void RS2MutableAuditDrawImmediate(const RS2MeshVertexLayout &layout, const void *vertices,
	unsigned int vertexCount);
void RS2MutableAuditDrawGeometry(const CRS2GeometryResource *geometry);
void RS2MutableAuditPresent();

//	The live-text path (RS2Text): font creation, draws, height queries.
void RS2MutableAuditFont(int size, bool bold);
void RS2MutableAuditText(int x, int y, const char *text);
void RS2MutableAuditTextHeight(int height);

//	Called before renderer shutdown.  Idempotent.
void RS2MutableAuditDump();

#endif	//	RS2MUTABLEAUDIT_H_INCLUDED
