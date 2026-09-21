//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	The packed colour RS2 passes across its own boundaries.
//
//	It lived in RS2RenderState.h while ambient light was its only user.  Draw
//	submission needs the same type for vertex diffuse and for 2D helpers, and
//	render state should not be the header everything includes to get a colour.
//
//	Deliberately distinct from RS2Color4, which is four floats.  Material and
//	light colours are float; vertex and UI colours are packed.  Casting one to
//	the other is wrong in both directions, and giving them one name would make
//	that mistake invisible.

#ifndef RS2COLOR_H_INCLUDED
#define RS2COLOR_H_INCLUDED

/*
 *	0xAARRGGBB.
 *
 *	Same width and same layout as the Direct3D packed colour it replaces, so
 *	existing values and literals carry over unchanged.  What changes is that a
 *	caller no longer has to name a Direct3D type to describe a colour.
 */
typedef unsigned long RS2PackedColor;

inline RS2PackedColor RS2MakePackedColor(
	unsigned int a, unsigned int r, unsigned int g, unsigned int b
){
	return ((RS2PackedColor)(a&0xff)<<24) | ((RS2PackedColor)(r&0xff)<<16)
		| ((RS2PackedColor)(g&0xff)<<8) | (RS2PackedColor)(b&0xff);
}

#endif	//	RS2COLOR_H_INCLUDED
