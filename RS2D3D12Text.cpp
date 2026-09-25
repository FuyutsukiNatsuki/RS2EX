//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//	Modified for RS2EX on 2026-09-26.
//
//	The Direct3D 12 side of live text (v0.1.6).
//
//	Direct3D 8 drew this through ID3DXFont, which exists only there.  The
//	replacement is deliberately not a new text system: GDI rasterises the line
//	with the font Direct3D 8 was given - the same LOGFONT, face, charset,
//	quality and height as v0.1.6's RS2D3D8Text.cpp - into a DIB, the coverage becomes
//	the alpha of a white A4R4G4B4 line in a mutable texture, and the line is
//	drawn as a screen-space quad in the requested colour.  The mutable texture
//	is the string texture's; the ordering it guarantees is what lets the edit
//	box draw three strings through one line in one frame.
//
//	The rectangle and flags are Direct3D 8's: from (x, y) to the bottom right
//	of the back buffer, DT_LEFT | DT_EXPANDTABS | DT_NOPREFIX.  The caret and
//	the composition underline come from RS2GetTextHeight(), the creation
//	height, on both backends.  Glyph edges are GDI's, not D3DX's, and are not
//	expected to match pixel for pixel.

#include "stdafx.h"
#include "RS2TextBackend.h"
#include "RS2Text.h"
#include "RS2Draw.h"
#include "RS2RenderState.h"
#include "RS2MaterialBinding.h"
#include "RS2TextureResource.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Texture.h"

//	One line.  Wide enough for a line across any back buffer the program
//	opens; tall enough for both heights it asks for (12, and 16 after a
//	reset).
static const int RS2D3D12_TEXT_WIDTH = 2048;
static const int RS2D3D12_TEXT_HEIGHT = 32;

static HFONT s_Font = 0;
static int s_Size = 0;
static HDC s_DC = 0;
static HBITMAP s_Bitmap = 0, s_OldBitmap = 0;
static HFONT s_OldFont = 0;
static unsigned long *s_Pixels = 0;
static CRS2TextureResource *s_Texture = 0;
static int s_TextureWidth = 0, s_TextureHeight = 0;
static int s_UsedWidth = 0, s_UsedHeight = 0;	//	non-zero texels left by the last line

void RS2D3D12_DestroyTextFont(){
	if(s_Texture){
		RS2DestroyTexture(s_Texture);
		s_Texture = 0;
	}
	if(s_DC){
		if(s_OldFont) SelectObject(s_DC, s_OldFont);
		if(s_OldBitmap) SelectObject(s_DC, s_OldBitmap);
		DeleteDC(s_DC);
	}
	if(s_Bitmap) DeleteObject(s_Bitmap);
	if(s_Font) DeleteObject(s_Font);
	s_DC = 0;
	s_Bitmap = s_OldBitmap = 0;
	s_Font = s_OldFont = 0;
	s_Pixels = 0;
	s_TextureWidth = s_TextureHeight = 0;
	s_UsedWidth = s_UsedHeight = 0;
	s_Size = 0;
}

void RS2D3D12_CreateTextFont(int size, RS2PackedColor, bool bold){
	RS2D3D12_DestroyTextFont();

	//	RS2D3D8_CreateTextFont's description, field for field.
	LOGFONT logFont = {
		size, 0, 0, 0,
		bold ? FW_BOLD : FW_NORMAL,
		FALSE, FALSE, FALSE,
		SHIFTJIS_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY,
		DEFAULT_PITCH|FF_DONTCARE,
		"\x82\x6c\x82\x72 \x82\x6f\x83\x53\x83\x56\x83\x62\x83\x4e"	//	MS P Gothic
	};

	BITMAPINFO bmi;

	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = RS2D3D12_TEXT_WIDTH;
	bmi.bmiHeader.biHeight = -RS2D3D12_TEXT_HEIGHT;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	s_Font = CreateFontIndirect(&logFont);
	s_DC = CreateCompatibleDC(NULL);
	if(s_DC) s_Bitmap = CreateDIBSection(s_DC, &bmi, DIB_RGB_COLORS, (void **)&s_Pixels, NULL, 0);
	s_Texture = RS2CreateMutableTexture(RS2D3D12_TEXT_WIDTH, RS2D3D12_TEXT_HEIGHT);
	if(!s_Font || !s_DC || !s_Bitmap || !s_Pixels || !s_Texture){
		Debug("[RS2EX D3D12 Text] font %d could not be created\n", size);
		RS2D3D12_DestroyTextFont();
		return;
	}

	s_Texture->GetRef().GetSize(&s_TextureWidth, &s_TextureHeight);
	s_OldBitmap = (HBITMAP)SelectObject(s_DC, s_Bitmap);
	s_OldFont = (HFONT)SelectObject(s_DC, s_Font);
	SetMapMode(s_DC, MM_TEXT);
	SetBkMode(s_DC, TRANSPARENT);
	SetTextColor(s_DC, 0x00ffffff);
	s_Size = size;
}

int RS2D3D12_GetTextHeight(){
	return s_Size;
}

void RS2D3D12_DrawText(int x, int y, RS2PackedColor color, const char *text){
	if(!s_Font || !s_Texture || !text) return;

	//	Direct3D 8's rectangle, clipped to the line.
	const int right = sv3.width, bottom = sv3.height;
	int w = right-x, h = bottom-y;

	if(w>s_TextureWidth) w = s_TextureWidth;
	if(h>s_TextureHeight) h = s_TextureHeight;
	if(w>RS2D3D12_TEXT_WIDTH) w = RS2D3D12_TEXT_WIDTH;
	if(h>RS2D3D12_TEXT_HEIGHT) h = RS2D3D12_TEXT_HEIGHT;
	if(w<=0 || h<=0 || !*text) return;

	//	Rasterise.  Only the part a line can reach is cleared.
	int row, column;
	const int clearWidth = (w>s_UsedWidth) ? w : s_UsedWidth;
	const int clearHeight = (h>s_UsedHeight) ? h : s_UsedHeight;
	RECT rect = {0, 0, w, h};

	for(row = 0; row<clearHeight; row++)
		memset(s_Pixels+row*RS2D3D12_TEXT_WIDTH, 0, clearWidth*4);
	DrawTextA(s_DC, text, -1, &rect, DT_LEFT|DT_EXPANDTABS|DT_NOPREFIX);
	GdiFlush();

	//	Coverage to a white A4R4G4B4 line.  The texels outside what this line
	//	and the last one touched are already zero.
	RS2TextureLock lock;

	if(!s_Texture->Lock(&lock)) return;

	int usedWidth = 0, usedHeight = 0;

	for(row = 0; row<clearHeight; row++){
		unsigned short *dst = (unsigned short *)((unsigned char *)lock.bits+row*lock.pitch);
		const unsigned long *src = s_Pixels+row*RS2D3D12_TEXT_WIDTH;

		for(column = 0; column<clearWidth; column++){
			const unsigned long p = src[column];
			unsigned int a = p&0xff;

			if(((p>>8)&0xff)>a) a = (p>>8)&0xff;
			if(((p>>16)&0xff)>a) a = (p>>16)&0xff;
			a >>= 4;
			dst[column] = a ? (unsigned short)((a<<12)|0x0fff) : 0;
			if(a){
				if(column+1>usedWidth) usedWidth = column+1;
				usedHeight = row+1;
			}
		}
	}
	s_Texture->Unlock();
	s_UsedWidth = usedWidth;
	s_UsedHeight = usedHeight;
	if(!usedWidth) return;

	//	A string-texture draw: the texel grid on the pixel grid, point
	//	sampled, straight alpha, over whatever is there.  The caller's state
	//	comes back afterwards; ID3DXFont does the same with a state block.
	const float u = (float)usedWidth/s_TextureWidth, v = (float)usedHeight/s_TextureHeight;
	const float x1 = x-0.5f, y1 = y-0.5f, x2 = x+usedWidth-0.5f, y2 = y+usedHeight-0.5f;
	VTX_TLX vertices[] = {
		x1, y1, 0.0f, 1.0f, color, 0.0f, 0.0f,
		x2, y1, 0.0f, 1.0f, color, u, 0.0f,
		x2, y2, 0.0f, 1.0f, color, u, v,
		x1, y2, 0.0f, 1.0f, color, 0.0f, v
	};

	RS2D3D12_PushOverlayState();
	RS2D3D12_PushTextureBinding();
	RS2BindTexture(0, s_Texture->GetRef());
	RS2BindTexture(1, RS2TextureRef());
	RS2SetTextureFilter(0, RS2_FILTER_POINT);
	RS2DrawImmediate(RS2LayoutTLX(), RS2_PRIMITIVE_TRIANGLE_FAN, vertices, 4);
	RS2D3D12_CountLiveTextDraw();
	RS2D3D12_PopTextureBinding();
	RS2D3D12_PopOverlayState();
}
