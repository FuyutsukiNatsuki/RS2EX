//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//	Modified for RS2EX on 2026-09-25.
//
//	See RS2D3D12DDSSmoke.h.
//
//	The fixtures are built from single-colour blocks so every expected pixel
//	is exact arithmetic: a 5- or 6-bit channel expands to 8 bits by bit
//	replication, which equals round(v * 255 / max) for every value, and a
//	block whose two endpoints and indices all select one colour decodes to
//	that colour everywhere.  No texel depends on interpolation between
//	endpoints, so the test does not assume anything about a decoder's
//	rounding - only about the format.

#include "stdafx.h"
#include "RS2D3D12DDSSmoke.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Texture.h"
#include "RS2Draw.h"
#include "RS2Lighting.h"
#include "RS2Material.h"
#include "RS2MaterialBinding.h"
#include "RS2MeshData.h"
#include "RS2RenderState.h"
#include "RS2Renderer.h"
#include "RS2TextureResource.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static const int RS2_DDS_FRAMES = 120;
static const DWORD RS2_DDS_HOLD_MS = 8000;
static const unsigned int RS2_DDS_CLEAR = 0x00102030;
static const int RS2_DDS_BACKGROUND[3] = { 16, 32, 48 };

bool RS2D3D12DDSSmokeRequested(){
	return CheckArguments("-dx12ddssmoke")!=FALSE;
}

////////////////////////////////////////////////////////////////////////////////
//	Fixture construction
////////////////////////////////////////////////////////////////////////////////

struct RS2DDSColour
{
	unsigned int r5, g6, b5;
};

static const RS2DDSColour RS2_DDS_RED = { 31, 0, 0 };
static const RS2DDSColour RS2_DDS_GREEN = { 0, 63, 0 };
static const RS2DDSColour RS2_DDS_BLUE = { 0, 0, 31 };
static const RS2DDSColour RS2_DDS_GREY = { 16, 32, 16 };
static const RS2DDSColour RS2_DDS_YELLOW = { 31, 63, 0 };

static unsigned short RS2DDS565(const RS2DDSColour &c){
	return (unsigned short)((c.r5<<11)|(c.g6<<5)|c.b5);
}

//	What the GPU returns for a 565 colour: bit replication to 8 bits.
static void RS2DDSExpand(const RS2DDSColour &c, int *rgb){
	rgb[0] = (int)((c.r5<<3)|(c.r5>>2));
	rgb[1] = (int)((c.g6<<2)|(c.g6>>4));
	rgb[2] = (int)((c.b5<<3)|(c.b5>>2));
}

static void RS2DDSPut16(std::vector<unsigned char> &out, unsigned int v){
	out.push_back((unsigned char)(v&0xff));
	out.push_back((unsigned char)((v>>8)&0xff));
}

static void RS2DDSPut32(std::vector<unsigned char> &out, unsigned int v){
	RS2DDSPut16(out, v&0xffff);
	RS2DDSPut16(out, v>>16);
}

//	A BC1 block of one opaque colour: c0 > c1 selects four-colour mode and
//	every index is 0.
static void RS2DDSBC1Solid(std::vector<unsigned char> &out, const RS2DDSColour &c){
	RS2DDSPut16(out, RS2DDS565(c));
	RS2DDSPut16(out, 0);
	RS2DDSPut32(out, 0);
}

//	A BC1 block that is transparent everywhere: c0 <= c1 selects the mode
//	with a transparent index, and every index is that one (3).
static void RS2DDSBC1Clear(std::vector<unsigned char> &out){
	RS2DDSPut16(out, 0);
	RS2DDSPut16(out, 0xffff);
	RS2DDSPut32(out, 0xffffffffu);
}

//	A BC3 block of one colour and one alpha: both alpha endpoints equal.
static void RS2DDSBC3Solid(std::vector<unsigned char> &out, const RS2DDSColour &c,
	unsigned int alpha){
	out.push_back((unsigned char)alpha);
	out.push_back((unsigned char)alpha);
	for(int i = 0; i<6; i++) out.push_back(0);
	RS2DDSBC1Solid(out, c);
}

static unsigned int RS2DDSFourCCValue(const char *code){
	return (unsigned int)(unsigned char)code[0] | ((unsigned int)(unsigned char)code[1]<<8)
		| ((unsigned int)(unsigned char)code[2]<<16) | ((unsigned int)(unsigned char)code[3]<<24);
}

//	The legacy DDS header, as every installed DDS file has it.
static std::vector<unsigned char> RS2DDSHeader(
	unsigned int width, unsigned int height, const char *fourCC, unsigned int mips,
	unsigned int caps2 = 0){
	std::vector<unsigned char> h;

	RS2DDSPut32(h, 0x20534444);		//	"DDS "
	RS2DDSPut32(h, 124);
	RS2DDSPut32(h, 0x1007|(mips ? 0x20000 : 0));
	RS2DDSPut32(h, height);
	RS2DDSPut32(h, width);
	RS2DDSPut32(h, 0);
	RS2DDSPut32(h, 0);
	RS2DDSPut32(h, mips);
	for(int i = 0; i<11; i++) RS2DDSPut32(h, 0);
	RS2DDSPut32(h, 32);			//	pixel format size
	RS2DDSPut32(h, 0x4);			//	DDPF_FOURCC
	RS2DDSPut32(h, RS2DDSFourCCValue(fourCC));
	for(int i = 0; i<5; i++) RS2DDSPut32(h, 0);
	RS2DDSPut32(h, 0x1000|(mips ? 0x400008 : 0));
	RS2DDSPut32(h, caps2);
	RS2DDSPut32(h, 0);
	RS2DDSPut32(h, 0);
	RS2DDSPut32(h, 0);
	return h;
}

static bool RS2DDSWrite(const char *path, const std::vector<unsigned char> &bytes){
	FILE *f = fopen(path, "wb");

	if(!f) return false;
	const bool ok = bytes.empty() || fwrite(&bytes[0], 1, bytes.size(), f)==bytes.size();
	fclose(f);
	return ok;
}

static void RS2DDSPath(char *path, const char *name){
	char directory[MAX_PATH];

	if(!GetTempPathA(MAX_PATH, directory)) directory[0] = 0;
	_snprintf(path, MAX_PATH-1, "%sRS2EX-dds-%s-%lu.dds", directory, name,
		(unsigned long)GetCurrentProcessId());
	path[MAX_PATH-1] = 0;
}

////////////////////////////////////////////////////////////////////////////////
//	Drawing
////////////////////////////////////////////////////////////////////////////////

struct RS2DDSScreenVertex
{
	float x, y, z, rhw;
	unsigned int diffuse;
	float u, v;
};

struct RS2DDSLitVertex
{
	float x, y, z;
	float nx, ny, nz;
	float u, v;
};

static int RS2DDSColumn(int column){ return 70+column*125; }
static int RS2DDSRow(int row){ return 90+row*140; }

/*
 *	A screen-space quad.  With u0 == u1 and v0 == v1 every pixel samples the
 *	same texel; otherwise u runs across the quad.
 */
static void RS2DDSQuad(const RS2MeshVertexLayout &layout, float cx, float cy, float half,
	float u0, float u1, float v, unsigned int diffuse){
	const RS2DDSScreenVertex quad[4] = {
		{ cx-half, cy-half, 0.5f, 1.0f, diffuse, u0, v },
		{ cx+half, cy-half, 0.5f, 1.0f, diffuse, u1, v },
		{ cx+half, cy+half, 0.5f, 1.0f, diffuse, u1, v },
		{ cx-half, cy+half, 0.5f, 1.0f, diffuse, u0, v }
	};

	RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, quad, 4);
}

static void RS2DDSPanel(const RS2MeshVertexLayout &layout, int column, int row,
	float u, float v){
	RS2DDSQuad(layout, (float)RS2DDSColumn(column), (float)RS2DDSRow(row), 32.0f, u, u, v, 0xffffffff);
}

//	A 2^n-pixel square whose u and v both run 0..1, for mip selection.
static void RS2DDSMipQuad(const RS2MeshVertexLayout &layout, float cx, float cy, float size){
	const float h = size*0.5f;
	const RS2DDSScreenVertex quad[4] = {
		{ cx-h, cy-h, 0.5f, 1.0f, 0xffffffff, 0.0f, 0.0f },
		{ cx+h, cy-h, 0.5f, 1.0f, 0xffffffff, 1.0f, 0.0f },
		{ cx+h, cy+h, 0.5f, 1.0f, 0xffffffff, 1.0f, 1.0f },
		{ cx-h, cy+h, 0.5f, 1.0f, 0xffffffff, 0.0f, 1.0f }
	};

	RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, quad, 4);
}

static void RS2DDSExpect(const char *name, int x, int y, const int *rgb, int tolerance){
	Debug("RS2D3D12DDS|expect|%s|%d|%d|%d|%d|%d|%d\n", name, x, y,
		rgb[0], rgb[1], rgb[2], tolerance);
}

static void RS2DDSStep(const char *name, bool passed, bool *all){
	Debug("RS2D3D12DDS|%-34s|%s\n", name, passed ? "pass" : "FAIL");
	if(!passed) *all = false;
}

static void RS2DDSIdentity(float *m){
	for(int i = 0; i<16; i++) m[i] = 0.0f;
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

////////////////////////////////////////////////////////////////////////////////
//	The test
////////////////////////////////////////////////////////////////////////////////

bool RS2D3D12DDSSmokeRun(){
	if(GetRS2Renderer().GetBackendType()!=RS2_RENDERER_D3D12){
		Debug("RS2D3D12DDS|this test needs -dx12\n");
		return false;
	}
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();
	if(!backend) return false;

	unsigned int width = 0, height = 0;

	GetRS2Renderer().GetViewportSize(&width, &height);
	if(width!=640 || height!=480){
		Debug("RS2D3D12DDS|expected 640 x 480; got %u x %u\n", width, height);
		return false;
	}
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	bool ok = true;

	//	---------------------------------------------------------- fixtures
	std::vector<unsigned char> quad = RS2DDSHeader(8, 8, "DXT1", 0);
	RS2DDSBC1Solid(quad, RS2_DDS_RED);
	RS2DDSBC1Solid(quad, RS2_DDS_GREEN);
	RS2DDSBC1Solid(quad, RS2_DDS_BLUE);
	RS2DDSBC1Solid(quad, RS2_DDS_GREY);

	std::vector<unsigned char> punch = RS2DDSHeader(8, 4, "DXT1", 0);
	RS2DDSBC1Solid(punch, RS2_DDS_YELLOW);
	RS2DDSBC1Clear(punch);

	std::vector<unsigned char> alpha = RS2DDSHeader(16, 4, "DXT5", 0);
	RS2DDSBC3Solid(alpha, RS2_DDS_RED, 255);
	RS2DDSBC3Solid(alpha, RS2_DDS_GREEN, 0);
	RS2DDSBC3Solid(alpha, RS2_DDS_BLUE, 128);
	RS2DDSBC3Solid(alpha, RS2_DDS_GREY, 64);

	//	Three stored levels of a 16 x 16 whose full chain is five: each level
	//	its own colour, so the level the sampler read is visible.
	std::vector<unsigned char> mips = RS2DDSHeader(16, 16, "DXT5", 3);
	for(int i = 0; i<16; i++) RS2DDSBC3Solid(mips, RS2_DDS_RED, 255);
	for(int i = 0; i<4; i++) RS2DDSBC3Solid(mips, RS2_DDS_GREEN, 255);
	RS2DDSBC3Solid(mips, RS2_DDS_BLUE, 255);

	std::vector<unsigned char> seam = RS2DDSHeader(8, 4, "DXT1", 0);
	RS2DDSBC1Solid(seam, RS2_DDS_RED);
	RS2DDSBC1Solid(seam, RS2_DDS_BLUE);

	char quadPath[MAX_PATH], punchPath[MAX_PATH], alphaPath[MAX_PATH];
	char mipsPath[MAX_PATH], seamPath[MAX_PATH];

	RS2DDSPath(quadPath, "quad");
	RS2DDSPath(punchPath, "punch");
	RS2DDSPath(alphaPath, "alpha");
	RS2DDSPath(mipsPath, "mips");
	RS2DDSPath(seamPath, "seam");

	const bool written = RS2DDSWrite(quadPath, quad) && RS2DDSWrite(punchPath, punch)
		&& RS2DDSWrite(alphaPath, alpha) && RS2DDSWrite(mipsPath, mips)
		&& RS2DDSWrite(seamPath, seam);

	RS2DDSStep("fixtures written", written, &ok);

	//	---------------------------------------------------------- refusals
	const unsigned int textureBaseline = RS2D3D12_GetLiveTextureCount();
	const unsigned int descriptorBaseline = backend->GetDescriptors()->GetLive();
	const unsigned int ddsLiveBaseline = RS2D3D12_GetLiveDDSTextureCount();
	const RS2D3D12TextureRuntimeStats statsBefore = RS2D3D12_GetTextureRuntimeStats();
	const UINT64 uploadBaseline = backend->GetTextureUpload()->GetSubmittedBytes();

	{
		struct Bad { const char *name; std::vector<unsigned char> bytes; unsigned long key; };
		std::vector<Bad> bad;
		Bad b;

		b.name = "badmagic"; b.bytes = quad; b.bytes[0] = 'X'; b.key = 0; bad.push_back(b);
		b.name = "shortheader"; b.bytes.assign(quad.begin(), quad.begin()+100); b.key = 0; bad.push_back(b);
		b.name = "shortpayload"; b.bytes.assign(quad.begin(), quad.end()-1); b.key = 0; bad.push_back(b);
		b.name = "dxt3"; b.bytes = quad; memcpy(&b.bytes[84], "DXT3", 4); b.key = 0; bad.push_back(b);
		b.name = "cubemap"; b.bytes = quad; b.bytes[112] = 0x00; b.bytes[113] = 0xfe; b.key = 0; bad.push_back(b);
		b.name = "keyed"; b.bytes = quad; b.key = 0xff000000; bad.push_back(b);

		bool refused = true;

		for(size_t i = 0; i<bad.size(); i++){
			char path[MAX_PATH];

			RS2DDSPath(path, bad[i].name);
			if(!RS2DDSWrite(path, bad[i].bytes)){
				refused = false;
				continue;
			}
			CRS2TextureResource *t = RS2CreateTextureFromFile(path, bad[i].key, 1);

			DeleteFileA(path);
			if(t){
				refused = false;
				RS2DestroyTexture(t);
			}
		}

		const RS2D3D12TextureRuntimeStats &now = RS2D3D12_GetTextureRuntimeStats();

		refused = refused && now.ddsFailures==statsBefore.ddsFailures+(unsigned int)bad.size()
			&& RS2D3D12_GetLiveTextureCount()==textureBaseline
			&& backend->GetDescriptors()->GetLive()==descriptorBaseline;
		RS2DDSStep("malformed and unsupported refused", refused, &ok);
	}

	//	---------------------------------------------------------- repetition
	unsigned int cycles = 0;

	for(; cycles<backend->GetDescriptors()->GetCapacity()+2; cycles++){
		CRS2TextureResource *t = RS2CreateTextureFromFile(quadPath, 0, 1);

		if(!t || !t->IsValid()){
			RS2DestroyTexture(t);
			break;
		}
		RS2DestroyTexture(t);
		if(RS2D3D12_GetLiveTextureCount()!=textureBaseline
				|| RS2D3D12_GetLiveDDSTextureCount()!=ddsLiveBaseline
				|| backend->GetDescriptors()->GetLive()!=descriptorBaseline) break;
	}
	RS2DDSStep("create/destroy past heap capacity",
		cycles==backend->GetDescriptors()->GetCapacity()+2, &ok);

	//	---------------------------------------------------------- textures
	const unsigned int shortfallsBefore = RS2D3D12_GetTextureRuntimeStats().ddsMipShortfalls;
	CRS2TextureResource *tQuad = RS2CreateTextureFromFile(quadPath, 0, 1);
	CRS2TextureResource *tPunch = RS2CreateTextureFromFile(punchPath, 0, 1);
	CRS2TextureResource *tAlpha = RS2CreateTextureFromFile(alphaPath, 0, 1);
	CRS2TextureResource *tMips = RS2CreateTextureFromFile(mipsPath, 0, 0);
	CRS2TextureResource *tTop = RS2CreateTextureFromFile(mipsPath, 0, 1);
	CRS2TextureResource *tSeam = RS2CreateTextureFromFile(seamPath, 0, 1);

	DeleteFileA(quadPath);
	DeleteFileA(punchPath);
	DeleteFileA(alphaPath);
	DeleteFileA(mipsPath);
	DeleteFileA(seamPath);

	const bool created = tQuad && tQuad->IsValid() && tQuad->GetWidth()==8
		&& tPunch && tPunch->IsValid() && tAlpha && tAlpha->IsValid() && tAlpha->GetWidth()==16
		&& tMips && tMips->IsValid() && tTop && tTop->IsValid()
		&& tSeam && tSeam->IsValid()
		&& RS2D3D12_GetLiveTextureCount()==textureBaseline+6
		&& RS2D3D12_GetLiveDDSTextureCount()==ddsLiveBaseline+6
		&& backend->GetDescriptors()->GetLive()==descriptorBaseline+6;

	RS2DDSStep("DDS textures created", created, &ok);
	RS2DDSStep("mip request beyond stored levels counted",
		RS2D3D12_GetTextureRuntimeStats().ddsMipShortfalls==shortfallsBefore+1, &ok);

	//	---------------------------------------------------------- expectations
	int rgb[3];
	const int bg[3] = { RS2_DDS_BACKGROUND[0], RS2_DDS_BACKGROUND[1], RS2_DDS_BACKGROUND[2] };

	RS2DDSExpand(RS2_DDS_RED, rgb);   RS2DDSExpect("bc1-red", RS2DDSColumn(0), RS2DDSRow(0), rgb, 0);
	RS2DDSExpand(RS2_DDS_GREEN, rgb); RS2DDSExpect("bc1-green", RS2DDSColumn(1), RS2DDSRow(0), rgb, 0);
	RS2DDSExpand(RS2_DDS_BLUE, rgb);  RS2DDSExpect("bc1-blue", RS2DDSColumn(2), RS2DDSRow(0), rgb, 0);
	RS2DDSExpand(RS2_DDS_GREY, rgb);  RS2DDSExpect("bc1-grey", RS2DDSColumn(3), RS2DDSRow(0), rgb, 0);
	{
		const int unbound[3] = { 0xe0, 0x20, 0xc0 };

		RS2DDSExpect("unbound-vertex-colour", RS2DDSColumn(4), RS2DDSRow(0), unbound, 0);
	}
	RS2DDSExpand(RS2_DDS_YELLOW, rgb); RS2DDSExpect("bc1-opaque-alphatest", RS2DDSColumn(0), RS2DDSRow(1), rgb, 0);
	RS2DDSExpect("bc1-punchthrough-cut", RS2DDSColumn(1), RS2DDSRow(1), bg, 0);
	{
		//	Alpha 128 over the clear colour, SrcAlpha / InvSrcAlpha.
		const float a = 128.0f/255.0f;
		int blended[3];

		RS2DDSExpand(RS2_DDS_BLUE, rgb);
		for(int i = 0; i<3; i++) blended[i] = (int)(rgb[i]*a + bg[i]*(1.0f-a) + 0.5f);
		RS2DDSExpect("bc3-alpha128-blend", RS2DDSColumn(2), RS2DDSRow(1), blended, 1);
	}
	RS2DDSExpect("bc3-alpha0-cut", RS2DDSColumn(3), RS2DDSRow(1), bg, 0);
	RS2DDSExpand(RS2_DDS_RED, rgb); RS2DDSExpect("bc3-alpha255-kept", RS2DDSColumn(4), RS2DDSRow(1), rgb, 0);

	//	Mip selection: 16 texels over 16, 8, 4 and 2 pixels read levels 0, 1,
	//	2 and - clamped to the last stored level - 2 again.
	const int mipX[4] = { 42, 66, 82, 94 };
	const float mipSize[4] = { 16.0f, 8.0f, 4.0f, 2.0f };
	{
		const RS2DDSColour *level[4] = { &RS2_DDS_RED, &RS2_DDS_GREEN, &RS2_DDS_BLUE, &RS2_DDS_BLUE };
		const char *name[4] = { "mip0-16px", "mip1-8px", "mip2-4px", "mip2-clamped-2px" };

		for(int i = 0; i<4; i++){
			RS2DDSExpand(*level[i], rgb);
			RS2DDSExpect(name[i], mipX[i], RS2DDSRow(2), rgb, 0);
		}
		RS2DDSExpand(RS2_DDS_RED, rgb);
		RS2DDSExpect("miplv1-top-only-8px", RS2DDSColumn(1), RS2DDSRow(2), rgb, 0);
	}

	//	The seam texture across a 64-pixel quad: 8 pixels per texel, red for
	//	texels 0-3 and blue for 4-7.
	{
		const int left = RS2DDSColumn(2)-32;
		int red[3], blue[3], mix[3];

		RS2DDSExpand(RS2_DDS_RED, red);
		RS2DDSExpand(RS2_DDS_BLUE, blue);
		RS2DDSExpect("point-texel1", left+8, RS2DDSRow(2), red, 0);
		RS2DDSExpect("point-texel3-edge", left+31, RS2DDSRow(2), red, 0);
		RS2DDSExpect("point-texel4-edge", left+32, RS2DDSRow(2), blue, 0);
		RS2DDSExpect("point-texel7", left+56, RS2DDSRow(2), blue, 0);

		const int lleft = RS2DDSColumn(3)-32;

		//	Pixel 31 samples at texel 3.375: 62.5% red, 37.5% blue.  Direct3D 8
		//	puts the pixel centre on the integer coordinate, so pixel 31 reads
		//	u = 31 / 64; this was 3.4375 until v0.1.5, computed for the
		//	half-pixel-off screen mapping the backend then had.
		for(int i = 0; i<3; i++) mix[i] = (int)(red[i]*0.625f + blue[i]*0.375f + 0.5f);
		RS2DDSExpect("linear-inside-red", lleft+8, RS2DDSRow(2), red, 0);
		RS2DDSExpect("linear-seam-mix", lleft+31, RS2DDSRow(2), mix, 3);
		RS2DDSExpect("linear-inside-blue", lleft+56, RS2DDSRow(2), blue, 0);
	}

	//	DDS, material and a directional light in one draw: the grey texel
	//	times the lit colour 0.9, 0.7, 0.35 (the lighting probe's facing patch).
	{
		int lit[3];
		const float light[3] = { 0.9f, 0.7f, 0.35f };

		RS2DDSExpand(RS2_DDS_GREY, rgb);
		for(int i = 0; i<3; i++) lit[i] = (int)(rgb[i]*light[i] + 0.5f);
		RS2DDSExpect("dds-material-lighting", RS2DDSColumn(4), RS2DDSRow(2), lit, 1);
	}

	//	---------------------------------------------------------- draw
	RS2MeshVertexLayout screen;

	screen.Clear();
	screen.stride = sizeof(RS2DDSScreenVertex);
	screen.positionOffset = 0;
	screen.positionSemantic = RS2_POSITION_ALREADY_TRANSFORMED;
	screen.diffuseOffset = sizeof(float)*4;
	screen.texCoordCount = 1;
	screen.texCoord[0].offset = sizeof(float)*4+4;
	screen.texCoord[0].components = 2;

	RS2MeshVertexLayout litLayout;

	litLayout.Clear();
	litLayout.stride = sizeof(RS2DDSLitVertex);
	litLayout.positionOffset = 0;
	litLayout.positionSemantic = RS2_POSITION_TRANSFORMED_BY_PIPELINE;
	litLayout.normalOffset = 12;
	litLayout.texCoordCount = 1;
	litLayout.texCoord[0].offset = 24;
	litLayout.texCoord[0].components = 2;

	RS2Material material;

	material.Diffuse = RS2MakeColor4(0.8f, 0.5f, 0.2f, 1.0f);
	material.Ambient = RS2MakeColor4(0.4f, 0.8f, 0.6f, 1.0f);
	material.Specular = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	material.Emissive = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
	material.Power = 0.0f;

	float identity[16];

	RS2DDSIdentity(identity);

	const unsigned int drawBaseline = RS2D3D12_GetDrawCount();
	const unsigned int refusedBaseline = RS2D3D12_GetRefusedDrawCount();
	const unsigned int ddsDrawBaseline = RS2D3D12_GetDDSTexturedDrawCount();
	unsigned int pipelinesAfterFirst = 0;
	bool framesOk = created;
	bool deferredOk = false;
	int frame;

	for(frame = 0; frame<RS2_DDS_FRAMES && framesOk; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_DDS_CLEAR, true)){
			framesOk = false;
			break;
		}
		RS2SetWorldTransform(identity);
		RS2SetViewTransform(identity);
		RS2SetProjectionTransform(identity);
		RS2SetDepthTest(false);
		RS2SetDepthWrite(false);
		RS2SetCullMode(RS2_CULL_NONE);
		RS2SetBlend(RS2_BLEND_DISABLED);
		RS2SetAlphaTest(false);
		RS2SetAlphaRef(128);
		RS2SetAlphaFunc(RS2_COMPARE_GREATER);
		RS2SetLighting(false);
		RS2SetTextureFilter(0, RS2_FILTER_POINT);

		//	Row 0: the four BC1 blocks, then an unbound stage.
		RS2BindTexture(0, tQuad->GetRef());
		RS2DDSPanel(screen, 0, 0, 0.25f, 0.25f);
		RS2DDSPanel(screen, 1, 0, 0.75f, 0.25f);
		RS2DDSPanel(screen, 2, 0, 0.25f, 0.75f);
		RS2DDSPanel(screen, 3, 0, 0.75f, 0.75f);
		RS2BindTexture(0, RS2TextureRef());
		RS2DDSQuad(screen, (float)RS2DDSColumn(4), (float)RS2DDSRow(0), 32.0f,
			0.5f, 0.5f, 0.5f, 0xffe020c0);

		//	A descriptor drawn this frame stays live until its fence.
		if(frame==0){
			CRS2TextureResource *transient = 0;
			char path[MAX_PATH];

			RS2DDSPath(path, "transient");
			if(RS2DDSWrite(path, quad)){
				transient = RS2CreateTextureFromFile(path, 0, 1);
				DeleteFileA(path);
				if(transient && transient->IsValid()){
					RS2BindTexture(0, transient->GetRef());
					RS2DDSPanel(screen, 0, 0, 0.25f, 0.25f);
					RS2BindTexture(0, RS2TextureRef());
					RS2DestroyTexture(transient);
					deferredOk = backend->GetDescriptors()->GetLive()==descriptorBaseline+7
						&& RS2D3D12_GetLiveTextureCount()==textureBaseline+6;
				}else RS2DestroyTexture(transient);
			}
		}

		//	Row 1: punch-through and interpolated alpha.
		RS2SetAlphaTest(true);
		RS2BindTexture(0, tPunch->GetRef());
		RS2DDSPanel(screen, 0, 1, 0.25f, 0.5f);
		RS2DDSPanel(screen, 1, 1, 0.75f, 0.5f);
		RS2SetAlphaTest(false);
		RS2SetBlend(RS2_BLEND_ALPHA);
		RS2BindTexture(0, tAlpha->GetRef());
		RS2DDSPanel(screen, 2, 1, 0.625f, 0.5f);
		RS2SetBlend(RS2_BLEND_DISABLED);
		RS2SetAlphaTest(true);
		RS2DDSPanel(screen, 3, 1, 0.375f, 0.5f);
		RS2DDSPanel(screen, 4, 1, 0.125f, 0.5f);
		RS2SetAlphaTest(false);

		//	Row 2: stored mips, a top-only request, point and linear.
		RS2BindTexture(0, tMips->GetRef());
		for(int i = 0; i<4; i++)
			RS2DDSMipQuad(screen, (float)mipX[i], (float)RS2DDSRow(2), mipSize[i]);
		RS2BindTexture(0, tTop->GetRef());
		RS2DDSMipQuad(screen, (float)RS2DDSColumn(1), (float)RS2DDSRow(2), 8.0f);
		RS2BindTexture(0, tSeam->GetRef());
		RS2DDSQuad(screen, (float)RS2DDSColumn(2), (float)RS2DDSRow(2), 32.0f, 0.0f, 1.0f, 0.5f, 0xffffffff);
		RS2SetTextureFilter(0, RS2_FILTER_LINEAR);
		RS2DDSQuad(screen, (float)RS2DDSColumn(3), (float)RS2DDSRow(2), 32.0f, 0.0f, 1.0f, 0.5f, 0xffffffff);
		RS2SetTextureFilter(0, RS2_FILTER_POINT);

		//	The combined draw: DDS Stage 0, a material and the directional
		//	light, pipeline-transformed with a normal facing the light.
		{
			const float cx = (float)RS2DDSColumn(4)/320.0f-1.0f;
			const float cy = 1.0f-(float)RS2DDSRow(2)/240.0f;
			const float hw = 32.0f/320.0f, hh = 32.0f/240.0f;
			const RS2DDSLitVertex lit[4] = {
				{ cx-hw, cy+hh, 0.5f, 0.0f, 0.0f, -1.0f, 0.75f, 0.75f },
				{ cx+hw, cy+hh, 0.5f, 0.0f, 0.0f, -1.0f, 0.75f, 0.75f },
				{ cx+hw, cy-hh, 0.5f, 0.0f, 0.0f, -1.0f, 0.75f, 0.75f },
				{ cx-hw, cy-hh, 0.5f, 0.0f, 0.0f, -1.0f, 0.75f, 0.75f }
			};

			RS2SetLighting(true);
			RS2SetSpecular(false);
			RS2SetAmbientLight(0xff404040);
			RS2SetDiffuseColorSource(RS2_COLOR_FROM_MATERIAL);
			RS2SetAmbientColorSource(RS2_COLOR_FROM_MATERIAL);
			RS2SetMaterial(material);
			RS2SetDirectionalLight(RS2MakeDirection(0.0f, 0.0f, 1.0f),
				RS2MakeColor4(1.0f, 1.0f, 1.0f, 1.0f));
			RS2BindTexture(0, tQuad->GetRef());
			RS2DrawImmediate(litLayout, RS2_PRIMITIVE_TRIANGLE_FAN, lit, 4);
			RS2SetLighting(false);
		}

		RS2BindTexture(0, RS2TextureRef());
		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();

		if(frame==0){
			backend->WaitForGpu();
			backend->CollectRetiredTextures();
			deferredOk = deferredOk
				&& backend->GetDescriptors()->GetLive()==descriptorBaseline+6;
			RS2DDSStep("in-flight descriptor retirement", deferredOk, &ok);
			pipelinesAfterFirst = backend->GetPipelineStateCount();
		}
	}

	//	---------------------------------------------------------- teardown
	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2SetLighting(true);
	RS2SetSpecular(true);
	RS2SetAmbientLight(0xff808080);
	RS2DestroyTexture(tQuad);
	RS2DestroyTexture(tPunch);
	RS2DestroyTexture(tAlpha);
	RS2DestroyTexture(tMips);
	RS2DestroyTexture(tTop);
	RS2DestroyTexture(tSeam);
	backend->WaitForGpu();
	backend->CollectRetiredTextures();

	const bool uploadsDone = backend->GetTextureUpload()->WaitForAll();
	const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
	const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;
	const unsigned int ddsDraws = RS2D3D12_GetDDSTexturedDrawCount()-ddsDrawBaseline;
	const unsigned int errors = backend->HasDebugLayer()
		? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_ERROR) : 0;
	const unsigned int warnings = backend->HasDebugLayer()
		? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;
	const RS2D3D12TextureRuntimeStats &after = RS2D3D12_GetTextureRuntimeStats();

	//	18 draws a frame, one more on the first; 17 of them sample a DDS.
	const bool countersOk = framesOk && deferredOk
		&& submitted==(unsigned int)(RS2_DDS_FRAMES*18+1) && refused==0
		&& ddsDraws==(unsigned int)(RS2_DDS_FRAMES*17+1)
		&& backend->GetPipelineStateCount()==pipelinesAfterFirst
		&& RS2D3D12_GetLiveTextureCount()==textureBaseline
		&& RS2D3D12_GetLiveDDSTextureCount()==ddsLiveBaseline
		&& backend->GetDescriptors()->GetLive()==descriptorBaseline
		&& uploadsDone && backend->GetTextureUpload()->GetPendingCount()==0
		&& errors==0 && warnings==0 && !backend->IsDeviceRemoved();

	RS2DDSStep("draw/texture/descriptor baselines", countersOk, &ok);
	Debug("RS2D3D12DDS|draws=%u ddsDraws=%u refused=%u cycles=%u pso=%u->%u\n",
		submitted, ddsDraws, refused, cycles, pipelinesAfterFirst,
		backend->GetPipelineStateCount());
	Debug("RS2D3D12DDS|dds attempts=%u success=%u failed=%u bc1=%u bc3=%u shortfall=%u live=%u->%u\n",
		after.ddsAttempts-statsBefore.ddsAttempts, after.ddsSuccesses-statsBefore.ddsSuccesses,
		after.ddsFailures-statsBefore.ddsFailures, after.ddsBC1-statsBefore.ddsBC1,
		after.ddsBC3-statsBefore.ddsBC3, after.ddsMipShortfalls-statsBefore.ddsMipShortfalls,
		ddsLiveBaseline, RS2D3D12_GetLiveDDSTextureCount());
	Debug("RS2D3D12DDS|textures=%u->%u descriptors=%u->%u peak=%u/%u uploadBytes=%llu\n",
		textureBaseline, RS2D3D12_GetLiveTextureCount(), descriptorBaseline,
		backend->GetDescriptors()->GetLive(), backend->GetDescriptors()->GetPeak(),
		backend->GetDescriptors()->GetCapacity(),
		(unsigned long long)(backend->GetTextureUpload()->GetSubmittedBytes()-uploadBaseline));
	Debug("RS2D3D12DDS|debug=%u errors=%u warnings=%u\n",
		backend->HasDebugLayer() ? 1u : 0u, errors, warnings);
	Debug("RS2D3D12DDS|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2_DDS_HOLD_MS);
	return ok;
}
