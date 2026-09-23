//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23.
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

void RS2SetDepthTest(bool enable){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetDepthTest(enable);
	else RS2D3D12_SetDepthTest(enable);
}

void RS2SetDepthWrite(bool enable){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetDepthWrite(enable);
	else RS2D3D12_SetDepthWrite(enable);
}

void RS2SetDepthFunc(RS2CompareFunc func){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetDepthFunc(func);
	else RS2D3D12_SetDepthFunc(func);
}

void RS2ClearDepth(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_ClearDepth();
	else RS2D3D12Unsupported("RS2ClearDepth");
}

void RS2SetBlend(RS2BlendMode mode){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetBlend(mode);
	else RS2D3D12_SetBlend(mode);
}

void RS2SetAlphaTest(bool enable){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetAlphaTest(enable);
	else RS2D3D12_SetAlphaTest(enable);
}

void RS2SetAlphaRef(unsigned int ref){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetAlphaRef(ref);
	else RS2D3D12_SetAlphaRef(ref);
}

void RS2SetAlphaFunc(RS2CompareFunc func){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetAlphaFunc(func);
	else RS2D3D12_SetAlphaFunc(func);
}

void RS2SetCullMode(RS2CullMode mode){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetCullMode(mode);
	else RS2D3D12_SetCullMode(mode);
}

void RS2SetShadeMode(RS2ShadeMode mode){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetShadeMode(mode);
	else RS2D3D12Unsupported("RS2SetShadeMode");
}

void RS2SetNormalizeNormals(bool enable){
	RS2LightingAuditNormalize(enable);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetNormalizeNormals(enable);
	else RS2D3D12Unsupported("RS2SetNormalizeNormals");
}

void RS2SetStencilTest(bool enable){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilTest(enable);
	else RS2D3D12Unsupported("RS2SetStencilTest");
}

void RS2SetStencilFunc(RS2CompareFunc func){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilFunc(func);
	else RS2D3D12Unsupported("RS2SetStencilFunc");
}

void RS2SetStencilRef(unsigned int ref){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilRef(ref);
	else RS2D3D12Unsupported("RS2SetStencilRef");
}

void RS2SetStencilReadMask(unsigned int mask){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilReadMask(mask);
	else RS2D3D12Unsupported("RS2SetStencilReadMask");
}

void RS2SetStencilWriteMask(unsigned int mask){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilWriteMask(mask);
	else RS2D3D12Unsupported("RS2SetStencilWriteMask");
}

void RS2SetStencilFailOp(RS2StencilOp op){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilFailOp(op);
	else RS2D3D12Unsupported("RS2SetStencilFailOp");
}

void RS2SetStencilDepthFailOp(RS2StencilOp op){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilDepthFailOp(op);
	else RS2D3D12Unsupported("RS2SetStencilDepthFailOp");
}

void RS2SetStencilPassOp(RS2StencilOp op){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetStencilPassOp(op);
	else RS2D3D12Unsupported("RS2SetStencilPassOp");
}

void RS2SetLighting(bool enable){
	RS2LightingAuditLighting(enable);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetLighting(enable);
	else RS2D3D12_SetLighting(enable);
}

void RS2SetAmbientLight(RS2PackedColor color){
	RS2LightingAuditAmbient(color);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetAmbientLight(color);
	else RS2D3D12_SetAmbientLight(color);
}

void RS2SetSpecular(bool enable){
	RS2LightingAuditSpecular(enable);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetSpecular(enable);
	else RS2D3D12_SetSpecular(enable);
}

void RS2SetDiffuseColorSource(RS2ColorSource source){
	RS2LightingAuditDiffuseSource(source);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetDiffuseColorSource(source);
	else RS2D3D12_SetDiffuseColorSource(source);
}

void RS2SetAmbientColorSource(RS2ColorSource source){
	RS2LightingAuditAmbientSource(source);
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetAmbientColorSource(source);
	else RS2D3D12_SetAmbientColorSource(source);
}

void RS2DisableFog(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_DisableFog();
	else RS2D3D12Unsupported("RS2DisableFog");
}

void RS2SetTextureFilter(unsigned int stage, RS2TextureFilter filter){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetTextureFilter(stage, filter);
	else RS2D3D12_SetTextureFilter(stage, filter);
}

void RS2SetBaseTextureCombine(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetBaseTextureCombine();
	// D3D12's Stage 0 shader always multiplies texture by diffuse colour.
}

void RS2SetSecondaryTextureCombine(unsigned int stage, bool enable){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetSecondaryTextureCombine(stage, enable);
	else RS2D3D12Unsupported("RS2SetSecondaryTextureCombine");
}

void RS2SetEnvironmentMapping(unsigned int stage, bool enable){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetEnvironmentMapping(stage, enable);
	else RS2D3D12Unsupported("RS2SetEnvironmentMapping");
}

void RS2SetUVTransform(unsigned int stage, bool enable){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetUVTransform(stage, enable);
	else RS2D3D12Unsupported("RS2SetUVTransform");
}

void RS2SetUVMatrix(unsigned int stage, const float *matrix){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SetUVMatrix(stage, matrix);
	else RS2D3D12Unsupported("RS2SetUVMatrix");
}

void RS2ApplyInitialRenderState(){
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_ApplyInitialRenderState();
	else RS2D3D12_ApplyInitialRenderState();
}
