//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	The Direct3D 8 implementations behind the RenderState boundary.
//
//	Internal to the renderer.  Game code includes RS2RenderState.h and never this:
//	the whole point of the boundary is that it does not know which of these
//	exists.
//
//	The public names live in RS2RenderState.cpp, which picks an implementation from
//	the active backend.  Before v0.1.0 the public names were compiled straight
//	out of the Direct3D 8 file, which meant a Direct3D 12 renderer would still
//	have reached sv3.pDev through every one of them.

#ifndef RS2RENDERSTATEBACKEND_H_INCLUDED
#define RS2RENDERSTATEBACKEND_H_INCLUDED

#include "RS2RenderState.h"

void RS2D3D8_SetDepthTest(bool enable);
void RS2D3D8_SetDepthWrite(bool enable);
void RS2D3D8_SetDepthFunc(RS2CompareFunc func);
void RS2D3D8_ClearDepth();
void RS2D3D8_SetBlend(RS2BlendMode mode);
void RS2D3D8_SetAlphaTest(bool enable);
void RS2D3D8_SetAlphaRef(unsigned int ref);
void RS2D3D8_SetAlphaFunc(RS2CompareFunc func);
void RS2D3D8_SetCullMode(RS2CullMode mode);
void RS2D3D8_SetShadeMode(RS2ShadeMode mode);
void RS2D3D8_SetNormalizeNormals(bool enable);
void RS2D3D8_SetStencilTest(bool enable);
void RS2D3D8_SetStencilFunc(RS2CompareFunc func);
void RS2D3D8_SetStencilRef(unsigned int ref);
void RS2D3D8_SetStencilReadMask(unsigned int mask);
void RS2D3D8_SetStencilWriteMask(unsigned int mask);
void RS2D3D8_SetStencilFailOp(RS2StencilOp op);
void RS2D3D8_SetStencilDepthFailOp(RS2StencilOp op);
void RS2D3D8_SetStencilPassOp(RS2StencilOp op);
void RS2D3D8_SetLighting(bool enable);
void RS2D3D8_SetAmbientLight(RS2PackedColor color);
void RS2D3D8_SetSpecular(bool enable);
void RS2D3D8_SetDiffuseColorSource(RS2ColorSource source);
void RS2D3D8_SetAmbientColorSource(RS2ColorSource source);
void RS2D3D8_DisableFog();
void RS2D3D8_SetTextureFilter(unsigned int stage, RS2TextureFilter filter);
void RS2D3D8_SetBaseTextureCombine();
void RS2D3D8_SetSecondaryTextureCombine(unsigned int stage, bool enable);
void RS2D3D8_SetEnvironmentMapping(unsigned int stage, bool enable);
void RS2D3D8_SetUVTransform(unsigned int stage, bool enable);
void RS2D3D8_SetUVMatrix(unsigned int stage, const float *matrix);
void RS2D3D8_ApplyInitialRenderState();

#endif	//	RS2RENDERSTATEBACKEND_H_INCLUDED
