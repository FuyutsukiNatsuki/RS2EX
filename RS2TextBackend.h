//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-25, 2026-09-26.
//
//	The Direct3D 8 and (v0.1.6) Direct3D 12 implementations behind the Text
//	boundary.
//
//	Internal to the renderer.  Game code includes RS2Text.h and never this:
//	the whole point of the boundary is that it does not know which of these
//	exists.
//
//	The public names live in RS2Text.cpp, which picks an implementation from
//	the active backend.  Before v0.1.0 the public names were compiled straight
//	out of the Direct3D 8 file, which meant a Direct3D 12 renderer would still
//	have reached sv3.pDev through every one of them.

#ifndef RS2TEXTBACKEND_H_INCLUDED
#define RS2TEXTBACKEND_H_INCLUDED

#include "RS2Text.h"


//	GDI into a mutable texture, drawn as a screen-space quad (RS2D3D12Text.cpp).
void RS2D3D12_CreateTextFont(int size, RS2PackedColor color, bool bold);
void RS2D3D12_DestroyTextFont();
void RS2D3D12_DrawText(int x, int y, RS2PackedColor color, const char *text);
int RS2D3D12_GetTextHeight();

#endif	//	RS2TEXTBACKEND_H_INCLUDED
