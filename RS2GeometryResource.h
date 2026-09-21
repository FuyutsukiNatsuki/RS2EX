//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	Who owns a geometry resource.
//
//	Until v0.1.1 CRS2GeometryResource was defined inside the Direct3D 8 draw
//	implementation and held Direct3D 8 buffers directly.  That was fine while
//	there was one backend.  With two, a resource has to know which backend
//	built it: game code holds these across frames, and handing one to the wrong
//	backend would be an interesting kind of crash to debug.
//
//	So the resource itself is neutral - counts, sizes, and which backend owns
//	the contents - and the contents are an opaque pointer the owning backend
//	understands and nothing else touches.  No Direct3D type appears here, and
//	RS2Draw.h still only forward-declares the class, so game code sees exactly
//	what it saw before.
//
//	Internal to the renderer.  Game code includes RS2Draw.h and never this.

#ifndef RS2GEOMETRYRESOURCE_H_INCLUDED
#define RS2GEOMETRYRESOURCE_H_INCLUDED

#include "RS2Renderer.h"
#include "RS2MeshData.h"

class CRS2GeometryResource
{
public:
	//	Which backend built payload, and therefore the only one that may read
	//	it.  Fixed at creation.
	RS2RendererBackendType backend;

	//	Backend-private.  Neutral code passes it along and never looks inside.
	void *payload;

	unsigned int stride;
	unsigned int vertexCount;
	unsigned int indexCount;

	//	Counted once the resource is fully built, so a half-built one that was
	//	thrown away never appears in the totals.
	bool counted;

	CRS2GeometryResource();
};

/*
 *	Allocate the neutral half.  The caller fills payload in.
 *
 *	backend		: the backend that will own the contents
 *	stride		: vertex size
 *	vertexCount	: vertices
 *	indexCount	: indices, or 0
 */
CRS2GeometryResource *RS2GeometryAllocate(
	RS2RendererBackendType backend,
	unsigned int stride,
	unsigned int vertexCount,
	unsigned int indexCount);

/*
 *	Add a finished resource to the live totals.
 */
void RS2GeometryCount(CRS2GeometryResource *geometry);

/*
 *	Remove it from the totals and free the neutral half.  The payload must
 *	already have been released by its backend.
 */
void RS2GeometryFree(CRS2GeometryResource *geometry);

/*
 *	Whether this resource may be used right now.
 *
 *	what	: the public function asking, for the report
 *
 *	False for a null resource, and false for one built by a backend that is not
 *	the active one - which is reported rather than ignored.  Silently doing
 *	nothing would look exactly like geometry that had not been drawn yet.
 */
bool RS2GeometryUsable(const CRS2GeometryResource *geometry, const char *what);

//	The totals the diagnostics read.  Neutral, so they answer for whichever
//	backend is running rather than only for Direct3D 8.
unsigned int RS2GeometryLiveCount();
unsigned int RS2GeometryTotalVertexBytes();
unsigned int RS2GeometryTotalIndexBytes();

#endif	//	RS2GEOMETRYRESOURCE_H_INCLUDED
