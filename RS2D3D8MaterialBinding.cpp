//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	The Direct3D 8 side of material and sampled-texture submission.  This is the
//	only place a RailSim material becomes a D3DMATERIAL8, and the only place a
//	texture reference becomes an IDirect3DTexture8*.

#include "stdafx.h"
#include "RS2MaterialBinding.h"

/*
 *	Set the material for subsequent geometry.
 *
 *	The conversion is a field copy, deliberately explicit.  RS2Color4 and
 *	D3DCOLORVALUE happen to have the same layout today, and reinterpreting one as
 *	the other would silently break the moment either changes.
 */
void RS2SetMaterial(const RS2Material &material){
	D3DMATERIAL8 mat;

	mat.Diffuse.r = material.Diffuse.r;
	mat.Diffuse.g = material.Diffuse.g;
	mat.Diffuse.b = material.Diffuse.b;
	mat.Diffuse.a = material.Diffuse.a;

	mat.Ambient.r = material.Ambient.r;
	mat.Ambient.g = material.Ambient.g;
	mat.Ambient.b = material.Ambient.b;
	mat.Ambient.a = material.Ambient.a;

	mat.Specular.r = material.Specular.r;
	mat.Specular.g = material.Specular.g;
	mat.Specular.b = material.Specular.b;
	mat.Specular.a = material.Specular.a;

	mat.Emissive.r = material.Emissive.r;
	mat.Emissive.g = material.Emissive.g;
	mat.Emissive.b = material.Emissive.b;
	mat.Emissive.a = material.Emissive.a;

	mat.Power = material.Power;

	sv3.pDev->SetMaterial(&mat);
}

/*
 *	Bind a sampled texture to a stage.
 *
 *	stage		: 0 base, 1 secondary/environment
 *	texture	: empty reference unbinds
 *
 *	The empty case matters: v0.0.8 moves the remaining native unbind calls here,
 *	and the API should not need changing when it does.
 */
void RS2BindTexture(unsigned int stage, const RS2TextureRef &texture){
	const CRS2TextureResource *resource = texture.GetResource();
	LPTEX8 native = resource ? (LPTEX8)resource->GetNativeForBackend() : NULL;

	sv3.pDev->SetTexture(stage, native);
}
