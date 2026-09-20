//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	The Direct3D 8 side of render state and scene lighting.
//
//	Every D3DRS_*, D3DTSS_* and D3DLIGHT8 the running program touches is here
//	or in the other RS2D3D8* files.  The values and their order are inherited
//	from lib/render.h, lib/texture.h, lib/light.cpp and lib/graphic.cpp
//	(Copyright (c) 2002 Midikyou) - moved, not redesigned.  Where 2.15 wrote
//	several states in sequence the sequence is reproduced exactly, because a
//	reordering here would be invisible in review and hard to find later.

#include "stdafx.h"
#include "RS2RenderState.h"
#include "RS2Lighting.h"
#include "RS2D3D8Lighting.h"

////////////////////////////////////////////////////////////////////////////////
//	Enum translation
////////////////////////////////////////////////////////////////////////////////

/*
 *	Unknown values fall through to the Direct3D default rather than asserting.
 *	Every caller is compiled against the same enum, so an unknown value means a
 *	programming error the release build should survive, not a reason to draw
 *	nothing.
 */
static D3DCMPFUNC RS2ToD3DCompare(RS2CompareFunc func){
	switch(func){
	case RS2_COMPARE_LESS_EQUAL:	return D3DCMP_LESSEQUAL;
	case RS2_COMPARE_GREATER:	return D3DCMP_GREATER;
	case RS2_COMPARE_ALWAYS:
	default:				return D3DCMP_ALWAYS;
	}
}

static DWORD RS2ToD3DCull(RS2CullMode mode){
	switch(mode){
	case RS2_CULL_CLOCKWISE:		return D3DCULL_CW;
	case RS2_CULL_NONE:			return D3DCULL_NONE;
	case RS2_CULL_COUNTER_CLOCKWISE:
	default:				return D3DCULL_CCW;
	}
}

static DWORD RS2ToD3DStencilOp(RS2StencilOp op){
	switch(op){
	case RS2_STENCIL_INCREMENT:	return D3DSTENCILOP_INCR;
	case RS2_STENCIL_DECREMENT:	return D3DSTENCILOP_DECR;
	case RS2_STENCIL_KEEP:
	default:				return D3DSTENCILOP_KEEP;
	}
}

static DWORD RS2ToD3DFilter(RS2TextureFilter filter){
	return filter==RS2_FILTER_LINEAR ? D3DTEXF_LINEAR : D3DTEXF_POINT;
}

static DWORD RS2ToD3DColorSource(RS2ColorSource source){
	return source==RS2_COLOR_FROM_MATERIAL ? D3DMCS_MATERIAL : D3DMCS_COLOR1;
}

////////////////////////////////////////////////////////////////////////////////
//	Depth
////////////////////////////////////////////////////////////////////////////////

void RS2SetDepthTest(bool enable){
	sv3.pDev->SetRenderState(D3DRS_ZENABLE, enable ? TRUE : FALSE);
}

void RS2SetDepthWrite(bool enable){
	sv3.pDev->SetRenderState(D3DRS_ZWRITEENABLE, enable ? TRUE : FALSE);
}

void RS2SetDepthFunc(RS2CompareFunc func){
	sv3.pDev->SetRenderState(D3DRS_ZFUNC, RS2ToD3DCompare(func));
}

void RS2ClearDepth(){
	sv3.pDev->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
}

////////////////////////////////////////////////////////////////////////////////
//	Blend
////////////////////////////////////////////////////////////////////////////////

/*
 *	Enable first, then the factors, and only when enabling - the shape
 *	devSetBlend() had.  Disabling therefore leaves the factors alone, which
 *	several passes depend on.
 */
void RS2SetBlend(RS2BlendMode mode){
	if(mode==RS2_BLEND_DISABLED){
		sv3.pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		return;
	}

	DWORD src = D3DBLEND_SRCALPHA;
	DWORD dst = D3DBLEND_INVSRCALPHA;

	switch(mode){
	case RS2_BLEND_ALPHA_ADD:
		src = D3DBLEND_SRCALPHA;	dst = D3DBLEND_ONE;		break;
	case RS2_BLEND_COLOR_PRESERVE:
		src = D3DBLEND_ZERO;		dst = D3DBLEND_ONE;		break;
	case RS2_BLEND_ALPHA:
	default:
		break;
	}

	sv3.pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	sv3.pDev->SetRenderState(D3DRS_SRCBLEND, src);
	sv3.pDev->SetRenderState(D3DRS_DESTBLEND, dst);
}

////////////////////////////////////////////////////////////////////////////////
//	Alpha test
////////////////////////////////////////////////////////////////////////////////

void RS2SetAlphaTest(bool enable){
	sv3.pDev->SetRenderState(D3DRS_ALPHATESTENABLE, enable ? TRUE : FALSE);
}

void RS2SetAlphaRef(unsigned int ref){
	sv3.pDev->SetRenderState(D3DRS_ALPHAREF, (DWORD)ref);
}

void RS2SetAlphaFunc(RS2CompareFunc func){
	sv3.pDev->SetRenderState(D3DRS_ALPHAFUNC, RS2ToD3DCompare(func));
}

////////////////////////////////////////////////////////////////////////////////
//	Raster
////////////////////////////////////////////////////////////////////////////////

void RS2SetCullMode(RS2CullMode mode){
	sv3.pDev->SetRenderState(D3DRS_CULLMODE, RS2ToD3DCull(mode));
}

void RS2SetShadeMode(RS2ShadeMode mode){
	sv3.pDev->SetRenderState(D3DRS_SHADEMODE,
		mode==RS2_SHADE_FLAT ? D3DSHADE_FLAT : D3DSHADE_GOURAUD);
}

void RS2SetNormalizeNormals(bool enable){
	sv3.pDev->SetRenderState(D3DRS_NORMALIZENORMALS, enable ? TRUE : FALSE);
}

////////////////////////////////////////////////////////////////////////////////
//	Stencil
////////////////////////////////////////////////////////////////////////////////

void RS2SetStencilTest(bool enable){
	sv3.pDev->SetRenderState(D3DRS_STENCILENABLE, enable ? TRUE : FALSE);
}

void RS2SetStencilFunc(RS2CompareFunc func){
	sv3.pDev->SetRenderState(D3DRS_STENCILFUNC, RS2ToD3DCompare(func));
}

void RS2SetStencilRef(unsigned int ref){
	sv3.pDev->SetRenderState(D3DRS_STENCILREF, (DWORD)ref);
}

void RS2SetStencilReadMask(unsigned int mask){
	sv3.pDev->SetRenderState(D3DRS_STENCILMASK, (DWORD)mask);
}

void RS2SetStencilWriteMask(unsigned int mask){
	sv3.pDev->SetRenderState(D3DRS_STENCILWRITEMASK, (DWORD)mask);
}

void RS2SetStencilFailOp(RS2StencilOp op){
	sv3.pDev->SetRenderState(D3DRS_STENCILFAIL, RS2ToD3DStencilOp(op));
}

void RS2SetStencilDepthFailOp(RS2StencilOp op){
	sv3.pDev->SetRenderState(D3DRS_STENCILZFAIL, RS2ToD3DStencilOp(op));
}

void RS2SetStencilPassOp(RS2StencilOp op){
	sv3.pDev->SetRenderState(D3DRS_STENCILPASS, RS2ToD3DStencilOp(op));
}

////////////////////////////////////////////////////////////////////////////////
//	Lighting and material source
////////////////////////////////////////////////////////////////////////////////

void RS2SetLighting(bool enable){
	sv3.pDev->SetRenderState(D3DRS_LIGHTING, enable ? TRUE : FALSE);
}

void RS2SetAmbientLight(RS2PackedColor color){
	sv3.pDev->SetRenderState(D3DRS_AMBIENT, (DWORD)color);
}

void RS2SetSpecular(bool enable){
	sv3.pDev->SetRenderState(D3DRS_SPECULARENABLE, enable ? TRUE : FALSE);
}

void RS2SetDiffuseColorSource(RS2ColorSource source){
	sv3.pDev->SetRenderState(
		D3DRS_DIFFUSEMATERIALSOURCE, RS2ToD3DColorSource(source));
}

void RS2SetAmbientColorSource(RS2ColorSource source){
	sv3.pDev->SetRenderState(
		D3DRS_AMBIENTMATERIALSOURCE, RS2ToD3DColorSource(source));
}

/*
 *	Both fog paths, disabled.
 *
 *	2.15 called devSetFog() and devSetPixelFog() one after the other, each
 *	writing FOGENABLE FALSE.  The second write is redundant, but it is what the
 *	startup sequence does, and this version is not the place to prune it.
 */
void RS2DisableFog(){
	sv3.pDev->SetRenderState(D3DRS_FOGENABLE, FALSE);
	sv3.pDev->SetRenderState(D3DRS_FOGENABLE, FALSE);
}

////////////////////////////////////////////////////////////////////////////////
//	Texture stage
////////////////////////////////////////////////////////////////////////////////

void RS2SetTextureFilter(unsigned int stage, RS2TextureFilter filter){
	const DWORD f = RS2ToD3DFilter(filter);

	sv3.pDev->SetTextureStageState(stage, D3DTSS_MAGFILTER, f);
	sv3.pDev->SetTextureStageState(stage, D3DTSS_MINFILTER, f);
	sv3.pDev->SetTextureStageState(stage, D3DTSS_MIPFILTER, f);
}

/*
 *	Colour first, then alpha, each as op/arg1/arg2 - the order
 *	devSetTexColor() and devSetTexAlpha() produced at startup.
 *
 *	The shadow overlay wrote the same six states in a different order
 *	(arg1, arg2, op, twice).  It now uses this order instead.  All six complete
 *	before the overlay draws and none is read back, so the result is identical;
 *	the alternative was two functions that differ only in ordering.
 */
void RS2SetBaseTextureCombine(){
	sv3.pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	sv3.pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	sv3.pDev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

	sv3.pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	sv3.pDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	sv3.pDev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

void RS2SetSecondaryTextureCombine(unsigned int stage, bool enable){
	sv3.pDev->SetTextureStageState(
		stage, D3DTSS_COLOROP, enable ? D3DTOP_MODULATE : D3DTOP_DISABLE);
	sv3.pDev->SetTextureStageState(stage, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	sv3.pDev->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
}

void RS2SetUVTransform(unsigned int stage, bool enable){
	sv3.pDev->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS,
		enable ? D3DTTFF_COUNT2 : D3DTTFF_DISABLE);
}

void RS2SetUVMatrix(unsigned int stage, const float *matrix){
	if(!matrix) return;

	sv3.pDev->SetTransform(
		(D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0+stage), (const D3DMATRIX *)matrix);
}

/*
 *	The environment matrix: remap the camera-space normal into the 0..1 texture
 *	square, with V flipped.  Value-for-value what devSetEnvMap() built inline.
 */
static const float RS2_ENV_MATRIX[16] = {
	0.5f,  0.0f, 0.0f, 0.0f,
	0.0f, -0.5f, 0.0f, 0.0f,
	0.5f,  0.5f, 1.0f, 0.0f,
	0.0f,  0.0f, 0.0f, 1.0f
};

void RS2SetEnvironmentMapping(unsigned int stage, bool enable){
	RS2SetUVTransform(stage, enable);

	if(enable) RS2SetUVMatrix(stage, RS2_ENV_MATRIX);
	else sv3.pDev->SetTransform(
		(D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0+stage), &MTX_FRONT);

	//	Passthrough, not the stage index.  See A-K1 in the audit: writing the
	//	stage number here would look more correct and change the output.
	sv3.pDev->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX,
		enable ? D3DTSS_TCI_CAMERASPACENORMAL : D3DTSS_TCI_PASSTHRU);
}

////////////////////////////////////////////////////////////////////////////////
//	Initial state
////////////////////////////////////////////////////////////////////////////////

/*
 *	What InitRenderState() set, in the order it set it.
 *
 *	Deliberately not exhaustive.  Alpha test, stencil, depth compare, material
 *	source, texture addressing and the stage-1 sampler are all used later while
 *	never being initialised here; they run on the Direct3D defaults until
 *	something sets them.  docs/v0.0.8-render-state-inventory.md lists them.
 *	Adding initial values would be a behaviour change dressed as tidiness.
 */
void RS2ApplyInitialRenderState(){
	RS2SetLighting(true);
	RS2SetAmbientLight(0xff808080);
	RS2SetSpecular(true);
	RS2SetShadeMode(RS2_SHADE_GOURAUD);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);
	RS2DisableFog();
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2SetNormalizeNormals(true);

	RS2SetBaseTextureCombine();
	RS2SetTextureFilter(0, RS2_FILTER_POINT);
}

////////////////////////////////////////////////////////////////////////////////
//	Scene light
////////////////////////////////////////////////////////////////////////////////

/*
 *	Assemble a D3DLIGHT8 and enable slot 0.
 *
 *	Range 1000 is meaningless for a directional light but is what 2.15 sent,
 *	and the structure is zeroed first for the same reason.
 */
void RS2D3D8_SubmitDirectionalLight(const RS2DirectionalLight &light){
	D3DLIGHT8 d3d;

	ZeroMemory(&d3d, sizeof(D3DLIGHT8));
	d3d.Type = D3DLIGHT_DIRECTIONAL;

	d3d.Diffuse.r = d3d.Specular.r = light.color.r;
	d3d.Diffuse.g = d3d.Specular.g = light.color.g;
	d3d.Diffuse.b = d3d.Specular.b = light.color.b;
	d3d.Diffuse.a = d3d.Specular.a = light.color.a;

	d3d.Range = 1000.0f;

	d3d.Direction.x = light.direction.x;
	d3d.Direction.y = light.direction.y;
	d3d.Direction.z = light.direction.z;

	sv3.pDev->SetLight(0, &d3d);
	sv3.pDev->LightEnable(0, TRUE);
}
