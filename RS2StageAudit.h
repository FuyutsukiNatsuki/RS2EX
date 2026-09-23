//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-24.
//
//	Developer-only texture-stage / environment / UV contract instrumentation.
//
//	v0.1.4 moves the second texture stage, environment mapping and texture
//	transforms to Direct3D 12.  What RailSim asks of them - which stages, which
//	textures, which matrices, with or without normals and lighting - is
//	written down here from the running program before anything is reproduced.
//
//	Hooks sit in the renderer-neutral public functions, see the same calls on
//	either backend, and change nothing.  Without -stageaudit each hook is one
//	cached flag test.

#ifndef RS2STAGEAUDIT_H_INCLUDED
#define RS2STAGEAUDIT_H_INCLUDED

#include "RS2MeshData.h"
#include "RS2Draw.h"
#include "RS2RenderState.h"

class CRS2GeometryResource;
class CRS2TextureResource;

bool RS2StageAuditEnabled();

void RS2StageAuditTextureCreated(const CRS2TextureResource *texture, const char *source,
	bool fromResource);
void RS2StageAuditTextureDestroyed(const CRS2TextureResource *texture);

void RS2StageAuditBind(unsigned int stage, const CRS2TextureResource *texture);
void RS2StageAuditFilter(unsigned int stage, RS2TextureFilter filter);
void RS2StageAuditCombine(unsigned int stage, bool enable);
void RS2StageAuditEnvironment(unsigned int stage, bool enable);
void RS2StageAuditUVMatrix(unsigned int stage, const float *matrix);
void RS2StageAuditUVTransform(unsigned int stage, bool enable);

//	Other state each draw is recorded under.
void RS2StageAuditLighting(bool enable);
void RS2StageAuditAlphaTest(bool enable);
void RS2StageAuditBlend(RS2BlendMode mode);

void RS2StageAuditGeometryCreated(
	const CRS2GeometryResource *geometry, const RS2MeshVertexLayout &layout);
void RS2StageAuditGeometryDestroyed(const CRS2GeometryResource *geometry);
void RS2StageAuditDrawImmediate(const RS2MeshVertexLayout &layout);
void RS2StageAuditDrawGeometry(const CRS2GeometryResource *geometry, bool indexed);

//	Called before renderer shutdown.  Idempotent.
void RS2StageAuditDump();

#endif	//	RS2STAGEAUDIT_H_INCLUDED
