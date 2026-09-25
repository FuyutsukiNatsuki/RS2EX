//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	See RS2MutableProbe.h.
//
//	A4R4G4B4 as Direct3D 8 lays it out: bits 15-12 alpha, 11-8 red, 7-4 green,
//	3-0 blue, one 16-bit word per texel, rows "pitch" bytes apart.  Every
//	expected colour below assumes 4-bit channels expand as v * 17 and the
//	ALPHA blend (SrcAlpha / InvSrcAlpha) over the clear colour, and the
//	Direct3D 8 run is what confirms those two assumptions.

#include "stdafx.h"
#include "RS2MutableProbe.h"
#include "RS2Renderer.h"
#include "RS2Draw.h"
#include "RS2RenderState.h"
#include "RS2Text.h"
#include "RS2TextureResource.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12Texture.h"

#include <stdio.h>

static const int RS2_MP_FRAMES = 60;
static const DWORD RS2_MP_HOLD_MS = 8000;
static const int RS2_MP_LIFETIME_FRAMES = 10;
static const int RS2_MP_LIFETIME_TEXTURES = 16;
static const unsigned int RS2_MP_CLEAR = 0x00102030;
static const int RS2_MP_BG[3] = { 16, 32, 48 };

bool RS2MutableProbeRequested(){
	return CheckArguments("-mutableprobe")!=FALSE || CheckArguments("-dx12mutablesmoke")!=FALSE;
}

static unsigned short RS2MPTexel(int a, int r, int g, int b){
	return (unsigned short)(((a&15)<<12)|((r&15)<<8)|((g&15)<<4)|(b&15));
}

//	What one texel shows on screen: white vertex colour, ALPHA blend over the
//	clear colour.
static void RS2MPExpected(unsigned short t, int *rgb){
	const int a = ((t>>12)&15)*17, c[3] = { ((t>>8)&15)*17, ((t>>4)&15)*17, (t&15)*17 };
	int i;

	for(i = 0; i<3; i++) rgb[i] = (c[i]*a+RS2_MP_BG[i]*(255-a)+127)/255;
}

static void RS2MPExpect(const char *name, int x, int y, const int *rgb, int tolerance){
	Debug("RS2MUTABLEPROBE|expect|%s|%d|%d|%d|%d|%d|%d\n", name, x, y, rgb[0], rgb[1], rgb[2], tolerance);
}

static void RS2MPStep(const char *name, bool passed, bool *all){
	Debug("RS2MUTABLEPROBE|%-34s|%s\n", name, passed ? "pass" : "FAIL");
	if(!passed) *all = false;
}

//	The contract texture: 64 x 64.  Rows 0-15 are 8 x 8 blocks of known
//	texels; rows 16-63 are a gradient, red = x / 4, green = y / 4, so a flip,
//	a mirror or a wrong pitch moves every value.
static const unsigned short s_Blocks[2][8] = {
	{ 0xFF00, 0xF0F0, 0xF00F, 0xFFFF, 0xF000, 0x0FFF, 0xF8C4, 0xF123 },
	{ 0x1FFF, 0x7FFF, 0x8FFF, 0xEFFF, 0x8F00, 0xF5A3, 0x0000, 0xF777 }
};
static const char *const s_BlockNames[2][8] = {
	{ "red", "green", "blue", "white", "black", "alpha0-white", "mixed-8c4", "nibbles-123" },
	{ "alpha1", "alpha7", "alpha8", "alpha14", "red-alpha8", "mixed-5a3", "alpha0-black", "grey-777" }
};

static unsigned short RS2MPContractTexel(int x, int y){
	if(y<16) return s_Blocks[y/8][x/8];
	return RS2MPTexel(15, x/4, y/4, 8);
}

static bool RS2MPWrite(CRS2TextureResource *t, int x0, int y0, int x1, int y1, unsigned short v){
	RS2TextureLock lock;

	if(!t || !t->Lock(&lock)) return false;

	int x, y;

	for(y = y0; y<y1; y++){
		unsigned short *row = (unsigned short *)((unsigned char *)lock.bits+lock.pitch*y);

		for(x = x0; x<x1; x++) row[x] = v;
	}
	t->Unlock();
	return true;
}

//	One region of the ordering texture, drawn at a screen rectangle.
static void RS2MPDrawRegion(CRS2TextureResource *t, int tx, int ty, int tw, int th,
	int x, int y, int w, int h){
	RS2BindTexture(0, t->GetRef());
	SetUVMap((float)tx/64.0f, (float)ty/64.0f, (float)(tx+tw)/64.0f, (float)(ty+th)/64.0f);
	TexMap2DRect(x, y, x+w, y+h, 0xffffffff);
}

static void RS2MPOrderExpect(const char *name, int x, int y, int w, int h, unsigned short v){
	int rgb[3];

	RS2MPExpected(v, rgb);
	RS2MPExpect(name, x+w/2, y+h/2, rgb, 1);
	RS2MPExpect(name, x+3, y+3, rgb, 1);
	RS2MPExpect(name, x+w-4, y+h-4, rgb, 1);
}

bool RS2MutableProbeRun(){
	const bool d3d12 = GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12;
	CRS2D3D12Backend *backend = d3d12 ? RS2D3D12GetActiveBackend() : 0;
	unsigned int width = 0, height = 0;
	bool ok = true;
	int i, x, y;

	GetRS2Renderer().GetViewportSize(&width, &height);
	Debug("RS2MUTABLEPROBE|begin|backend=%s|viewport=%ux%u\n",
		GetRS2Renderer().GetBackendName(), width, height);
	if(width!=640 || height!=480){
		Debug("RS2MUTABLEPROBE|expected a 640 x 480 viewport|FAIL\n");
		return false;
	}
	if(d3d12 && !backend) return false;
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	const unsigned int textureBaseline = d3d12 ? RS2D3D12_GetLiveTextureCount() : 0;
	const unsigned int descriptorBaseline = d3d12 ? backend->GetDescriptors()->GetLive() : 0;
	const RS2D3D12MutableStats mutableBaseline = RS2D3D12_GetMutableStats();

	//	---------------------------------------------------------- contract texture
	CRS2TextureResource *contract = RS2CreateMutableTexture(64, 64);
	CRS2TextureResource *order = RS2CreateMutableTexture(64, 64);

	RS2MPStep("create 64 x 64 mutable textures", contract && order
		&& contract->GetWidth()==64 && contract->GetHeight()==64, &ok);
	if(!contract || !order){
		RS2DestroyTexture(contract);
		RS2DestroyTexture(order);
		Debug("RS2MUTABLEPROBE|FAIL\n");
		return false;
	}

	{
		RS2TextureLock lock;
		const bool locked = contract->Lock(&lock);

		Debug("RS2MUTABLEPROBE|lock|bits=%s|pitch=%d|tight=%d\n", lock.bits ? "valid" : "null",
			lock.pitch, 64*2);
		RS2MPStep("first Lock", locked && lock.bits && lock.pitch>=128, &ok);
		if(locked){
			//	A second Lock of a locked texture: Direct3D 8 refuses it.
			RS2TextureLock again;
			const bool twice = contract->Lock(&again);

			Debug("RS2MUTABLEPROBE|doubleLock|result=%d\n", twice ? 1 : 0);
			RS2MPStep("second Lock refused", !twice, &ok);
			if(twice) contract->Unlock();
			for(y = 0; y<64; y++){
				unsigned short *row = (unsigned short *)((unsigned char *)lock.bits+lock.pitch*y);

				for(x = 0; x<64; x++) row[x] = RS2MPContractTexel(x, y);
			}
			contract->Unlock();
		}
		//	And an Unlock with nothing locked.
		contract->Unlock();
		Debug("RS2MUTABLEPROBE|unlockWithoutLock|survived\n");
	}

	//	Destroy while locked.
	{
		CRS2TextureResource *doomed = RS2CreateMutableTexture(32, 32);
		RS2TextureLock lock;
		const bool locked = doomed && doomed->Lock(&lock);

		RS2DestroyTexture(doomed);
		Debug("RS2MUTABLEPROBE|destroyWhileLocked|locked=%d|survived\n", locked ? 1 : 0);
	}

	//	The ordering texture starts magenta everywhere.
	RS2MPWrite(order, 0, 0, 64, 64, RS2MPTexel(15, 15, 0, 15));

	//	---------------------------------------------------------- text
	HFONT font = CreateFont(FONT_HEIGHT, 0, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE,
		SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
		VARIABLE_PITCH|FF_MODERN, NULL);
	CStringTexture *strings = new CStringTexture(FONT_HEIGHT, 1);

	strings->SetFont(font);
	Debug("RS2MUTABLEPROBE|textHeight|%d\n", RS2GetTextHeight());
	RS2MPStep("RS2GetTextHeight is the start-up height", RS2GetTextHeight()==FONT_HEIGHT, &ok);

	//	---------------------------------------------------------- expectations
	//	Contract texture at (16, 16), 4 pixels per texel.
	for(y = 0; y<2; y++){
		for(x = 0; x<8; x++){
			int rgb[3];

			RS2MPExpected(s_Blocks[y][x], rgb);
			RS2MPExpect(s_BlockNames[y][x], 16+x*32+16, 16+y*32+16, rgb, 1);
			RS2MPExpect(s_BlockNames[y][x], 16+x*32+5, 16+y*32+5, rgb, 1);
		}
	}
	{
		static const int points[][2] = {
			{ 0, 16 }, { 63, 16 }, { 0, 63 }, { 63, 63 }, { 31, 40 }, { 12, 55 }, { 50, 20 }
		};
		for(i = 0; i<(int)(sizeof(points)/sizeof(points[0])); i++){
			int rgb[3];
			char name[32];

			_snprintf(name, sizeof(name), "gradient-%d-%d", points[i][0], points[i][1]);
			name[sizeof(name)-1] = 0;
			RS2MPExpected(RS2MPContractTexel(points[i][0], points[i][1]), rgb);
			RS2MPExpect(name, 16+points[i][0]*4+2, 16+points[i][1]*4+2, rgb, 1);
		}
	}
	Debug("RS2MUTABLEPROBE|cell|contract-point|16|16|256|256\n");
	Debug("RS2MUTABLEPROBE|cell|contract-linear|288|16|128|128\n");

	//	Ordering cases at y = 300, 48 x 48 each.
	const unsigned short red = RS2MPTexel(15, 15, 0, 0), green = RS2MPTexel(15, 0, 15, 0);
	const unsigned short blue = RS2MPTexel(15, 0, 0, 15), yellow = RS2MPTexel(15, 15, 15, 0);
	const unsigned short cyan = RS2MPTexel(15, 0, 15, 15), magenta = RS2MPTexel(15, 15, 0, 15);
	const unsigned short frameColour[2] = { RS2MPTexel(15, 8, 4, 2), RS2MPTexel(15, 2, 12, 6) };

	RS2MPOrderExpect("same-frame-update", 16, 300, 48, 48, frameColour[(RS2_MP_FRAMES-1)&1]);
	RS2MPOrderExpect("update-draw-first", 80, 300, 48, 48, red);
	RS2MPOrderExpect("update-draw-second", 144, 300, 48, 48, green);
	RS2MPOrderExpect("back-to-back", 208, 300, 48, 48, yellow);
	RS2MPOrderExpect("partial-new", 272, 300, 24, 48, cyan);
	RS2MPOrderExpect("partial-kept", 296, 300, 24, 48, magenta);

	Debug("RS2MUTABLEPROBE|cell|strings|300|180|320|60\n");
	Debug("RS2MUTABLEPROBE|cell|livetext|300|380|320|60\n");

	//	---------------------------------------------------------- lifetime
	//	Mutable textures created, written, drawn and destroyed inside a frame
	//	that is still being recorded, frame after frame.  The frames below
	//	clear over them, so the final picture does not show them.
	int lifetimeFrame;
	if(d3d12){
		backend->WaitForGpu();
		backend->CollectRetiredTextures();
	}
	const unsigned int liveBeforeLoop = RS2D3D12_GetMutableStats().live;
	const unsigned int texturesBeforeLoop = d3d12 ? RS2D3D12_GetLiveTextureCount() : 0;
	const unsigned int descriptorsBeforeLoop = d3d12 ? backend->GetDescriptors()->GetLive() : 0;

	for(lifetimeFrame = 0; lifetimeFrame<RS2_MP_LIFETIME_FRAMES; lifetimeFrame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_MP_CLEAR, true)) break;
		RS2SetLighting(false);
		RS2SetDepthTest(false);
		RS2SetDepthWrite(false);
		RS2SetBlend(RS2_BLEND_ALPHA);
		RS2SetBaseTextureCombine();
		RS2SetTextureFilter(0, RS2_FILTER_POINT);
		for(i = 0; i<RS2_MP_LIFETIME_TEXTURES; i++){
			CRS2TextureResource *t = RS2CreateMutableTexture(16+i, 16);

			if(!t) continue;
			RS2MPWrite(t, 0, 0, 16, 16, RS2MPTexel(15, i&15, lifetimeFrame&15, 8));
			RS2MPDrawRegion(t, 0, 0, 16, 16, 16+i*20, 440, 16, 16);
			RS2DestroyTexture(t);
		}
		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
	}
	if(d3d12){
		const RS2D3D12MutableStats &now = RS2D3D12_GetMutableStats();

		backend->WaitForGpu();
		backend->CollectRetiredTextures();
		Debug("RS2MUTABLEPROBE|lifetime|frames=%d|created=%u|live=%u->%u\n", lifetimeFrame,
			now.creates-mutableBaseline.creates, liveBeforeLoop, now.live);
		RS2MPStep("lifetime loop", lifetimeFrame==RS2_MP_LIFETIME_FRAMES
			&& now.createFailures==mutableBaseline.createFailures
			&& now.live==liveBeforeLoop
			&& RS2D3D12_GetLiveTextureCount()==texturesBeforeLoop
			&& backend->GetDescriptors()->GetLive()==descriptorsBeforeLoop, &ok);
	}

	//	---------------------------------------------------------- frames
	const unsigned int drawBaseline = d3d12 ? RS2D3D12_GetDrawCount() : 0;
	const unsigned int refusedBaseline = d3d12 ? RS2D3D12_GetRefusedDrawCount() : 0;
	unsigned int pipelinesAfterFirst = 0;
	bool framesOk = true;
	int frame;

	for(frame = 0; frame<RS2_MP_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_MP_CLEAR, true)){
			framesOk = false;
			break;
		}
		RS2SetLighting(false);
		RS2SetDepthTest(false);
		RS2SetDepthWrite(false);
		RS2SetAlphaTest(false);
		RS2SetBlend(RS2_BLEND_ALPHA);
		RS2SetBaseTextureCombine();

		//	The contract texture, point-sampled, then linear.
		RS2SetTextureFilter(0, RS2_FILTER_POINT);
		RS2BindTexture(0, contract->GetRef());
		SetUVMap(0.0f, 0.0f, 1.0f, 1.0f);
		TexMap2DRect(16, 16, 16+256, 16+256, 0xffffffff);
		RS2SetTextureFilter(0, RS2_FILTER_LINEAR);
		TexMap2DRect(288, 16, 288+128, 16+128, 0xffffffff);
		RS2SetTextureFilter(0, RS2_FILTER_POINT);

		//	Update then draw, every frame, one region.
		RS2MPWrite(order, 0, 0, 16, 16, frameColour[frame&1]);
		RS2MPDrawRegion(order, 0, 0, 16, 16, 16, 300, 48, 48);

		//	Update, draw, update the same region, draw again.
		RS2MPWrite(order, 16, 0, 32, 16, red);
		RS2MPDrawRegion(order, 16, 0, 16, 16, 80, 300, 48, 48);
		RS2MPWrite(order, 16, 0, 32, 16, green);
		RS2MPDrawRegion(order, 16, 0, 16, 16, 144, 300, 48, 48);

		//	Two updates, no draw between: the second wins.
		RS2MPWrite(order, 32, 0, 48, 16, blue);
		RS2MPWrite(order, 32, 0, 48, 16, yellow);
		RS2MPDrawRegion(order, 32, 0, 16, 16, 208, 300, 48, 48);

		//	A partial update: the left half changes, the right half keeps
		//	what the texture has held since before the first frame.
		RS2MPWrite(order, 48, 0, 56, 16, cyan);
		RS2MPDrawRegion(order, 48, 0, 16, 16, 272, 300, 48, 48);

		//	Text, both ways.
		strings->RenderLeft(300, 180, 0xffffffff, 0, "RailSim II 2.15 text 0123456789");
		strings->RenderLeft(300, 200, 0xffffff00, 0xff000000, "\x95\xb6\x8e\x9a\x97\xf1 Shift-JIS");
		RS2DrawText(300, 380, 0xffffffff, "Live text RS2DrawText 0123456789");
		RS2DrawText(300, 400, 0xff80ff80, "\x95\xd2\x8f\x57\x92\x86 edit box");

		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
		if(frame==0 && backend) pipelinesAfterFirst = backend->GetPipelineStateCount();
	}

	//	---------------------------------------------------------- teardown
	delete strings;
	DeleteObject(font);
	RS2DestroyTexture(contract);
	RS2DestroyTexture(order);
	RS2SetLighting(true);
	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);

	ok = ok && framesOk;
	if(d3d12){
		backend->WaitForGpu();
		backend->CollectRetiredTextures();

		const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
		const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;
		const unsigned int errors = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_ERROR) : 0;
		const unsigned int warnings = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;

		RS2MPStep("draws submitted, none refused", submitted>0 && refused==0, &ok);
		RS2MPStep("pipelines stable after the first frame",
			backend->GetPipelineStateCount()==pipelinesAfterFirst, &ok);
		RS2MPStep("texture / descriptor baselines",
			RS2D3D12_GetLiveTextureCount()==textureBaseline
			&& backend->GetDescriptors()->GetLive()==descriptorBaseline, &ok);
		RS2MPStep("debug layer clean", errors==0 && warnings==0, &ok);

		//	Per frame: four regions of the ordering texture change and are
		//	drawn, the partial one no longer changes, and two lines of live
		//	text replace each other in one texture.
		const RS2D3D12MutableStats &m = RS2D3D12_GetMutableStats();
		const unsigned int uploads = m.uploads-mutableBaseline.uploads;

		RS2MPStep("mutable textures back to the baseline", m.live==mutableBaseline.live, &ok);
		RS2MPStep("locks balanced (one destroyed locked); misuse counted",
			m.locks-mutableBaseline.locks==m.unlocks-mutableBaseline.unlocks
				+(m.destroyedLocked-mutableBaseline.destroyedLocked)
			&& m.refusedLocks-mutableBaseline.refusedLocks==1
			&& m.unlocksWithoutLock-mutableBaseline.unlocksWithoutLock==1
			&& m.destroyedLocked-mutableBaseline.destroyedLocked==1, &ok);
		RS2MPStep("one upload per changed region and draw",
			uploads>=(unsigned int)(6*RS2_MP_FRAMES)
			&& m.coalescedUnlocks-mutableBaseline.coalescedUnlocks>=(unsigned int)RS2_MP_FRAMES
			&& m.unchangedUnlocks>mutableBaseline.unchangedUnlocks
			&& m.refusedUploads==mutableBaseline.refusedUploads, &ok);
		Debug("RS2MUTABLEPROBE|mutable|creates=%u|failures=%u|live=%u|peak=%u\n",
			m.creates-mutableBaseline.creates, m.createFailures-mutableBaseline.createFailures,
			m.live, m.peak);
		Debug("RS2MUTABLEPROBE|mutable|locks=%u|unlocks=%u|refusedLocks=%u\n",
			m.locks-mutableBaseline.locks, m.unlocks-mutableBaseline.unlocks,
			m.refusedLocks-mutableBaseline.refusedLocks);
		Debug("RS2MUTABLEPROBE|mutable|unlocksWithoutLock=%u|destroyedLocked=%u\n",
			m.unlocksWithoutLock-mutableBaseline.unlocksWithoutLock,
			m.destroyedLocked-mutableBaseline.destroyedLocked);
		Debug("RS2MUTABLEPROBE|mutable|uploads=%u|coalesced=%u|unchanged=%u|refused=%u\n",
			uploads, m.coalescedUnlocks-mutableBaseline.coalescedUnlocks,
			m.unchangedUnlocks-mutableBaseline.unchangedUnlocks,
			m.refusedUploads-mutableBaseline.refusedUploads);
		Debug("RS2MUTABLEPROBE|mutable|bytes=%u|largest=%u\n",
			(unsigned int)(m.uploadBytes-mutableBaseline.uploadBytes), m.largestUpload);
		Debug("RS2MUTABLEPROBE|draws=%u|refused=%u|pso=%u->%u\n", submitted, refused,
			pipelinesAfterFirst, backend->GetPipelineStateCount());
		Debug("RS2MUTABLEPROBE|textures=%u->%u|descriptors=%u->%u\n", textureBaseline,
			RS2D3D12_GetLiveTextureCount(), descriptorBaseline, backend->GetDescriptors()->GetLive());
		Debug("RS2MUTABLEPROBE|debug=%u|errors=%u|warnings=%u\n",
			backend->HasDebugLayer() ? 1u : 0u, errors, warnings);
	}

	Debug("RS2MUTABLEPROBE|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2_MP_HOLD_MS);
	return ok;
}
