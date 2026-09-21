//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	Live text.
//
//	RailSim II 2.15 kept an ID3DXFont in a global, created it from the active
//	device, and let the edit box and the view controls call DrawTextA on it.
//	That is the last place normal UI code owned a Direct3D object.
//
//	This is the same font with the same job, described without naming one.
//	Only two things use it - the edit box while composing, and the view-control
//	messages - so the surface is deliberately small.
//
//	Static text does not come through here.  CStringTexture rasterises through
//	GDI into a texture and draws that, which is why this path is described as
//	the expensive one in the original comments and still is.
//
//	This header deliberately does not include d3d8.h or d3dx8.h.

#ifndef RS2TEXT_H_INCLUDED
#define RS2TEXT_H_INCLUDED

#include "RS2Color.h"

/*
 *	Create the UI font, replacing any existing one.
 *
 *	size		: cell height
 *	color	: default colour, overridden per draw
 *	bold		: the only weight distinction the program makes
 *
 *	The face and charset are fixed - MS Gothic, Shift-JIS - because the
 *	content is Japanese and the metrics the edit box does its caret arithmetic
 *	with depend on them.
 */
void RS2CreateTextFont(int size, RS2PackedColor color, bool bold);
void RS2DestroyTextFont();

/*
 *	Draw a line of text at a position, in screen pixels.
 *
 *	Left-aligned, tabs expanded, no prefix handling - the flags 2.15 used,
 *	kept because the edit box positions its caret against the result.
 */
void RS2DrawText(int x, int y, RS2PackedColor color, const char *text);
//	[RS2EX] TextF() is not carried over - it had no caller.

/*
 *	The cell height the font was created with.
 *
 *	The edit box advances the caret by half of it per character.  That is a
 *	fixed-pitch assumption inherited from 2.15, not a new one.
 */
int RS2GetTextHeight();

#endif	//	RS2TEXT_H_INCLUDED
