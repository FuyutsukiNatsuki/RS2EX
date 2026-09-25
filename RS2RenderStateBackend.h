//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23, 2026-09-24, 2026-09-25, 2026-09-26.
//
//	The Direct3D 8 implementations behind the RenderState boundary.
//
//	Internal to the renderer.  Game code includes RS2RenderState.h and never this:
//	the whole point of the boundary is that it does not know which of these
//	exists.
//
//	The public names live in RS2RenderState.cpp, which picks an implementation from
//	the active backend.  Before v0.1.0 the public names were compiled straight
//	out of the Direct3D 8 file, which meant a Direct3D 12 renderer would still
//	have reached sv3.pDev through every one of them.

#ifndef RS2RENDERSTATEBACKEND_H_INCLUDED
#define RS2RENDERSTATEBACKEND_H_INCLUDED

#include "RS2RenderState.h"


//	-lightingaudit only: log the lighting defaults Direct3D 8 supplies itself.

//	-stageaudit only: log the stage 0 / 1 texture-stage state the device holds.
//	-shadowaudit: the stencil / depth / cull / shade / fog state the device
//	holds, and whether the per-pass clear includes stencil.

#endif	//	RS2RENDERSTATEBACKEND_H_INCLUDED
