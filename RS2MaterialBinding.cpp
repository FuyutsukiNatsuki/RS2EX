//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23.
//
//	MaterialBinding: the public boundary, routed to the active backend.
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
#include "RS2MaterialBindingBackend.h"
#include "RS2D3D12Unsupported.h"
#include "RS2D3D12TextureBackend.h"
#include "RS2LightingAudit.h"
#include "RS2D3D12Draw.h"

void RS2SetMaterial(const RS2Material &material){
	RS2LightingAuditMaterial(material);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetMaterial(material);
	else RS2D3D12_SetMaterial(material);
}

void RS2BindTexture(unsigned int stage, const RS2TextureRef &texture){
	RS2LightingAuditTexture(stage, !texture.IsEmpty());
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_BindTexture(stage, texture);
	else RS2D3D12_BindTexture(stage, texture);
}
