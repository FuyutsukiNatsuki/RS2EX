//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	RS2-owned material values.
//
//	RailSim II 2.15 used D3DMATERIAL8 directly, aliased as MAT8, everywhere a
//	material was stored or edited - mesh material tables, customizers, selection
//	highlights, the shadow material.  That made a Direct3D 8 struct part of the
//	game's own vocabulary, and a different renderer could not be introduced
//	without touching all of it.
//
//	This is the same five fields with the same meanings, owned by RailSim.  It is
//	deliberately a plain value: customizers copy it, edit single components and
//	assign it back, and some of them use out-of-range values as "not specified"
//	sentinels.  Nothing here validates or normalises, because that behaviour is
//	part of the content contract.

#ifndef RS2MATERIAL_H_INCLUDED
#define RS2MATERIAL_H_INCLUDED

/*
 *	An RGBA colour.
 *
 *	Not clamped.  Customizer scripts rely on being able to store negative values
 *	to mean "this field was not given".
 */
struct RS2Color4
{
	float r, g, b, a;

	RS2Color4() : r(0.0f), g(0.0f), b(0.0f), a(0.0f){}
	RS2Color4(float red, float green, float blue, float alpha)
		: r(red), g(green), b(blue), a(alpha){}
};

/*
 *	A fixed-function material.
 *
 *	Field-for-field what the renderer has always consumed.  Translating this into
 *	whatever a backend needs is the backend's job - see RS2MaterialBinding.h.
 */
struct RS2Material
{
	RS2Color4 diffuse;
	RS2Color4 ambient;
	RS2Color4 specular;
	RS2Color4 emissive;
	float power;

	RS2Material() : power(0.0f){}
};

#endif	//	RS2MATERIAL_H_INCLUDED
