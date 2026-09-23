//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	See RS2LightingProbe.h.
//
//	Every patch is a flat quad with one normal, so what the fixed-function
//	pipeline computes per vertex is the same at all four corners - except
//	specular, which depends on where the viewer is.  Direct3D 8 defaults to a
//	local viewer, so a highlight is brightest where the patch faces the eye
//	and falls off across it.  That gradient is part of what is being measured,
//	not noise.
//
//	The camera is identity: object, world, view and clip space coincide, the
//	eye sits at the origin looking down +z, and z = 0.5 is on screen.  The one
//	light travels along +z if Direct3D stores the direction the light travels,
//	which is what the facing / back patch pair establishes.

#include "stdafx.h"
#include "RS2LightingProbe.h"
#include "RS2Renderer.h"
#include "RS2Draw.h"
#include "RS2MeshData.h"
#include "RS2Material.h"
#include "RS2MaterialBinding.h"
#include "RS2RenderState.h"
#include "RS2Lighting.h"
#include "RS2TextureResource.h"
#include "RS2DecodedImage.h"
#include "RS2D3D12Draw.h"

#include <stdio.h>

static const int RS2_PROBE_FRAMES = 120;
static const DWORD RS2_PROBE_HOLD_MS = 8000;

//	Nothing else in the program clears to this, so a capture of the wrong
//	window cannot pass for a probe.
static const unsigned int RS2_PROBE_CLEAR = 0x00102030;

//	Global ambient for every patch that does not say otherwise: a quarter, so
//	ambient terms are visible without swamping the diffuse ones.
static const RS2PackedColor RS2_PROBE_AMBIENT = 0xff404040;

//	A packed vertex colour whose channels are far apart and far from the
//	material colours, so which one arrived is never ambiguous.
static const unsigned int RS2_PROBE_VERTEX_COLOUR = 0xff3060c0;

enum RS2ProbeTexture
{
	RS2_PROBE_TEX_NONE,
	RS2_PROBE_TEX_FLAT,		//	uniform (192,128,64)
	RS2_PROBE_TEX_OPAQUE,	//	alpha fixture, red texel, alpha 255
	RS2_PROBE_TEX_HALF,		//	alpha fixture, green texel, alpha 128
	RS2_PROBE_TEX_CLEAR		//	alpha fixture, blue texel, alpha 0
};

struct RS2ProbeMaterialDef
{
	float d[4], a[4], s[4], e[4], p;
};

//	M0 is the ordinary material.  The rest change one field each.
static const RS2ProbeMaterialDef s_Materials[] = {
	/* 0 base      */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0.0f },
	/* 1 emissive  */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 0, 0, 0, 0 }, { 0.3f, 0.1f, 0.5f, 0.0f }, 0.0f },
	/* 2 overflow  */ { { 1, 1, 1, 1 }, { 1, 1, 1, 1 },
	                    { 0, 0, 0, 0 }, { 0.5f, 0.5f, 0.5f, 0.0f }, 0.0f },
	/* 3 spec p10  */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, 10.0f },
	/* 4 spec p50  */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, 50.0f },
	/* 5 spec red  */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 1, 0, 0, 1 }, { 0, 0, 0, 0 }, 10.0f },
	/* 6 spec p0   */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, 0.0f },
	/* 7 alpha 0.5 */ { { 0.8f, 0.5f, 0.2f, 0.5f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0.0f },
	/* 8 spec p1   */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	                    { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, 1.0f }
};

//	Normals.  The light travels along +z.
static const float RS2_N_FACING[3] = { 0.0f, 0.0f, -1.0f };		//	toward the eye
static const float RS2_N_HALF[3] = { 0.8660254f, 0.0f, -0.5f };	//	60 degrees off
static const float RS2_N_BACK[3] = { 0.0f, 0.0f, 1.0f };		//	away from the eye

//	A surface turned just past the light but still toward the eye: N.L is
//	negative while N.H is not.  Whether the highlight survives says whether
//	the pipeline cuts specular off at the terminator.  The light for this one
//	patch comes from behind and to the left.
static const float RS2_N_PAST_LIGHT[3] = { -0.3f, 0.0f, -0.9539392f };
static const float RS2_L_BEHIND[3] = { 0.9f, 0.0f, -0.4358899f };

struct RS2ProbePatch
{
	const char *name;
	bool normal, diffuse, uv;
	bool lighting, specular;
	RS2ColorSource diffuseSource, ambientSource;
	const float *n;
	const float *light;		//	direction the light travels; 0 for +z
	int material;
	RS2ProbeTexture texture;
	float worldScale;
	bool blend, alphaTest;
	RS2PackedColor ambient;
};

#define V RS2_COLOR_FROM_VERTEX
#define M RS2_COLOR_FROM_MATERIAL
#define AMB RS2_PROBE_AMBIENT

//	name                     normal diff   uv     light  spec   dsrc asrc  n                 light          mat tex                    scale blend  atest  ambient
static const RS2ProbePatch s_Patches[5][6] = {
	{	//	row 0: the basic terms, material sources
		{ "unlit-nocolour",         true,  false, false, false, false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "lit-facing",             true,  false, false, true,  false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "lit-half",               true,  false, false, true,  false, M, M, RS2_N_HALF,       0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "lit-back",               true,  false, false, true,  false, M, M, RS2_N_BACK,       0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "emissive-back",          true,  false, false, true,  false, M, M, RS2_N_BACK,       0,            1, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "spec-behind-light",      true,  false, false, true,  true,  M, M, RS2_N_PAST_LIGHT, RS2_L_BEHIND, 8, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB }
	},
	{	//	row 1: colour sources, and what happens when the vertex has none
		{ "src-vv",                 true,  true,  false, true,  false, V, V, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "src-mm-with-colour",     true,  true,  false, true,  false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "src-vm",                 true,  true,  false, true,  false, V, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "src-vv-nocolour-facing", true,  false, false, true,  false, V, V, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "src-vv-nocolour-back",   true,  false, false, true,  false, V, V, RS2_N_BACK,       0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "unlit-colour-srcm",      true,  true,  false, false, false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB }
	},
	{	//	row 2: specular
		{ "spec-off-p10",           true,  false, false, true,  false, M, M, RS2_N_FACING,     0,            3, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "spec-on-p10",            true,  false, false, true,  true,  M, M, RS2_N_FACING,     0,            3, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "spec-on-p50",            true,  false, false, true,  true,  M, M, RS2_N_FACING,     0,            4, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "spec-on-red",            true,  false, false, true,  true,  M, M, RS2_N_FACING,     0,            5, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "spec-on-p10-textured",   true,  false, true,  true,  true,  M, M, RS2_N_FACING,     0,            3, RS2_PROBE_TEX_FLAT,   1.0f, false, false, AMB },
		{ "spec-on-p0",             true,  false, false, true,  true,  M, M, RS2_N_FACING,     0,            6, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB }
	},
	{	//	row 3: texture modulation, clamping, missing normals
		{ "tex-lit",                true,  false, true,  true,  false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_FLAT,   1.0f, false, false, AMB },
		{ "tex-unlit",              true,  false, true,  false, false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_FLAT,   1.0f, false, false, AMB },
		{ "tex-vertex-lit",         true,  true,  true,  true,  false, V, V, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_FLAT,   1.0f, false, false, AMB },
		{ "overflow",               true,  false, false, true,  false, M, M, RS2_N_FACING,     0,            2, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "nonormal-lit",           false, false, false, true,  false, M, M, 0,                0,            1, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB },
		{ "nonormal-colour-lit",    false, true,  false, true,  false, V, V, 0,                0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, AMB }
	},
	{	//	row 4: scale, alpha
		{ "scaled-facing",          true,  false, false, true,  false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_NONE,   2.0f, false, false, AMB },
		{ "diffuse-alpha-blend",    true,  false, false, true,  false, M, M, RS2_N_FACING,     0,            7, RS2_PROBE_TEX_NONE,   1.0f, true,  false, AMB },
		{ "alphatest-opaque",       true,  false, true,  true,  false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_OPAQUE, 1.0f, false, true,  AMB },
		{ "alphatest-cut",          true,  false, true,  true,  false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_CLEAR,  1.0f, false, true,  AMB },
		{ "texalpha-half-blend",    true,  false, true,  true,  false, M, M, RS2_N_FACING,     0,            0, RS2_PROBE_TEX_HALF,   1.0f, true,  false, AMB },
		{ "ambient-zero-back",      true,  false, false, true,  false, M, M, RS2_N_BACK,       0,            0, RS2_PROBE_TEX_NONE,   1.0f, false, false, 0xff000000 }
	}
};

#undef V
#undef M
#undef AMB

bool RS2LightingProbeRequested(){
	return CheckArguments("-lightingprobe")!=FALSE;
}

static void RS2ProbeIdentity(float *m, float scale){
	int i;

	for(i = 0; i<16; i++) m[i] = 0.0f;
	m[0] = m[5] = m[10] = scale;
	m[15] = 1.0f;
}

/*
 *	Offsets in the order the Direct3D 8 flexible vertex format requires -
 *	position, normal, diffuse, texture coordinates - so the same bytes are
 *	valid for both backends.
 */
static void RS2ProbeLayout(RS2MeshVertexLayout *layout, bool normal, bool diffuse, bool uv){
	unsigned int offset = 12;

	layout->Clear();
	layout->positionOffset = 0;
	layout->positionSemantic = RS2_POSITION_TRANSFORMED_BY_PIPELINE;
	if(normal){
		layout->normalOffset = (int)offset;
		offset += 12;
	}
	if(diffuse){
		layout->diffuseOffset = (int)offset;
		offset += 4;
	}
	if(uv){
		layout->texCoordCount = 1;
		layout->texCoord[0].offset = offset;
		layout->texCoord[0].components = 2;
		offset += 8;
	}
	layout->stride = offset;
}

static void RS2ProbePut(unsigned char *at, const void *value, unsigned int bytes){
	memcpy(at, value, bytes);
}

/*
 *	One quad as a fan, in the patch's own layout.  With a scaled world the
 *	positions are divided by the scale so the patch lands in the same place;
 *	only the normal sees the scale, which is the point of that patch.
 */
static void RS2ProbeQuad(
	unsigned char *out, const RS2MeshVertexLayout &layout, const RS2ProbePatch &patch,
	float cx, float cy, float hw, float hh, float u, float v){
	const float corner[4][2] = {
		{ cx-hw, cy+hh }, { cx+hw, cy+hh }, { cx+hw, cy-hh }, { cx-hw, cy-hh }
	};
	int i;

	for(i = 0; i<4; i++){
		unsigned char *vertex = out+i*layout.stride;
		const float position[3] = {
			corner[i][0]/patch.worldScale, corner[i][1]/patch.worldScale, 0.5f/patch.worldScale
		};

		RS2ProbePut(vertex+layout.positionOffset, position, 12);
		if(layout.HasNormal()) RS2ProbePut(vertex+layout.normalOffset, patch.n, 12);
		if(layout.HasDiffuse()){
			const unsigned int colour = RS2_PROBE_VERTEX_COLOUR;

			RS2ProbePut(vertex+layout.diffuseOffset, &colour, 4);
		}
		if(layout.texCoordCount){
			const float uv[2] = { u, v };

			RS2ProbePut(vertex+layout.texCoord[0].offset, uv, 8);
		}
	}
}

static RS2Material RS2ProbeMaterial(int index){
	const RS2ProbeMaterialDef &d = s_Materials[index];
	RS2Material m;

	m.Diffuse = RS2MakeColor4(d.d[0], d.d[1], d.d[2], d.d[3]);
	m.Ambient = RS2MakeColor4(d.a[0], d.a[1], d.a[2], d.a[3]);
	m.Specular = RS2MakeColor4(d.s[0], d.s[1], d.s[2], d.s[3]);
	m.Emissive = RS2MakeColor4(d.e[0], d.e[1], d.e[2], d.e[3]);
	m.Power = d.p;
	return m;
}

/*
 *	A 4 x 4 24-bit BMP of one colour.  Written by hand because both backends'
 *	loaders read BMP and the bytes are few enough to check by eye.
 */
static bool RS2ProbeWriteFlatBmp(const char *path){
	unsigned char file[54+4*4*3];
	unsigned int i;

	ZeroMemory(file, sizeof(file));
	file[0] = 'B';
	file[1] = 'M';
	*(unsigned int *)(file+2) = sizeof(file);
	*(unsigned int *)(file+10) = 54;
	*(unsigned int *)(file+14) = 40;
	*(int *)(file+18) = 4;
	*(int *)(file+22) = 4;
	*(unsigned short *)(file+26) = 1;
	*(unsigned short *)(file+28) = 24;
	*(unsigned int *)(file+34) = 4*4*3;

	//	BGR order: (192, 128, 64).
	for(i = 0; i<16; i++){
		file[54+i*3+0] = 64;
		file[54+i*3+1] = 128;
		file[54+i*3+2] = 192;
	}

	FILE *f = fopen(path, "wb");

	if(!f) return false;
	const bool ok = fwrite(file, 1, sizeof(file), f)==sizeof(file);
	fclose(f);
	return ok;
}

static CRS2TextureResource *RS2ProbeTexture(const char *name, bool alphaFixture){
	char directory[MAX_PATH], path[MAX_PATH];

	if(!GetTempPathA(MAX_PATH, directory)) return 0;
	_snprintf(path, MAX_PATH-1, "%sRS2EX-probe-%s-%lu.%s", directory, name,
		(unsigned long)GetCurrentProcessId(), alphaFixture ? "png" : "bmp");
	path[MAX_PATH-1] = 0;

	const bool written = alphaFixture
		? RS2WriteKnownAlphaPngFixture(path) : RS2ProbeWriteFlatBmp(path);

	if(!written) return 0;

	CRS2TextureResource *texture = RS2CreateTextureFromFile(path, 0, 1);

	DeleteFileA(path);
	if(texture && !texture->IsValid()){
		RS2DestroyTexture(texture);
		texture = 0;
	}
	return texture;
}

bool RS2LightingProbeRun(){
	const bool d3d12 = GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12;
	unsigned int width = 0, height = 0;

	GetRS2Renderer().GetViewportSize(&width, &height);
	Debug("RS2LIGHTPROBE|begin|backend=%s|viewport=%ux%u\n",
		GetRS2Renderer().GetBackendName(), width, height);
	if(width!=640 || height!=480){
		Debug("RS2LIGHTPROBE|expected a 640 x 480 viewport|FAIL\n");
		return false;
	}

	//	The final frame is held on this thread; keep the window where a
	//	capture can see it without being sent a message it cannot answer.
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	CRS2TextureResource *flat = RS2ProbeTexture("flat", false);
	CRS2TextureResource *alpha = RS2ProbeTexture("alpha", true);

	if(!flat || !alpha){
		Debug("RS2LIGHTPROBE|texture creation failed|FAIL\n");
		RS2DestroyTexture(flat);
		RS2DestroyTexture(alpha);
		return false;
	}

	//	Patch placement, in clip space.  Logged in pixels for the checker.
	const float columnX[6] = { -0.8333f, -0.5f, -0.1667f, 0.1667f, 0.5f, 0.8333f };
	const float rowY[5] = { 0.76f, 0.38f, 0.0f, -0.38f, -0.76f };
	const float halfW = 0.12f, halfH = 0.15f;
	int row, column, frame;

	for(row = 0; row<5; row++){
		for(column = 0; column<6; column++){
			Debug("RS2LIGHTPROBE|patch|%d|%d|%s|x=%d|y=%d|hw=%d|hh=%d\n", row, column,
				s_Patches[row][column].name,
				(int)((columnX[column]+1.0f)*0.5f*width+0.5f),
				(int)((1.0f-rowY[row])*0.5f*height+0.5f),
				(int)(halfW*0.5f*width), (int)(halfH*0.5f*height));
		}
	}

	const unsigned int drawBaseline = d3d12 ? RS2D3D12_GetDrawCount() : 0;
	const unsigned int refusedBaseline = d3d12 ? RS2D3D12_GetRefusedDrawCount() : 0;
	bool framesOk = true;
	unsigned char vertices[4*64];

	for(frame = 0; frame<RS2_PROBE_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_PROBE_CLEAR, true)){
			framesOk = false;
			break;
		}

		//	After the pass has begun: beginning a pass submits the engine's
		//	view matrix, which would replace this one.
		float identity[16];

		RS2ProbeIdentity(identity, 1.0f);
		RS2SetViewTransform(identity);
		RS2SetProjectionTransform(identity);
		RS2SetDepthTest(false);
		RS2SetDepthWrite(false);
		RS2SetCullMode(RS2_CULL_NONE);
		RS2SetTextureFilter(0, RS2_FILTER_POINT);
		RS2SetAlphaRef(128);
		RS2SetAlphaFunc(RS2_COMPARE_GREATER);
		RS2SetDirectionalLight(RS2MakeDirection(0.0f, 0.0f, 1.0f),
			RS2MakeColor4(1.0f, 1.0f, 1.0f, 1.0f));

		for(row = 0; row<5; row++){
			for(column = 0; column<6; column++){
				const RS2ProbePatch &patch = s_Patches[row][column];
				RS2MeshVertexLayout layout;
				float world[16];
				float u = 0.5f, v = 0.5f;

				if(patch.light)
					RS2SetDirectionalLight(
						RS2MakeDirection(patch.light[0], patch.light[1], patch.light[2]),
						RS2MakeColor4(1.0f, 1.0f, 1.0f, 1.0f));

				RS2ProbeLayout(&layout, patch.normal, patch.diffuse, patch.uv);
				RS2ProbeIdentity(world, patch.worldScale);
				RS2SetWorldTransform(world);

				RS2SetLighting(patch.lighting);
				RS2SetSpecular(patch.specular);
				RS2SetAmbientLight(patch.ambient);
				RS2SetDiffuseColorSource(patch.diffuseSource);
				RS2SetAmbientColorSource(patch.ambientSource);
				RS2SetMaterial(RS2ProbeMaterial(patch.material));
				RS2SetBlend(patch.blend ? RS2_BLEND_ALPHA : RS2_BLEND_DISABLED);
				RS2SetAlphaTest(patch.alphaTest);

				switch(patch.texture){
				case RS2_PROBE_TEX_FLAT:
					RS2BindTexture(0, flat->GetRef());
					break;
				case RS2_PROBE_TEX_OPAQUE:
					RS2BindTexture(0, alpha->GetRef());
					u = 0.25f; v = 0.25f;
					break;
				case RS2_PROBE_TEX_HALF:
					RS2BindTexture(0, alpha->GetRef());
					u = 0.75f; v = 0.25f;
					break;
				case RS2_PROBE_TEX_CLEAR:
					RS2BindTexture(0, alpha->GetRef());
					u = 0.25f; v = 0.75f;
					break;
				default:
					RS2BindTexture(0, RS2TextureRef());
					break;
				}

				RS2ProbeQuad(vertices, layout, patch,
					columnX[column], rowY[row], halfW, halfH, u, v);
				RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, vertices, 4);

				if(patch.light)
					RS2SetDirectionalLight(RS2MakeDirection(0.0f, 0.0f, 1.0f),
						RS2MakeColor4(1.0f, 1.0f, 1.0f, 1.0f));
			}
		}

		RS2BindTexture(0, RS2TextureRef());
		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
	}

	//	What the engine's start-up leaves behind, so nothing downstream of a
	//	probe would inherit its state.  The probe exits right after, but a
	//	test that leaves the renderer odd is a trap for the next one.
	RS2SetLighting(true);
	RS2SetSpecular(true);
	RS2SetAmbientLight(RS2_PROBE_AMBIENT);
	RS2SetAlphaTest(false);
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2DestroyTexture(flat);
	RS2DestroyTexture(alpha);

	bool ok = framesOk;

	if(d3d12){
		const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
		const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;

		Debug("RS2LIGHTPROBE|submitted=%u|refused=%u\n", submitted, refused);
		ok = ok && refused==0 && submitted==(unsigned int)(RS2_PROBE_FRAMES*30);
	}

	Debug("RS2LIGHTPROBE|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2_PROBE_HOLD_MS);
	return ok;
}
