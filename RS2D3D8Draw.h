//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	Internal Direct3D 8 draw helpers.
//
//	Not part of the public draw boundary.  This exists so the mesh resource and
//	the draw implementation share one layout-to-FVF conversion instead of two
//	that can drift apart.

#ifndef RS2D3D8DRAW_H_INCLUDED
#define RS2D3D8DRAW_H_INCLUDED

struct RS2MeshVertexLayout;

/*
 *	Translate a layout into an FVF and verify the stride it implies.
 *
 *	Returns false when the layout cannot be expressed, or when the derived
 *	stride disagrees with the one the layout claims - which would mean the
 *	vertex bytes get reinterpreted rather than read.
 */
bool RS2D3D8_LayoutToFVF(const RS2MeshVertexLayout &layout, unsigned long *outFvf);

#endif	//	RS2D3D8DRAW_H_INCLUDED
