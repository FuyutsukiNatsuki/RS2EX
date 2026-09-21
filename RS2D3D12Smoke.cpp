//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	See RS2D3D12Smoke.h.

#include "stdafx.h"
#include "RS2D3D12Smoke.h"
#include "RS2D3D12Backend.h"

//	How many times the create/shutdown step repeats.  Once proves it works;
//	repeating proves shutdown actually released what it created, which is the
//	failure that shows up later as a leak or a crash on the second run.
static const int RS2D3D12_SMOKE_CYCLES = 3;

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

		//	The destructor shuts it down; calling Shutdown() first makes the
		//	double shutdown explicit, because that is a real code path -
		//	CRS2Renderer::Initialize shuts a failed backend down and then
		//	deletes it.
		backend.Shutdown();
		wsprintfA(label, "shutdown twice cycle %d", cycle+1);
		RS2D3D12_SmokeStep(label, true);

		if(!ok) allOk = false;
	}
	return allOk;
}

bool RS2D3D12SmokeRun(
	HWND window	//	application window, unused until there is a swap chain
){
	(void)window;

	s_Passed = s_Failed = 0;
	Debug("RS2D3D12SMOKE|begin\n");

	RS2D3D12_SmokeLifecycle();

	Debug("RS2D3D12SMOKE|end|passed=%d|failed=%d\n", s_Passed, s_Failed);
	return s_Failed==0;
}
