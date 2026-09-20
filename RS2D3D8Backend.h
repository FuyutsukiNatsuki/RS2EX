//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Legacy Direct3D 8 renderer backend.
//
//	The implementation here is inherited from the UDX library
//	(Copyright (c) 2002 Midikyou, lib/graphic.cpp) and moved rather than
//	rewritten: adapter selection, the HAL/HAL-SW/REF fallback chain, present
//	parameters, the depth/stencil format search and the reset sequence all keep
//	their original semantics.  See the Git history for what came from where.
//
//	This is the only component allowed to own the Direct3D 8 device.  sv3.pDev
//	stays exposed for legacy resource and draw code (plan section 6), but no new
//	high-level code may take a lifecycle dependency on it.

#ifndef RS2D3D8BACKEND_H_INCLUDED
#define RS2D3D8BACKEND_H_INCLUDED

#include "RS2Renderer.h"

class CRS2D3D8Backend : public IRS2RendererBackend
{
private:
	bool m_Initialized;

	//	Viewport size ownership.
	//
	//	Direct3D 8 changes the viewport behind our back in two places: Reset()
	//	and SetRenderTarget() both reset it to the full size of the target.
	//	Tracking it here - instead of calling GetViewport() - keeps one source
	//	of truth and gives an explicit API a state it can actually reproduce.
	unsigned int m_ViewportWidth;
	unsigned int m_ViewportHeight;

	bool CreateDevice(int width, int height);
	void SyncViewportToBackBuffer();

public:
	CRS2D3D8Backend();
	virtual ~CRS2D3D8Backend();

	virtual bool Initialize(int width, int height);
	virtual void Shutdown();

	virtual bool BeginRenderPass(unsigned int clearColor, bool clearColorBuffer);
	virtual void EndRenderPass();
	virtual void Present();

	virtual bool Reset();

	virtual void SetViewport(
		unsigned int x,
		unsigned int y,
		unsigned int width,
		unsigned int height,
		float minZ,
		float maxZ);

	virtual void GetViewportSize(
		unsigned int *width,
		unsigned int *height) const;

	virtual const char *GetName() const;
};

#endif	//	RS2D3D8BACKEND_H_INCLUDED
