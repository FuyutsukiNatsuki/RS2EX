//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//	Modified for RS2EX on 2026-09-21, 2026-09-22.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D8Backend.h"
#include "RS2D3D12Availability.h"
#include "RS2D3D12Backend.h"

/*
 *	The renderer instance
 *
 *	Constructed on first use, so construction is inert and nothing here runs
 *	before the main window exists.
 *
 *	Deliberately never deleted.  A plain function-local static would be one of
 *	the last statics constructed - InitDirect3D() is the first caller - and so
 *	one of the first destroyed, ahead of theApp and ahead of the texture and
 *	mesh lists that release D3D resources from their own destructors.  That
 *	would release the device in the middle of static teardown and leave
 *	FreeDirect3D() calling into a destroyed object.
 *
 *	Letting it outlive static destruction keeps teardown where RailSim II 2.15
 *	put it: FreeDirect3D(), from ~CApp, before DestroyWindow().  What leaks is
 *	one object holding a null backend pointer, at process exit.
 */
CRS2Renderer &GetRS2Renderer(){
	static CRS2Renderer *renderer = new CRS2Renderer;
	return *renderer;
}

/*
 *	Which backend was asked for.
 *
 *	[RS2EX] Direct3D 8 is the default and stays the reference renderer.
 *	-dx12 is a developer switch: no settings entry and nothing saved, so a
 *	comparison is one command line away and no configuration has to migrate
 *	while the Direct3D 12 backend is still incomplete.
 *
 *	Resolved once.  A selection that could change mid-run would make every
 *	dispatch decision built on it unanswerable.
 */
static RS2RendererBackendType RS2RequestedBackend(){
	static int requested = -1;

	if(requested<0)
		requested = CheckArguments("-dx12") ? RS2_RENDERER_D3D12 : RS2_RENDERER_D3D8;
	return (RS2RendererBackendType)requested;
}

CRS2Renderer::CRS2Renderer()
	: m_Backend(NULL),
	  m_BackendType(RS2_RENDERER_D3D8),
	  m_InRenderPass(false)
{
}

CRS2Renderer::~CRS2Renderer(){
	Shutdown();
}

/*
 *	Bring up the renderer
 *
 *	width	: back buffer width
 *	height	: back buffer height
 */
bool CRS2Renderer::Initialize(int width, int height){
	if(m_Backend) return true;

	m_BackendType = RS2RequestedBackend();

	if(m_BackendType==RS2_RENDERER_D3D12){
		//	Asked for explicitly, so failing loudly is the point.  Falling back
		//	to Direct3D 8 would make every -dx12 test result mean "one of the
		//	two backends worked", which is not a result.
		if(!RS2D3D12IsRuntimeUsable()){
			Debug("[RS2EX Renderer] -dx12 requested but Direct3D 12 is unusable here\n");
			return false;
		}

		m_Backend = new CRS2D3D12Backend;
	}else{
		m_Backend = new CRS2D3D8Backend;
	}

	Debug("[RS2EX Renderer] backend = %s\n", GetBackendName());
	Debug("[RS2EX Renderer] initialize %d x %d\n", width, height);

	if(!m_Backend->Initialize(width, height)){
		Debug("[RS2EX Renderer] initialize failed\n");

		//	A failed Initialize() can still have created the Direct3D object or
		//	the device.  RailSim II 2.15 released those from FreeDirect3D() at
		//	teardown; doing it here instead is the same release, just earlier.
		m_Backend->Shutdown();
		delete m_Backend;
		m_Backend = NULL;
		return false;
	}

	//	[RS2EX] v0.1.0 stopped here rather than continue: the Direct3D 12
	//	backend could not create a texture or a vertex buffer, and scene
	//	construction did not survive being handed nothing.
	//
	//	v0.1.1 found what that actually was - a single scalar the engine reads
	//	out of sv3 that only the Direct3D 8 backend was writing - and fixed it,
	//	so the program now runs.  It still draws nothing until the Direct3D 12
	//	draw path lands, but running and drawing nothing is a state you can
	//	investigate; refusing to start is not.
	if(m_BackendType==RS2_RENDERER_D3D12){
		//	Logged here because this is the one place a Direct3D 12 backend is
		//	actually installed behind the renderer, which is what the capture,
		//	offscreen and pixel-read guards ask.
		Debug("[RS2EX Renderer] readback supported = %s\n",
			SupportsReadback() ? "yes" : "no");
	}
	return true;
}

void CRS2Renderer::Shutdown(){
	if(!m_Backend) return;

	Debug("[RS2EX Renderer] shutdown\n");

	m_Backend->Shutdown();
	delete m_Backend;
	m_Backend = NULL;
	m_InRenderPass = false;
}

/*
 *	Begin a render pass
 *
 *	clearColor		: clear colour, used only when clearColorBuffer is true
 *	clearColorBuffer: false to keep the colour buffer and clear depth only
 *
 *	returns			: false when the pass could not be started
 */
bool CRS2Renderer::BeginRenderPass(unsigned int clearColor, bool clearColorBuffer){
	if(!m_Backend) return false;

#ifdef _DEBUG
	if(m_InRenderPass) Debug("[RS2EX Renderer] begin while a pass is active\n");
#endif

	if(!m_Backend->BeginRenderPass(clearColor, clearColorBuffer)) return false;

	m_InRenderPass = true;
	return true;
}

/*
 *	End a render pass
 *
 *	Ending is separate from presenting: stereo, window division and offscreen
 *	rendering all finish a scene without putting it on the screen.
 */
void CRS2Renderer::EndRenderPass(){
	if(!m_Backend) return;

#ifdef _DEBUG
	if(!m_InRenderPass) Debug("[RS2EX Renderer] end while no pass is active\n");
#endif

	//	Forwarded unconditionally.  RailSim II 2.15 ends the frame even when
	//	BeginScene() failed on a lost device, and the diagnostic above must not
	//	turn that into a behaviour change.
	m_Backend->EndRenderPass();

	m_InRenderPass = false;
}

void CRS2Renderer::Present(){
	if(!m_Backend) return;

#ifdef _DEBUG
	if(m_InRenderPass) Debug("[RS2EX Renderer] present while a pass is active\n");
#endif

	m_Backend->Present();
}

bool CRS2Renderer::Reset(){
	if(!m_Backend) return false;
	return m_Backend->Reset();
}

void CRS2Renderer::SetViewport(
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height,
	float minZ,
	float maxZ
){
	if(!m_Backend) return;
	m_Backend->SetViewport(x, y, width, height, minZ, maxZ);
}

void CRS2Renderer::GetViewportSize(unsigned int *width, unsigned int *height) const{
	if(!m_Backend){
		if(width) *width = 1;
		if(height) *height = 1;
		return;
	}
	m_Backend->GetViewportSize(width, height);
}

const char *CRS2Renderer::GetBackendName() const{
	return m_Backend ? m_Backend->GetName() : "none";
}


RS2RendererBackendType CRS2Renderer::GetBackendType() const{
	return m_BackendType;
}

bool CRS2Renderer::IsReady() const{
	return m_Backend!=NULL;
}

void CRS2Renderer::ClearTarget(unsigned int color){
	if(m_Backend) m_Backend->ClearTarget(color);
}

/*
 *	False when there is no backend at all: a caller asking whether readback
 *	works should get "no" rather than a crash.
 */
bool CRS2Renderer::SupportsReadback() const{
	return m_Backend ? m_Backend->SupportsReadback() : false;
}
