//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-25, 2026-09-26.
//
//	Text: the public boundary, routed to the active backend.
//
//	Each function here is the name game code calls.  It chooses an
//	implementation and nothing else - no logic, no state, no reordering - so
//	that the Direct3D 8 path is exactly what it was before the routing existed.
//
//	The Direct3D 12 side (v0.1.6) is RS2D3D12Text.cpp.  A backend that is
//	neither says so, once per call site, and returns safely: what it must never
//	do is fall through to the Direct3D 8 implementation, which would reach a
//	device that backend does not own.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2TextBackend.h"
#include "RS2D3D12Unsupported.h"
#include "RS2MutableAudit.h"

void RS2CreateTextFont(int size, RS2PackedColor color, bool bold){
	RS2MutableAuditFont(size, bold);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12)
		RS2D3D12_CreateTextFont(size, color, bold);
	else RS2D3D12Unsupported("RS2CreateTextFont");
}

void RS2DestroyTextFont(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12)
		RS2D3D12_DestroyTextFont();
	else RS2D3D12Unsupported("RS2DestroyTextFont");
}

void RS2DrawText(int x, int y, RS2PackedColor color, const char *text){
	RS2MutableAuditText(x, y, text);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12)
		RS2D3D12_DrawText(x, y, color, text);
	else RS2D3D12Unsupported("RS2DrawText");
}

int RS2GetTextHeight(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12){
		const int height = RS2D3D12_GetTextHeight();

		RS2MutableAuditTextHeight(height);
		return height;
	}

	RS2D3D12Unsupported("RS2GetTextHeight");
	return 0;
}
