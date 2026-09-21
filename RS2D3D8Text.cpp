//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	The Direct3D 8 side of live text.  Provenance: the font description, the
//	DrawText flags and the rectangle are from lib/font.cpp (Copyright (c) 2002
//	Midikyou) - moved, not redesigned.
//
//	Begin()/End() batching is not exposed.  2.15 declared BeginFont and
//	EndFont and never called them, so wrapping them would be inventing an API
//	rather than preserving one.

#include "stdafx.h"
#include "RS2Text.h"

static LPD3DXFONT s_Font = 0;
static int s_Size = 0;
static RS2PackedColor s_Color = 0xffffffff;

void RS2CreateTextFont(int size, RS2PackedColor color, bool bold){
	RS2DestroyTextFont();

	LOGFONT logFont = {
		size, 0, 0, 0,
		bold ? FW_BOLD : FW_NORMAL,
		FALSE, FALSE, FALSE,
		SHIFTJIS_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY,
		DEFAULT_PITCH|FF_DONTCARE,
		"‚l‚r ‚oƒSƒVƒbƒN"
	};

	D3DXCreateFontIndirect(sv3.pDev, &logFont, &s_Font);
	s_Size = size;
	s_Color = color;
}

void RS2DestroyTextFont(){
	RELEASE(s_Font);
	s_Size = 0;
}

int RS2GetTextHeight(){
	return s_Size;
}

/*
 *	Draw one line.
 *
 *	The rectangle is the whole back buffer from (x, y), which is what 2.15
 *	passed: the text is never clipped to a narrower box, and the edit box
 *	relies on being able to draw past its own width while composing.
 */
void RS2DrawText(int x, int y, RS2PackedColor color, const char *text){
	if(!s_Font || !text) return;

	RECT rect = {x, y, sv3.width, sv3.height};

	s_Color = color;
	s_Font->DrawTextA(
		text, -1, &rect, DT_LEFT|DT_EXPANDTABS|DT_NOPREFIX, (D3DCOLOR)s_Color);
}
