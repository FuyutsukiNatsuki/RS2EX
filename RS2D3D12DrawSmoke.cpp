//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	The first visible Direct3D 12 draw, through the public boundary.
//
//	-dx12drawsmoke deliberately uses RS2DrawImmediate and the RS2 transform
//	functions, the same ones the game calls, rather than reaching for the
//	backend's command list.  A test that recorded a draw directly would prove
//	Direct3D 12 works and nothing about whether RS2 geometry can reach it,
//	which is the actual question this release asks.
//
//	It needs the renderer to be running Direct3D 12, so it runs after the
//	renderer is up and refuses if Direct3D 8 is the active backend.

#include "stdafx.h"
#include "RS2D3D12DrawSmoke.h"
#include "RS2D3D12Draw.h"
#include "RS2Renderer.h"
#include "RS2Draw.h"
#include "RS2MeshData.h"

//	Long enough to photograph without racing the exit, the same reasoning as
//	the bootstrap smoke.
static const int RS2D3D12_DRAW_FRAMES = 300;

//	A background nothing else in the program uses, so a screenshot cannot be
//	mistaken for a real scene or for an empty one.
static const unsigned int RS2D3D12_DRAW_CLEAR = 0x00202840;

//	Fully opaque and far apart, so what arrives on screen identifies both which
//	primitive drew it and whether the packed colour was read in the right byte
//	order.  0xAARRGGBB, as everywhere else in the engine.
static const unsigned int RS2D3D12_DRAW_RED = 0xffcc2020;
static const unsigned int RS2D3D12_DRAW_BLUE = 0xff2040cc;
static const unsigned int RS2D3D12_DRAW_GREEN = 0xff20cc40;

struct RS2DrawSmokeVertexP
{
	float x, y, z;
	unsigned int diffuse;
};

struct RS2DrawSmokeVertexS
{
	float x, y, z, rhw;
	unsigned int diffuse;
};

bool RS2D3D12DrawSmokeRequested(){
	return CheckArguments("-dx12drawsmoke")!=FALSE;
}

/*
 *	The layout for pipeline-transformed vertices: position and a packed colour.
 */
static void RS2DrawSmokeLayoutP(RS2MeshVertexLayout *layout){
	layout->Clear();
	layout->stride = sizeof(RS2DrawSmokeVertexP);
	layout->positionOffset = 0;
	layout->positionSemantic = RS2_POSITION_TRANSFORMED_BY_PIPELINE;
	layout->diffuseOffset = (int)(sizeof(float)*3);
}

/*
 *	The layout for already-transformed vertices, the XYZRHW equivalent.
 */
static void RS2DrawSmokeLayoutS(RS2MeshVertexLayout *layout){
	layout->Clear();
	layout->stride = sizeof(RS2DrawSmokeVertexS);
	layout->positionOffset = 0;
	layout->positionSemantic = RS2_POSITION_ALREADY_TRANSFORMED;
	layout->diffuseOffset = (int)(sizeof(float)*4);
}

static void RS2DrawSmokeIdentity(float *m){
	int i;

	for(i = 0; i<16; i++) m[i] = 0.0f;
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

bool RS2D3D12DrawSmokeRun(){
	if(GetRS2Renderer().GetBackendType()!=RS2_RENDERER_D3D12){
		Debug("RS2D3D12DRAW|this test needs -dx12; the active backend is %s\n",
			GetRS2Renderer().GetBackendName());
		return false;
	}

	//	Identity everywhere, so the vertices below are already in clip space.
	//	The matrix path is still exercised - the shader multiplies by it - but
	//	the test data stays readable, and a wrong matrix convention would show
	//	up as geometry in the wrong place rather than as arithmetic nobody can
	//	check by eye.
	float identity[16];

	RS2DrawSmokeIdentity(identity);
	RS2SetWorldTransform(identity);
	RS2SetViewTransform(identity);
	RS2SetProjectionTransform(identity);

	RS2MeshVertexLayout layoutP, layoutS;

	RS2DrawSmokeLayoutP(&layoutP);
	RS2DrawSmokeLayoutS(&layoutS);

	unsigned int width = 0, height = 0;

	GetRS2Renderer().GetViewportSize(&width, &height);
	Debug("RS2D3D12DRAW|viewport %u x %u\n", width, height);

	//	Where the window is, so a capture harness can find it without asking
	//	the operating system which window belongs to this process - an answer
	//	that proved unreliable for a window shown late and closed seconds later.
	{
		RECT rect;

		GetWindowRect(svw.hWnd, &rect);
		Debug("RS2D3D12DRAW|window %ld x %ld at %ld,%ld\n",
			rect.right-rect.left, rect.bottom-rect.top, rect.left, rect.top);
	}

	//	Two triangles in clip space, wound opposite ways, side by side.  Which
	//	one survives says what the default cull mode does, and that is worth
	//	establishing by looking rather than by reasoning about two libraries'
	//	opposite conventions.
	const RS2DrawSmokeVertexP clockwise[3] = {
		{ -0.90f,  0.70f, 0.5f, RS2D3D12_DRAW_RED },
		{ -0.30f,  0.70f, 0.5f, RS2D3D12_DRAW_RED },
		{ -0.60f, -0.40f, 0.5f, RS2D3D12_DRAW_RED }
	};

	const RS2DrawSmokeVertexP counterClockwise[3] = {
		{  0.30f,  0.70f, 0.5f, RS2D3D12_DRAW_BLUE },
		{  0.00f, -0.40f, 0.5f, RS2D3D12_DRAW_BLUE },
		{  0.60f, -0.40f, 0.5f, RS2D3D12_DRAW_BLUE }
	};

	//	A screen-space quad as a triangle fan: it exercises the already
	//	transformed path and the fan expansion at once, and Direct3D 12 has no
	//	fan topology, so if the expansion is wrong this comes out as a single
	//	triangle or nothing at all.
	const float left = (float)width*0.10f;
	const float right = (float)width*0.45f;
	const float top = (float)height*0.70f;
	const float bottom = (float)height*0.92f;

	const RS2DrawSmokeVertexS quad[4] = {
		{ left,  top,    0.5f, 1.0f, RS2D3D12_DRAW_GREEN },
		{ right, top,    0.5f, 1.0f, RS2D3D12_DRAW_GREEN },
		{ right, bottom, 0.5f, 1.0f, RS2D3D12_DRAW_GREEN },
		{ left,  bottom, 0.5f, 1.0f, RS2D3D12_DRAW_GREEN }
	};

	int frame;

	for(frame = 0; frame<RS2D3D12_DRAW_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2D3D12_DRAW_CLEAR, true)){
			Debug("RS2D3D12DRAW|the render pass would not begin\n");
			return false;
		}

		RS2DrawImmediate(layoutP, RS2_PRIMITIVE_TRIANGLE_LIST, clockwise, 3);
		RS2DrawImmediate(layoutP, RS2_PRIMITIVE_TRIANGLE_LIST, counterClockwise, 3);
		RS2DrawImmediate(layoutS, RS2_PRIMITIVE_TRIANGLE_FAN, quad, 4);

		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
	}

	Debug("RS2D3D12DRAW|submitted=%u refused=%u\n",
		RS2D3D12_GetDrawCount(), RS2D3D12_GetRefusedDrawCount());

	//	Three draws a frame, every frame, or something refused them.
	const bool ok = RS2D3D12_GetDrawCount()>=(unsigned int)(RS2D3D12_DRAW_FRAMES*3)
		&& RS2D3D12_GetRefusedDrawCount()==0;

	Debug("RS2D3D12DRAW|%s\n", ok ? "pass" : "FAIL");
	return ok;
}
