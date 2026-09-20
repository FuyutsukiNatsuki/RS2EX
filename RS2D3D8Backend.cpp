//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Device lifecycle ownership arrives here in stages; each stage is its own
//	commit so the move stays reviewable.  Until a stage lands, the method below
//	says so rather than pretending to own something it does not.

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D8Backend.h"

CRS2D3D8Backend::CRS2D3D8Backend()
	: m_Initialized(false),
	  m_ViewportWidth(1),
	  m_ViewportHeight(1)
{
}

CRS2D3D8Backend::~CRS2D3D8Backend(){
	Shutdown();
}

const char *CRS2D3D8Backend::GetName() const{
	return "Direct3D 8 (Legacy)";
}

/*
 *	Re-seed the tracked viewport from the current back buffer
 *
 *	Direct3D 8 silently resets the viewport to the full render target on both
 *	CreateDevice() and Reset(), so the tracked size has to follow.
 */
void CRS2D3D8Backend::SyncViewportToBackBuffer(){
	m_ViewportWidth = sv3.d3dpp.BackBufferWidth;
	m_ViewportHeight = sv3.d3dpp.BackBufferHeight;
}

//	--- not yet owned by the backend -------------------------------------------
//	Filled in by the initialization, reset, render-pass and viewport commits.

bool CRS2D3D8Backend::Initialize(int width, int height){
	(void)width;
	(void)height;
	return false;
}

void CRS2D3D8Backend::Shutdown(){
}

bool CRS2D3D8Backend::BeginRenderPass(unsigned int clearColor, bool clearColorBuffer){
	(void)clearColor;
	(void)clearColorBuffer;
	return false;
}

void CRS2D3D8Backend::EndRenderPass(){
}

void CRS2D3D8Backend::Present(){
}

bool CRS2D3D8Backend::Reset(){
	return false;
}

void CRS2D3D8Backend::SetViewport(
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height,
	float minZ,
	float maxZ
){
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)minZ;
	(void)maxZ;
}

void CRS2D3D8Backend::GetViewportSize(unsigned int *width, unsigned int *height) const{
	if(width) *width = m_ViewportWidth;
	if(height) *height = m_ViewportHeight;
}
