//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Submitting materials and sampled textures to the active renderer.
//
//	The game says "draw with this material" and "sample this texture"; how those
//	become D3DMATERIAL8 and IDirect3DTexture8 is the backend's business and
//	appears nowhere above this line.
//
//	This header deliberately does not include d3d8.h.

#ifndef RS2MATERIALBINDING_H_INCLUDED
#define RS2MATERIALBINDING_H_INCLUDED

#include "RS2Material.h"
#include "RS2TextureResource.h"

/*
 *	Set the material subsequent geometry is drawn with.
 */
void RS2SetMaterial(const RS2Material &material);

/*
 *	Bind a sampled texture to a stage.
 *
 *	stage		: 0 for the base texture, 1 for the secondary/environment texture
 *	texture	: an empty reference unbinds the stage
 *
 *	The stage is a plain integer on purpose - a caller should not need a
 *	Direct3D enum to say "the second texture".
 */
void RS2BindTexture(unsigned int stage, const RS2TextureRef &texture);

#endif	//	RS2MATERIALBINDING_H_INCLUDED
