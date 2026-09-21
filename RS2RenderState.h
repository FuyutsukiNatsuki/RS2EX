//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	RS2-owned render state.
//
//	RailSim II 2.15 set Direct3D 8 render states directly: D3DRS_ZENABLE,
//	D3DRS_STENCILPASS, D3DTSS_COLOROP and the rest appeared in scene code,
//	plugin previews and the shadow pass.  This header says what the game wants
//	instead of how Direct3D spells it.
//
//	Two rules shaped the API.
//
//	It does not re-expose Direct3D.  There is no SetState(key, value), no
//	DWORD-typed state key, and no enum that is a renamed D3DRS_*.  A caller
//	that wants the depth test off asks for that, and cannot ask for anything
//	the renderer has not agreed to.
//
//	It preserves the order of the native writes.  Where 2.15 wrote several
//	states in sequence, the setters here are one-to-one with those writes, so
//	the migration cannot reorder them by accident.  The exceptions are the
//	places that already had a grouping helper - blend, sampler filter - where
//	the group and its internal order are kept as they were.
//
//	There is no state stack and no save/restore.  The audit found no live
//	device readback anywhere in the program: every pass either ends on a fixed
//	value, re-applies a config value, or deliberately leaves state for the next
//	pass.  Adding general restore would change all three.
//
//	This header deliberately does not include d3d8.h.

#ifndef RS2RENDERSTATE_H_INCLUDED
#define RS2RENDERSTATE_H_INCLUDED

//	[RS2EX] RS2PackedColor moved to RS2Color.h in v0.0.9 - drawing needs the
//	same type, and render state should not be the header everything includes
//	to get a colour.
#include "RS2Color.h"

enum RS2CompareFunc
{
	RS2_COMPARE_ALWAYS,
	RS2_COMPARE_LESS_EQUAL,
	RS2_COMPARE_GREATER		//	not GREATER_EQUAL - see the alpha test
};

enum RS2CullMode
{
	RS2_CULL_NONE,
	RS2_CULL_COUNTER_CLOCKWISE,
	RS2_CULL_CLOCKWISE
};

enum RS2ShadeMode
{
	RS2_SHADE_FLAT,
	RS2_SHADE_GOURAUD
};

/*
 *	The blend modes this program actually uses.
 *
 *	Not a general factor pair: 2.15 defined six blend macros and used two of
 *	them, plus one direct setting in the shadow pass.  Those three plus
 *	disabled is the whole vocabulary.
 */
enum RS2BlendMode
{
	RS2_BLEND_DISABLED,
	RS2_BLEND_ALPHA,		//	SrcAlpha / InvSrcAlpha
	RS2_BLEND_ALPHA_ADD,	//	SrcAlpha / One
	RS2_BLEND_COLOR_PRESERVE	//	Zero / One - writes stencil, keeps colour
};

enum RS2StencilOp
{
	RS2_STENCIL_KEEP,
	RS2_STENCIL_INCREMENT,
	RS2_STENCIL_DECREMENT
};

enum RS2TextureFilter
{
	RS2_FILTER_POINT,
	RS2_FILTER_LINEAR
};

/*
 *	Where the fixed-function pipeline reads a colour from.
 */
enum RS2ColorSource
{
	RS2_COLOR_FROM_VERTEX,	//	the vertex diffuse colour
	RS2_COLOR_FROM_MATERIAL	//	the bound material
};

////////////////////////////////////////////////////////////////////////////////
//	Depth
////////////////////////////////////////////////////////////////////////////////

void RS2SetDepthTest(bool enable);
void RS2SetDepthWrite(bool enable);
void RS2SetDepthFunc(RS2CompareFunc func);
void RS2ClearDepth();

////////////////////////////////////////////////////////////////////////////////
//	Blend
////////////////////////////////////////////////////////////////////////////////

/*
 *	Disabling leaves the factors where they were.  Several passes rely on that:
 *	they disable blending at the end without restoring an entry value, and the
 *	next pass that wants blending sets the mode it needs.
 */
void RS2SetBlend(RS2BlendMode mode);

////////////////////////////////////////////////////////////////////////////////
//	Alpha test
////////////////////////////////////////////////////////////////////////////////

/*
 *	Three separate setters, because the one place that uses the alpha test
 *	writes enable, then ref, then func - and on the way out writes only enable,
 *	leaving ref and func behind.  A combined setter would have to invent a rule
 *	for the disable case.
 */
void RS2SetAlphaTest(bool enable);
void RS2SetAlphaRef(unsigned int ref);
void RS2SetAlphaFunc(RS2CompareFunc func);

////////////////////////////////////////////////////////////////////////////////
//	Raster
////////////////////////////////////////////////////////////////////////////////

void RS2SetCullMode(RS2CullMode mode);
void RS2SetShadeMode(RS2ShadeMode mode);
void RS2SetNormalizeNormals(bool enable);

////////////////////////////////////////////////////////////////////////////////
//	Stencil
////////////////////////////////////////////////////////////////////////////////

/*
 *	One setter per state, matching the shadow pass write for write.  The
 *	overlay reads the ref and masks the volume pass left set, so grouping these
 *	into a descriptor would hide a real dependency between the two passes.
 */
void RS2SetStencilTest(bool enable);
void RS2SetStencilFunc(RS2CompareFunc func);
void RS2SetStencilRef(unsigned int ref);
void RS2SetStencilReadMask(unsigned int mask);
void RS2SetStencilWriteMask(unsigned int mask);
void RS2SetStencilFailOp(RS2StencilOp op);
void RS2SetStencilDepthFailOp(RS2StencilOp op);
void RS2SetStencilPassOp(RS2StencilOp op);

////////////////////////////////////////////////////////////////////////////////
//	Lighting and material source
////////////////////////////////////////////////////////////////////////////////

void RS2SetLighting(bool enable);
void RS2SetAmbientLight(RS2PackedColor color);
void RS2SetSpecular(bool enable);

/*
 *	Diffuse and ambient are set separately, not as a pair.  Most call sites do
 *	set both, but the startup path sets ambient alone, and that asymmetry is
 *	existing behaviour rather than an oversight to tidy up.
 */
void RS2SetDiffuseColorSource(RS2ColorSource source);
void RS2SetAmbientColorSource(RS2ColorSource source);

////////////////////////////////////////////////////////////////////////////////
//	Fog
////////////////////////////////////////////////////////////////////////////////

/*
 *	Fog is off in this program and nothing turns it on.  2.15 carried two
 *	enable paths - vertex and table - that no call site reaches; they stay in
 *	the backend rather than becoming an RS2 feature that has never run.
 */
void RS2DisableFog();

////////////////////////////////////////////////////////////////////////////////
//	Texture stage
////////////////////////////////////////////////////////////////////////////////

/*
 *	stage		: 0 base, 1 secondary/environment
 */
void RS2SetTextureFilter(unsigned int stage, RS2TextureFilter filter);

/*
 *	Stage 0 samples the texture and modulates it with the vertex colour, for
 *	both RGB and alpha.  This is the startup combiner and the one the shadow
 *	overlay re-establishes.
 */
void RS2SetBaseTextureCombine();

/*
 *	Stage 1 modulates its texture with what stage 0 produced, or is switched
 *	off.  Only the colour path is touched - the alpha equivalent exists in 2.15
 *	as two commented-out lines, and reviving it would change the output.
 */
void RS2SetSecondaryTextureCombine(unsigned int stage, bool enable);

/*
 *	Environment mapping: generate texture coordinates from the camera-space
 *	normal and apply the fixed environment matrix, or go back to passing the
 *	mesh coordinates through.
 *
 *	Disabling restores the identity matrix and plain passthrough.  It does not
 *	set the coordinate index to the stage number; passthrough is what 2.15
 *	writes and what the rest of the frame expects.
 */
void RS2SetEnvironmentMapping(unsigned int stage, bool enable);

/*
 *	UV transform for ordinary animated textures.
 *
 *	The matrix and the enable flag are separate because disabling deliberately
 *	leaves the last matrix in place - no caller depends on it being identity,
 *	and writing one back would be a behaviour change.
 *
 *	matrix	: 16 floats, row-major, as the existing MTX4 stores them.
 */
void RS2SetUVTransform(unsigned int stage, bool enable);
void RS2SetUVMatrix(unsigned int stage, const float *matrix);

////////////////////////////////////////////////////////////////////////////////
//	Initial state
////////////////////////////////////////////////////////////////////////////////

/*
 *	Apply the startup render state.
 *
 *	Called on device initialize and again after a reset.  It sets what 2.15's
 *	InitRenderState set, in the same order, and no more: several states the
 *	program later depends on are left at the Direct3D defaults, and that list
 *	is recorded in docs/v0.0.8-render-state-inventory.md rather than guessed at.
 */
void RS2ApplyInitialRenderState();

#endif	//	RS2RENDERSTATE_H_INCLUDED
