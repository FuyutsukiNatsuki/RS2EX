//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-24.
//
//	See RS2StageProbe.h.
//
//	The camera is identity, as in the lighting probe: object, world, view and
//	clip space coincide and the eye looks down +z, so a vertex normal is also
//	its camera-space normal unless a patch rotates its world on purpose.
//
//	Most patches read a texture whose texel (x, y) is (x, y, 128): the colour
//	that lands on screen says which texture coordinate was sampled, to 1/256.
//	That turns "what does environment mapping generate" and "what does this
//	UV matrix do" into numbers that can be read off a screenshot instead of
//	shapes that have to be judged by eye.

#include "stdafx.h"
#include "RS2StageProbe.h"
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
#include "RS2D3D12Backend.h"
#include "RS2D3D12Texture.h"

#include <stdio.h>
#include <vector>

static const int RS2_STAGE_FRAMES = 120;
static const DWORD RS2_STAGE_HOLD_MS = 8000;
static const unsigned int RS2_STAGE_CLEAR = 0x00102030;

enum RS2StageTexture
{
	RS2_ST_NONE,
	RS2_ST_FLAT,	//	4 x 4, (192, 128, 64)
	RS2_ST_GRAD,	//	256 x 256, texel (x, y) = (x, y, 128)
	RS2_ST_CHECK,	//	4 x 4 checker, (240, 40, 40) / (40, 40, 240)
	RS2_ST_ALPHA,	//	the 2 x 2 alpha fixture: red 255, green 128, blue 0, black 255
	RS2_ST_WHITE,	//	4 x 4, (255, 255, 255)
	RS2_ST_COUNT
};

enum RS2StageUV
{
	RS2_UV_OFF,			//	transform off, matrix untouched
	RS2_UV_ON,			//	set the matrix, enable
	RS2_UV_SET_OFF,		//	set the matrix, leave the transform disabled
	RS2_UV_REENABLE		//	enable without setting a matrix: the stored one
};

//	Stage 0 matrices, as the engine's customizers build them: 2D texture
//	coordinates take their translation from the third row.
static const float RS2_UV_IDENTITY[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
static const float RS2_UV_TRANSLATE[16] = { 1,0,0,0, 0,1,0,0, 0.25f,0.125f,1,0, 0,0,0,1 };
static const float RS2_UV_SCALE[16] = { 0.5f,0,0,0, 0,2,0,0, 0,0,1,0, 0,0,0,1 };
static const float RS2_UV_COMBINED[16] = { 0.5f,0,0,0, 0,0.5f,0,0, 0.375f,0.0625f,1,0, 0,0,0,1 };
static const float RS2_UV_FOURTH_ROW[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0.25f,0.125f,0,1 };

//	Normals.  The eye is at the origin looking down +z.
static const float N_PX[3] = { 1, 0, 0 };
static const float N_NX[3] = { -1, 0, 0 };
static const float N_PY[3] = { 0, 1, 0 };
static const float N_NY[3] = { 0, -1, 0 };
static const float N_PZ[3] = { 0, 0, 1 };
static const float N_NZ[3] = { 0, 0, -1 };
static const float N_DIAG[3] = { 0.5773503f, 0.5773503f, -0.5773503f };
static const float N_DIAG2[3] = { -0.6f, 0.3f, -0.7416198f };
static const float N_ALPHA1[3] = { -0.6f, -0.6f, -0.5291503f };
static const float N_ALPHA3[3] = { 0.1998f, -0.8991f, 0.3896f };
static const float N_LEFT[3] = { -0.8f, 0.0f, -0.6f };
static const float N_RIGHT[3] = { 0.8f, 0.0f, -0.6f };

struct RS2StageMaterialDef
{
	float d[4], a[4], s[4], e[4], p;
};

static const RS2StageMaterialDef s_StageMaterials[] = {
	/* 0 base */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	               { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0.0f },
	/* 1 spec */ { { 0.8f, 0.5f, 0.2f, 1.0f }, { 0.4f, 0.8f, 0.6f, 1.0f },
	               { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, 10.0f }
};

struct RS2StagePatch
{
	const char *name;
	RS2StageTexture tex0, tex1, tex1First;	//	tex1First: bound before tex1, to test a rebind
	bool combine, env;
	RS2StageUV uv;
	const float *uvMatrix;
	bool lighting, specular;
	int material;
	bool normal;
	const float *n0, *n1;		//	left and right normal; n1 0 means n0 everywhere
	float u0, u1, v;			//	stage 0 texture coordinate, u varying left to right
	unsigned int colour;		//	0: no vertex colour
	RS2TextureFilter filter0, filter1;
	bool blend, alphaTest;
	bool worldRotZ;				//	rotate the world 90 degrees about z
	float worldScale;			//	uniform world scale; positions are divided by it
};

#define P RS2_FILTER_POINT
#define L RS2_FILTER_LINEAR
#define T0 RS2_ST_NONE
#define FL RS2_ST_FLAT
#define GR RS2_ST_GRAD
#define CK RS2_ST_CHECK
#define AL RS2_ST_ALPHA
#define WH RS2_ST_WHITE

//	Where stage 1 is being measured, stage 0 carries a white texture.  The
//	first probe run found that Direct3D 8 applies no stage 1 at all when
//	stage 0 has no texture, which made every such patch measure nothing; that
//	behaviour now has its own patch (env-s0-untextured) instead.

//	name                    tex0 tex1 first comb   env    uv                 matrix             lit    spec   mat nrm    n0        n1       u0     u1     v      colour      f0 f1 blend  atest  rot    scale
static const RS2StagePatch s_StagePatches[6][6] = {
	{	//	row 0: the secondary combine and what it depends on
		{ "s0-only",              FL, T0, T0, false, false, RS2_UV_OFF,       0,                 false, false, 0, true,  N_NZ,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "combine-env",          FL, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_NZ,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "bound-combine-off",    FL, GR, T0, false, true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_NZ,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "unbound-combine-on",   FL, T0, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_NZ,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "lit-specular-env",     FL, GR, T0, true,  true,  RS2_UV_OFF,       0,                 true,  true,  1, true,  N_NZ,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "rebind",               WH, CK, GR, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_DIAG2,  0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f }
	},
	{	//	row 1: environment coordinates, axis normals, gradient on stage 1
		{ "env+x",                WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_PX,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env-x",                WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_NX,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env+y",                WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_PY,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env-y",                WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_NY,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env+z",                WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_PZ,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env-z",                WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_NZ,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f }
	},
	{	//	row 2: more environment cases
		{ "env-diagonal",         WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_DIAG,   0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env-diagonal2",        WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_DIAG2,  0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env-world-rotated",    WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_PX,     0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, true,  1.0f },
		{ "env-lit",              WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 true,  false, 0, true,  N_DIAG2,  0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env-no-normal",        WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, false, 0,        0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "env-with-uv0",         GR, GR, T0, true,  true,  RS2_UV_ON,        RS2_UV_TRANSLATE,  false, false, 0, true,  N_DIAG2,  0,       0.3f,  0.3f,  0.6f,  0,          P, P, false, false, false, 1.0f }
	},
	{	//	row 3: stage 0 texture transforms, gradient on stage 0
		{ "uv-identity",          GR, T0, T0, false, false, RS2_UV_ON,        RS2_UV_IDENTITY,   false, false, 0, true,  N_NZ,     0,       0.3f,  0.3f,  0.6f,  0,          P, P, false, false, false, 1.0f },
		{ "uv-translate",         GR, T0, T0, false, false, RS2_UV_ON,        RS2_UV_TRANSLATE,  false, false, 0, true,  N_NZ,     0,       0.3f,  0.3f,  0.6f,  0,          P, P, false, false, false, 1.0f },
		{ "uv-scale",             GR, T0, T0, false, false, RS2_UV_ON,        RS2_UV_SCALE,      false, false, 0, true,  N_NZ,     0,       0.3f,  0.3f,  0.3f,  0,          P, P, false, false, false, 1.0f },
		{ "uv-combined",          GR, T0, T0, false, false, RS2_UV_ON,        RS2_UV_COMBINED,   false, false, 0, true,  N_NZ,     0,       0.3f,  0.3f,  0.6f,  0,          P, P, false, false, false, 1.0f },
		{ "uv-set-while-off",     GR, T0, T0, false, false, RS2_UV_SET_OFF,   RS2_UV_TRANSLATE,  false, false, 0, true,  N_NZ,     0,       0.3f,  0.3f,  0.6f,  0,          P, P, false, false, false, 1.0f },
		{ "uv-reenable-stored",   GR, T0, T0, false, false, RS2_UV_REENABLE,  0,                 false, false, 0, true,  N_NZ,     0,       0.3f,  0.3f,  0.6f,  0,          P, P, false, false, false, 1.0f }
	},
	{	//	row 4: filters, alpha, the fourth matrix row
		{ "s1-point",             WH, CK, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_LEFT,   N_RIGHT, 0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "s1-linear",            WH, CK, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_LEFT,   N_RIGHT, 0.5f,  0.5f,  0.5f,  0,          P, L, false, false, false, 1.0f },
		{ "s0-point-s1-linear",   CK, CK, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_LEFT,   N_RIGHT, 0.0f,  1.0f,  0.4f,  0,          P, L, false, false, false, 1.0f },
		{ "alpha-env-a",          WH, AL, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_ALPHA1, 0,       0.5f,  0.5f,  0.5f,  0x80ffffff, P, P, true,  false, false, 1.0f },
		{ "alpha-env-b",          WH, AL, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_ALPHA3, 0,       0.5f,  0.5f,  0.5f,  0x80ffffff, P, P, true,  false, false, 1.0f },
		{ "uv-fourth-row",        GR, T0, T0, false, false, RS2_UV_ON,        RS2_UV_FOURTH_ROW, false, false, 0, true,  N_NZ,     0,       0.3f,  0.3f,  0.6f,  0,          P, P, false, false, false, 1.0f }
	},
	{	//	row 5: what stage 1 does not do, and what it keeps
		{ "env-s0-untextured",    T0, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_DIAG2,  0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f },
		{ "alphatest-env",        WH, AL, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_ALPHA3, 0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, true,  false, 1.0f },
		{ "env-scaled-world",     WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_DIAG2,  0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 2.0f },
		{ "s0-linear-s1-point",   CK, CK, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_LEFT,   N_RIGHT, 0.0f,  1.0f,  0.4f,  0,          L, P, false, false, false, 1.0f },
		{ "env-vertex-colour",    WH, GR, T0, true,  true,  RS2_UV_OFF,       0,                 false, false, 0, true,  N_DIAG2,  0,       0.5f,  0.5f,  0.5f,  0xffff4020, P, P, false, false, false, 1.0f },
		{ "lit-env-flat",         FL, GR, T0, true,  true,  RS2_UV_OFF,       0,                 true,  false, 0, true,  N_DIAG2,  0,       0.5f,  0.5f,  0.5f,  0,          P, P, false, false, false, 1.0f }
	}
};

#undef P
#undef L
#undef T0
#undef FL
#undef GR
#undef CK
#undef AL
#undef WH

bool RS2StageProbeRequested(){
	return CheckArguments("-stageprobe")!=FALSE;
}

////////////////////////////////////////////////////////////////////////////////
//	Textures
////////////////////////////////////////////////////////////////////////////////

/*
 *	A 24-bit bottom-up BMP.  texel(x, y) is in top-down order, as a decoder
 *	hands it to the renderer.
 */
static bool RS2StageWriteBmp(const char *path, unsigned int w, unsigned int h,
	void (*texel)(unsigned int x, unsigned int y, unsigned char *rgb)){
	const unsigned int rowBytes = (w*3+3)&~3u;
	std::vector<unsigned char> file(54+rowBytes*h, 0);
	unsigned int x, y;

	file[0] = 'B';
	file[1] = 'M';
	*(unsigned int *)&file[2] = (unsigned int)file.size();
	*(unsigned int *)&file[10] = 54;
	*(unsigned int *)&file[14] = 40;
	*(int *)&file[18] = (int)w;
	*(int *)&file[22] = (int)h;
	*(unsigned short *)&file[26] = 1;
	*(unsigned short *)&file[28] = 24;
	*(unsigned int *)&file[34] = rowBytes*h;
	for(y = 0; y<h; y++){
		unsigned char *row = &file[54+(h-1-y)*rowBytes];

		for(x = 0; x<w; x++){
			unsigned char rgb[3];

			texel(x, y, rgb);
			row[x*3+0] = rgb[2];
			row[x*3+1] = rgb[1];
			row[x*3+2] = rgb[0];
		}
	}

	FILE *f = fopen(path, "wb");

	if(!f) return false;
	const bool ok = fwrite(&file[0], 1, file.size(), f)==file.size();
	fclose(f);
	return ok;
}

static void RS2StageFlat(unsigned int, unsigned int, unsigned char *rgb){
	rgb[0] = 192; rgb[1] = 128; rgb[2] = 64;
}

static void RS2StageGradient(unsigned int x, unsigned int y, unsigned char *rgb){
	rgb[0] = (unsigned char)x; rgb[1] = (unsigned char)y; rgb[2] = 128;
}

static void RS2StageWhite(unsigned int, unsigned int, unsigned char *rgb){
	rgb[0] = rgb[1] = rgb[2] = 255;
}

static void RS2StageChecker(unsigned int x, unsigned int y, unsigned char *rgb){
	const bool a = ((x+y)&1)==0;

	rgb[0] = a ? 240 : 40; rgb[1] = 40; rgb[2] = a ? 40 : 240;
}

static CRS2TextureResource *RS2StageMakeTexture(RS2StageTexture which){
	char directory[MAX_PATH], path[MAX_PATH];
	bool written = false;

	if(!GetTempPathA(MAX_PATH, directory)) return 0;
	_snprintf(path, MAX_PATH-1, "%sRS2EX-stage-%d-%lu.%s", directory, (int)which,
		(unsigned long)GetCurrentProcessId(), which==RS2_ST_ALPHA ? "png" : "bmp");
	path[MAX_PATH-1] = 0;

	switch(which){
	case RS2_ST_FLAT: written = RS2StageWriteBmp(path, 4, 4, RS2StageFlat); break;
	case RS2_ST_GRAD: written = RS2StageWriteBmp(path, 256, 256, RS2StageGradient); break;
	case RS2_ST_CHECK: written = RS2StageWriteBmp(path, 4, 4, RS2StageChecker); break;
	case RS2_ST_ALPHA: written = RS2WriteKnownAlphaPngFixture(path); break;
	case RS2_ST_WHITE: written = RS2StageWriteBmp(path, 4, 4, RS2StageWhite); break;
	default: return 0;
	}
	if(!written) return 0;

	//	One level: the patches are small and magnified, and mip selection is
	//	not what is being measured here.
	CRS2TextureResource *texture = RS2CreateTextureFromFile(path, 0, 1);

	DeleteFileA(path);
	if(texture && !texture->IsValid()){
		RS2DestroyTexture(texture);
		texture = 0;
	}
	return texture;
}

////////////////////////////////////////////////////////////////////////////////
//	Geometry
////////////////////////////////////////////////////////////////////////////////

static void RS2StageLayout(RS2MeshVertexLayout *layout, bool normal, bool diffuse){
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
	layout->texCoordCount = 1;
	layout->texCoord[0].offset = offset;
	layout->texCoord[0].components = 2;
	offset += 8;
	layout->stride = offset;
}

static void RS2StageNormal(const RS2StagePatch &p, bool right, float *out){
	const float *n = (right && p.n1) ? p.n1 : p.n0;

	out[0] = n[0]; out[1] = n[1]; out[2] = n[2];
}

/*
 *	One quad as a fan.  Left corners take n0 and u0, right corners n1 and u1.
 *	With a rotated world the positions are pre-rotated the other way so the
 *	patch lands where it would have; only the normal feels the rotation.
 */
static void RS2StageQuad(unsigned char *out, const RS2MeshVertexLayout &layout,
	const RS2StagePatch &p, float cx, float cy, float hw, float hh){
	const float corner[4][2] = {
		{ cx-hw, cy+hh }, { cx+hw, cy+hh }, { cx+hw, cy-hh }, { cx-hw, cy-hh }
	};
	const bool right[4] = { false, true, true, false };
	int i;

	for(i = 0; i<4; i++){
		unsigned char *vertex = out+i*layout.stride;
		float position[3] = {
			corner[i][0]/p.worldScale, corner[i][1]/p.worldScale, 0.5f/p.worldScale
		};

		if(p.worldRotZ){
			//	world = RotZ(90): (x, y) -> (-y, x).  Store its inverse.
			const float x = position[0], y = position[1];

			position[0] = y;
			position[1] = -x;
		}
		memcpy(vertex+layout.positionOffset, position, 12);
		if(layout.HasNormal()){
			float n[3];

			RS2StageNormal(p, right[i], n);
			memcpy(vertex+layout.normalOffset, n, 12);
		}
		if(layout.HasDiffuse()) memcpy(vertex+layout.diffuseOffset, &p.colour, 4);

		const float uv[2] = { right[i] ? p.u1 : p.u0, p.v };

		memcpy(vertex+layout.texCoord[0].offset, uv, 8);
	}
}

static RS2Material RS2StageMaterial(int index){
	const RS2StageMaterialDef &d = s_StageMaterials[index];
	RS2Material m;

	m.Diffuse = RS2MakeColor4(d.d[0], d.d[1], d.d[2], d.d[3]);
	m.Ambient = RS2MakeColor4(d.a[0], d.a[1], d.a[2], d.a[3]);
	m.Specular = RS2MakeColor4(d.s[0], d.s[1], d.s[2], d.s[3]);
	m.Emissive = RS2MakeColor4(d.e[0], d.e[1], d.e[2], d.e[3]);
	m.Power = d.p;
	return m;
}

static void RS2StageMatrix(float *m, bool rotZ, float scale){
	int i;

	for(i = 0; i<16; i++) m[i] = 0.0f;
	m[15] = 1.0f;
	m[10] = scale;
	if(rotZ){
		//	Row vectors: (1, 0, 0) -> (0, 1, 0).
		m[1] = scale;
		m[4] = -scale;
	}else{
		m[0] = m[5] = scale;
	}
}

////////////////////////////////////////////////////////////////////////////////
//	The probe
////////////////////////////////////////////////////////////////////////////////

bool RS2StageProbeRun(){
	const bool d3d12 = GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12;
	unsigned int width = 0, height = 0;

	GetRS2Renderer().GetViewportSize(&width, &height);
	Debug("RS2STAGEPROBE|begin|backend=%s|viewport=%ux%u\n",
		GetRS2Renderer().GetBackendName(), width, height);
	if(width!=640 || height!=480){
		Debug("RS2STAGEPROBE|expected a 640 x 480 viewport|FAIL\n");
		return false;
	}
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	CRS2TextureResource *textures[RS2_ST_COUNT] = { 0 };
	int t;
	bool created = true;

	for(t = RS2_ST_FLAT; t<RS2_ST_COUNT; t++){
		textures[t] = RS2StageMakeTexture((RS2StageTexture)t);
		if(!textures[t]) created = false;
	}
	if(!created){
		Debug("RS2STAGEPROBE|texture creation failed|FAIL\n");
		for(t = 0; t<RS2_ST_COUNT; t++) RS2DestroyTexture(textures[t]);
		return false;
	}

	const float columnX[6] = { -0.8333f, -0.5f, -0.1667f, 0.1667f, 0.5f, 0.8333f };
	const float rowY[6] = { 0.8f, 0.48f, 0.16f, -0.16f, -0.48f, -0.8f };
	const float halfW = 0.12f, halfH = 0.12f;
	int row, column, frame;

	for(row = 0; row<6; row++){
		for(column = 0; column<6; column++){
			Debug("RS2STAGEPROBE|patch|%d|%d|%s|x=%d|y=%d|hw=%d|hh=%d\n", row, column,
				s_StagePatches[row][column].name,
				(int)((columnX[column]+1.0f)*0.5f*width+0.5f),
				(int)((1.0f-rowY[row])*0.5f*height+0.5f),
				(int)(halfW*0.5f*width), (int)(halfH*0.5f*height));
		}
	}

	CRS2D3D12Backend *backend = d3d12 ? RS2D3D12GetActiveBackend() : 0;
	const unsigned int drawBaseline = d3d12 ? RS2D3D12_GetDrawCount() : 0;
	const unsigned int refusedBaseline = d3d12 ? RS2D3D12_GetRefusedDrawCount() : 0;
	unsigned int pipelinesAfterFirst = 0;
	bool framesOk = true;
	unsigned char vertices[4*64];

	for(frame = 0; frame<RS2_STAGE_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_STAGE_CLEAR, true)){
			framesOk = false;
			break;
		}

		float identity[16];

		RS2StageMatrix(identity, false, 1.0f);
		RS2SetViewTransform(identity);
		RS2SetProjectionTransform(identity);
		RS2SetDepthTest(false);
		RS2SetDepthWrite(false);
		RS2SetCullMode(RS2_CULL_NONE);
		RS2SetAlphaRef(128);
		RS2SetAlphaFunc(RS2_COMPARE_GREATER);
		RS2SetAmbientLight(0xff404040);
		RS2SetDiffuseColorSource(RS2_COLOR_FROM_MATERIAL);
		RS2SetAmbientColorSource(RS2_COLOR_FROM_MATERIAL);
		RS2SetDirectionalLight(RS2MakeDirection(0.0f, 0.0f, 1.0f),
			RS2MakeColor4(1.0f, 1.0f, 1.0f, 1.0f));

		for(row = 0; row<6; row++){
			for(column = 0; column<6; column++){
				const RS2StagePatch &p = s_StagePatches[row][column];
				RS2MeshVertexLayout layout;
				float world[16];

				RS2StageLayout(&layout, p.normal, p.colour!=0);
				RS2StageMatrix(world, p.worldRotZ, p.worldScale);
				RS2SetWorldTransform(world);
				RS2SetLighting(p.lighting);
				RS2SetSpecular(p.specular);
				RS2SetMaterial(RS2StageMaterial(p.material));
				RS2SetBlend(p.blend ? RS2_BLEND_ALPHA : RS2_BLEND_DISABLED);
				RS2SetAlphaTest(p.alphaTest);
				RS2SetTextureFilter(0, p.filter0);
				RS2SetTextureFilter(1, p.filter1);

				switch(p.uv){
				case RS2_UV_ON:
					RS2SetUVTransform(0, true);
					RS2SetUVMatrix(0, p.uvMatrix);
					break;
				case RS2_UV_SET_OFF:
					RS2SetUVMatrix(0, p.uvMatrix);
					RS2SetUVTransform(0, false);
					break;
				case RS2_UV_REENABLE:
					RS2SetUVTransform(0, true);
					break;
				default:
					break;
				}

				//	The order CMesh::Render uses: environment, bind, combine.
				if(p.env) RS2SetEnvironmentMapping(1, true);
				if(p.tex1First!=RS2_ST_NONE) RS2BindTexture(1, textures[p.tex1First]->GetRef());
				RS2BindTexture(1, p.tex1!=RS2_ST_NONE ? textures[p.tex1]->GetRef() : RS2TextureRef());
				if(p.combine) RS2SetSecondaryTextureCombine(1, true);
				RS2BindTexture(0, p.tex0!=RS2_ST_NONE ? textures[p.tex0]->GetRef() : RS2TextureRef());

				RS2StageQuad(vertices, layout, p, columnX[column], rowY[row], halfW, halfH);
				RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, vertices, 4);

				//	And the way back, as CMesh::Render leaves it.
				if(p.env) RS2SetEnvironmentMapping(1, false);
				RS2BindTexture(1, RS2TextureRef());
				if(p.combine) RS2SetSecondaryTextureCombine(1, false);
				if(p.uv==RS2_UV_ON || p.uv==RS2_UV_REENABLE) RS2SetUVTransform(0, false);
			}
		}

		RS2BindTexture(0, RS2TextureRef());
		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
		if(frame==0 && backend) pipelinesAfterFirst = backend->GetPipelineStateCount();
	}

	RS2SetLighting(true);
	RS2SetSpecular(true);
	RS2SetAmbientLight(0xff808080);
	RS2SetAlphaTest(false);
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	for(t = 0; t<RS2_ST_COUNT; t++) RS2DestroyTexture(textures[t]);

	bool ok = framesOk;

	if(d3d12 && backend){
		const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
		const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;
		const unsigned int pipelines = backend->GetPipelineStateCount();
		const unsigned int errors = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_ERROR) : 0;
		const unsigned int warnings = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;

		backend->WaitForGpu();
		backend->CollectRetiredTextures();
		Debug("RS2STAGEPROBE|submitted=%u|refused=%u\n", submitted, refused);
		Debug("RS2STAGEPROBE|pipelines=%u->%u|%s\n", pipelinesAfterFirst, pipelines,
			pipelines==pipelinesAfterFirst ? "pass" : "FAIL");
		Debug("RS2STAGEPROBE|textures=%u|descriptors=%u\n",
			RS2D3D12_GetLiveTextureCount(), backend->GetDescriptors()->GetLive());
		Debug("RS2STAGEPROBE|debug=%u|errors=%u|warnings=%u\n",
			backend->HasDebugLayer() ? 1u : 0u, errors, warnings);
		ok = ok && refused==0 && submitted==(unsigned int)(RS2_STAGE_FRAMES*36)
			&& pipelines==pipelinesAfterFirst && errors==0 && warnings==0;
	}

	Debug("RS2STAGEPROBE|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2_STAGE_HOLD_MS);
	return ok;
}
