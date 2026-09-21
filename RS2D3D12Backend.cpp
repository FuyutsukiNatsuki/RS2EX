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
 *	Frame lifecycle: not this work package.
 *
 *	There is no swap chain to render into, so refusing is the truthful answer.
 *	CRS2Renderer will not select this backend for a normal run until the
 *	lifecycle exists; -dx12smoke exercises what is built so far.
 */
bool CRS2D3D12Backend::BeginRenderPass(unsigned int clearColor, bool clearColorBuffer){
	(void)clearColor;
	(void)clearColorBuffer;
	return false;
}

void CRS2D3D12Backend::EndRenderPass(){
}

void CRS2D3D12Backend::Present(){
}

/*
 *	Reset means "rebuild the size-dependent swap-chain resources" for this
 *	backend, not the Direct3D 8 device reset it is named after.  There are no
 *	such resources yet.
 */
bool CRS2D3D12Backend::Reset(){
	return true;
}

void CRS2D3D12Backend::ClearTarget(unsigned int color){
	(void)color;
}

void CRS2D3D12Backend::SetViewport(
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

void CRS2D3D12Backend::GetViewportSize(
	unsigned int *width,
	unsigned int *height
) const{
	if(width) *width = m_Width;
	if(height) *height = m_Height;
}

const char *CRS2D3D12Backend::GetName() const{
	return m_Name;
}
