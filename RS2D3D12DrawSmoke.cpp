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
#include "RS2D3D12TextureSmoke.h"
#include "RS2D3D12Draw.h"
#include "RS2Renderer.h"
#include "RS2Draw.h"
#include "RS2MeshData.h"
#include "RS2TextureResource.h"
#include "RS2MaterialBinding.h"
#include "RS2RenderState.h"
#include "RS2D3D12Texture.h"
#include "RS2D3D12Backend.h"
#include "RS2DecodedImage.h"

//	Enough draws to prove repeated submission. Present is not guaranteed to
//	pace an occluded or background window on every machine, so a separate hold
//	below keeps the accepted final frame available for capture.
static const int RS2D3D12_DRAW_FRAMES = 300;
static const DWORD RS2D3D12_DRAW_CAPTURE_HOLD_MS = 8000;

//	A background nothing else in the program uses, so a screenshot cannot be
//	mistaken for a real scene or for an empty one.
static const unsigned int RS2D3D12_DRAW_CLEAR = 0x00202840;

//	Fully opaque and far apart, so what arrives on screen identifies both which
//	primitive drew it and whether the packed colour was read in the right byte
//	order.  0xAARRGGBB, as everywhere else in the engine.
static const unsigned int RS2D3D12_DRAW_RED = 0xffcc2020;
static const unsigned int RS2D3D12_DRAW_BLUE = 0xff2040cc;
static const unsigned int RS2D3D12_DRAW_GREEN = 0xff20cc40;
static const unsigned int RS2D3D12_DRAW_MAGENTA = 0xffcc40cc;
static const unsigned int RS2D3D12_DRAW_YELLOW = 0xffe0c020;

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

struct RS2DrawSmokeVertexSUV
{
	float x, y, z, rhw;
	unsigned int diffuse;
	float u, v;
};

bool RS2D3D12DrawSmokeRequested(){
	return CheckArguments("-dx12drawsmoke")!=FALSE
		|| CheckArguments("-dx12alphasmoke")!=FALSE
		|| RS2D3D12TextureSmokeRequested();
}

static bool RS2D3D12AlphaSmokeRun();

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
	if(RS2D3D12TextureSmokeRequested()) return RS2D3D12TextureSmokeRun();
	if(GetRS2Renderer().GetBackendType()!=RS2_RENDERER_D3D12){
		Debug("RS2D3D12DRAW|this test needs -dx12; the active backend is %s\n",
			GetRS2Renderer().GetBackendName());
		return false;
	}
	if(CheckArguments("-dx12alphasmoke")) return RS2D3D12AlphaSmokeRun();

	//	Identity everywhere, so the vertices below are already in clip space.
	//	The matrix path is still exercised - the shader multiplies by it - but
	//	the test data stays readable, and a wrong matrix convention would show
	//	up as geometry in the wrong place rather than as arithmetic nobody can
	//	check by eye.
	float identity[16];

	RS2DrawSmokeIdentity(identity);

	RS2MeshVertexLayout layoutP, layoutS;

	RS2DrawSmokeLayoutP(&layoutP);
	RS2DrawSmokeLayoutS(&layoutS);
	RS2MeshVertexLayout layoutTexture = layoutS;
	layoutTexture.stride = sizeof(RS2DrawSmokeVertexSUV);
	layoutTexture.texCoordCount = 1;
	layoutTexture.texCoord[0].offset = sizeof(RS2DrawSmokeVertexS);
	layoutTexture.texCoord[0].components = 2;

	unsigned int width = 0, height = 0;

	GetRS2Renderer().GetViewportSize(&width, &height);
	Debug("RS2D3D12DRAW|viewport %u x %u\n", width, height);

	//	The capture harness cannot synchronously move this window once the smoke
	//	holds its final frame on this UI thread. Put this developer-only window
	//	above ordinary desktop windows before drawing instead. The window goes
	//	away with the smoke, so no persistent application state is changed.
	if(sv3.fWindowed){
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
	}

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

	//	Two more shapes use retained resources.  Their colours and positions do
	//	not overlap the immediate shapes, so the screenshot proves both public
	//	resource draw calls reached the pipeline rather than merely proving that
	//	the resources could be allocated.
	const RS2DrawSmokeVertexS bufferedTriangle[3] = {
		{ width*0.58f, height*0.12f, 0.5f, 1.0f, RS2D3D12_DRAW_MAGENTA },
		{ width*0.88f, height*0.12f, 0.5f, 1.0f, RS2D3D12_DRAW_MAGENTA },
		{ width*0.73f, height*0.48f, 0.5f, 1.0f, RS2D3D12_DRAW_MAGENTA }
	};
	const RS2DrawSmokeVertexS indexedQuad[4] = {
		{ width*0.58f, height*0.70f, 0.5f, 1.0f, RS2D3D12_DRAW_YELLOW },
		{ width*0.88f, height*0.70f, 0.5f, 1.0f, RS2D3D12_DRAW_YELLOW },
		{ width*0.88f, height*0.92f, 0.5f, 1.0f, RS2D3D12_DRAW_YELLOW },
		{ width*0.58f, height*0.92f, 0.5f, 1.0f, RS2D3D12_DRAW_YELLOW }
	};
	const unsigned int indexedQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };
	const RS2DrawSmokeVertexSUV texturedQuad[4] = {
		{ width*0.38f, height*0.10f, 0.3f, 1.0f, 0xffffffff, 0.0f, 0.0f },
		{ width*0.62f, height*0.10f, 0.3f, 1.0f, 0xffffffff, 1.0f, 0.0f },
		{ width*0.62f, height*0.50f, 0.3f, 1.0f, 0xffffffff, 1.0f, 1.0f },
		{ width*0.38f, height*0.50f, 0.3f, 1.0f, 0xffffffff, 0.0f, 1.0f }
	};
	const unsigned int textureBaseline = RS2D3D12_GetLiveTextureCount();
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();
	if(!backend) return false;
	const unsigned int descriptorBaseline = backend->GetDescriptors()->GetLive();
	CRS2TextureResource *fileTexture = RS2CreateTextureFromFile(
		"Help\\logo.png", 0, 0);
	const bool fileCreated = fileTexture && fileTexture->IsValid();
	RS2DestroyTexture(fileTexture);
	CRS2TextureResource *sampledTexture = RS2CreateTextureFromResource(
		"OPENING", 0, 0);
	const bool textureCreated = fileCreated && sampledTexture
		&& sampledTexture->IsValid()
		&& RS2D3D12_GetLiveTextureCount()==textureBaseline+1;
	Debug("RS2D3D12DRAW|public file/resource texture |%s\n",
		textureCreated ? "pass" : "FAIL");
	if(!textureCreated){
		RS2DestroyTexture(sampledTexture);
		return false;
	}
	RS2SetBaseTextureCombine();

	const unsigned int liveBaseline = RS2GetLiveGeometryCount();
	const unsigned int vertexBaseline = RS2GetGeometryVertexBytes();
	const unsigned int indexBaseline = RS2GetGeometryIndexBytes();
	bool resourceOk = true;

	//	A failed build must leave all counters at baseline.  These cover an
	//	unusable layout and an index outside the vertex buffer.
	RS2MeshVertexLayout invalidLayout;
	invalidLayout.Clear();
	invalidLayout.stride = sizeof(RS2DrawSmokeVertexS);
	CRS2GeometryResource *invalid = RS2CreateGeometry(
		invalidLayout, bufferedTriangle, 3);
	if(invalid){
		resourceOk = false;
		RS2DestroyGeometry(invalid);
	}
	const unsigned int invalidIndices[3] = { 0, 1, 4 };
	invalid = RS2CreateIndexedGeometry(
		layoutS, indexedQuad, 4, invalidIndices, 3);
	if(invalid){
		resourceOk = false;
		RS2DestroyGeometry(invalid);
	}
	resourceOk = resourceOk
		&& RS2GetLiveGeometryCount()==liveBaseline
		&& RS2GetGeometryVertexBytes()==vertexBaseline
		&& RS2GetGeometryIndexBytes()==indexBaseline;

	//	Repeated creation/destruction is the useful drift test here.  Absolute
	//	working-set values vary with the driver and allocator and are not treated
	//	as correctness evidence.
	int cycle;
	for(cycle = 0; cycle<32 && resourceOk; cycle++){
		CRS2GeometryResource *temporary = RS2CreateGeometry(
			layoutS, bufferedTriangle, 3);
		if(!temporary){
			resourceOk = false;
			break;
		}
		RS2DestroyGeometry(temporary);
		resourceOk = RS2GetLiveGeometryCount()==liveBaseline
			&& RS2GetGeometryVertexBytes()==vertexBaseline
			&& RS2GetGeometryIndexBytes()==indexBaseline;
	}

	CRS2GeometryResource *buffered = RS2CreateGeometry(
		layoutS, bufferedTriangle, 3);
	CRS2GeometryResource *indexed = RS2CreateIndexedGeometry(
		layoutS, indexedQuad, 4, indexedQuadIndices, 6);

	resourceOk = resourceOk && buffered && indexed
		&& RS2GetGeometryVertexCount(buffered)==3
		&& RS2GetGeometryVertexCount(indexed)==4
		&& RS2GetLiveGeometryCount()==liveBaseline+2
		&& RS2GetGeometryVertexBytes()==vertexBaseline
			+(unsigned int)(sizeof(bufferedTriangle)+sizeof(indexedQuad))
		&& RS2GetGeometryIndexBytes()==indexBaseline
			+(unsigned int)(6*sizeof(WORD));

	Debug("RS2D3D12DRAW|resource create/counter drift |%s\n",
		resourceOk ? "pass" : "FAIL");

	if(!buffered || !indexed){
		RS2DestroyGeometry(indexed);
		RS2DestroyGeometry(buffered);
		RS2DestroyTexture(sampledTexture);
		return false;
	}

	const unsigned int drawBaseline = RS2D3D12_GetDrawCount();
	const unsigned int refusedBaseline = RS2D3D12_GetRefusedDrawCount();
	bool framesOk = true;

	int frame;

	for(frame = 0; frame<RS2D3D12_DRAW_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2D3D12_DRAW_CLEAR, true)){
			Debug("RS2D3D12DRAW|the render pass would not begin\n");
			framesOk = false;
			break;
		}

		//	BeginRenderPass currently imports the engine view matrix for the real
		//	scene. Submit the smoke's known transforms after that compatibility
		//	step, through the same public boundary the game uses.
		RS2SetWorldTransform(identity);
		RS2SetViewTransform(identity);
		RS2SetProjectionTransform(identity);

		RS2DrawImmediate(layoutP, RS2_PRIMITIVE_TRIANGLE_LIST, clockwise, 3);
		RS2DrawImmediate(layoutP, RS2_PRIMITIVE_TRIANGLE_LIST, counterClockwise, 3);
		RS2DrawImmediate(layoutS, RS2_PRIMITIVE_TRIANGLE_FAN, quad, 4);
		RS2DrawBuffered(buffered, RS2_PRIMITIVE_TRIANGLE_LIST, 0, 3);
		RS2DrawIndexed(indexed, RS2_PRIMITIVE_TRIANGLE_LIST, 0, 6);
		RS2SetTextureFilter(0, frame%2 ? RS2_FILTER_LINEAR : RS2_FILTER_POINT);
		RS2BindTexture(0, sampledTexture->GetRef());
		RS2DrawImmediate(layoutTexture, RS2_PRIMITIVE_TRIANGLE_FAN,
			texturedQuad, 4);
		RS2BindTexture(0, RS2TextureRef());

		//	One frame proves both range guards and both whole-primitive guards.
		//	The final refusal count is exact, so an unrelated refusal fails too.
		if(frame==0){
			RS2DrawBuffered(buffered, RS2_PRIMITIVE_TRIANGLE_LIST, 2, 3);
			RS2DrawBuffered(buffered, RS2_PRIMITIVE_TRIANGLE_LIST, 0, 2);
			RS2DrawIndexed(indexed, RS2_PRIMITIVE_TRIANGLE_LIST, 4, 3);
			RS2DrawIndexed(indexed, RS2_PRIMITIVE_TRIANGLE_LIST, 0, 5);
		}

		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
	}

	const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
	const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;

	RS2DestroyTexture(sampledTexture);
	backend->WaitForGpu();
	backend->CollectRetiredTextures();
	RS2DestroyGeometry(indexed);
	RS2DestroyGeometry(buffered);
	const unsigned int debugWarnings = backend->HasDebugLayer()
		? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;
	Debug("RS2D3D12DRAW|debug errors/warnings=%u\n", debugWarnings);
	resourceOk = resourceOk
		&& RS2D3D12_GetLiveTextureCount()==textureBaseline
		&& backend->GetDescriptors()->GetLive()==descriptorBaseline
		&& debugWarnings==0
		&& RS2GetLiveGeometryCount()==liveBaseline
		&& RS2GetGeometryVertexBytes()==vertexBaseline
		&& RS2GetGeometryIndexBytes()==indexBaseline;

	Debug("RS2D3D12DRAW|submitted=%u refused=%u intentional=4\n",
		submitted, refused);
	Debug("RS2D3D12DRAW|resource destroy/baseline   |%s\n",
		resourceOk ? "pass" : "FAIL");

	// Six valid draws a frame, one through Stage 0, and four intentional refusals.
	const bool ok = framesOk && resourceOk
		&& submitted==(unsigned int)(RS2D3D12_DRAW_FRAMES*6)
		&& refused==4;

	Debug("RS2D3D12DRAW|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2D3D12_DRAW_CAPTURE_HOLD_MS);
	return ok;
}

// A separate view of the existing public draw smoke, with no scene-dependent
// pixels. Rows sample the fixture's alpha 0/128/255 texels; columns exercise
// off, ALWAYS, LEQUAL/128, GREATER/128, LEQUAL/0, GREATER/0 respectively.
// The matching screenshot checker verifies all eighteen interiors exactly.
static bool RS2D3D12AlphaSmokeRun(){
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();
	if(!backend) return false;
	unsigned int width = 0, height = 0;
	GetRS2Renderer().GetViewportSize(&width, &height);
	if(width!=640 || height!=480){
		Debug("RS2D3D12ALPHA|expected a 640 x 480 viewport\n");
		return false;
	}
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	char directory[MAX_PATH], path[MAX_PATH];
	if(!GetTempPathA(MAX_PATH, directory)) return false;
	_snprintf(path, MAX_PATH-1, "%sRS2EX-alpha-%lu.png",
		directory, (unsigned long)GetCurrentProcessId());
	path[MAX_PATH-1] = 0;
	if(!RS2WriteKnownAlphaPngFixture(path)) return false;

	const unsigned int textureBaseline = RS2D3D12_GetLiveTextureCount();
	const unsigned int descriptorBaseline = backend->GetDescriptors()->GetLive();
	if(textureBaseline || descriptorBaseline){
		Debug("RS2D3D12ALPHA|nonempty start: textures=%u descriptors=%u\n",
			textureBaseline, descriptorBaseline);
		DeleteFileA(path);
		return false;
	}
	CRS2TextureResource *texture = RS2CreateTextureFromFile(path, 0, 1);
	DeleteFileA(path);
	if(!texture || !texture->IsValid()){
		RS2DestroyTexture(texture);
		Debug("RS2D3D12ALPHA|fixture texture creation failed\n");
		return false;
	}

	RS2MeshVertexLayout layout;
	RS2DrawSmokeLayoutS(&layout);
	layout.stride = sizeof(RS2DrawSmokeVertexSUV);
	layout.texCoordCount = 1;
	layout.texCoord[0].offset = sizeof(RS2DrawSmokeVertexS);
	layout.texCoord[0].components = 2;
	float identity[16];
	RS2DrawSmokeIdentity(identity);
	const unsigned int refs[6] = { 128, 128, 128, 128, 0, 0 };
	const RS2CompareFunc funcs[6] = {
		RS2_COMPARE_GREATER, RS2_COMPARE_ALWAYS,
		RS2_COMPARE_LESS_EQUAL, RS2_COMPARE_GREATER,
		RS2_COMPARE_LESS_EQUAL, RS2_COMPARE_GREATER
	};
	const float u[3] = { 0.25f, 0.75f, 0.25f };
	const float v[3] = { 0.75f, 0.25f, 0.25f };
	const unsigned int pipelineBaseline = backend->GetPipelineStateCount();
	const unsigned int drawBaseline = RS2D3D12_GetDrawCount();
	const unsigned int refusedBaseline = RS2D3D12_GetRefusedDrawCount();
	bool framesOk = true;

	for(int frame = 0; frame<RS2D3D12_DRAW_FRAMES; frame++){
		if(!GetRS2Renderer().BeginRenderPass(0x00102030, true)){
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
		RS2SetTextureFilter(0, RS2_FILTER_POINT);
		RS2BindTexture(0, texture->GetRef());

		for(unsigned int column = 0; column<6; column++){
			RS2SetAlphaTest(column!=0);
			RS2SetAlphaRef(refs[column]);
			RS2SetAlphaFunc(funcs[column]);
			for(unsigned int row = 0; row<3; row++){
				const float x = (float)(60+column*100);
				const float y = (float)(100+row*110);
				const RS2DrawSmokeVertexSUV quad[4] = {
					{ x-32, y-32, 0.5f, 1.0f, 0xffffffff, u[row], v[row] },
					{ x+32, y-32, 0.5f, 1.0f, 0xffffffff, u[row], v[row] },
					{ x+32, y+32, 0.5f, 1.0f, 0xffffffff, u[row], v[row] },
					{ x-32, y+32, 0.5f, 1.0f, 0xffffffff, u[row], v[row] }
				};
				RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, quad, 4);
			}
		}
		RS2BindTexture(0, RS2TextureRef());
		RS2SetAlphaTest(false);
		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
	}

	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2DestroyTexture(texture);
	backend->WaitForGpu();
	backend->CollectRetiredTextures();
	const unsigned int submitted = RS2D3D12_GetDrawCount()-drawBaseline;
	const unsigned int refused = RS2D3D12_GetRefusedDrawCount()-refusedBaseline;
	const unsigned int pipelineDelta = backend->GetPipelineStateCount()-pipelineBaseline;
	const unsigned int warnings = backend->HasDebugLayer()
		? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;
	const bool ok = framesOk && submitted==RS2D3D12_DRAW_FRAMES*18
		&& refused==0 && warnings==0
		&& pipelineDelta==1
		&& RS2D3D12_GetLiveTextureCount()==textureBaseline
		&& backend->GetDescriptors()->GetLive()==descriptorBaseline;
	Debug("RS2D3D12ALPHA|submitted=%u refused=%u warnings=%u newPSO=%u\n",
		submitted, refused, warnings, pipelineDelta);
	Debug("RS2D3D12ALPHA|%s\n", ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2D3D12_DRAW_CAPTURE_HOLD_MS);
	return ok;
}
