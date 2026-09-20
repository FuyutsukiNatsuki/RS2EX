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

	mat.Diffuse.r = material.diffuse.r;
	mat.Diffuse.g = material.diffuse.g;
	mat.Diffuse.b = material.diffuse.b;
	mat.Diffuse.a = material.diffuse.a;

	mat.Ambient.r = material.ambient.r;
	mat.Ambient.g = material.ambient.g;
	mat.Ambient.b = material.ambient.b;
	mat.Ambient.a = material.ambient.a;

	mat.Specular.r = material.specular.r;
	mat.Specular.g = material.specular.g;
	mat.Specular.b = material.specular.b;
	mat.Specular.a = material.specular.a;

	mat.Emissive.r = material.emissive.r;
	mat.Emissive.g = material.emissive.g;
	mat.Emissive.b = material.emissive.b;
	mat.Emissive.a = material.emissive.a;

	mat.Power = material.power;

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
