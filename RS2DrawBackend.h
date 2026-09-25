//	Modified for RS2EX on 2026-09-26.
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
#include "RS2GeometryResource.h"

//	Geometry.  The neutral side allocates and frees the resource itself and
//	counts it; these fill in and release the payload behind it, and are the
//	only code that knows what is in there.


#endif	//	RS2DRAWBACKEND_H_INCLUDED
