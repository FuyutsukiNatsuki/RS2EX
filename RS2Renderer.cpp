//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D8Backend.h"

/*
 *	The renderer instance
 *
 *	Function-local static: construction is inert, and nothing here runs before
 *	the main window exists.
 */
CRS2Renderer &GetRS2Renderer(){
	static CRS2Renderer renderer;
	return renderer;
}

CRS2Renderer::CRS2Renderer()
	: m_Backend(NULL),
	  m_InRenderPass(false),
	  m_BeginPassCount(0),
	  m_EndPassCount(0),
	  m_PresentCount(0)
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

	//	One backend, chosen at compile time.  No settings UI, no runtime
	//	selection, no plugin mechanism - see plan section 10.
	m_Backend = new CRS2D3D8Backend;

	Debug("[RS2EX Renderer] backend = %s\n", m_Backend->GetName());
	Debug("[RS2EX Renderer] initialize %d x %d\n", width, height);

	if(!m_Backend->Initialize(width, height)){
		Debug("[RS2EX Renderer] initialize failed\n");
		delete m_Backend;
		m_Backend = NULL;
		return false;
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
	m_BeginPassCount++;
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
	m_EndPassCount++;
}

void CRS2Renderer::Present(){
	if(!m_Backend) return;

#ifdef _DEBUG
	if(m_InRenderPass) Debug("[RS2EX Renderer] present while a pass is active\n");
#endif

	m_Backend->Present();
	m_PresentCount++;
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
