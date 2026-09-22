//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//	Modified for RS2EX on 2026-09-22.
//
//	See RS2D3D12Smoke.h.

#include "stdafx.h"
#include "RS2D3D12Smoke.h"
#include "RS2D3D12Backend.h"
#include "RS2DecodedImage.h"
#include "RS2Renderer.h"
#include "RS2TextureResource.h"

//	How many times the create/shutdown step repeats.  Once proves it works;
//	repeating proves shutdown actually released what it created, which is the
//	failure that shows up later as a leak or a crash on the second run.
static const int RS2D3D12_SMOKE_CYCLES = 3;

//	Frames to present in the lifecycle step.  Present(1, 0) waits for vertical
//	sync, so this is also how long the window shows the test colour: five
//	seconds at sixty hertz, which is enough for a capture harness to find
//	the window, move it and photograph it without racing the exit.
static const int RS2D3D12_SMOKE_FRAMES = 300;

//	Deliberately not black, not white and not a colour the program uses, so a
//	screenshot of it cannot be mistaken for anything else.
static const unsigned int RS2D3D12_SMOKE_COLOR = 0x00336699;

static int s_Passed = 0;
static int s_Failed = 0;

static void RS2D3D12_SmokeStep(
	const char *name,	//	what was tried
	bool ok			//	whether it worked
){
	if(ok) s_Passed++; else s_Failed++;
	Debug("RS2D3D12SMOKE|%-28s|%s\n", name, ok ? "pass" : "FAIL");
}

bool RS2D3D12SmokeRequested(){
	return CheckArguments("-dx12smoke")!=FALSE;
}

/*
 *	Create and shut the backend down several times over.
 */
static bool RS2D3D12_SmokeLifecycle(){
	int cycle;
	bool allOk = true;

	for(cycle = 0; cycle<RS2D3D12_SMOKE_CYCLES; cycle++){
		CRS2D3D12Backend backend;
		char label[64];

		const bool ok = backend.Initialize(640, 480);

		wsprintfA(label, "initialize cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, ok);

		if(!ok){
			allOk = false;
			continue;
		}

		wsprintfA(label, "texture upload cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label,
			RS2D3D12TextureUploadSmoke(backend.GetTextureUpload()));
		wsprintfA(label, "descriptor cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label,
			RS2D3D12DescriptorsSmoke(backend.GetDescriptors(), backend.GetDevice()));

		//	Asking the GPU to catch up on a queue that has never been given
		//	work should return promptly rather than hang: if the fence and its
		//	event are wired up wrongly, this is where it shows.
		backend.WaitForGpu();
		wsprintfA(label, "wait for gpu cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, true);

		unsigned int w = 0, h = 0;

		backend.GetViewportSize(&w, &h);
		wsprintfA(label, "viewport size cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, w==640 && h==480);

		wsprintfA(label, "readback refused cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, !backend.SupportsReadback());

		//	The guards in Capture, the offscreen target and GetPixelColor ask
		//	CRS2Renderer, which delegates to whichever backend is installed.
		//	This test builds a backend directly and never installs one, so
		//	asking the renderer here would answer about nothing at all.  What
		//	can be established here is the backend end of that chain, above;
		//	the renderer end is logged by -dx12, where a backend really is
		//	installed.

		wsprintfA(label, "device present cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, !backend.IsDeviceRemoved());

		wsprintfA(label, "swap chain cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, backend.HasSwapChain());

		//	Two: one for each vertex shader, built at start-up so the whole
		//	chain - root signature, compiled shader, input layout, pipeline
		//	state - is proven where a failure is reported rather than
		//	discovered later as geometry that did not appear.
		wsprintfA(label, "pipeline states cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, backend.GetPipelineStateCount()>=2);

		//	Not a pass or fail on its own: a device without stencil is a
		//	fact about the machine, not a defect.  It is reported because
		//	the stencil shadow passes will need to know.
		Debug("RS2D3D12SMOKE|stencil available         |%s\n",
			backend.HasStencil() ? "yes" : "no");

		//	The destructor shuts it down; calling Shutdown() first makes the
		//	double shutdown explicit, because that is a real code path -
		//	CRS2Renderer::Initialize shuts a failed backend down and then
		//	deletes it.
		backend.Shutdown();
		wsprintfA(label, "shutdown twice cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, true);

		wsprintfA(label, "no leaked references cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, backend.GetDeviceReferencesAfterShutdown()==0);

		if(!ok) allOk = false;
	}
	return allOk;
}

/*
 *	Clear and present, for long enough to be photographed.
 *
 *	window	: the application window, shown so the result is visible
 *
 *	Two logical passes per displayed frame, because that is the shape RailSim
 *	uses for stereo and window division and the part of the lifecycle most
 *	likely to be got wrong: the command list has to stay open across them and
 *	close exactly once.  The second pass keeps the colour buffer, so if
 *	EndRenderPass were closing the list this would present a frame that had
 *	only been half recorded.
 */
/*
 *	Present a number of frames, two logical passes each.
 *
 *	backend	: the backend under test
 *	frames	: how many displayed frames to run
 *	allOk	: cleared if any pass was refused
 *
 *	returns	: the value of *allOk
 */
static bool RS2D3D12_SmokeFrames(
	CRS2D3D12Backend &backend,	//	backend under test
	int frames,			//	displayed frames to run
	bool *allOk,			//	cleared on a refused pass
	bool *alternated		//	set when the frame index ever changes
){
	int frame;

	for(frame = 0; frame<frames; frame++){
		const unsigned int before = backend.GetFrameIndex();

		if(!backend.BeginRenderPass(RS2D3D12_SMOKE_COLOR, true)) *allOk = false;
		backend.EndRenderPass();

		//	Second pass, keeping the colour: depth is still cleared.
		if(!backend.BeginRenderPass(0, false)) *allOk = false;
		backend.EndRenderPass();

		backend.Present();

		//	Checked here rather than after the loop.  With two buffers and an
		//	even number of presents the index comes back to where it started,
		//	so comparing once at the end proves nothing at all.
		if(alternated && backend.GetFrameIndex()!=before) *alternated = true;
	}
	return *allOk;
}

static bool RS2D3D12_SmokeLifecycleFrames(HWND window){
	CRS2D3D12Backend backend;

	if(!backend.Initialize(640, 480)){
		RS2D3D12_SmokeStep("initialize for frames", false);
		return false;
	}
	RS2D3D12_SmokeStep("initialize for frames", true);
	RS2D3D12_SmokeStep("texture upload with frames",
		RS2D3D12TextureUploadSmoke(backend.GetTextureUpload()));
	RS2D3D12_SmokeStep("descriptors with frames",
		RS2D3D12DescriptorsSmoke(backend.GetDescriptors(), backend.GetDevice()));

	//	The window is created before the display size is known, so it has no
	//	area until the Direct3D 8 start-up sizes it - and this test runs
	//	instead of that start-up.  Size it the same way the program does, so
	//	the presented colour is actually visible: a log line saying Present
	//	succeeded is a weaker claim than a window anyone can photograph.
	svw.winW = 640;
	svw.winH = 480;
	if(sv3.fWindowed) AdjustWindow();
	ShowWindow(window, SW_SHOW);
	UpdateWindow(window);

	{
		RECT rect;

		GetWindowRect(window, &rect);
		Debug("RS2D3D12SMOKE|window %ld x %ld at %ld,%ld\n",
			rect.right-rect.left, rect.bottom-rect.top, rect.left, rect.top);
	}

	backend.ClearTarget(RS2D3D12_SMOKE_COLOR);
	RS2D3D12_SmokeStep("clear target outside a frame", true);

	bool allPasses = true;
	bool indexMoved = false;

	RS2D3D12_SmokeFrames(backend, RS2D3D12_SMOKE_FRAMES, &allPasses, &indexMoved);

	RS2D3D12_SmokeStep("two passes per frame", allPasses);
	RS2D3D12_SmokeStep("back buffer alternates", indexMoved);

	//	Resize while presenting.  svw is what the window procedure writes on
	//	WM_SIZE, and this test has no message pump, so setting it directly is
	//	what a real resize looks like from the backend's side.
	svw.winW = 800;
	svw.winH = 600;
	if(sv3.fWindowed) AdjustWindow();

	bool resized = true;

	RS2D3D12_SmokeFrames(backend, 60, &resized, NULL);

	unsigned int w = 0, h = 0;

	backend.GetViewportSize(&w, &h);
	RS2D3D12_SmokeStep("resize to 800 x 600", resized && w==800 && h==600);

	//	Minimised: a client area of zero, which DXGI will not accept.  The
	//	frames still have to be survivable.
	const int keepW = svw.winW, keepH = svw.winH;

	svw.winW = 0;
	svw.winH = 0;

	bool minimised = true;

	RS2D3D12_SmokeFrames(backend, 10, &minimised, NULL);
	backend.GetViewportSize(&w, &h);
	RS2D3D12_SmokeStep("zero size does not resize", minimised && w==800 && h==600);

	svw.winW = keepW;
	svw.winH = keepH;

	//	Back to the original size, which is also a resize in the other
	//	direction - shrinking releases buffers that growing had just made.
	svw.winW = 640;
	svw.winH = 480;
	if(sv3.fWindowed) AdjustWindow();

	bool restored = true;

	RS2D3D12_SmokeFrames(backend, 60, &restored, NULL);
	backend.GetViewportSize(&w, &h);
	RS2D3D12_SmokeStep("restore to 640 x 480", restored && w==640 && h==480);

	backend.WaitForGpu();
	RS2D3D12_SmokeStep("wait after presenting", true);

	//	Read before shutdown, while the info queue still exists.  A release
	//	build has no debug layer, and a zero count there would mean "nothing
	//	was checked" rather than "nothing was wrong", so it is not counted as
	//	a step at all.
	if(backend.HasDebugLayer()){
		const unsigned int errors =
			backend.CountDebugMessages(D3D12_MESSAGE_SEVERITY_ERROR);
		const unsigned int warnings =
			backend.CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING);

		Debug("RS2D3D12SMOKE|debug layer errors=%u warnings=%u\n", errors, warnings);
		RS2D3D12_SmokeStep("no debug layer errors", errors==0);
	}else{
		Debug("RS2D3D12SMOKE|debug layer not available, nothing checked\n");
	}

	backend.Shutdown();
	RS2D3D12_SmokeStep("shutdown after presenting", true);
	return allPasses && indexMoved;
}

bool RS2D3D12SmokeRun(
	HWND window	//	application window the swap chain presents to
){
	s_Passed = s_Failed = 0;
	Debug("RS2D3D12SMOKE|begin\n");

	//	The backend reads svw.hWnd itself, as the Direct3D 8 one does, so
	//	this is a check that the window exists rather than a way to pass it.
	RS2D3D12_SmokeStep("window available", window!=NULL);
	if(!window){
		Debug("RS2D3D12SMOKE|end|passed=%d|failed=%d\n", s_Passed, s_Failed);
		return false;
	}

	//	WP1: a texture with D3D12 ownership must keep its opaque payload,
	//	dimensions and operations without neutral code interpreting it as D3D8.
	RS2D3D12_SmokeStep("neutral texture ownership", RS2TextureOwnershipSmoke());
	RS2D3D12_SmokeStep("neutral image decode", RS2DecodedImageSmoke());

	RS2D3D12_SmokeLifecycle();
	RS2D3D12_SmokeLifecycleFrames(window);

	Debug("RS2D3D12SMOKE|end|passed=%d|failed=%d\n", s_Passed, s_Failed);
	return s_Failed==0;
}
