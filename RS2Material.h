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
//	This is the same five fields with the same meanings, owned by RailSim.
//
//	Two deliberate decisions:
//
//	Both types are plain aggregates, with no constructors.  The selection
//	material tables, the shadow material and the AncientNight light material are
//	all brace-initialised, CMesh copies its material table with memcpy(), and
//	CMaterialChanger zeroes its member with ZeroMemory().  A constructor would
//	break the first and quietly change the meaning of the other two.
//
//	The field names keep their historical spelling.  Renaming Diffuse to diffuse
//	would touch every material expression in the tree without changing anything,
//	and these are the names the customizer scripts and the content documentation
//	use.  What v0.0.7 removes is the Direct3D type, not the vocabulary.
//
//	Nothing here validates or normalises: customizers store out-of-range values
//	as "not specified" sentinels, and that is part of the content contract.

#ifndef RS2MATERIAL_H_INCLUDED
#define RS2MATERIAL_H_INCLUDED

/*
 *	An RGBA colour.
 *
 *	Not clamped.  See the sentinel note above.
 */
struct RS2Color4
{
	float r, g, b, a;
};

inline RS2Color4 RS2MakeColor4(float r, float g, float b, float a){
	RS2Color4 c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

/*
 *	A fixed-function material.
 *
 *	Field-for-field what the renderer has always consumed.  Translating this into
 *	whatever a backend needs is the backend's job - see RS2MaterialBinding.h.
 */
struct RS2Material
{
	RS2Color4 Diffuse;
	RS2Color4 Ambient;
	RS2Color4 Specular;
	RS2Color4 Emissive;
	float Power;
};

#endif	//	RS2MATERIAL_H_INCLUDED
