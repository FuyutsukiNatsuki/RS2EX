//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	Text: the public boundary, routed to the active backend.
//
//	Each function here is the name game code calls.  It chooses an
//	implementation and nothing else - no logic, no state, no reordering - so
//	that the Direct3D 8 path is exactly what it was before the routing existed.
//
//	The Direct3D 12 side is not implemented in v0.1.0.  It says so, once per
//	call site, and returns safely.  What it must never do is fall through to
//	the Direct3D 8 implementation: that would reach a device this backend does
//	not own, and it is the failure the whole release is built to prevent.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2TextBackend.h"
#include "RS2D3D12Unsupported.h"

void RS2CreateTextFont(int size, RS2PackedColor color, bool bold){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_CreateTextFont(size, color, bold);
	else RS2D3D12Unsupported("RS2CreateTextFont");
}

void RS2DestroyTextFont(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DestroyTextFont();
	else RS2D3D12Unsupported("RS2DestroyTextFont");
}

void RS2DrawText(int x, int y, RS2PackedColor color, const char *text){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DrawText(x, y, color, text);
	else RS2D3D12Unsupported("RS2DrawText");
}

int RS2GetTextHeight(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		return RS2D3D8_GetTextHeight();

	RS2D3D12Unsupported("RS2GetTextHeight");
	return 0;
}
