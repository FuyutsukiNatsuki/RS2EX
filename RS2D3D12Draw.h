//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23, 2026-09-24, 2026-09-25.
//
//	The Direct3D 12 side of the draw boundary.
//
//	Everything the public RS2Draw functions need when Direct3D 12 is the active
//	backend.  RS2Draw.cpp chooses between these and the Direct3D 8 ones; this
//	header is internal and no game code includes it.
//
//	No Direct3D type appears in the signatures, because the callers are the
//	neutral dispatch layer, not renderer code.

#ifndef RS2D3D12DRAW_H_INCLUDED
#define RS2D3D12DRAW_H_INCLUDED

#include "RS2Draw.h"
#include "RS2MeshData.h"
#include "RS2RenderState.h"
#include "RS2GeometryResource.h"
#include "RS2Material.h"
#include "RS2Lighting.h"

class CRS2GeometryResource;

//	Transforms.  The engine owns these matrices; the backend only remembers
//	what it was last told and combines them when a draw needs them.
void RS2D3D12_SetWorldTransform(const float *matrix);
void RS2D3D12_SetViewTransform(const float *matrix);
void RS2D3D12_SetProjectionTransform(const float *matrix);

//	Render state that the pipeline key is built from.  Set by the render-state
//	boundary; read here when a draw picks its pipeline.
void RS2D3D12_SetDepthTest(bool enable);
void RS2D3D12_SetDepthWrite(bool enable);
void RS2D3D12_SetDepthFunc(RS2CompareFunc func);
void RS2D3D12_SetCullMode(RS2CullMode mode);
void RS2D3D12_SetBlend(RS2BlendMode mode);
void RS2D3D12_SetAlphaTest(bool enable);
void RS2D3D12_SetAlphaRef(unsigned int ref);
void RS2D3D12_SetAlphaFunc(RS2CompareFunc func);

//	Material and fixed-function lighting (v0.1.3).  Semantic state only: the
//	values reach the shader as per-draw constants, never as pipeline keys.
void RS2D3D12_SetMaterial(const RS2Material &material);
void RS2D3D12_SetLighting(bool enable);
void RS2D3D12_SetAmbientLight(RS2PackedColor color);
void RS2D3D12_SetSpecular(bool enable);
void RS2D3D12_SetDiffuseColorSource(RS2ColorSource source);
void RS2D3D12_SetAmbientColorSource(RS2ColorSource source);

//	The engine keeps the light (RS2Lighting.cpp); this only records what to
//	submit, the counterpart of RS2D3D8_SubmitDirectionalLight.
void RS2D3D12_SubmitDirectionalLight(const RS2DirectionalLight &light);

//	What the lighting state did, for the smokes and the scene audit.
struct RS2D3D12LightingStats
{
	unsigned int materialCalls, lightingCalls, ambientCalls, specularCalls;
	unsigned int diffuseSourceCalls, ambientSourceCalls, lightCalls;
	unsigned int litDraws, unlitDraws, litNormalDraws, litNoNormalDraws;
	unsigned int specularDraws;	//	lit, specular on, normal present
};
const RS2D3D12LightingStats &RS2D3D12_GetLightingStats();

//	Stage 1, environment mapping and texture transforms (v0.1.4).  Stages 0
//	and 1 only; anything past 1 is refused and reported.
void RS2D3D12_SetSecondaryTextureCombine(unsigned int stage, bool enable);
void RS2D3D12_SetEnvironmentMapping(unsigned int stage, bool enable);
void RS2D3D12_SetUVTransform(unsigned int stage, bool enable);
void RS2D3D12_SetUVMatrix(unsigned int stage, const float *matrix);

struct RS2D3D12StageStats
{
	unsigned int combineCalls, environmentCalls, uvTransformCalls, uvMatrixCalls;
	unsigned int stage1Draws;		//	stage 1 actually sampled
	unsigned int environmentDraws;	//	stage 1 coordinates from the normal
	unsigned int uvTransformedDraws;	//	stage 0 coordinates transformed
	unsigned int stage1Skipped;		//	combine on, but no stage 0 or 1 texture
};
const RS2D3D12StageStats &RS2D3D12_GetStageStats();

//	Stencil, shade mode and fog (v0.1.5).  Only what CShadowVolume uses: one
//	stencil state for both faces, the reference as dynamic state, shade mode
//	and fog disable accepted as compatibility no-ops (see the audit).
void RS2D3D12_SetStencilTest(bool enable);
void RS2D3D12_SetStencilFunc(RS2CompareFunc func);
void RS2D3D12_SetStencilRef(unsigned int ref);
void RS2D3D12_SetStencilReadMask(unsigned int mask);
void RS2D3D12_SetStencilWriteMask(unsigned int mask);
void RS2D3D12_SetStencilFailOp(RS2StencilOp op);
void RS2D3D12_SetStencilDepthFailOp(RS2StencilOp op);
void RS2D3D12_SetStencilPassOp(RS2StencilOp op);
void RS2D3D12_SetShadeMode(RS2ShadeMode mode);
void RS2D3D12_DisableFog();

struct RS2D3D12StencilStats
{
	//	Setter calls: test, func, ref, read mask, write mask, fail, depth fail, pass.
	unsigned int calls[8];
	unsigned int shadeCalls, flatCalls, fogCalls;
	unsigned int stencilDraws;	//	drawn with the stencil test on
	unsigned int volumeDraws;	//	of those, colour-preserving (the shadow volume)
	unsigned int overlayDraws;	//	of those, visible (the shadow overlay)
	unsigned int refChanges;	//	reference different from the previous stencil draw's
};
const RS2D3D12StencilStats &RS2D3D12_GetStencilStats();

/*
 *	Put the state back where the engine expects to find it at start-up.
 *
 *	Direct3D 12 has no runtime defaults to inherit, so this says what they are
 *	rather than leaving them to whatever the struct was initialised with.
 */
void RS2D3D12_ApplyInitialRenderState();

//	A screen-space draw the renderer makes for itself (v0.1.6 live text): the
//	caller's depth, cull, blend, alpha-test, stencil and stage settings are
//	saved and replaced with a string-texture draw's - no depth, no culling,
//	alpha blend, stage 0 only - then put back.  One level deep.
void RS2D3D12_PushOverlayState();
void RS2D3D12_PopOverlayState();

/*
 *	Draw vertices that exist only for the duration of the call.
 *
 *	The data is copied into this frame's scratch memory, because Direct3D 12
 *	has no equivalent of DrawPrimitiveUP and the GPU reads the buffer long
 *	after this function returns.
 */
void RS2D3D12_DrawImmediate(
	const RS2MeshVertexLayout &layout,
	RS2PrimitiveType primitive,
	const void *vertices,
	unsigned int vertexCount);

//	Geometry resources.  The neutral side owns the resource and these fill in
//	and release what is behind it, exactly as the Direct3D 8 side does.
bool RS2D3D12_CreateGeometry(
	CRS2GeometryResource *geometry,
	const RS2MeshVertexLayout &layout,
	const void *vertices);
bool RS2D3D12_CreateIndexedGeometry(
	CRS2GeometryResource *geometry,
	const RS2MeshVertexLayout &layout,
	const void *vertices,
	const unsigned int *indices);
void RS2D3D12_DestroyGeometry(CRS2GeometryResource *geometry);

void RS2D3D12_DrawBuffered(
	const CRS2GeometryResource *geometry,
	RS2PrimitiveType primitive,
	unsigned int firstVertex,
	unsigned int vertexCount);

void RS2D3D12_DrawIndexed(
	const CRS2GeometryResource *geometry,
	RS2PrimitiveType primitive,
	unsigned int firstIndex,
	unsigned int indexCount);

/*
 *	How many draws the Direct3D 12 backend has submitted.
 *
 *	Read by the smoke tests and the validation record.  A renderer that is
 *	running and presenting but has submitted nothing looks exactly like one
 *	that is drawing something invisible, and this tells the two apart.
 */
unsigned int RS2D3D12_GetDrawCount();
unsigned int RS2D3D12_GetTexturedDrawCount();
unsigned int RS2D3D12_GetDDSTexturedDrawCount();

/*
 *	How many draws were refused, for any reason.
 */
unsigned int RS2D3D12_GetRefusedDrawCount();

#endif	//	RS2D3D12DRAW_H_INCLUDED
