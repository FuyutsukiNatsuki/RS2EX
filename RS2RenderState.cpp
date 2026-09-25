//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23, 2026-09-24, 2026-09-25, 2026-09-26.
//
//	RenderState: the public boundary, routed to the active backend.
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
#include "RS2RenderStateBackend.h"
#include "RS2D3D12Unsupported.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12TextureBackend.h"
#include "RS2LightingAudit.h"
#include "RS2StageAudit.h"
#include "RS2ShadowAudit.h"
#include "RS2MutableAudit.h"

void RS2SetDepthTest(bool enable){
	RS2ShadowAuditSet(RS2_SA_DEPTH_TEST, enable ? 1 : 0);
	RS2D3D12_SetDepthTest(enable);
}

void RS2SetDepthWrite(bool enable){
	RS2ShadowAuditSet(RS2_SA_DEPTH_WRITE, enable ? 1 : 0);
	RS2D3D12_SetDepthWrite(enable);
}

void RS2SetDepthFunc(RS2CompareFunc func){
	RS2ShadowAuditSet(RS2_SA_DEPTH_FUNC, (unsigned int)func);
	RS2D3D12_SetDepthFunc(func);
}

void RS2ClearDepth(){
	RS2ShadowAuditSet(RS2_SA_CLEAR_DEPTH, 0);
	RS2D3D12Unsupported("RS2ClearDepth");
}

void RS2SetBlend(RS2BlendMode mode){
	RS2MutableAuditBlend(mode);
	RS2ShadowAuditSet(RS2_SA_BLEND, (unsigned int)mode);
	RS2StageAuditBlend(mode);
	RS2D3D12_SetBlend(mode);
}

void RS2SetAlphaTest(bool enable){
	RS2MutableAuditAlphaTest(enable);
	RS2StageAuditAlphaTest(enable);
	RS2D3D12_SetAlphaTest(enable);
}

void RS2SetAlphaRef(unsigned int ref){
	RS2D3D12_SetAlphaRef(ref);
}

void RS2SetAlphaFunc(RS2CompareFunc func){
	RS2D3D12_SetAlphaFunc(func);
}

void RS2SetCullMode(RS2CullMode mode){
	RS2ShadowAuditSet(RS2_SA_CULL, (unsigned int)mode);
	RS2D3D12_SetCullMode(mode);
}

void RS2SetShadeMode(RS2ShadeMode mode){
	RS2ShadowAuditSet(RS2_SA_SHADE, (unsigned int)mode);
	RS2D3D12_SetShadeMode(mode);
}

void RS2SetNormalizeNormals(bool enable){
	RS2LightingAuditNormalize(enable);
	RS2D3D12Unsupported("RS2SetNormalizeNormals");
}

void RS2SetStencilTest(bool enable){
	RS2ShadowAuditSet(RS2_SA_STENCIL_TEST, enable ? 1 : 0);
	RS2D3D12_SetStencilTest(enable);
}

void RS2SetStencilFunc(RS2CompareFunc func){
	RS2ShadowAuditSet(RS2_SA_STENCIL_FUNC, (unsigned int)func);
	RS2D3D12_SetStencilFunc(func);
}

void RS2SetStencilRef(unsigned int ref){
	RS2ShadowAuditSet(RS2_SA_STENCIL_REF, ref);
	RS2D3D12_SetStencilRef(ref);
}

void RS2SetStencilReadMask(unsigned int mask){
	RS2ShadowAuditSet(RS2_SA_STENCIL_READ_MASK, mask);
	RS2D3D12_SetStencilReadMask(mask);
}

void RS2SetStencilWriteMask(unsigned int mask){
	RS2ShadowAuditSet(RS2_SA_STENCIL_WRITE_MASK, mask);
	RS2D3D12_SetStencilWriteMask(mask);
}

void RS2SetStencilFailOp(RS2StencilOp op){
	RS2ShadowAuditSet(RS2_SA_STENCIL_FAIL, (unsigned int)op);
	RS2D3D12_SetStencilFailOp(op);
}

void RS2SetStencilDepthFailOp(RS2StencilOp op){
	RS2ShadowAuditSet(RS2_SA_STENCIL_DEPTH_FAIL, (unsigned int)op);
	RS2D3D12_SetStencilDepthFailOp(op);
}

void RS2SetStencilPassOp(RS2StencilOp op){
	RS2ShadowAuditSet(RS2_SA_STENCIL_PASS, (unsigned int)op);
	RS2D3D12_SetStencilPassOp(op);
}

void RS2SetLighting(bool enable){
	RS2MutableAuditLighting(enable);
	RS2LightingAuditLighting(enable);
	RS2StageAuditLighting(enable);
	RS2D3D12_SetLighting(enable);
}

void RS2SetAmbientLight(RS2PackedColor color){
	RS2LightingAuditAmbient(color);
	RS2D3D12_SetAmbientLight(color);
}

void RS2SetSpecular(bool enable){
	RS2LightingAuditSpecular(enable);
	RS2D3D12_SetSpecular(enable);
}

void RS2SetDiffuseColorSource(RS2ColorSource source){
	RS2LightingAuditDiffuseSource(source);
	RS2D3D12_SetDiffuseColorSource(source);
}

void RS2SetAmbientColorSource(RS2ColorSource source){
	RS2LightingAuditAmbientSource(source);
	RS2D3D12_SetAmbientColorSource(source);
}

void RS2DisableFog(){
	RS2ShadowAuditSet(RS2_SA_FOG_DISABLE, 0);
	RS2D3D12_DisableFog();
}

void RS2SetTextureFilter(unsigned int stage, RS2TextureFilter filter){
	RS2MutableAuditFilter(stage, filter);
	RS2StageAuditFilter(stage, filter);
	RS2D3D12_SetTextureFilter(stage, filter);
}

void RS2SetBaseTextureCombine(){
	RS2ShadowAuditSet(RS2_SA_BASE_COMBINE, 0);
	// D3D12's Stage 0 shader always multiplies texture by diffuse colour.
}

void RS2SetSecondaryTextureCombine(unsigned int stage, bool enable){
	RS2StageAuditCombine(stage, enable);
	RS2D3D12_SetSecondaryTextureCombine(stage, enable);
}

void RS2SetEnvironmentMapping(unsigned int stage, bool enable){
	RS2StageAuditEnvironment(stage, enable);
	RS2D3D12_SetEnvironmentMapping(stage, enable);
}

void RS2SetUVTransform(unsigned int stage, bool enable){
	RS2StageAuditUVTransform(stage, enable);
	RS2D3D12_SetUVTransform(stage, enable);
}

void RS2SetUVMatrix(unsigned int stage, const float *matrix){
	RS2StageAuditUVMatrix(stage, matrix);
	RS2D3D12_SetUVMatrix(stage, matrix);
}

void RS2ApplyInitialRenderState(){
	RS2D3D12_ApplyInitialRenderState();
}
