//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	Developer-only material / lighting contract instrumentation.
//
//	v0.1.3 moves material and fixed-function lighting to Direct3D 12.  Before a
//	shader can reproduce what Direct3D 8 did, somebody has to write down what
//	RailSim actually asks for: which materials reach the device, whether
//	lighting is on while geometry without normals is drawn, whether any lit
//	geometry is scaled unevenly.  Reasoning about that from the source would
//	be a guess, and the plan forbids guessing it.
//
//	The hooks sit in the renderer-neutral public functions, so they see the
//	same calls whichever backend is running and add nothing to either backend.
//	Without -lightingaudit every hook is one cached flag test.  Nothing here
//	changes a value on its way to the renderer.

#ifndef RS2LIGHTINGAUDIT_H_INCLUDED
#define RS2LIGHTINGAUDIT_H_INCLUDED

#include "RS2Material.h"
#include "RS2RenderState.h"
#include "RS2Lighting.h"
#include "RS2MeshData.h"
#include "RS2Draw.h"

class CRS2GeometryResource;

bool RS2LightingAuditEnabled();

void RS2LightingAuditMaterial(const RS2Material &material);
void RS2LightingAuditLighting(bool enable);
void RS2LightingAuditAmbient(RS2PackedColor color);
void RS2LightingAuditSpecular(bool enable);
void RS2LightingAuditDiffuseSource(RS2ColorSource source);
void RS2LightingAuditAmbientSource(RS2ColorSource source);
void RS2LightingAuditNormalize(bool enable);

//	Called with the value the engine keeps, after normalisation.
void RS2LightingAuditDirectionalLight(const RS2DirectionalLight &light);

void RS2LightingAuditWorld(const float *matrix);
void RS2LightingAuditTexture(unsigned int stage, bool bound);

//	Buffered geometry does not carry its layout in the neutral resource, so
//	the audit remembers it from creation until destruction.
void RS2LightingAuditGeometryCreated(
	const CRS2GeometryResource *geometry, const RS2MeshVertexLayout &layout);
void RS2LightingAuditGeometryDestroyed(const CRS2GeometryResource *geometry);

void RS2LightingAuditDrawImmediate(
	const RS2MeshVertexLayout &layout, RS2PrimitiveType primitive, unsigned int vertexCount);
void RS2LightingAuditDrawGeometry(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int count, bool indexed);

//	Called before renderer shutdown, while Debug() still works.  Idempotent.
void RS2LightingAuditDump();

#endif	//	RS2LIGHTINGAUDIT_H_INCLUDED
