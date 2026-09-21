//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//	Modified for RS2EX on 2026-09-22.
//
//	See RS2D3D12Backend.h.

#include "stdafx.h"
#include "RS2D3D12Backend.h"

//	The same inputs the Direct3D 8 backend consults, so both backends
//	answer "windowed or not" the same way.
extern bool g_FullScreen;
extern char *g_PluginViewArg;

/*
 *	Turn on the debug layer, if there is one.
 *
 *	Must happen before device creation or it has no effect.  A missing debug
 *	layer is normal on a machine without the graphics tools installed, so it is
 *	reported and then ignored: refusing to run without it would make a
 *	developer build depend on an optional Windows feature.
 *
 *	GPU-based validation is deliberately not enabled.  It is expensive enough
 *	to change the timing of everything around it, and this backend has no draw
 *	calls for it to validate yet.
 */
static void RS2D3D12_EnableDebugLayer(){
#ifdef _DEBUG
	ID3D12Debug *debug = 0;

	if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))){
		debug->EnableDebugLayer();
		debug->Release();
		Debug("[RS2EX D3D12] debug layer enabled\n");
	}else{
		Debug("[RS2EX D3D12] no debug layer available\n");
	}
#endif
}

CRS2D3D12Backend::CRS2D3D12Backend()
	: m_Factory(0),
	  m_Device(0),
	  m_Queue(0),
	  m_CommandList(0),
	  m_SwapChain(0),
	  m_RtvHeap(0),
	  m_DsvHeap(0),
	  m_RtvStride(0),
	  m_DepthBuffer(0),
	  m_DepthFormat(DXGI_FORMAT_UNKNOWN),
	  m_HasStencil(false),
	  m_Windowed(true),
	  m_InfoQueue(0),
	  m_ZeroSizeLogged(false),
	  m_FrameRecording(false),
	  m_PassActive(false),
	  m_Fence(0),
	  m_FenceEvent(NULL),
	  m_NextFenceValue(1),
	  m_FrameIndex(0),
	  m_Width(0),
	  m_Height(0)
{
	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++){
		m_Allocator[i] = 0;
		m_BackBuffer[i] = 0;
		m_FenceValue[i] = 0;
	}
	ZeroMemory(&m_Viewport, sizeof(m_Viewport));
	ZeroMemory(&m_Scissor, sizeof(m_Scissor));
	lstrcpynA(m_Name, "Direct3D 12", sizeof(m_Name));
}

CRS2D3D12Backend::~CRS2D3D12Backend(){
	Shutdown();
}

/*
 *	Pick an adapter and create the device on it.
 *
 *	The first hardware adapter that can reach the minimum feature level wins.
 *	Software adapters are skipped: WARP would succeed on a machine whose real
 *	adapter or driver cannot run Direct3D 12, and a test that silently ran on
 *	WARP would report success for a configuration nobody plays on.  A
 *	deliberate software path can be its own switch when something needs it.
 */
bool CRS2D3D12Backend::CreateDevice(){
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_Factory));

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] CreateDXGIFactory1 failed (0x%08lx)\n", (unsigned long)hr);
		return false;
	}

	UINT index;

	for(index = 0; ; index++){
		IDXGIAdapter1 *adapter = 0;

		if(m_Factory->EnumAdapters1(index, &adapter)==DXGI_ERROR_NOT_FOUND) break;
		if(!adapter) continue;

		DXGI_ADAPTER_DESC1 desc;

		if(FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)){
			adapter->Release();
			continue;
		}

		hr = D3D12CreateDevice(
			adapter, RS2D3D12_MIN_FEATURE_LEVEL, IID_PPV_ARGS(&m_Device));

		if(SUCCEEDED(hr)){
			char name[160];

			if(WideCharToMultiByte(CP_ACP, 0, desc.Description, -1,
					name, sizeof(name), NULL, NULL)<=0)
				lstrcpynA(name, "(description unavailable)", sizeof(name));

			wsprintfA(m_Name, "Direct3D 12 (%s)", name);
			Debug("[RS2EX D3D12] adapter %u: %s (vendor %04x device %04x)\n",
				index, name, (unsigned)desc.VendorId, (unsigned)desc.DeviceId);
			Debug("[RS2EX D3D12] video memory %u MB\n",
				(unsigned)(desc.DedicatedVideoMemory/(1024*1024)));

			//	Only succeeds when the debug layer is active, which is the
			//	only time there is anything to read.
			m_Device->QueryInterface(IID_PPV_ARGS(&m_InfoQueue));

			adapter->Release();
			return true;
		}

		Debug("[RS2EX D3D12] adapter %u rejected (0x%08lx)\n",
			index, (unsigned long)hr);
		adapter->Release();
	}

	Debug("[RS2EX D3D12] no hardware adapter reached the minimum feature level\n");
	return false;
}

/*
 *	The command queue, the frame allocators and the one command list.
 *
 *	One direct queue: there is no compute or copy work to overlap yet, and a
 *	queue that exists without work on it is a thing to keep synchronised for
 *	nothing.
 *
 *	The list is created against frame 0's allocator and closed immediately,
 *	because a freshly created list is open and the frame code expects to open
 *	it itself.
 */
bool CRS2D3D12Backend::CreateCommandObjects(){
	D3D12_COMMAND_QUEUE_DESC desc;

	ZeroMemory(&desc, sizeof(desc));
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	HRESULT hr = m_Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_Queue));

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] CreateCommandQueue failed (0x%08lx)\n", (unsigned long)hr);
		return false;
	}

	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++){
		hr = m_Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Allocator[i]));

		if(FAILED(hr)){
			Debug("[RS2EX D3D12] CreateCommandAllocator %u failed (0x%08lx)\n",
				i, (unsigned long)hr);
			return false;
		}
	}

	hr = m_Device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Allocator[0], NULL,
		IID_PPV_ARGS(&m_CommandList));

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] CreateCommandList failed (0x%08lx)\n", (unsigned long)hr);
		return false;
	}

	m_CommandList->Close();
	return true;
}

bool CRS2D3D12Backend::CreateFence(){
	HRESULT hr = m_Device->CreateFence(
		0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] CreateFence failed (0x%08lx)\n", (unsigned long)hr);
		return false;
	}

	m_FenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!m_FenceEvent){
		Debug("[RS2EX D3D12] CreateEvent failed (%lu)\n", GetLastError());
		return false;
	}
	return true;
}

/*
 *	Pick a depth/stencil format the device actually supports.
 *
 *	D24S8 first, because that is what the Direct3D 8 backend asks for and what
 *	the stencil-based shadow passes expect.  A fallback is recorded rather than
 *	glossed over: a backend that claimed stencil it did not have would corrupt
 *	those passes the moment they were ported, and quietly.
 */
DXGI_FORMAT CRS2D3D12Backend::ChooseDepthFormat(){
	static const DXGI_FORMAT candidates[] = {
		DXGI_FORMAT_D24_UNORM_S8_UINT,
		DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
		DXGI_FORMAT_D32_FLOAT,
		DXGI_FORMAT_D16_UNORM
	};

	int i;

	for(i = 0; i<(int)(sizeof(candidates)/sizeof(candidates[0])); i++){
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support;

		ZeroMemory(&support, sizeof(support));
		support.Format = candidates[i];

		if(FAILED(m_Device->CheckFeatureSupport(
				D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
			continue;

		if(!(support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)) continue;

		m_HasStencil = (candidates[i]==DXGI_FORMAT_D24_UNORM_S8_UINT)
			|| (candidates[i]==DXGI_FORMAT_D32_FLOAT_S8X24_UINT);

		if(i)
			Debug("[RS2EX D3D12] depth format %d, not the preferred D24S8 (stencil %s)\n",
				(int)candidates[i], m_HasStencil ? "yes" : "NO");
		return candidates[i];
	}

	Debug("[RS2EX D3D12] no usable depth/stencil format\n");
	m_HasStencil = false;
	return DXGI_FORMAT_UNKNOWN;
}

/*
 *	The swap chain.
 *
 *	window	: the application window
 *
 *	Two buffers and flip-discard, which is what Direct3D 12 expects and what
 *	DXGI is optimised for; the Direct3D 8 backend's single back buffer and blt
 *	model has no flip-model equivalent.  No multisampling - flip-model swap
 *	chains do not allow it, and none was used before.
 *
 *	DXGI's own Alt+Enter handling is switched off.  RailSim already owns the
 *	fullscreen decision and its window architecture, and letting DXGI change
 *	mode behind the program would fight it.
 */
bool CRS2D3D12Backend::CreateSwapChain(
	HWND window	//	application window
){
	if(!window){
		Debug("[RS2EX D3D12] no window to present to\n");
		return false;
	}

	//	The same question the Direct3D 8 backend asks, from the same inputs, so
	//	the two backends agree about what was requested.
	m_Windowed = (!g_FullScreen || g_PluginViewArg || CheckArguments("-win"))!=0;

	DXGI_SWAP_CHAIN_DESC1 desc;

	ZeroMemory(&desc, sizeof(desc));
	desc.Width = m_Width;
	desc.Height = m_Height;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = RS2D3D12_FRAME_COUNT;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs;

	ZeroMemory(&fs, sizeof(fs));
	fs.Windowed = m_Windowed ? TRUE : FALSE;

	IDXGISwapChain1 *chain1 = 0;
	HRESULT hr = m_Factory->CreateSwapChainForHwnd(
		m_Queue, window, &desc, &fs, NULL, &chain1);

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] CreateSwapChainForHwnd failed (0x%08lx)\n",
			(unsigned long)hr);
		return false;
	}

	hr = chain1->QueryInterface(IID_PPV_ARGS(&m_SwapChain));
	chain1->Release();

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] IDXGISwapChain3 unavailable (0x%08lx)\n",
			(unsigned long)hr);
		return false;
	}

	m_Factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

	Debug("[RS2EX D3D12] swap chain %u x %u, %d buffers, %s\n",
		m_Width, m_Height, RS2D3D12_FRAME_COUNT,
		m_Windowed ? "windowed" : "fullscreen requested");
	return true;
}

/*
 *	A view onto each swap-chain buffer.
 *
 *	The descriptor increment is asked for rather than assumed: it is a device
 *	property, and hard-coding it is a classic way to write something that works
 *	on one vendor.
 */
bool CRS2D3D12Backend::CreateRenderTargets(){
	if(!m_RtvHeap){
		D3D12_DESCRIPTOR_HEAP_DESC desc;

		ZeroMemory(&desc, sizeof(desc));
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = RS2D3D12_FRAME_COUNT;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		if(FAILED(m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_RtvHeap)))){
			Debug("[RS2EX D3D12] RTV heap creation failed\n");
			return false;
		}
	}

	m_RtvStride = m_Device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++){
		if(FAILED(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffer[i])))){
			Debug("[RS2EX D3D12] swap-chain buffer %u unavailable\n", i);
			return false;
		}
		m_Device->CreateRenderTargetView(m_BackBuffer[i], NULL, handle);
		handle.ptr += m_RtvStride;
	}
	return true;
}

/*
 *	One depth buffer, the size of the back buffers.
 *
 *	In the bootstrap rather than deferred, because BeginRenderPass already
 *	promises a depth clear on every pass.  A backend that accepted that promise
 *	without a depth buffer would be lying about the one thing v0.0.9 was caught
 *	getting wrong.
 *
 *	One buffer serves both frame contexts: only one is being written at a time.
 */
bool CRS2D3D12Backend::CreateDepthBuffer(){
	m_DepthFormat = ChooseDepthFormat();
	if(m_DepthFormat==DXGI_FORMAT_UNKNOWN) return false;

	if(!m_DsvHeap){
		D3D12_DESCRIPTOR_HEAP_DESC heap;

		ZeroMemory(&heap, sizeof(heap));
		heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heap.NumDescriptors = 1;

		if(FAILED(m_Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_DsvHeap)))){
			Debug("[RS2EX D3D12] DSV heap creation failed\n");
			return false;
		}
	}

	D3D12_HEAP_PROPERTIES props;

	ZeroMemory(&props, sizeof(props));
	props.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC res;

	ZeroMemory(&res, sizeof(res));
	res.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	res.Width = m_Width;
	res.Height = m_Height;
	res.DepthOrArraySize = 1;
	res.MipLevels = 1;
	res.Format = m_DepthFormat;
	res.SampleDesc.Count = 1;
	res.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	res.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	//	The optimised clear value has to match what the clear actually uses, or
	//	the driver loses its fast path and the debug layer says so.
	D3D12_CLEAR_VALUE clear;

	ZeroMemory(&clear, sizeof(clear));
	clear.Format = m_DepthFormat;
	clear.DepthStencil.Depth = 1.0f;
	clear.DepthStencil.Stencil = 0;

	HRESULT hr = m_Device->CreateCommittedResource(
		&props, D3D12_HEAP_FLAG_NONE, &res,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_DepthBuffer));

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] depth buffer creation failed (0x%08lx)\n",
			(unsigned long)hr);
		return false;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC view;

	ZeroMemory(&view, sizeof(view));
	view.Format = m_DepthFormat;
	view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	m_Device->CreateDepthStencilView(
		m_DepthBuffer, &view, m_DsvHeap->GetCPUDescriptorHandleForHeapStart());

	Debug("[RS2EX D3D12] depth buffer %u x %u, stencil %s\n",
		m_Width, m_Height, m_HasStencil ? "yes" : "no");
	return true;
}

/*
 *	Release what depends on the back-buffer size.
 *
 *	Separate from Shutdown() because resize needs exactly this and nothing
 *	else: the device, queue, allocators and fence all survive a resize, and
 *	rebuilding them would be slower and a chance to get the ordering wrong.
 *	The descriptor heaps survive too - only the views written into them change.
 */
void CRS2D3D12Backend::ReleaseSizeDependentResources(){
	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++) RELEASE(m_BackBuffer[i]);
	RELEASE(m_DepthBuffer);
}

/*
 *	Bring the backend up.
 *
 *	width	: back buffer width
 *	height	: back buffer height
 *
 *	A partial failure releases everything rather than leaving the caller with
 *	half a backend: CRS2Renderer deletes it on failure, and a destructor that
 *	had to cope with every partial state is how leaks get written.
 */
bool CRS2D3D12Backend::Initialize(int width, int height){
	RS2D3D12_EnableDebugLayer();

	//	A window with no area cannot have a swap chain, and asking for one
	//	is how a minimised start-up turns into a failure instead of a wait.
	if(width<=0 || height<=0){
		Debug("[RS2EX D3D12] refusing to initialise at %d x %d\n", width, height);
		return false;
	}

	m_Width = (unsigned int)width;
	m_Height = (unsigned int)height;
	m_FrameIndex = 0;

	if(!CreateDevice() || !CreateCommandObjects() || !CreateFence()
			|| !CreateSwapChain(svw.hWnd) || !CreateRenderTargets()
			|| !CreateDepthBuffer()){
		Shutdown();
		return false;
	}

	//	The whole target, until something asks for less.  Leaving this zeroed
	//	would clip every pass away and look exactly like a backend that draws
	//	nothing.
	SetViewport(0, 0, m_Width, m_Height, 0.0f, 1.0f);

	Debug("[RS2EX D3D12] ready: %d frame contexts, %s\n",
		RS2D3D12_FRAME_COUNT, GetName());
	return true;
}

void CRS2D3D12Backend::WaitForGpu(){
	if(!m_Queue || !m_Fence || !m_FenceEvent) return;

	const UINT64 target = m_NextFenceValue++;

	if(FAILED(m_Queue->Signal(m_Fence, target))) return;

	if(m_Fence->GetCompletedValue()<target){
		if(SUCCEEDED(m_Fence->SetEventOnCompletion(target, m_FenceEvent)))
			WaitForSingleObject(m_FenceEvent, INFINITE);
	}
}

/*
 *	Release everything, in the reverse order of creation.
 *
 *	The GPU is waited for first.  Releasing a command allocator or a resource
 *	the GPU is still reading is the classic Direct3D 12 crash, and it happens
 *	at shutdown more often than anywhere else because that is where everything
 *	is released at once.
 *
 *	Safe to call twice, and safe on a backend that failed half way through
 *	Initialize().
 */
void CRS2D3D12Backend::Shutdown(){
	WaitForGpu();

	if(m_FenceEvent){
		CloseHandle(m_FenceEvent);
		m_FenceEvent = NULL;
	}

	RELEASE(m_InfoQueue);
	RELEASE(m_Fence);
	RELEASE(m_CommandList);

	ReleaseSizeDependentResources();
	RELEASE(m_RtvHeap);
	RELEASE(m_DsvHeap);

	//	A swap chain left in exclusive fullscreen cannot be released, so it
	//	is put back into windowed mode first.  This is the documented way to
	//	leave, and skipping it is how a shutdown turns into a hang.
	if(m_SwapChain){
		BOOL fullscreen = FALSE;

		if(SUCCEEDED(m_SwapChain->GetFullscreenState(&fullscreen, NULL)) && fullscreen)
			m_SwapChain->SetFullscreenState(FALSE, NULL);
	}
	RELEASE(m_SwapChain);

	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++){
		RELEASE(m_Allocator[i]);
		m_FenceValue[i] = 0;
	}

	RELEASE(m_Queue);
	RELEASE(m_Device);
	RELEASE(m_Factory);

	m_NextFenceValue = 1;
	m_FrameIndex = 0;
	m_Width = m_Height = 0;
	m_RtvStride = 0;
	m_DepthFormat = DXGI_FORMAT_UNKNOWN;
	m_HasStencil = false;
}

/*
 *	Wait until one frame context's submitted work has finished.
 *
 *	index	: frame context to wait for
 *
 *	This is the only wait a normal frame does, and usually it does not block at
 *	all: by the time a context comes round again the GPU has long finished with
 *	it.  Waiting for the whole GPU here instead would serialise the pipeline
 *	for nothing.
 */
void CRS2D3D12Backend::WaitForFrame(
	unsigned int index	//	frame context
){
	if(!m_Fence || !m_FenceEvent) return;
	if(index>=RS2D3D12_FRAME_COUNT) return;

	const UINT64 target = m_FenceValue[index];

	if(!target || m_Fence->GetCompletedValue()>=target) return;

	if(SUCCEEDED(m_Fence->SetEventOnCompletion(target, m_FenceEvent)))
		WaitForSingleObject(m_FenceEvent, INFINITE);
}

/*
 *	One resource transition.
 *
 *	resource	: what is changing state
 *	before		: the state it is in
 *	after		: the state it should be in
 *
 *	Written out rather than taken from d3dx12.h: that header is a sample
 *	convenience, not part of the SDK, and this build has no other reason to
 *	carry it.
 */
void CRS2D3D12Backend::SubmitBarrier(
	ID3D12Resource *resource,		//	resource to transition
	D3D12_RESOURCE_STATES before,	//	current state
	D3D12_RESOURCE_STATES after		//	wanted state
){
	if(!m_CommandList || !resource) return;

	D3D12_RESOURCE_BARRIER barrier;

	ZeroMemory(&barrier, sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	m_CommandList->ResourceBarrier(1, &barrier);
}

/*
 *	Point the command list at the current back buffer and the depth buffer, and
 *	restore the viewport and scissor.
 *
 *	Done on every pass rather than only on the first.  A logical pass may have
 *	changed the viewport - window division does exactly that - and a command
 *	list reset forgets both, so there is no state here worth trying to track.
 */
void CRS2D3D12Backend::BindTargets(){
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_DsvHeap->GetCPUDescriptorHandleForHeapStart();

	rtv.ptr += m_FrameIndex*m_RtvStride;

	m_CommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	m_CommandList->RSSetViewports(1, &m_Viewport);
	m_CommandList->RSSetScissorRects(1, &m_Scissor);
}

/*
 *	Open the command list for a displayed frame.
 *
 *	returns	: false if the list could not be opened
 *
 *	The allocator is only reset once its frame context has finished on the GPU.
 *	Resetting one the GPU is still reading is the classic Direct3D 12 crash,
 *	and it is silent until it is not.
 */
bool CRS2D3D12Backend::BeginRecording(){
	WaitForFrame(m_FrameIndex);

	if(FAILED(m_Allocator[m_FrameIndex]->Reset())) return false;
	if(FAILED(m_CommandList->Reset(m_Allocator[m_FrameIndex], NULL))) return false;

	SubmitBarrier(m_BackBuffer[m_FrameIndex],
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	m_FrameRecording = true;
	return true;
}

/*
 *	Begin a logical render pass.
 *
 *	clearColor		: clear colour, used only when clearColorBuffer is true
 *	clearColorBuffer: false to keep the colour buffer and clear depth only
 *
 *	Several of these happen before one Present - stereo draws twice, window
 *	division up to four times - so only the first opens the command list.  The
 *	Direct3D 8 backend has the same split; here it decides whether a frame is
 *	being recorded at all.
 */
bool CRS2D3D12Backend::BeginRenderPass(unsigned int clearColor, bool clearColorBuffer){
	if(!m_SwapChain || !m_CommandList) return false;

	//	Checked once per displayed frame, before anything is recorded.  Doing
	//	it between logical passes would resize out from under a frame that is
	//	half submitted.
	if(!m_FrameRecording){
		if(!ResizeIfNeeded()) return false;
		if(!BeginRecording()) return false;
	}

	BindTargets();

	if(clearColorBuffer){
		D3D12_CPU_DESCRIPTOR_HANDLE rtv =
			m_RtvHeap->GetCPUDescriptorHandleForHeapStart();

		rtv.ptr += m_FrameIndex*m_RtvStride;

		//	The engine colour is 0xAARRGGBB, the same as the Direct3D 8
		//	backend receives.  Alpha is dropped rather than carried into the
		//	back buffer: there is nothing behind it to blend with.
		const float rgba[4] = {
			((clearColor>>16)&0xff)/255.0f,
			((clearColor>>8)&0xff)/255.0f,
			(clearColor&0xff)/255.0f,
			1.0f
		};

		m_CommandList->ClearRenderTargetView(rtv, rgba, 0, NULL);
	}

	//	Depth is cleared on every pass whether or not the colour is, which is
	//	the promise the interface has made since v0.0.4.  Stencil goes with it
	//	only when the format actually has stencil bits.
	D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;

	if(m_HasStencil)
		flags = (D3D12_CLEAR_FLAGS)(flags|D3D12_CLEAR_FLAG_STENCIL);

	m_CommandList->ClearDepthStencilView(
		m_DsvHeap->GetCPUDescriptorHandleForHeapStart(), flags, 1.0f, 0, 0, NULL);

	m_PassActive = true;
	return true;
}

/*
 *	End a logical pass.
 *
 *	The command list stays open.  Closing it here would turn every logical pass
 *	into its own submission, which is not what the interface means and would
 *	present a half-drawn frame in stereo or window-division mode.
 */
void CRS2D3D12Backend::EndRenderPass(){
	m_PassActive = false;
}

/*
 *	Submit the frame and show it.
 *
 *	Present(1, 0) keeps the vertical sync the Direct3D 8 backend has always
 *	presented with; RailSim's simulation is driven by its own clock, so the
 *	frame rate is a display decision and changing it here would be an
 *	unrelated change.
 */
void CRS2D3D12Backend::Present(){
	if(!m_SwapChain || !m_FrameRecording) return;

	SubmitBarrier(m_BackBuffer[m_FrameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	if(FAILED(m_CommandList->Close())){
		Debug("[RS2EX D3D12] command list would not close\n");
		m_FrameRecording = false;
		return;
	}

	ID3D12CommandList *lists[1] = { m_CommandList };

	m_Queue->ExecuteCommandLists(1, lists);

	const HRESULT hr = m_SwapChain->Present(1, 0);

	if(FAILED(hr)) Debug("[RS2EX D3D12] Present failed (0x%08lx)\n", (unsigned long)hr);

	//	Remember what this context has to finish before its allocator may be
	//	reset again, then move on to the buffer DXGI has just made current.
	m_FenceValue[m_FrameIndex] = m_NextFenceValue;
	m_Queue->Signal(m_Fence, m_NextFenceValue);
	m_NextFenceValue++;

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	m_FrameRecording = false;
	m_PassActive = false;
}

/*
 *	Rebuild the resources that depend on the back-buffer size.
 *
 *	Reset() means this for a Direct3D 12 backend, not the device reset it is
 *	named after.  There is no such thing as a lost Direct3D 12 device to
 *	recover from: a device that goes away is removed, which is a different
 *	situation with a different answer.
 *
 *	The device, queue, allocators, fence and descriptor heaps all survive.
 *	Only the back buffers, the depth buffer and the views into the heaps are
 *	rebuilt.
 */
bool CRS2D3D12Backend::Reset(){
	if(!m_SwapChain) return false;

	const unsigned int width = (svw.winW>0) ? (unsigned int)svw.winW : 0;
	const unsigned int height = (svw.winH>0) ? (unsigned int)svw.winH : 0;

	if(!width || !height){
		Debug("[RS2EX D3D12] refusing to resize to %u x %u\n", width, height);
		return false;
	}

	//	Everything in flight has to finish first: ResizeBuffers releases the
	//	back buffers, and releasing one the GPU is still writing into is the
	//	same crash as resetting a live allocator, with the same silence.
	WaitForGpu();
	ReleaseSizeDependentResources();

	m_Width = width;
	m_Height = height;

	//	DXGI_FORMAT_UNKNOWN keeps the format the swap chain already has, which
	//	is the point: resize changes the size and nothing else.
	const HRESULT hr = m_SwapChain->ResizeBuffers(
		RS2D3D12_FRAME_COUNT, m_Width, m_Height, DXGI_FORMAT_UNKNOWN, 0);

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] ResizeBuffers failed (0x%08lx)\n", (unsigned long)hr);
		if(hr==DXGI_ERROR_DEVICE_REMOVED || hr==DXGI_ERROR_DEVICE_RESET)
			Debug("[RS2EX D3D12] device removed: 0x%08lx\n",
				(unsigned long)m_Device->GetDeviceRemovedReason());
		return false;
	}

	//	Flip-model buffer order changes across a resize, so the current index
	//	has to be asked for again rather than assumed to be unchanged.
	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

	//	Nothing is outstanding after the wait above, so the recorded fence
	//	values would only make the next frame wait on work that has already
	//	finished.
	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++) m_FenceValue[i] = 0;

	if(!CreateRenderTargets() || !CreateDepthBuffer()) return false;

	SetViewport(0, 0, m_Width, m_Height, 0.0f, 1.0f);

	Debug("[RS2EX D3D12] resized to %u x %u\n", m_Width, m_Height);
	return true;
}

/*
 *	Resize when the window has changed size, and not otherwise.
 *
 *	returns	: false only when a resize was needed and failed
 *
 *	A minimised window reports a client area of zero, and asking DXGI for a
 *	zero-sized back buffer is an error rather than a no-op.  Skipping is all
 *	this does: the tracked size is left alone, the frame renders into the
 *	buffers that already exist, and the first frame with a real size resizes
 *	normally.  Nothing is latched, so no resize can be lost.  The Direct3D 8
 *	backend has the same guard for the same reason.
 */
bool CRS2D3D12Backend::ResizeIfNeeded(){
	if(svw.winW<=0 || svw.winH<=0){
		if(!m_ZeroSizeLogged){
			Debug("[RS2EX D3D12] window has zero size: skipping resize\n");
			m_ZeroSizeLogged = true;
		}
		return true;
	}

	m_ZeroSizeLogged = false;

	if((unsigned int)svw.winW==m_Width && (unsigned int)svw.winH==m_Height)
		return true;

	return Reset();
}

/*
 *	Clear the whole target, outside any frame.
 *
 *	color	: clear colour, 0xAARRGGBB
 *
 *	This is the one-time start-up clear, and it can arrive when no frame is
 *	being recorded, so it records and submits its own work and waits for it.
 *	Nothing here is on a hot path - it runs once - so correctness is the only
 *	consideration.
 */
void CRS2D3D12Backend::ClearTarget(unsigned int color){
	if(!m_SwapChain || !m_CommandList) return;

	//	Not while a frame is open: that command list belongs to the frame, and
	//	borrowing it would submit half of one.
	if(m_FrameRecording) return;

	WaitForGpu();

	if(FAILED(m_Allocator[m_FrameIndex]->Reset())) return;
	if(FAILED(m_CommandList->Reset(m_Allocator[m_FrameIndex], NULL))) return;

	SubmitBarrier(m_BackBuffer[m_FrameIndex],
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();

	rtv.ptr += m_FrameIndex*m_RtvStride;

	const float rgba[4] = {
		((color>>16)&0xff)/255.0f,
		((color>>8)&0xff)/255.0f,
		(color&0xff)/255.0f,
		1.0f
	};

	D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;

	if(m_HasStencil)
		flags = (D3D12_CLEAR_FLAGS)(flags|D3D12_CLEAR_FLAG_STENCIL);

	m_CommandList->ClearRenderTargetView(rtv, rgba, 0, NULL);
	m_CommandList->ClearDepthStencilView(
		m_DsvHeap->GetCPUDescriptorHandleForHeapStart(), flags, 1.0f, 0, 0, NULL);

	SubmitBarrier(m_BackBuffer[m_FrameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	if(FAILED(m_CommandList->Close())) return;

	ID3D12CommandList *lists[1] = { m_CommandList };

	m_Queue->ExecuteCommandLists(1, lists);
	WaitForGpu();
}

/*
 *	Set the viewport, and the scissor that goes with it.
 *
 *	x, y			: top-left corner
 *	width, height	: size
 *	minZ, maxZ		: depth range
 *
 *	Cached unconditionally and submitted only when a command list is open.
 *	Window division calls this between logical passes of the same frame, and
 *	the first call of a frame arrives before the list has been reset, so a
 *	version that only submitted would lose it.
 *
 *	Direct3D 12 has no implicit scissor: geometry outside the viewport is still
 *	rasterised unless a scissor rectangle says otherwise, where Direct3D 8
 *	clipped to the viewport on its own.  Deriving the scissor from the same
 *	rectangle keeps the behaviour the engine already expects, without adding a
 *	scissor concept to the interface that nothing would set.
 */
void CRS2D3D12Backend::SetViewport(
	unsigned int x,			//	left
	unsigned int y,			//	top
	unsigned int width,		//	width
	unsigned int height,	//	height
	float minZ,				//	near depth
	float maxZ				//	far depth
){
	m_Viewport.TopLeftX = (FLOAT)x;
	m_Viewport.TopLeftY = (FLOAT)y;
	m_Viewport.Width = (FLOAT)width;
	m_Viewport.Height = (FLOAT)height;
	m_Viewport.MinDepth = minZ;
	m_Viewport.MaxDepth = maxZ;

	m_Scissor.left = (LONG)x;
	m_Scissor.top = (LONG)y;
	m_Scissor.right = (LONG)(x+width);
	m_Scissor.bottom = (LONG)(y+height);

	if(m_FrameRecording && m_CommandList){
		m_CommandList->RSSetViewports(1, &m_Viewport);
		m_CommandList->RSSetScissorRects(1, &m_Scissor);
	}
}

void CRS2D3D12Backend::GetViewportSize(
	unsigned int *width,
	unsigned int *height
) const{
	if(width) *width = m_Width;
	if(height) *height = m_Height;
}

unsigned int CRS2D3D12Backend::CountDebugMessages(
	int severity	//	D3D12_MESSAGE_SEVERITY_ERROR and so on
){
	if(!m_InfoQueue) return 0;

	const UINT64 stored = m_InfoQueue->GetNumStoredMessages();
	unsigned int matched = 0;
	unsigned int logged = 0;
	UINT64 i;

	for(i = 0; i<stored; i++){
		SIZE_T bytes = 0;

		if(FAILED(m_InfoQueue->GetMessage(i, NULL, &bytes)) || !bytes) continue;

		D3D12_MESSAGE *message = (D3D12_MESSAGE *)new char[bytes];

		if(SUCCEEDED(m_InfoQueue->GetMessage(i, message, &bytes))
				&& (int)message->Severity<=severity){
			matched++;

			//	A few is enough to identify what is wrong; the count says how
			//	much of it there is.
			if(logged<8){
				logged++;
				Debug("[RS2EX D3D12] debug layer: %s\n", message->pDescription);
			}
		}
		delete [] (char *)message;
	}
	return matched;
}

const char *CRS2D3D12Backend::GetName() const{
	return m_Name;
}
