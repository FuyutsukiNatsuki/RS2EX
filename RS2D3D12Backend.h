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
#include "RS2D3D12Pipeline.h"
#include "RS2D3D12Descriptors.h"
#include "RS2D3D12Texture.h"
#include "RS2D3D12Upload.h"
#include <list>

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

	//	Engine-facing mode.  A false value uses a borderless monitor-sized HWND;
	//	the D3D12 swap chain itself deliberately remains windowed so the legacy
	//	mesh importer's independent Direct3D 8 device remains available.
	bool m_Windowed;

	//	So a minimised window says so once instead of once per frame.
	bool m_ZeroSizeLogged;

	//	Set once the adapter has reported the device gone.  There is no
	//	recovery in v0.1.0, so this only stops the log repeating itself.
	bool m_DeviceRemoved;

	//	What the device's final Release() returned.  Zero is correct.
	unsigned int m_DeviceReferencesAfterShutdown;

	//	The command list is open across every logical pass of one displayed
	//	frame, because RailSim runs several - stereo, window division - before
	//	a single Present.
	bool m_FrameRecording;
	bool m_PassActive;

	//	What SetViewport() was last told.  Re-submitted whenever the command
	//	list is reset, because a reset forgets it.
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_Scissor;


	//	The debug layer's message queue, when there is one.  Null in release
	//	builds and on a machine without the graphics tools.
	ID3D12InfoQueue *m_InfoQueue;

	//	Root signature, shaders and the pipeline-state cache.  Built once and
	//	shared by every frame; only the states inside it are added to.
	CRS2D3D12Pipeline m_Pipeline;

	//	Scratch memory, one block per frame context.  Reset when its context is
	//	reused, which only happens after that context's fence has completed.
	CRS2D3D12Upload m_Upload[RS2D3D12_FRAME_COUNT];

	//	Texture copies use independent one-shot command objects and a dedicated
	//	fence timeline on the same direct queue.  This keeps uploads out of a
	//	possibly open frame list without a whole-GPU wait per texture.
	CRS2D3D12TextureUpload m_TextureUpload;
	CRS2D3D12Descriptors m_Descriptors;
	struct RetiredTexture
	{
		ID3D12Resource *resource;
		RS2D3D12SrvSlot slot;
		UINT64 fenceValue;
	};
	std::list<RetiredTexture> m_RetiredTextures;

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

	void WaitForFrame(unsigned int index);
	void SubmitBarrier(
		ID3D12Resource *resource,
		D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after);
	void BindTargets();
	bool BeginRecording();
	bool ResizeIfNeeded();
	void ReportDeviceFailure(const char *what, long hr);
	void PublishCompatibilityState();
	bool CreatePipeline();

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
	 *	How many debug-layer messages of this severity or worse are stored,
	 *	logging the first few.
	 *
	 *	severity	: D3D12_MESSAGE_SEVERITY_ERROR and so on
	 *	returns		: the count, and 0 when there is no debug layer
	 *
	 *	Those messages go to OutputDebugString, which the program's own log
	 *	never sees, so without this a claim that the debug layer said nothing
	 *	would just be an assumption.
	 */
	unsigned int CountDebugMessages(int severity);

	/*
	 *	Whether there is a debug layer to read at all.
	 *
	 *	Release builds do not enable it, so a zero error count there means
	 *	"nothing was checked" rather than "nothing was wrong".
	 */
	bool HasDebugLayer() const{ return m_InfoQueue!=0; }

	/*
	 *	How many pipeline states have been built.
	 *
	 *	A draw that needed a new one and could not get it draws nothing, so a
	 *	count that stops growing while the picture is wrong says where to look.
	 */
	unsigned int GetPipelineStateCount() const{ return m_Pipeline.GetStateCount(); }

	/*
	 *	The most scratch memory any one frame has used, in bytes.
	 */
	unsigned int GetUploadPeak() const;

	/*
	 *	What a draw needs from the backend.
	 *
	 *	Only the draw implementation calls these, and only while a frame is
	 *	being recorded - which IsRecording() is how it checks.
	 */
	bool IsRecording() const{ return m_FrameRecording; }
	ID3D12GraphicsCommandList *GetCommandList() const{ return m_CommandList; }
	CRS2D3D12Upload *GetUpload(){ return &m_Upload[m_FrameIndex]; }
	CRS2D3D12Pipeline *GetPipeline(){ return &m_Pipeline; }
	CRS2D3D12TextureUpload *GetTextureUpload(){ return &m_TextureUpload; }
	CRS2D3D12Descriptors *GetDescriptors(){ return &m_Descriptors; }
	ID3D12Device *GetDevice() const{ return m_Device; }
	void RetireTexture(ID3D12Resource *resource, const RS2D3D12SrvSlot &slot);
	void CollectRetiredTextures();

	/*
	 *	Whether the device has been reported gone.
	 */
	bool IsDeviceRemoved() const{ return m_DeviceRemoved; }

	/*
	 *	References the device still had when it was released.
	 *
	 *	Zero means everything created from it was released first.  Read
	 *	after Shutdown().
	 */
	unsigned int GetDeviceReferencesAfterShutdown() const{
		return m_DeviceReferencesAfterShutdown; }

	/*
	 *	Whether a swap chain and its render targets exist.
	 */
	bool HasSwapChain() const{ return m_SwapChain!=0; }

	/*
	 *	Which frame context the next frame will use.
	 *
	 *	Exposed for the smoke test, which checks that it actually alternates -
	 *	a swap chain that never advances presents the same buffer for ever and
	 *	looks fine until something moves.
	 */
	unsigned int GetFrameIndex() const{ return m_FrameIndex; }
};

/*
 *	The backend the renderer is currently running, or null.
 *
 *	The draw implementation needs the command list and this frame's scratch
 *	memory, and it is called from the neutral dispatch layer rather than from
 *	the backend, so it has to be able to find it.  Set while the backend is
 *	initialised and cleared when it shuts down.
 */
CRS2D3D12Backend *RS2D3D12GetActiveBackend();

#endif	//	RS2D3D12BACKEND_H_INCLUDED
