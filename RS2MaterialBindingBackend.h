//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	The Direct3D 8 implementations behind the MaterialBinding boundary.
//
//	Internal to the renderer.  Game code includes RS2MaterialBinding.h and never this:
//	the whole point of the boundary is that it does not know which of these
//	exists.
//
//	The public names live in RS2MaterialBinding.cpp, which picks an implementation from
//	the active backend.  Before v0.1.0 the public names were compiled straight
//	out of the Direct3D 8 file, which meant a Direct3D 12 renderer would still
//	have reached sv3.pDev through every one of them.

#ifndef RS2MATERIALBINDINGBACKEND_H_INCLUDED
#define RS2MATERIALBINDINGBACKEND_H_INCLUDED

#include "RS2MaterialBinding.h"
#include "RS2Material.h"
#include "RS2TextureResource.h"

void RS2D3D8_SetMaterial(const RS2Material &material);
void RS2D3D8_BindTexture(unsigned int stage, const RS2TextureRef &texture);

#endif	//	RS2MATERIALBINDINGBACKEND_H_INCLUDED
