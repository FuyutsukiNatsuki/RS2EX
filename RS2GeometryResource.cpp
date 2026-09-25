//	Modified for RS2EX on 2026-09-26.
//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	See RS2GeometryResource.h.

#include "stdafx.h"
#include "RS2GeometryResource.h"

//	Live totals.  A resource enters them once it is fully built and leaves them
//	when it is freed, so a creation that failed half way never shows up.
static unsigned int s_LiveGeometry = 0;
static unsigned int s_VertexBytes = 0;
static unsigned int s_IndexBytes = 0;

//	Both backends store indices sixteen bits wide: the Direct3D 8 one because
//	that is what CMesh always did, and the Direct3D 12 one because v0.1.1 keeps
//	that contract deliberately.  If one ever stops, this stops being a neutral
//	calculation and the backend has to report its own size.
#define RS2_GEOMETRY_INDEX_BYTES	2

CRS2GeometryResource::CRS2GeometryResource()
	: backend(RS2_RENDERER_NONE),
	  payload(0),
	  stride(0),
	  vertexCount(0),
	  indexCount(0),
	  counted(false)
{
}

CRS2GeometryResource *RS2GeometryAllocate(
	RS2RendererBackendType backend,	//	backend that will own the contents
	unsigned int stride,		//	vertex size
	unsigned int vertexCount,	//	vertices
	unsigned int indexCount		//	indices, or 0
){
	CRS2GeometryResource *geometry = new CRS2GeometryResource;

	geometry->backend = backend;
	geometry->stride = stride;
	geometry->vertexCount = vertexCount;
	geometry->indexCount = indexCount;

	return geometry;
}

void RS2GeometryCount(
	CRS2GeometryResource *geometry	//	finished resource
){
	if(!geometry || geometry->counted) return;

	geometry->counted = true;
	s_LiveGeometry++;
	s_VertexBytes += geometry->stride*geometry->vertexCount;
	s_IndexBytes += geometry->indexCount*RS2_GEOMETRY_INDEX_BYTES;
}

void RS2GeometryFree(
	CRS2GeometryResource *geometry	//	resource whose payload is already gone
){
	if(!geometry) return;

	if(geometry->counted){
		if(s_LiveGeometry) s_LiveGeometry--;
		s_VertexBytes -= geometry->stride*geometry->vertexCount;
		s_IndexBytes -= geometry->indexCount*RS2_GEOMETRY_INDEX_BYTES;
	}
	delete geometry;
}

bool RS2GeometryUsable(
	const CRS2GeometryResource *geometry,	//	resource being used
	const char *what			//	public function asking
){
	if(!geometry) return false;
	if(geometry->backend==GetRS2Renderer().GetBackendType()) return true;

	//	Reported once per call site.  A resource from the other backend is a
	//	programming error, not a condition to route around, and doing nothing
	//	quietly would look exactly like geometry that had not been drawn yet.
	static const char *seen[16];
	static unsigned int count = 0;
	unsigned int i;

	for(i = 0; i<count; i++) if(seen[i]==what) return false;
	if(count<16) seen[count++] = what;

	Debug("[RS2EX Draw] %s was given geometry built by the other backend\n", what);
	return false;
}

unsigned int RS2GeometryLiveCount(){ return s_LiveGeometry; }
unsigned int RS2GeometryTotalVertexBytes(){ return s_VertexBytes; }
unsigned int RS2GeometryTotalIndexBytes(){ return s_IndexBytes; }
