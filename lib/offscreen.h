//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-26.

#include "..\RS2RenderResource.h"

/*
 *	[RS2EX] The offscreen target of the high-resolution capture.
 *
 *	It was a Direct3D 8 render-target texture read back with CopyRects.
 *	Direct3D 12 has no readback yet (screenshot / capture refresh is v0.4.0
 *	scope), and v0.2.0 removed Direct3D 8, so this is an empty shell: it
 *	creates nothing and every call is a no-op.  HidefCapture() checks
 *	SupportsReadback() first and never gets here.
 */
class COffScreen{
public:
	COffScreen(){}
	~COffScreen(){}

	BOOL Create(int, int){ return FALSE; }
	void Free(){}
	BOOL Begin(RS2PackedColor = 0xff000000){ return FALSE; }
	void End(){}
};
