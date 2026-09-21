//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//	Modified for RS2EX on 2026-09-22.
//
//	The Direct3D 12 renderer backend.
//
//	Built in stages, one work package per commit, and each stage says what it
//	does not do yet rather than pretending otherwise.  This one owns the
//	device, the command queue, two frame contexts and the fence.  There is no
//	swap chain and nothing is drawn or presented, so BeginRenderPass refuses
//	and the renderer will not select this backend for a normal run yet.
//
//	Nothing here belongs in sv3.  sv3 is the Direct3D 8 device and its
//	parameters; a second backend keeping its objects there would make "which
//	backend is running" answerable by looking at a pointer, which is exactly
//	the habit v0.0.9 removed.
//
//	This header includes d3d12.h, so only the renderer implementation may
//	include it.  RS2Renderer.h stays free of Direct3D types.

#ifndef RS2D3D12BACKEND_H_INCLUDED
#define RS2D3D12BACKEND_H_INCLUDED

#include "RS2Renderer.h"
#include "RS2D3D12.h"

//	One command allocator per swap-chain buffer, so the allocator being reset
//	is never one the GPU is still reading.  Two buffers, two contexts.
#define RS2D3D12_FRAME_COUNT	2

class CRS2D3D12Backend : public IRS2RendererBackend
{
private:
	IDXGIFactory4 *m_Factory;
	ID3D12Device *m_Device;
	ID3D12CommandQueue *m_Queue;

	ID3D12CommandAllocator *m_Allocator[RS2D3D12_FRAME_COUNT];
	ID3D12GraphicsCommandList *m_CommandList;

	IDXGISwapChain3 *m_SwapChain;

	//	One render-target view per swap-chain buffer, and one depth buffer
	//	shared by both - only one is being written at a time.
	ID3D12DescriptorHeap *m_RtvHeap;
	ID3D12DescriptorHeap *m_DsvHeap;
	UINT m_RtvStride;
	ID3D12Resource *m_BackBuffer[RS2D3D12_FRAME_COUNT];
	ID3D12Resource *m_DepthBuffer;
	DXGI_FORMAT m_DepthFormat;

	//	False when the depth format had to fall back to one without stencil.
	//	Claiming stencil that is not there would corrupt the shadow passes the
	//	moment they were ported.
	bool m_HasStencil;

	bool m_Windowed;

	ID3D12Fence *m_Fence;
	HANDLE m_FenceEvent;

	//	The value signalled after each frame context's work, so a context can
	//	be waited for individually instead of stalling on the whole GPU.
	UINT64 m_FenceValue[RS2D3D12_FRAME_COUNT];
	UINT64 m_NextFenceValue;

	unsigned int m_FrameIndex;

	unsigned int m_Width;
	unsigned int m_Height;

	//	Reported by GetName(), so the log names the adapter that is actually
	//	running rather than the API.
	char m_Name[192];

	bool CreateDevice();
	bool CreateCommandObjects();
	bool CreateFence();
	bool CreateSwapChain(HWND window);
	bool CreateRenderTargets();
	bool CreateDepthBuffer();
	void ReleaseSizeDependentResources();
	DXGI_FORMAT ChooseDepthFormat();

public:
	CRS2D3D12Backend();
	virtual ~CRS2D3D12Backend();

	virtual bool Initialize(int width, int height);
	virtual void Shutdown();

	virtual RS2RendererBackendType GetType() const{ return RS2_RENDERER_D3D12; }

	virtual bool BeginRenderPass(unsigned int clearColor, bool clearColorBuffer);
	virtual void EndRenderPass();
	virtual void Present();

	virtual bool Reset();

	virtual void ClearTarget(unsigned int color);

	//	Direct3D 12 readback is not implemented and will not be in v0.1.0, so
	//	the capture, offscreen and pixel-read paths guarded in v0.0.9 stop here
	//	instead of reaching for a device this backend does not have.
	virtual bool SupportsReadback() const{ return false; }

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

	/*
	 *	Wait until the GPU has finished everything submitted so far.
	 *
	 *	For shutdown, resize and one-shot work only.  A per-frame wait would
	 *	serialise the pipeline for no reason; per-frame synchronisation waits
	 *	on one frame context instead.
	 */
	void WaitForGpu();

	/*
	 *	Whether the depth buffer has stencil bits.
	 *
	 *	Reported rather than assumed, because a depth-only fallback would
	 *	silently break stencil work rather than fail it.
	 */
	bool HasStencil() const{ return m_HasStencil; }

	/*
	 *	Whether a swap chain and its render targets exist.
	 */
	bool HasSwapChain() const{ return m_SwapChain!=0; }
};

#endif	//	RS2D3D12BACKEND_H_INCLUDED
