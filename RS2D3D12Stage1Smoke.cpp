//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-24.
//
//	See RS2D3D12Stage1Smoke.h.
//
//	The camera is identity, as in the stage probe: object, world, view and
//	clip space coincide and the eye looks down +z.  Every patch is one colour
//	from edge to edge - its normal and texture coordinate are the same at all
//	four corners - so an expected colour is a single number and the checker
//	can test several points of the patch against it.
//
//	The expectations come from a small model of the measured contract below
//	(RS2S1Model), fed with the texels the decoder produces from the same
//	files the renderer loads.  The environment normals are solved for: each
//	one is chosen so the coordinate it generates lands on the centre of a
//	texel, which keeps point sampling away from the texel edges where the two
//	renderers are allowed to round differently.

#include "stdafx.h"
#include "RS2D3D12Stage1Smoke.h"
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
#include "RS2D3D12Unsupported.h"

#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

static const int RS2_S1_FRAMES = 120;
static const DWORD RS2_S1_HOLD_MS = 8000;
static const unsigned int RS2_S1_CLEAR = 0x00102030;
static const float RS2_S1_BACKGROUND[3] = { 16.0f, 32.0f, 48.0f };
static const unsigned int RS2_S1_AMBIENT = 0xff404040;

//	The installed default environment's map: the one texture real content
//	binds to stage 1 (v0.1.4 WP0).
static const char *RS2_S1_ENVMAP_PATH = "Env\\Default\\EnvMap.png";

bool RS2D3D12Stage1SmokeRequested(){
	return CheckArguments("-dx12stage1smoke")!=FALSE;
}

////////////////////////////////////////////////////////////////////////////////
//	Fixtures
////////////////////////////////////////////////////////////////////////////////

enum RS2S1Texture
{
	RS2_S1_NONE,
	RS2_S1_FLAT,	//	4 x 4 BMP, (192, 128, 64)
	RS2_S1_WHITE,	//	4 x 4 BMP, white
	RS2_S1_GRAD,	//	256 x 256 BMP, texel (x, y) = (x, y, 128)
	RS2_S1_CHECK,	//	4 x 4 BMP checker, (240, 40, 40) / (40, 40, 240)
	RS2_S1_ALPHA,	//	the 2 x 2 PNG alpha fixture, one texel alpha 0
	RS2_S1_ENVMAP,	//	Env\Default\EnvMap.png
	RS2_S1_COUNT
};

static void RS2S1Flat(unsigned int, unsigned int, unsigned char *rgb){
	rgb[0] = 192; rgb[1] = 128; rgb[2] = 64;
}

static void RS2S1White(unsigned int, unsigned int, unsigned char *rgb){
	rgb[0] = rgb[1] = rgb[2] = 255;
}

static void RS2S1Gradient(unsigned int x, unsigned int y, unsigned char *rgb){
	rgb[0] = (unsigned char)x; rgb[1] = (unsigned char)y; rgb[2] = 128;
}

static void RS2S1Checker(unsigned int x, unsigned int y, unsigned char *rgb){
	const bool a = ((x+y)&1)==0;

	rgb[0] = a ? 240 : 40; rgb[1] = 40; rgb[2] = a ? 40 : 240;
}

//	A 24-bit bottom-up BMP; texel(x, y) in top-down order.
static bool RS2S1WriteBmp(const char *path, unsigned int w, unsigned int h,
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

/*
 *	Write a fixture (or find the installed map), decode it the way the
 *	renderer will, and create the texture from the same file.
 */
static CRS2TextureResource *RS2S1MakeTexture(RS2S1Texture which, CRS2DecodedImage *decoded){
	char directory[MAX_PATH], path[MAX_PATH];
	bool written = false, temporary = true;

	if(which==RS2_S1_ENVMAP){
		_snprintf(path, MAX_PATH-1, "%s", RS2_S1_ENVMAP_PATH);
		path[MAX_PATH-1] = 0;
		written = GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES;
		temporary = false;
	}else{
		if(!GetTempPathA(MAX_PATH, directory)) return 0;
		_snprintf(path, MAX_PATH-1, "%sRS2EX-stage1-%d-%lu.%s", directory, (int)which,
			(unsigned long)GetCurrentProcessId(), which==RS2_S1_ALPHA ? "png" : "bmp");
		path[MAX_PATH-1] = 0;
		switch(which){
		case RS2_S1_FLAT: written = RS2S1WriteBmp(path, 4, 4, RS2S1Flat); break;
		case RS2_S1_WHITE: written = RS2S1WriteBmp(path, 4, 4, RS2S1White); break;
		case RS2_S1_GRAD: written = RS2S1WriteBmp(path, 256, 256, RS2S1Gradient); break;
		case RS2_S1_CHECK: written = RS2S1WriteBmp(path, 4, 4, RS2S1Checker); break;
		case RS2_S1_ALPHA: written = RS2WriteKnownAlphaPngFixture(path); break;
		default: return 0;
		}
	}
	if(!written){
		Debug("RS2D3D12STAGE1|fixture %d not available (%s)\n", (int)which, path);
		return 0;
	}

	std::string error;
	const bool decodedOk = RS2DecodeImageFile(path, 0, 1, decoded, &error);

	//	One level: patches sample at a constant coordinate, so mip selection
	//	is not being measured.
	CRS2TextureResource *texture = decodedOk ? RS2CreateTextureFromFile(path, 0, 1) : 0;

	if(temporary) DeleteFileA(path);
	if(texture && !texture->IsValid()){
		RS2DestroyTexture(texture);
		texture = 0;
	}
	return texture;
}

////////////////////////////////////////////////////////////////////////////////
//	The contract, as a model
////////////////////////////////////////////////////////////////////////////////

static float RS2S1Wrap(float x){
	return x-(float)floor(x);
}

//	RGBA, 0..255.
static void RS2S1Texel(const CRS2DecodedImage &image, int x, int y, float *out){
	const RS2DecodedMip *mip = image.GetMip(0);
	const int w = (int)mip->width, h = (int)mip->height;

	x = ((x%w)+w)%w;
	y = ((y%h)+h)%h;

	const unsigned char *p = mip->Data()+y*mip->rowPitch+x*4;

	out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
}

static void RS2S1Sample(const CRS2DecodedImage &image, float u, float v,
	RS2TextureFilter filter, float *out){
	const float w = (float)image.GetWidth(), h = (float)image.GetHeight();

	u = RS2S1Wrap(u)*w;
	v = RS2S1Wrap(v)*h;
	if(filter!=RS2_FILTER_LINEAR){
		RS2S1Texel(image, (int)floor(u), (int)floor(v), out);
		return;
	}

	const float tu = u-0.5f, tv = v-0.5f;
	const int x0 = (int)floor(tu), y0 = (int)floor(tv);
	const float fx = tu-(float)x0, fy = tv-(float)y0;
	float a[4], b[4], c[4], d[4];
	int i;

	RS2S1Texel(image, x0, y0, a);
	RS2S1Texel(image, x0+1, y0, b);
	RS2S1Texel(image, x0, y0+1, c);
	RS2S1Texel(image, x0+1, y0+1, d);
	for(i = 0; i<4; i++)
		out[i] = (a[i]*(1-fx)+b[i]*fx)*(1-fy)+(c[i]*(1-fx)+d[i]*fx)*fy;
}

//	The generated coordinate: u = 0.5 nx + 0.5 nz, v = -0.5 ny + 0.5 nz.
static void RS2S1EnvironmentUV(const float *n, float *u, float *v){
	*u = RS2S1Wrap(0.5f*n[0]+0.5f*n[2]);
	*v = RS2S1Wrap(-0.5f*n[1]+0.5f*n[2]);
}

/*
 *	A unit normal whose environment coordinate is (u, v).
 *
 *	With nz = t, nx = a - t and ny = t - b, where a = 2u and b = 2v up to the
 *	wrap; unit length makes t a root of 3t^2 - 2(a+b)t + a^2 + b^2 - 1.  Of
 *	all the solutions the one facing the eye most (smallest nz) is kept, so a
 *	lit patch receives the light.
 */
static bool RS2S1SolveNormal(float u, float v, float *n){
	bool found = false;
	int ka, kb, root;

	for(ka = 0; ka<2; ka++){
		for(kb = 0; kb<2; kb++){
			const double a = 2.0*u-2.0*ka, b = 2.0*v-2.0*kb;
			const double disc = (a+b)*(a+b)-3.0*(a*a+b*b-1.0);

			if(disc<0) continue;
			for(root = 0; root<2; root++){
				const double t = ((a+b)+(root ? 1.0 : -1.0)*sqrt(disc))/3.0;

				if(!found || t<n[2]){
					n[0] = (float)(a-t);
					n[1] = (float)(t-b);
					n[2] = (float)t;
					found = true;
				}
			}
		}
	}
	return found;
}

////////////////////////////////////////////////////////////////////////////////
//	Patches
////////////////////////////////////////////////////////////////////////////////

enum RS2S1UV
{
	RS2_S1_UV_OFF,		//	transform off, matrix untouched
	RS2_S1_UV_ON,		//	set the matrix, enable
	RS2_S1_UV_SET_OFF,	//	set the matrix, leave the transform disabled
	RS2_S1_UV_REENABLE	//	enable without setting a matrix: the stored one
};

enum RS2S1Target
{
	RS2_S1_AT_NONE,		//	no environment coordinate (normal facing the eye)
	RS2_S1_AT_GRAD,		//	the centre of gradient texel (76, 179)
	RS2_S1_AT_CHECK,	//	a quarter of the way from checker texel (1, 1) to (2, 1)
	RS2_S1_AT_ALPHA0,	//	the centre of the alpha fixture's transparent texel
	RS2_S1_AT_ENVMAP	//	the centre of an environment map texel
};

enum RS2S1Special
{
	RS2_S1_PLAIN,
	RS2_S1_DESTROYED,	//	stage 1 texture destroyed while bound (first frame)
	RS2_S1_FOREIGN		//	stage 1 given a texture another backend owns
};

struct RS2S1Patch
{
	const char *name;
	RS2S1Texture tex0, tex1, tex1First;	//	tex1First: bound first, to test a rebind
	bool combine, env, envOffFirst;		//	envOffFirst: enable, then disable, then draw
	RS2S1UV uv;
	const float *uvMatrix;
	bool lighting, normal;
	RS2S1Target target;
	float u0, v0;						//	stage 0 coordinate, the same at every corner
	unsigned int colour;				//	0: no vertex colour
	RS2TextureFilter filter0, filter1;
	bool blend, alphaTest;
	RS2S1Special special;
};

static const float RS2_S1_TRANSLATE[16] = { 1,0,0,0, 0,1,0,0, 0.25f,0.125f,1,0, 0,0,0,1 };
static const float RS2_S1_SCALE[16] = { 0.5f,0,0,0, 0,2,0,0, 0,0,1,0, 0,0,0,1 };
static const float RS2_S1_IDENTITY[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

#define P RS2_FILTER_POINT
#define L RS2_FILTER_LINEAR
#define NO RS2_S1_NONE
#define FL RS2_S1_FLAT
#define WH RS2_S1_WHITE
#define GR RS2_S1_GRAD
#define CK RS2_S1_CHECK
#define AL RS2_S1_ALPHA
#define EM RS2_S1_ENVMAP

//	CMesh::Render always pairs environment on / bind / combine on with the
//	reverse after the draw; the patches follow the same order.
//
//	name                     tex0 tex1 first comb   env    envOff uv                  matrix             lit    nrm    target            u0     v0     colour      f0 f1 blend  atest  special
static const RS2S1Patch s_S1Patches[] = {
	{ "modulate",              FL, GR, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "combine-off",           FL, GR, NO, false, true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "unbound-combine-on",    FL, NO, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "rebind-last-wins",      FL, GR, WH, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },

	{ "s1-point",              WH, CK, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_CHECK,  0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "s1-linear",             WH, CK, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_CHECK,  0.5f,  0.5f,  0,          P, L, false, false, RS2_S1_PLAIN },
	{ "s0-linear-s1-point",    CK, GR, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.4375f, 0.375f, 0,        L, P, false, false, RS2_S1_PLAIN },
	{ "s0-point-s1-linear",    CK, CK, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_CHECK,  0.4375f, 0.375f, 0,        P, L, false, false, RS2_S1_PLAIN },

	{ "alpha-kept-blend",      WH, AL, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_ALPHA0, 0.5f,  0.5f,  0x80ffffff, P, P, true,  false, RS2_S1_PLAIN },
	{ "alphatest-kept",        WH, AL, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_ALPHA0, 0.5f,  0.5f,  0,          P, P, false, true,  RS2_S1_PLAIN },
	{ "uv-translate",          GR, NO, NO, false, false, false, RS2_S1_UV_ON,       RS2_S1_TRANSLATE,  false, true,  RS2_S1_AT_NONE,   0.3f,  0.6f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "uv-scale",              GR, NO, NO, false, false, false, RS2_S1_UV_ON,       RS2_S1_SCALE,      false, true,  RS2_S1_AT_NONE,   0.3f,  0.3f,  0,          P, P, false, false, RS2_S1_PLAIN },

	{ "uv-disabled-keeps",     GR, NO, NO, false, false, false, RS2_S1_UV_SET_OFF,  RS2_S1_TRANSLATE,  false, true,  RS2_S1_AT_NONE,   0.3f,  0.6f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "uv-reenable-stored",    GR, NO, NO, false, false, false, RS2_S1_UV_REENABLE, 0,                 false, true,  RS2_S1_AT_NONE,   0.3f,  0.6f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "env-disabled-passthru", WH, GR, NO, true,  true,  true,  RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_NONE,   0.3f,  0.6f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "env-no-normal",         WH, GR, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, false, RS2_S1_AT_NONE,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },

	{ "env-lit",               FL, GR, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 true,  true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "env-png-real",          WH, EM, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_ENVMAP, 0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "destroyed-while-bound", FL, NO, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_DESTROYED },
	{ "foreign-rejected",      FL, NO, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_FOREIGN },

	{ "uv-with-env",           GR, GR, NO, true,  true,  false, RS2_S1_UV_ON,       RS2_S1_TRANSLATE,  false, true,  RS2_S1_AT_GRAD,   0.3f,  0.6f,  0,          P, P, false, false, RS2_S1_PLAIN },
	{ "env-vertex-colour",     WH, GR, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0xffff4020, P, P, false, false, RS2_S1_PLAIN },
	{ "env-s0-untextured",     NO, GR, NO, true,  true,  false, RS2_S1_UV_OFF,      0,                 false, true,  RS2_S1_AT_GRAD,   0.5f,  0.5f,  0,          P, P, false, false, RS2_S1_PLAIN }
};

#undef P
#undef L
#undef NO
#undef FL
#undef WH
#undef GR
#undef CK
#undef AL
#undef EM

static const int RS2_S1_PATCHES = (int)(sizeof(s_S1Patches)/sizeof(s_S1Patches[0]));
static const int RS2_S1_COLUMNS = 4;

//	Per frame on Direct3D 12, from the table: which draws sample stage 1,
//	which take its coordinate from the normal, which transform stage 0, and
//	which have combine on without a stage 1 that can take part.
static const unsigned int RS2_S1_STAGE1_DRAWS = 14;
static const unsigned int RS2_S1_ENV_DRAWS = 13;
static const unsigned int RS2_S1_UV_DRAWS = 4;
static const unsigned int RS2_S1_SKIPPED = 4;
static const unsigned int RS2_S1_BINDS = 17;		//	non-empty stage 1 binds, the rebind twice
static const unsigned int RS2_S1_LINEAR1 = 2;		//	stage 1 linear filter calls

static RS2Material RS2S1Material(){
	RS2Material m;

	m.Diffuse = RS2MakeColor4(0.8f, 0.5f, 0.2f, 1.0f);
	m.Ambient = RS2MakeColor4(0.4f, 0.8f, 0.6f, 1.0f);
	m.Specular = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	m.Emissive = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	m.Power = 0.0f;
	return m;
}

struct RS2S1Prepared
{
	float normal[3];
	float envU, envV;
	int x, y, hw, hh;		//	client pixels
	float cx, cy;			//	clip space
};

/*
 *	What the contract says the patch shows, RGB 0..255.
 *
 *	stage 0:  texture0(uv0) x diffuse, or diffuse without a texture
 *	stage 1:  only with combine on, a stage 1 texture and a stage 0 texture;
 *	          RGB times texture1(uv1), alpha untouched
 *	then the alpha test on the stage 0 alpha, then the blend.
 */
static void RS2S1Model(const RS2S1Patch &p, const RS2S1Prepared &q,
	const CRS2DecodedImage *images, float *rgb, int *tolerance){
	const float identity[4] = { 255, 255, 255, 255 };
	float diffuse[4], colour[4], texel[4];
	int i;

	*tolerance = 2;
	if(p.lighting){
		const float ambient = (float)((RS2_S1_AMBIENT>>16)&0xff)/255.0f;
		const float md[3] = { 0.8f, 0.5f, 0.2f }, ma[3] = { 0.4f, 0.8f, 0.6f };
		const float nDotL = q.normal[2]<0 ? -q.normal[2] : 0.0f;	//	light along +z

		for(i = 0; i<3; i++){
			const float c = ma[i]*ambient+md[i]*nDotL;

			diffuse[i] = (c>1.0f ? 1.0f : c)*255.0f;
		}
		diffuse[3] = 255.0f;
		*tolerance = 3;
	}else if(p.colour){
		diffuse[0] = (float)((p.colour>>16)&0xff);
		diffuse[1] = (float)((p.colour>>8)&0xff);
		diffuse[2] = (float)(p.colour&0xff);
		diffuse[3] = (float)(p.colour>>24);
	}else{
		for(i = 0; i<4; i++) diffuse[i] = identity[i];
	}

	float u0 = p.u0, v0 = p.v0;

	if(p.uv==RS2_S1_UV_ON || p.uv==RS2_S1_UV_REENABLE){
		//	The re-enabled case uses the matrix the previous patch stored.
		const float *m = p.uvMatrix ? p.uvMatrix : RS2_S1_TRANSLATE;
		const float u = p.u0*m[0]+p.v0*m[4]+m[8];
		const float v = p.u0*m[1]+p.v0*m[5]+m[9];

		u0 = u; v0 = v;
	}
	if(p.tex0!=RS2_S1_NONE){
		RS2S1Sample(images[p.tex0], u0, v0, p.filter0, texel);
		for(i = 0; i<4; i++) colour[i] = texel[i]*diffuse[i]/255.0f;
		if(p.filter0==RS2_FILTER_LINEAR) *tolerance = 4;
	}else{
		for(i = 0; i<4; i++) colour[i] = diffuse[i];
	}

	const bool stage1 = p.combine && p.tex1!=RS2_S1_NONE && p.tex0!=RS2_S1_NONE
		&& p.special==RS2_S1_PLAIN;

	if(stage1){
		float u1 = p.u0, v1 = p.v0;		//	pass-through: texture coordinate 0

		if(p.env && !p.envOffFirst){
			u1 = q.envU;
			v1 = q.envV;
		}
		RS2S1Sample(images[p.tex1], u1, v1, p.filter1, texel);
		for(i = 0; i<3; i++) colour[i] = colour[i]*texel[i]/255.0f;
		if(p.filter1==RS2_FILTER_LINEAR) *tolerance = 4;
	}

	if(p.alphaTest && !(colour[3]>128.0f)){
		for(i = 0; i<3; i++) rgb[i] = RS2_S1_BACKGROUND[i];
		return;
	}
	if(p.blend){
		const float a = colour[3]/255.0f;

		for(i = 0; i<3; i++) rgb[i] = colour[i]*a+RS2_S1_BACKGROUND[i]*(1.0f-a);
		return;
	}
	for(i = 0; i<3; i++) rgb[i] = colour[i];
}

static void RS2S1Layout(RS2MeshVertexLayout *layout, bool normal, bool diffuse){
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

static void RS2S1Quad(unsigned char *out, const RS2MeshVertexLayout &layout,
	const RS2S1Patch &p, const RS2S1Prepared &q, float hw, float hh){
	const float corner[4][2] = {
		{ q.cx-hw, q.cy+hh }, { q.cx+hw, q.cy+hh }, { q.cx+hw, q.cy-hh }, { q.cx-hw, q.cy-hh }
	};
	int i;

	for(i = 0; i<4; i++){
		unsigned char *vertex = out+i*layout.stride;
		const float position[3] = { corner[i][0], corner[i][1], 0.5f };
		const float uv[2] = { p.u0, p.v0 };

		memcpy(vertex+layout.positionOffset, position, 12);
		if(layout.HasNormal()) memcpy(vertex+layout.normalOffset, q.normal, 12);
		if(layout.HasDiffuse()) memcpy(vertex+layout.diffuseOffset, &p.colour, 4);
		memcpy(vertex+layout.texCoord[0].offset, uv, 8);
	}
}

static void RS2S1Step(const char *name, bool passed, bool *all){
	Debug("RS2D3D12STAGE1|%-40s|%s\n", name, passed ? "pass" : "FAIL");
	if(!passed) *all = false;
}

static void RS2S1Identity(float *m){
	int i;

	for(i = 0; i<16; i++) m[i] = 0.0f;
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

//	A payload no Direct3D 12 code may interpret: the resource says another
//	backend owns it.  Nothing is allocated, so there is nothing to free.
static void RS2S1ForeignDestroy(void *){}
static const RS2TexturePayloadOps s_S1ForeignOps = { RS2S1ForeignDestroy, 0, 0 };

////////////////////////////////////////////////////////////////////////////////
//	The smoke
////////////////////////////////////////////////////////////////////////////////

bool RS2D3D12Stage1SmokeRun(){
	const bool d3d12 = GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12;
	CRS2D3D12Backend *backend = d3d12 ? RS2D3D12GetActiveBackend() : 0;
	unsigned int width = 0, height = 0;

	GetRS2Renderer().GetViewportSize(&width, &height);
	Debug("RS2D3D12STAGE1|begin|backend=%s|viewport=%ux%u\n",
		GetRS2Renderer().GetBackendName(), width, height);
	if(width!=640 || height!=480){
		Debug("RS2D3D12STAGE1|expected a 640 x 480 viewport|FAIL\n");
		return false;
	}
	if(d3d12 && !backend) return false;
	if(!d3d12) Debug("RS2D3D12STAGE1|Direct3D 8: expectations only, no counters\n");
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	bool ok = true;
	int t, i;

	//	---------------------------------------------------------- baselines
	const unsigned int textureBaseline = d3d12 ? RS2D3D12_GetLiveTextureCount() : 0;
	const unsigned int descriptorBaseline = d3d12 ? backend->GetDescriptors()->GetLive() : 0;
	const RS2D3D12TextureRuntimeStats textureStats =
		d3d12 ? RS2D3D12_GetTextureRuntimeStats() : RS2D3D12TextureRuntimeStats();
	const RS2D3D12StageStats stageStats = d3d12 ? RS2D3D12_GetStageStats() : RS2D3D12StageStats();
	const unsigned int unsupportedBaseline = d3d12 ? RS2D3D12UnsupportedCount() : 0;

	//	---------------------------------------------------------- textures
	CRS2TextureResource *textures[RS2_S1_COUNT] = { 0 };
	CRS2DecodedImage images[RS2_S1_COUNT];
	bool created = true;

	for(t = RS2_S1_FLAT; t<RS2_S1_COUNT; t++){
		textures[t] = RS2S1MakeTexture((RS2S1Texture)t, &images[t]);
		if(!textures[t] || !images[t].IsValid()) created = false;
	}

	CRS2DecodedImage doomedImage;
	CRS2TextureResource *doomed = d3d12 ? RS2S1MakeTexture(RS2_S1_GRAD, &doomedImage) : 0;

	RS2S1Step("textures created (PNG, BMP, EnvMap.png)", created && (!d3d12 || doomed), &ok);
	if(!ok){
		for(t = 0; t<RS2_S1_COUNT; t++) RS2DestroyTexture(textures[t]);
		RS2DestroyTexture(doomed);
		Debug("RS2D3D12STAGE1|FAIL\n");
		return false;
	}
	if(d3d12){
		RS2S1Step("texture and descriptor count +7",
			RS2D3D12_GetLiveTextureCount()==textureBaseline+7
			&& backend->GetDescriptors()->GetLive()==descriptorBaseline+7, &ok);
	}

	//	---------------------------------------------------------- targets
	float targetGrad[2] = { 76.5f/256.0f, 179.5f/256.0f };
	float targetCheck[2] = { 0.4375f, 0.375f };
	float targetAlpha[2] = { -1.0f, -1.0f };
	float targetEnv[2];

	{
		const CRS2DecodedImage &alpha = images[RS2_S1_ALPHA];
		const int w = (int)alpha.GetWidth(), h = (int)alpha.GetHeight();
		int x, y;

		for(y = 0; y<h; y++){
			for(x = 0; x<w; x++){
				float texel[4];

				RS2S1Texel(alpha, x, y, texel);
				if(texel[3]==0.0f && targetAlpha[0]<0){
					targetAlpha[0] = ((float)x+0.5f)/(float)w;
					targetAlpha[1] = ((float)y+0.5f)/(float)h;
				}
			}
		}
		RS2S1Step("alpha fixture has a transparent texel", targetAlpha[0]>=0, &ok);

		const CRS2DecodedImage &env = images[RS2_S1_ENVMAP];

		targetEnv[0] = ((float)(int)(0.3f*env.GetWidth())+0.5f)/(float)env.GetWidth();
		targetEnv[1] = ((float)(int)(0.7f*env.GetHeight())+0.5f)/(float)env.GetHeight();
		Debug("RS2D3D12STAGE1|envmap=%ux%u\n", env.GetWidth(), env.GetHeight());
	}

	//	---------------------------------------------------------- layout
	const float columnX[RS2_S1_COLUMNS] = { -0.75f, -0.25f, 0.25f, 0.75f };
	const int rows = (RS2_S1_PATCHES+RS2_S1_COLUMNS-1)/RS2_S1_COLUMNS;
	const float halfW = 0.1f, halfH = 0.1f;
	std::vector<RS2S1Prepared> prepared(RS2_S1_PATCHES);
	bool solved = true;

	for(i = 0; i<RS2_S1_PATCHES; i++){
		const RS2S1Patch &p = s_S1Patches[i];
		RS2S1Prepared &q = prepared[i];
		const float *target = 0;

		switch(p.target){
		case RS2_S1_AT_GRAD: target = targetGrad; break;
		case RS2_S1_AT_CHECK: target = targetCheck; break;
		case RS2_S1_AT_ALPHA0: target = targetAlpha; break;
		case RS2_S1_AT_ENVMAP: target = targetEnv; break;
		default: break;
		}
		q.normal[0] = 0.0f; q.normal[1] = 0.0f; q.normal[2] = -1.0f;
		if(target && !RS2S1SolveNormal(target[0], target[1], q.normal)) solved = false;
		if(p.normal) RS2S1EnvironmentUV(q.normal, &q.envU, &q.envV);
		else q.envU = q.envV = 0.0f;	//	no normal: the coordinate of a zero normal

		q.cx = columnX[i%RS2_S1_COLUMNS];
		q.cy = 1.0f-(2.0f*(float)(i/RS2_S1_COLUMNS)+1.0f)/(float)rows;
		q.x = (int)((q.cx+1.0f)*0.5f*width+0.5f);
		q.y = (int)((1.0f-q.cy)*0.5f*height+0.5f);
		q.hw = (int)(halfW*0.5f*width);
		q.hh = (int)(halfH*0.5f*height);

		float rgb[3];
		int tolerance, e;

		RS2S1Model(p, q, images, rgb, &tolerance);

		const int expected[3] = {
			(int)floor(rgb[0]+0.5f), (int)floor(rgb[1]+0.5f), (int)floor(rgb[2]+0.5f)
		};
		const int offsets[5][2] = {
			{ 0, 0 }, { -q.hw/2, -q.hh/2 }, { q.hw/2, -q.hh/2 }, { -q.hw/2, q.hh/2 }, { q.hw/2, q.hh/2 }
		};

		Debug("RS2D3D12STAGE1|patch|%d|%s|x=%d|y=%d|n=%.4f,%.4f,%.4f|env=%.4f,%.4f\n", i, p.name,
			q.x, q.y, q.normal[0], q.normal[1], q.normal[2], q.envU, q.envV);
		for(e = 0; e<5; e++){
			Debug("RS2D3D12STAGE1|expect|%s|%d|%d|%d|%d|%d|%d\n", p.name,
				q.x+offsets[e][0], q.y+offsets[e][1],
				expected[0], expected[1], expected[2], tolerance);
		}
	}
	RS2S1Step("environment normals solved", solved, &ok);

	//	---------------------------------------------------------- stages past 1
	//	Refused and reported, never silently applied: RailSim does not reach
	//	them (v0.1.4 WP0).  Direct3D 8 would honour them, so it is spared.
	if(d3d12){
		const unsigned int otherBefore = RS2D3D12_GetTextureRuntimeStats().otherStageBinds;
		const unsigned int filterBefore = RS2D3D12_GetTextureRuntimeStats().rejectedFilters;

		RS2BindTexture(2, textures[RS2_S1_WHITE]->GetRef());
		RS2SetTextureFilter(2, RS2_FILTER_LINEAR);
		RS2SetSecondaryTextureCombine(2, true);
		RS2SetEnvironmentMapping(2, true);
		RS2SetUVTransform(2, true);
		RS2SetUVMatrix(2, RS2_S1_IDENTITY);
		RS2S1Step("stage 2 bind and filter refused",
			RS2D3D12_GetTextureRuntimeStats().otherStageBinds==otherBefore+1
			&& RS2D3D12_GetTextureRuntimeStats().rejectedFilters==filterBefore+1, &ok);
	}

	//	---------------------------------------------------------- frames
	CRS2TextureResource foreign;

	foreign.AdoptPayloadFromBackend(RS2_RENDERER_D3D8, (void *)&s_S1ForeignOps, 4, 4, &s_S1ForeignOps);

	const unsigned int drawBaseline = d3d12 ? RS2D3D12_GetDrawCount() : 0;
	const unsigned int refusedBaseline = d3d12 ? RS2D3D12_GetRefusedDrawCount() : 0;
	unsigned int pipelinesAfterFirst = 0;
	bool framesOk = true, destroyedOk = true;
	unsigned char vertices[4*64];
	int frame;

	for(frame = 0; frame<RS2_S1_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_S1_CLEAR, true)){
			framesOk = false;
			break;
		}

		float identity[16];

		RS2S1Identity(identity);
		RS2SetWorldTransform(identity);
		RS2SetViewTransform(identity);
		RS2SetProjectionTransform(identity);
		RS2SetDepthTest(false);
		RS2SetDepthWrite(false);
		RS2SetCullMode(RS2_CULL_NONE);
		RS2SetAlphaRef(128);
		RS2SetAlphaFunc(RS2_COMPARE_GREATER);
		RS2SetAmbientLight(RS2_S1_AMBIENT);
		RS2SetDiffuseColorSource(RS2_COLOR_FROM_MATERIAL);
		RS2SetAmbientColorSource(RS2_COLOR_FROM_MATERIAL);
		RS2SetDirectionalLight(RS2MakeDirection(0.0f, 0.0f, 1.0f),
			RS2MakeColor4(1.0f, 1.0f, 1.0f, 1.0f));
		RS2SetMaterial(RS2S1Material());
		RS2SetSpecular(false);

		for(i = 0; i<RS2_S1_PATCHES; i++){
			const RS2S1Patch &p = s_S1Patches[i];
			RS2MeshVertexLayout layout;

			RS2S1Layout(&layout, p.normal, p.colour!=0);
			RS2SetLighting(p.lighting);
			RS2SetBlend(p.blend ? RS2_BLEND_ALPHA : RS2_BLEND_DISABLED);
			RS2SetAlphaTest(p.alphaTest);
			RS2SetTextureFilter(0, p.filter0);
			RS2SetTextureFilter(1, p.filter1);

			switch(p.uv){
			case RS2_S1_UV_ON:
				RS2SetUVTransform(0, true);
				RS2SetUVMatrix(0, p.uvMatrix);
				break;
			case RS2_S1_UV_SET_OFF:
				RS2SetUVMatrix(0, p.uvMatrix);
				RS2SetUVTransform(0, false);
				break;
			case RS2_S1_UV_REENABLE:
				RS2SetUVTransform(0, true);
				break;
			default:
				break;
			}

			if(p.env) RS2SetEnvironmentMapping(1, true);
			if(p.envOffFirst) RS2SetEnvironmentMapping(1, false);
			if(p.tex1First!=RS2_S1_NONE) RS2BindTexture(1, textures[p.tex1First]->GetRef());
			RS2BindTexture(1, p.tex1!=RS2_S1_NONE ? textures[p.tex1]->GetRef() : RS2TextureRef());
			if(d3d12 && p.special==RS2_S1_FOREIGN) RS2BindTexture(1, foreign.GetRef());
			if(d3d12 && p.special==RS2_S1_DESTROYED && frame==0){
				//	Direct3D 8 keeps a bound texture alive through its own
				//	reference; Direct3D 12 retires the view and must not
				//	read it.  Real content never does this - CMesh unbinds
				//	first - so only the Direct3D 12 run exercises it.
				RS2BindTexture(1, doomed->GetRef());
				RS2DestroyTexture(doomed);
				doomed = 0;
			}
			if(p.combine) RS2SetSecondaryTextureCombine(1, true);
			RS2BindTexture(0, p.tex0!=RS2_S1_NONE ? textures[p.tex0]->GetRef() : RS2TextureRef());

			RS2S1Quad(vertices, layout, p, prepared[i], halfW, halfH);
			RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, vertices, 4);

			if(p.env && !p.envOffFirst) RS2SetEnvironmentMapping(1, false);
			RS2BindTexture(1, RS2TextureRef());
			if(p.combine) RS2SetSecondaryTextureCombine(1, false);
			if(p.uv==RS2_S1_UV_ON || p.uv==RS2_S1_UV_REENABLE) RS2SetUVTransform(0, false);
		}

		RS2BindTexture(0, RS2TextureRef());
		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();

		if(frame==0 && d3d12){
			//	The destroyed texture's view is retired once the frame that
			//	could still read it has finished.
			backend->WaitForGpu();
			backend->CollectRetiredTextures();
			destroyedOk = RS2D3D12_GetLiveTextureCount()==textureBaseline+6
				&& backend->GetDescriptors()->GetLive()==descriptorBaseline+6;
			pipelinesAfterFirst = backend->GetPipelineStateCount();
		}
	}

	//	---------------------------------------------------------- teardown
	foreign.Free();
	RS2SetLighting(true);
	RS2SetSpecular(true);
	RS2SetAmbientLight(0xff808080);
	RS2SetAlphaTest(false);
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2SetTextureFilter(0, RS2_FILTER_POINT);
	RS2SetTextureFilter(1, RS2_FILTER_POINT);
	for(t = 0; t<RS2_S1_COUNT; t++) RS2DestroyTexture(textures[t]);
	RS2DestroyTexture(doomed);

	ok = ok && framesOk;
	if(d3d12){
		backend->WaitForGpu();
		backend->CollectRetiredTextures();

		const bool uploadsDone = backend->GetTextureUpload()->WaitForAll();
		const unsigned int frames = (unsigned int)RS2_S1_FRAMES;
		const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
		const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;
		const unsigned int pipelines = backend->GetPipelineStateCount();
		const unsigned int errors = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_ERROR) : 0;
		const unsigned int warnings = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;
		const RS2D3D12StageStats &s = RS2D3D12_GetStageStats();
		const RS2D3D12TextureRuntimeStats &x = RS2D3D12_GetTextureRuntimeStats();
		const unsigned int stage1Draws = s.stage1Draws-stageStats.stage1Draws;
		const unsigned int envDraws = s.environmentDraws-stageStats.environmentDraws;
		const unsigned int uvDraws = s.uvTransformedDraws-stageStats.uvTransformedDraws;
		const unsigned int skipped = s.stage1Skipped-stageStats.stage1Skipped;
		const unsigned int binds = x.stage1Binds-textureStats.stage1Binds;
		const unsigned int rejected = x.rejectedBinds-textureStats.rejectedBinds;
		const unsigned int linear1 = x.stage1LinearFilters-textureStats.stage1LinearFilters;
		const unsigned int point1 = x.stage1PointFilters-textureStats.stage1PointFilters;

		RS2S1Step("destroyed-while-bound retired, not read", destroyedOk, &ok);
		RS2S1Step("draws submitted, none refused",
			submitted==frames*(unsigned int)RS2_S1_PATCHES && refused==0, &ok);
		RS2S1Step("stage 1 / environment / UV draw counts",
			stage1Draws==frames*RS2_S1_STAGE1_DRAWS && envDraws==frames*RS2_S1_ENV_DRAWS
			&& uvDraws==frames*RS2_S1_UV_DRAWS && skipped==frames*RS2_S1_SKIPPED, &ok);
		RS2S1Step("stage 1 binds, rebinds and the foreign refusal",
			binds==frames*RS2_S1_BINDS+1 && rejected==frames, &ok);
		RS2S1Step("stage 1 point / linear filters",
			linear1==frames*RS2_S1_LINEAR1
			&& point1==frames*((unsigned int)RS2_S1_PATCHES-RS2_S1_LINEAR1)+1, &ok);
		RS2S1Step("pipelines stable after the first frame", pipelines==pipelinesAfterFirst, &ok);
		RS2S1Step("texture / descriptor / upload baselines",
			RS2D3D12_GetLiveTextureCount()==textureBaseline
			&& backend->GetDescriptors()->GetLive()==descriptorBaseline
			&& uploadsDone && backend->GetTextureUpload()->GetPendingCount()==0, &ok);
		RS2S1Step("debug layer clean, device alive",
			errors==0 && warnings==0 && !backend->IsDeviceRemoved(), &ok);

		Debug("RS2D3D12STAGE1|draws=%u refused=%u stage1=%u env=%u uv=%u skipped=%u pso=%u->%u\n",
			submitted, refused, stage1Draws, envDraws, uvDraws, skipped,
			pipelinesAfterFirst, pipelines);
		Debug("RS2D3D12STAGE1|binds=%u unbinds=%u rejected=%u point=%u linear=%u unsupported=%u->%u\n",
			binds, x.stage1Unbinds-textureStats.stage1Unbinds, rejected, point1, linear1,
			unsupportedBaseline, RS2D3D12UnsupportedCount());
		Debug("RS2D3D12STAGE1|textures=%u->%u descriptors=%u->%u peak=%u/%u\n",
			textureBaseline, RS2D3D12_GetLiveTextureCount(), descriptorBaseline,
			backend->GetDescriptors()->GetLive(), backend->GetDescriptors()->GetPeak(),
			backend->GetDescriptors()->GetCapacity());
		Debug("RS2D3D12STAGE1|debug=%u errors=%u warnings=%u\n",
			backend->HasDebugLayer() ? 1u : 0u, errors, warnings);
	}

	Debug("RS2D3D12STAGE1|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2_S1_HOLD_MS);
	return ok;
}
