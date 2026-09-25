//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	See RS2ShadowProbe.h.
//
//	The camera is identity: world, view and projection are all the unit
//	matrix, so a vertex's x and y are clip space and its z is the depth the
//	depth test sees.  Every cell has a receiver at z 0.5 written to depth, and
//	shadow-volume quads in front of it (z < 0.5) or behind it (z > 0.5).
//
//	A quad is "front" when culling counter-clockwise keeps it: clockwise when
//	its clip-space corners are listed with y pointing up (found by the first
//	run of this probe, which had it the other way round and saw every
//	receiver culled).  A volume is drawn twice, as CShadowVolume draws it: culling CCW
//	with the pass op INCR, then culling CW with DECR.  The stencil buffer is
//	cleared to 0 with every pass, on both backends.

#include "stdafx.h"
#include "RS2ShadowProbe.h"
#include "RS2Renderer.h"
#include "RS2Draw.h"
#include "RS2MeshData.h"
#include "RS2RenderState.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Backend.h"

#include <stdio.h>
#include <vector>

extern bool g_StencilEnabled;

static const int RS2_SP_FRAMES = 60;
static const DWORD RS2_SP_HOLD_MS = 8000;
static const unsigned int RS2_SP_CLEAR = 0x00102030;
static const unsigned int RS2_SP_RECEIVER = 0xffc8c8c8;	//	(200, 200, 200)
static const unsigned int RS2_SP_MARKER = 0xff28a03c;	//	(40, 160, 60)
static const unsigned int RS2_SP_SHADOW = 0x80000000;	//	what the real overlay used

//	Cells: 4 x 3, 150 x 140 pixels.  Regions: three per cell, 40 x 80.
static const int RS2_SP_CELL_W = 150, RS2_SP_CELL_H = 140;
static const int RS2_SP_REGION_X[3] = { 8, 55, 102 };
static const int RS2_SP_REGION_Y = 30, RS2_SP_REGION_W = 40, RS2_SP_REGION_H = 80;

bool RS2ShadowProbeRequested(){
	return CheckArguments("-shadowprobe")!=FALSE;
}

enum RS2SPResult { RS2_SP_LIT, RS2_SP_SHADOW_, RS2_SP_GREEN };

struct RS2SPCell
{
	const char *name;
	RS2SPResult expect[3];
};

static const RS2SPCell s_Cells[12] = {
	{ "baseline",         { RS2_SP_LIT,     RS2_SP_LIT,     RS2_SP_LIT } },
	{ "zpass",            { RS2_SP_SHADOW_, RS2_SP_LIT,     RS2_SP_LIT } },
	{ "count-ref2",       { RS2_SP_SHADOW_, RS2_SP_LIT,     RS2_SP_LIT } },
	{ "func-greater",     { RS2_SP_LIT,     RS2_SP_SHADOW_, RS2_SP_SHADOW_ } },
	{ "read-mask",        { RS2_SP_LIT,     RS2_SP_SHADOW_, RS2_SP_LIT } },
	{ "write-mask",       { RS2_SP_LIT,     RS2_SP_SHADOW_, RS2_SP_LIT } },
	{ "depth-fail-op",    { RS2_SP_SHADOW_, RS2_SP_LIT,     RS2_SP_LIT } },
	{ "fail-op",          { RS2_SP_SHADOW_, RS2_SP_LIT,     RS2_SP_LIT } },
	{ "decrement-wraps",  { RS2_SP_SHADOW_, RS2_SP_LIT,     RS2_SP_LIT } },
	{ "no-leak-after",    { RS2_SP_SHADOW_, RS2_SP_GREEN,   RS2_SP_GREEN } },
	{ "real-sequence",    { RS2_SP_SHADOW_, RS2_SP_LIT,     RS2_SP_LIT } },
	{ "overlay-depth",    { RS2_SP_SHADOW_, RS2_SP_LIT,     RS2_SP_LIT } }
};

////////////////////////////////////////////////////////////////////////////////
//	Geometry
////////////////////////////////////////////////////////////////////////////////

static int s_CellX, s_CellY;	//	top-left pixel of the cell being drawn

static float RS2SPClipX(int px){ return (float)px/320.0f-1.0f; }
static float RS2SPClipY(int py){ return 1.0f-(float)py/240.0f; }

static void RS2SPCellOrigin(int cell){
	s_CellX = 10+(cell%4)*158;
	s_CellY = 20+(cell/4)*153;
}

static void RS2SPRegionRect(int region, int *x0, int *y0, int *x1, int *y1){
	*x0 = s_CellX+RS2_SP_REGION_X[region];
	*y0 = s_CellY+RS2_SP_REGION_Y;
	*x1 = *x0+RS2_SP_REGION_W;
	*y1 = *y0+RS2_SP_REGION_H;
}

//	Two triangles, position only, clockwise on screen when front is true.
static void RS2SPQuad(std::vector<float> &out, int x0, int y0, int x1, int y1, float z, bool front){
	const float l = RS2SPClipX(x0), r = RS2SPClipX(x1);
	const float t = RS2SPClipY(y0), b = RS2SPClipY(y1);
	const float ccw[6][2] = { { l, b }, { r, b }, { r, t }, { l, b }, { r, t }, { l, t } };
	int i;

	for(i = 0; i<6; i++){
		const int k = front ? 5-i : i;

		out.push_back(ccw[k][0]);
		out.push_back(ccw[k][1]);
		out.push_back(z);
	}
}

static void RS2SPRegionQuad(std::vector<float> &out, int region, float z, bool front){
	int x0, y0, x1, y1;

	RS2SPRegionRect(region, &x0, &y0, &x1, &y1);
	RS2SPQuad(out, x0, y0, x1, y1, z, front);
}

static void RS2SPDrawVolume(const std::vector<float> &v){
	if(v.empty()) return;
	RS2DrawImmediate(RS2LayoutPositionOnly(), RS2_PRIMITIVE_TRIANGLE_LIST,
		&v[0], (unsigned int)(v.size()/3));
}

//	A coloured quad, pipeline-transformed, the way scene geometry arrives.
static void RS2SPSolid(int x0, int y0, int x1, int y1, float z, unsigned int colour){
	const float l = RS2SPClipX(x0), r = RS2SPClipX(x1);
	const float t = RS2SPClipY(y0), b = RS2SPClipY(y1);
	const VTX_L v[6] = {
		{ l, t, z, colour }, { r, t, z, colour }, { l, b, z, colour },
		{ r, t, z, colour }, { r, b, z, colour }, { l, b, z, colour }
	};

	RS2DrawImmediate(RS2LayoutL(), RS2_PRIMITIVE_TRIANGLE_LIST, v, 6);
}

//	The overlay as Fill2DRect draws it, but at a chosen screen depth.
static void RS2SPOverlayAt(float z){
	const float x1 = (float)s_CellX-0.5f, y1 = (float)s_CellY-0.5f;
	const float x2 = (float)(s_CellX+RS2_SP_CELL_W)-0.5f, y2 = (float)(s_CellY+RS2_SP_CELL_H)-0.5f;
	const VTX_TL vt[4] = {
		{ x1, y2, z, 1.0f, RS2_SP_SHADOW }, { x1, y1, z, 1.0f, RS2_SP_SHADOW },
		{ x2, y1, z, 1.0f, RS2_SP_SHADOW }, { x2, y2, z, 1.0f, RS2_SP_SHADOW }
	};

	RS2DrawImmediate(RS2LayoutTL(), RS2_PRIMITIVE_TRIANGLE_FAN, vt, 4);
}

////////////////////////////////////////////////////////////////////////////////
//	The shadow sequence, as CShadowVolume::Render / ::Draw issue it
////////////////////////////////////////////////////////////////////////////////

//	Render()'s set-up, call for call.  The pass-A culling is inherited.
static void RS2SPVolumeBegin(){
	RS2SetDepthTest(true);
	RS2SetDepthWrite(false);
	RS2SetStencilTest(true);
	RS2SetShadeMode(RS2_SHADE_FLAT);
	RS2SetStencilFunc(RS2_COMPARE_ALWAYS);
	RS2SetStencilDepthFailOp(RS2_STENCIL_KEEP);
	RS2SetStencilFailOp(RS2_STENCIL_KEEP);
	RS2SetStencilRef(1);
	RS2SetStencilReadMask(0xffffffff);
	RS2SetStencilWriteMask(0xffffffff);
	RS2SetStencilPassOp(RS2_STENCIL_INCREMENT);
	RS2SetBlend(RS2_BLEND_COLOR_PRESERVE);
}

static void RS2SPVolumeBack(){
	RS2SetStencilPassOp(RS2_STENCIL_DECREMENT);
	RS2SetCullMode(RS2_CULL_CLOCKWISE);
}

static void RS2SPVolumeEnd(){
	RS2SetShadeMode(RS2_SHADE_GOURAUD);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2SetDepthWrite(true);
	RS2SetStencilTest(false);
	RS2SetBlend(RS2_BLEND_DISABLED);
}

//	The whole of Render(): the same volume, twice, culling reversed.
static void RS2SPVolume(const std::vector<float> &v){
	RS2SPVolumeBegin();
	RS2SPDrawVolume(v);
	RS2SPVolumeBack();
	RS2SPDrawVolume(v);
	RS2SPVolumeEnd();
}

//	Draw()'s set-up around the overlay.
static void RS2SPOverlayBegin(unsigned int ref, RS2CompareFunc func){
	RS2SetDepthTest(false);
	RS2SetStencilTest(true);
	RS2DisableFog();
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2SetBaseTextureCombine();
	RS2SetStencilRef(ref);
	RS2SetStencilFunc(func);
	RS2SetStencilPassOp(RS2_STENCIL_KEEP);
}

static void RS2SPOverlayEnd(){
	RS2SetDepthTest(true);
	RS2SetStencilTest(false);
	RS2SetBlend(RS2_BLEND_DISABLED);
}

static void RS2SPOverlay(unsigned int ref, RS2CompareFunc func){
	RS2SPOverlayBegin(ref, func);
	Fill2DRect(s_CellX, s_CellY, s_CellX+RS2_SP_CELL_W, s_CellY+RS2_SP_CELL_H, RS2_SP_SHADOW);
	RS2SPOverlayEnd();
}

////////////////////////////////////////////////////////////////////////////////
//	Cases
////////////////////////////////////////////////////////////////////////////////

static unsigned int s_PsoBeforeRef2, s_PsoAfterRef2;

static void RS2SPCase(int cell, CRS2D3D12Backend *backend, bool firstFrame){
	std::vector<float> v;

	switch(cell){
	case 0:		//	no stencil at all
		break;

	case 1:		//	z-pass: closed in front of the receiver, open across it, behind it
		RS2SPRegionQuad(v, 0, 0.3f, true);  RS2SPRegionQuad(v, 0, 0.7f, false);
		RS2SPRegionQuad(v, 1, 0.3f, true);  RS2SPRegionQuad(v, 1, 0.4f, false);
		RS2SPRegionQuad(v, 2, 0.6f, true);  RS2SPRegionQuad(v, 2, 0.7f, false);
		RS2SPVolume(v);
		RS2SPOverlay(1, RS2_COMPARE_LESS_EQUAL);
		break;

	case 2:		//	two front faces count 2; the overlay asks for ref 2
		RS2SPRegionQuad(v, 0, 0.2f, true);  RS2SPRegionQuad(v, 0, 0.3f, true);
		RS2SPRegionQuad(v, 1, 0.3f, true);
		RS2SPVolume(v);
		if(firstFrame && backend) s_PsoBeforeRef2 = backend->GetPipelineStateCount();
		RS2SPOverlay(2, RS2_COMPARE_LESS_EQUAL);
		if(firstFrame && backend) s_PsoAfterRef2 = backend->GetPipelineStateCount();
		break;

	case 3:		//	GREATER: shade where 1 > stencil
		RS2SPRegionQuad(v, 0, 0.3f, true);
		RS2SPVolume(v);
		RS2SPOverlay(1, RS2_COMPARE_GREATER);
		break;

	case 4:		//	read mask 0x01 against stencil 2, 1, 0
		RS2SPRegionQuad(v, 0, 0.2f, true);  RS2SPRegionQuad(v, 0, 0.3f, true);
		RS2SPRegionQuad(v, 1, 0.3f, true);
		RS2SPVolume(v);
		RS2SPOverlayBegin(1, RS2_COMPARE_LESS_EQUAL);
		RS2SetStencilReadMask(0x01);
		Fill2DRect(s_CellX, s_CellY, s_CellX+RS2_SP_CELL_W, s_CellY+RS2_SP_CELL_H, RS2_SP_SHADOW);
		RS2SetStencilReadMask(0xffffffff);
		RS2SPOverlayEnd();
		break;

	case 5:{	//	write masks 0x00, 0xff, 0x02 on one increment each
		std::vector<float> a, b, c;

		RS2SPRegionQuad(a, 0, 0.3f, true);
		RS2SPRegionQuad(b, 1, 0.3f, true);
		RS2SPRegionQuad(c, 2, 0.3f, true);
		RS2SPVolumeBegin();
		RS2SetStencilWriteMask(0x00);	RS2SPDrawVolume(a);
		RS2SetStencilWriteMask(0xff);	RS2SPDrawVolume(b);
		RS2SetStencilWriteMask(0x02);	RS2SPDrawVolume(c);
		RS2SetStencilWriteMask(0xffffffff);
		RS2SPVolumeEnd();
		RS2SPOverlay(1, RS2_COMPARE_LESS_EQUAL);
		break;
	}

	case 6:		//	depth-fail op: count what is hidden behind the receiver
		RS2SPRegionQuad(v, 0, 0.7f, true);
		RS2SPRegionQuad(v, 1, 0.3f, true);
		RS2SPVolumeBegin();
		RS2SetStencilPassOp(RS2_STENCIL_KEEP);
		RS2SetStencilDepthFailOp(RS2_STENCIL_INCREMENT);
		RS2SPDrawVolume(v);
		RS2SetStencilDepthFailOp(RS2_STENCIL_KEEP);
		RS2SPVolumeEnd();
		RS2SPOverlay(1, RS2_COMPARE_LESS_EQUAL);
		break;

	case 7:{	//	fail op: a comparison that fails (0 > 0) increments
		std::vector<float> a, b;

		RS2SPRegionQuad(a, 0, 0.3f, true);
		RS2SPRegionQuad(b, 1, 0.3f, true);
		RS2SPVolumeBegin();
		RS2SetStencilPassOp(RS2_STENCIL_KEEP);
		RS2SetStencilFailOp(RS2_STENCIL_INCREMENT);
		RS2SetStencilRef(0);
		RS2SetStencilFunc(RS2_COMPARE_GREATER);
		RS2SPDrawVolume(a);
		RS2SetStencilFunc(RS2_COMPARE_ALWAYS);
		RS2SPDrawVolume(b);
		RS2SetStencilFailOp(RS2_STENCIL_KEEP);
		RS2SPVolumeEnd();
		RS2SPOverlay(1, RS2_COMPARE_LESS_EQUAL);
		break;
	}

	case 8:		//	a back face alone takes 0 to 255, which is >= 1
		RS2SPRegionQuad(v, 0, 0.3f, false);
		RS2SPRegionQuad(v, 1, 0.3f, true);  RS2SPRegionQuad(v, 1, 0.4f, false);
		RS2SPVolume(v);
		RS2SPOverlay(1, RS2_COMPARE_LESS_EQUAL);
		break;

	case 9:{	//	after the sequence, ordinary draws ignore the stencil left behind
		int x0, y0, x1, y1;

		RS2SPRegionQuad(v, 0, 0.3f, true);  RS2SPRegionQuad(v, 0, 0.7f, false);
		RS2SPRegionQuad(v, 2, 0.3f, true);  RS2SPRegionQuad(v, 2, 0.7f, false);
		RS2SPVolume(v);
		RS2SPOverlay(1, RS2_COMPARE_LESS_EQUAL);
		RS2SPRegionRect(1, &x0, &y0, &x1, &y1);
		RS2SPSolid(x0, y0, x1, y1, 0.4f, RS2_SP_MARKER);
		RS2SPRegionRect(2, &x0, &y0, &x1, &y1);
		RS2SPSolid(x0, y0, x1, y1, 0.4f, RS2_SP_MARKER);
		break;
	}

	case 10:	//	Render() and Draw() exactly, nothing added
		RS2SPRegionQuad(v, 0, 0.3f, true);  RS2SPRegionQuad(v, 0, 0.7f, false);
		RS2SPRegionQuad(v, 1, 0.3f, true);  RS2SPRegionQuad(v, 1, 0.4f, false);
		RS2SPRegionQuad(v, 2, 0.6f, true);  RS2SPRegionQuad(v, 2, 0.7f, false);
		RS2BindTexture(0, RS2TextureRef());
		RS2SPVolume(v);
		RS2SPOverlay(1, RS2_COMPARE_LESS_EQUAL);
		break;

	case 11:	//	the overlay sits behind the receiver: only depth off draws it
		RS2SPRegionQuad(v, 0, 0.3f, true);  RS2SPRegionQuad(v, 0, 0.7f, false);
		RS2SPRegionQuad(v, 1, 0.3f, true);  RS2SPRegionQuad(v, 1, 0.7f, false);
		RS2SPVolume(v);
		{
			//	Region 0 with depth off, as the real overlay; region 1 with
			//	depth on, which the receiver in front then hides.  The two
			//	overlays cover only their own regions.
			int x0, y0, x1, y1;

			RS2SPOverlayBegin(1, RS2_COMPARE_LESS_EQUAL);
			RS2SPRegionRect(0, &x0, &y0, &x1, &y1);
			{
				const VTX_TL vt[4] = {
					{ x0-0.5f, y1-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW }, { x0-0.5f, y0-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW },
					{ x1-0.5f, y0-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW }, { x1-0.5f, y1-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW }
				};
				RS2DrawImmediate(RS2LayoutTL(), RS2_PRIMITIVE_TRIANGLE_FAN, vt, 4);
			}
			RS2SetDepthTest(true);
			RS2SPRegionRect(1, &x0, &y0, &x1, &y1);
			{
				const VTX_TL vt[4] = {
					{ x0-0.5f, y1-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW }, { x0-0.5f, y0-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW },
					{ x1-0.5f, y0-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW }, { x1-0.5f, y1-0.5f, 0.99f, 1.0f, RS2_SP_SHADOW }
				};
				RS2DrawImmediate(RS2LayoutTL(), RS2_PRIMITIVE_TRIANGLE_FAN, vt, 4);
			}
			RS2SPOverlayEnd();
		}
		break;
	}
}

static void RS2SPIdentity(float *m){
	int i;

	for(i = 0; i<16; i++) m[i] = 0.0f;
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

////////////////////////////////////////////////////////////////////////////////
//	The probe
////////////////////////////////////////////////////////////////////////////////

bool RS2ShadowProbeRun(){
	const bool d3d12 = GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12;
	CRS2D3D12Backend *backend = d3d12 ? RS2D3D12GetActiveBackend() : 0;
	unsigned int width = 0, height = 0;
	int cell, region, frame;

	GetRS2Renderer().GetViewportSize(&width, &height);
	Debug("RS2SHADOWPROBE|begin|backend=%s|viewport=%ux%u|stencil=%d\n",
		GetRS2Renderer().GetBackendName(), width, height, g_StencilEnabled ? 1 : 0);
	if(width!=640 || height!=480){
		Debug("RS2SHADOWPROBE|expected a 640 x 480 viewport|FAIL\n");
		return false;
	}
	if(d3d12 && !backend) return false;
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	//	Expectations: the centre of each region and four points around it.
	for(cell = 0; cell<12; cell++){
		RS2SPCellOrigin(cell);
		Debug("RS2SHADOWPROBE|cell|%d|%s|x=%d|y=%d|w=%d|h=%d\n", cell, s_Cells[cell].name,
			s_CellX, s_CellY, RS2_SP_CELL_W, RS2_SP_CELL_H);
		for(region = 0; region<3; region++){
			int x0, y0, x1, y1, e;
			const RS2SPResult r = s_Cells[cell].expect[region];
			const int rgb[3][3] = { { 200, 200, 200 }, { 100, 100, 100 }, { 40, 160, 60 } };
			const int tolerance = r==RS2_SP_SHADOW_ ? 2 : 1;

			RS2SPRegionRect(region, &x0, &y0, &x1, &y1);
			const int cx = (x0+x1)/2, cy = (y0+y1)/2;
			const int off[5][2] = { { 0, 0 }, { -12, -25 }, { 12, -25 }, { -12, 25 }, { 12, 25 } };

			for(e = 0; e<5; e++){
				Debug("RS2SHADOWPROBE|expect|%s-%c|%d|%d|%d|%d|%d|%d\n", s_Cells[cell].name,
					"LMR"[region], cx+off[e][0], cy+off[e][1],
					rgb[r][0], rgb[r][1], rgb[r][2], tolerance);
			}
		}
		//	Between the regions the stencil is 0: shadowed only where the
		//	overlay passes on 0, which is the GREATER cell alone.
		{
			const int gap = cell==3 ? 100 : 200;

			Debug("RS2SHADOWPROBE|expect|%s-gap|%d|%d|%d|%d|%d|%d\n", s_Cells[cell].name,
				s_CellX+RS2_SP_REGION_X[1]-4, s_CellY+RS2_SP_REGION_Y+40,
				gap, gap, gap, cell==3 ? 2 : 1);
		}
	}

	const unsigned int drawBaseline = d3d12 ? RS2D3D12_GetDrawCount() : 0;
	const unsigned int refusedBaseline = d3d12 ? RS2D3D12_GetRefusedDrawCount() : 0;
	const RS2D3D12StencilStats statsBefore = d3d12 ? RS2D3D12_GetStencilStats() : RS2D3D12StencilStats();
	unsigned int pipelinesBefore = backend ? backend->GetPipelineStateCount() : 0;
	unsigned int pipelinesAfterFirst = 0, drawsFirst = 0;
	bool framesOk = true;

	for(frame = 0; frame<RS2_SP_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_SP_CLEAR, true)){
			framesOk = false;
			break;
		}

		float identity[16];

		RS2SPIdentity(identity);
		RS2SetWorldTransform(identity);
		RS2SetViewTransform(identity);
		RS2SetProjectionTransform(identity);
		RS2SetLighting(false);
		RS2SetAlphaTest(false);
		RS2SetDepthTest(true);
		RS2SetDepthWrite(true);
		RS2SetDepthFunc(RS2_COMPARE_LESS_EQUAL);
		RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
		RS2SetBlend(RS2_BLEND_DISABLED);
		RS2BindTexture(0, RS2TextureRef());

		//	The scene: one receiver per cell.
		for(cell = 0; cell<12; cell++){
			RS2SPCellOrigin(cell);
			RS2SPSolid(s_CellX, s_CellY, s_CellX+RS2_SP_CELL_W, s_CellY+RS2_SP_CELL_H,
				0.5f, RS2_SP_RECEIVER);
		}
		//	Then each cell's shadow case, one after another in the same pass.
		for(cell = 0; cell<12; cell++){
			RS2SPCellOrigin(cell);
			RS2SPCase(cell, backend, frame==0);
		}

		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
		if(frame==0 && backend){
			pipelinesAfterFirst = backend->GetPipelineStateCount();
			drawsFirst = RS2D3D12_GetDrawCount()-drawBaseline;
		}
	}

	RS2SetLighting(true);
	RS2SetBlend(RS2_BLEND_ALPHA);

	bool ok = framesOk;

	if(d3d12 && backend){
		backend->WaitForGpu();

		const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
		const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;
		const unsigned int pipelines = backend->GetPipelineStateCount();
		const RS2D3D12StencilStats &s = RS2D3D12_GetStencilStats();
		const unsigned int errors = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_ERROR) : 0;
		const unsigned int warnings = backend->HasDebugLayer()
			? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;
		const bool refOk = s_PsoAfterRef2==s_PsoBeforeRef2;

		Debug("RS2SHADOWPROBE|submitted=%u|perFrame=%u|refused=%u\n", submitted, drawsFirst, refused);
		Debug("RS2SHADOWPROBE|pipelines=%u->%u->%u|stencilPso=%u|%s\n", pipelinesBefore,
			pipelinesAfterFirst, pipelines, backend->GetPipeline()->GetStencilStateCount(),
			pipelines==pipelinesAfterFirst ? "pass" : "FAIL");
		Debug("RS2SHADOWPROBE|refChangeBuildsNoPso|%u->%u|%s\n", s_PsoBeforeRef2, s_PsoAfterRef2,
			refOk ? "pass" : "FAIL");
		Debug("RS2SHADOWPROBE|stencilDraws=%u|volume=%u|overlay=%u|refChanges=%u\n",
			s.stencilDraws-statsBefore.stencilDraws, s.volumeDraws-statsBefore.volumeDraws,
			s.overlayDraws-statsBefore.overlayDraws, s.refChanges-statsBefore.refChanges);
		Debug("RS2SHADOWPROBE|debug=%u|errors=%u|warnings=%u\n",
			backend->HasDebugLayer() ? 1u : 0u, errors, warnings);
		ok = ok && refused==0 && submitted==drawsFirst*(unsigned int)RS2_SP_FRAMES
			&& pipelines==pipelinesAfterFirst && refOk && errors==0 && warnings==0;
	}

	Debug("RS2SHADOWPROBE|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2_SP_HOLD_MS);
	return ok;
}
