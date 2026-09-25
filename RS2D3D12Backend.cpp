//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//	Modified for RS2EX on 2026-09-22, 2026-09-23, 2026-09-24, 2026-09-25, 2026-09-26.
//
//	See RS2D3D12Backend.h.

#include "stdafx.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Unsupported.h"
#include "RS2Renderer.h"
#include "RS2Display.h"
#include "RS2Text.h"
#include "RS2TextBackend.h"

//	The same inputs the Direct3D 8 backend consults, so both backends
//	answer "windowed or not" the same way.
extern bool g_FullScreen;
//	[RS2EX] Read by the engine to decide whether stencil shadows can run.
extern bool g_StencilEnabled;

//	Scratch memory per frame context.
//
//	Chosen from what the engine can ask for in one frame rather than by
//	rounding: a quad dump batch is 10922 quads of six vertices at 36 bytes,
//	which is 2.3 MB, and a frame can flush more than one.  Eight megabytes
//	leaves room for that plus constants and fan expansion.  The peak actually
//	used is logged at shutdown, because the right number here is a measurement.
//
//	v0.1.5: 32 MB.  Stencil shadows send their volumes through
//	RS2DrawImmediate, about 1.07 million vertices a frame in the accepted
//	shadow-enabled fixture, and the measured peak there was 13,578 KB against
//	828 KB without shadows.  Still fixed and reused, still refusing rather than
//	wrapping when it runs out (user decision, 2026-09-25); a growable allocator
//	is reconsidered only if real content comes near this.
#define RS2D3D12_UPLOAD_BYTES	(32*1024*1024)

extern char *g_PluginViewArg;

//	Set once the backend is up, cleared when it goes.  There is only ever one.
static CRS2D3D12Backend *s_Active = 0;

CRS2D3D12Backend *RS2D3D12GetActiveBackend(){ return s_Active; }

/*
 *	Make the existing main window cover its monitor without taking exclusive
 *	ownership of the display.
 *
 *	v0.1.x chose borderless because the legacy mesh importer's own Direct3D 8
 *	device failed with D3DERR_NOTAVAILABLE while a Direct3D 12 swap chain
 *	owned the adapter exclusively.  That importer is gone (v0.2.0), and the
 *	back buffer no longer stays at the configured resolution to be stretched
 *	over the monitor: CreateSwapChain sizes it to the client area this makes,
 *	which is the monitor's resolution.  Borderless stays because exclusive
 *	fullscreen is not part of v0.2.0 (plan section 16.10), and because it is
 *	explicit about who shows and sizes the HWND - an exclusive Direct3D 8
 *	device used to do that for the application.
 */
static bool RS2D3D12_PrepareBorderlessWindow(HWND window){
	HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info;

	ZeroMemory(&info, sizeof(info));
	info.cbSize = sizeof(info);

	if(!monitor || !GetMonitorInfo(monitor, &info)){
		Debug("[RS2EX D3D12] could not query the fullscreen monitor\n");
		return false;
	}

	SetLastError(0);
	const LONG previous = SetWindowLong(window, GWL_STYLE, WS_POPUP);
	if(!previous && GetLastError()!=0){
		Debug("[RS2EX D3D12] could not make the window borderless\n");
		return false;
	}

	if(!SetWindowPos(window, HWND_TOP,
			info.rcMonitor.left, info.rcMonitor.top,
			info.rcMonitor.right-info.rcMonitor.left,
			info.rcMonitor.bottom-info.rcMonitor.top,
			SWP_NOACTIVATE|SWP_FRAMECHANGED)){
		Debug("[RS2EX D3D12] could not size the borderless window\n");
		return false;
	}
	return true;
}

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
	  m_DeviceRemoved(false),
	  m_DeviceReferencesAfterShutdown(0),
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

	//	-dx12nostencil (developer): skip the stencil formats, to exercise the
	//	path a device without D24S8 takes - no stencil claimed, the engine's
	//	own warning and shadow disable, no fallback.
	const bool noStencil = CheckArguments("-dx12nostencil")!=FALSE;

	for(i = 0; i<(int)(sizeof(candidates)/sizeof(candidates[0])); i++){
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support;

		if(noStencil && (candidates[i]==DXGI_FORMAT_D24_UNORM_S8_UINT
				|| candidates[i]==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)) continue;

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
	//	the two backends agree about what the user requested.  m_Windowed is the
	//	engine-facing mode; the D3D12 swap chain itself stays windowed even for a
	//	fullscreen request, using a monitor-sized borderless HWND instead.
	m_Windowed = (!g_FullScreen || g_PluginViewArg || CheckArguments("-win"))!=0;
	if(!m_Windowed && !RS2D3D12_PrepareBorderlessWindow(window)) return false;

	//	[RS2EX] v0.2.0: the back buffer is the client area, not the size that
	//	was asked for.  Borderless, the client area is the whole monitor.
	//	Windowed, it is the configured resolution unless Windows would not
	//	make a window that large, in which case it is what Windows gave.
	//	Either way one back-buffer pixel is one screen pixel, so nothing is
	//	stretched, and the UI and cursor lay themselves out on the same size.
	//	svw is updated too: it is what the per-frame resize check compares
	//	against, and a borderless window never receives the WM_SIZE handling
	//	that would otherwise set it.
	RECT client;

	if(GetClientRect(window, &client)
			&& client.right>client.left && client.bottom>client.top){
		const unsigned int cw = (unsigned int)(client.right-client.left);
		const unsigned int ch = (unsigned int)(client.bottom-client.top);

		if(cw!=m_Width || ch!=m_Height)
			Debug("[RS2EX D3D12] %u x %u requested; the window's client area is %u x %u\n",
				m_Width, m_Height, cw, ch);
		m_Width = cw;
		m_Height = ch;
		svw.winW = (int)cw;
		svw.winH = (int)ch;
	}

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
	fs.Windowed = TRUE;

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

	//	What was asked for and what was granted are different questions.
	//	DXGI can refuse exclusive fullscreen and hand back a windowed swap
	//	chain, and a log that only recorded the request would not show it.
	BOOL actuallyFullscreen = FALSE;

	m_SwapChain->GetFullscreenState(&actuallyFullscreen, NULL);

	const char *granted = actuallyFullscreen ? "exclusive fullscreen"
		: (m_Windowed ? "windowed" : "borderless fullscreen");

	Debug("[RS2EX D3D12] swap chain %u x %u, %d buffers, %s requested, %s granted\n",
		m_Width, m_Height, RS2D3D12_FRAME_COUNT,
		m_Windowed ? "windowed" : "fullscreen",
		granted);
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
	//	Not claimed until the buffer below exists: a failed or stencil-less
	//	depth buffer leaves the engine's own warning-and-disable path in place.
	g_StencilEnabled = false;

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

	/*
	 *	[RS2EX] v0.1.5: publish stencil to the engine, as the Direct3D 8
	 *	backend does when it finds D24S8.  Only for D24S8 itself - the format
	 *	every pipeline state is built for - so a fallback format never claims
	 *	a capability the pipelines could not honour.  The stencil state, the
	 *	shadow's auxiliary state and the -shadowprobe gate were in place
	 *	before this line was added (v0.1.5 WP1-WP4).
	 */
	g_StencilEnabled = m_HasStencil && m_DepthFormat==DXGI_FORMAT_D24_UNORM_S8_UINT;
	Debug("[RS2EX D3D12] stencil shadows %s\n", g_StencilEnabled ? "available" : "unavailable");
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
 *	The pipeline and the per-frame scratch memory.
 */
bool CRS2D3D12Backend::CreatePipeline(){
	if(!m_Pipeline.Create(m_Device, m_DepthFormat)) return false;

	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++)
		if(!m_Upload[i].Create(m_Device, RS2D3D12_UPLOAD_BYTES)) return false;

	Debug("[RS2EX D3D12] %d frame scratch blocks of %u KB\n",
		RS2D3D12_FRAME_COUNT, (unsigned)(RS2D3D12_UPLOAD_BYTES/1024));

	// Prove both position paths, both diffuse cases, and the Stage 0 shader
	// path before any game draw. A layout with UV but no bound texture must
	// still select the untextured shader. Other state combinations remain lazy.
	unsigned int semantic, diffuse, variant;

	for(semantic = 0; semantic<2; semantic++)
	for(diffuse = 0; diffuse<2; diffuse++)
	for(variant = 0; variant<3; variant++){
		RS2MeshVertexLayout layout;
		RS2D3D12PipelineKey key;

		layout.Clear();
		layout.positionSemantic = semantic
			? RS2_POSITION_ALREADY_TRANSFORMED : RS2_POSITION_TRANSFORMED_BY_PIPELINE;
		layout.positionOffset = 0;
		layout.stride = semantic ? 16 : 12;
		if(diffuse){
			layout.diffuseOffset = (int)layout.stride;
			layout.stride += 4;
		}
		if(variant){
			layout.texCoordCount = 1;
			layout.texCoord[0].offset = (int)layout.stride;
			layout.texCoord[0].components = 2;
			layout.stride += 8;
		}

		ZeroMemory(&key, sizeof(key));
		if(!RS2D3D12_DescribeLayout(layout, &key)){
			Debug("[RS2EX D3D12] warm-up layout %u/%u/%u failed\n",
				semantic, diffuse, variant);
			return false;
		}

		key.textured = variant==2 ? 1 : 0;
		key.topology = (unsigned char)D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		key.depthTest = 1;
		key.depthWrite = 1;
		key.depthFunc = (unsigned char)RS2_COMPARE_LESS_EQUAL;
		key.cullMode = (unsigned char)RS2_CULL_COUNTER_CLOCKWISE;
		key.blendMode = (unsigned char)RS2_BLEND_ALPHA;

		if(!m_Pipeline.Get(key)){
			Debug("[RS2EX D3D12] warm-up pipeline %u/%u/%u failed\n",
				semantic, diffuse, variant);
			return false;
		}
	}
	return true;
}

unsigned int CRS2D3D12Backend::GetUploadPeak() const{
	unsigned int peak = 0;
	unsigned int i;

	for(i = 0; i<RS2D3D12_FRAME_COUNT; i++)
		if(m_Upload[i].GetPeak()>peak) peak = m_Upload[i].GetPeak();
	return peak;
}

/*
 *	Fill in the scalar values the engine still reads out of sv3.
 *
 *	sv3 is the Direct3D 8 device and its parameters, and nothing of this
 *	backend's belongs in it - no device, no swap chain, no descriptor.  But
 *	four plain numbers in that struct are written by the Direct3D 8 backend and
 *	read by the engine, and with any other backend running they stay zero:
 *
 *	    capsMaxPrim   CVertexDump.h, CShadowVolume.h
 *	    width/height  CGameMode, CSceneryMode, Capture, effect, graphic
 *	    fWindowed     CCursor, input, window
 *
 *	This was found the hard way.  QUAD_DUMP_MAX is capsMaxPrim/6, so a zero
 *	made the rail-profile dump allocate a zero-length buffer and then write six
 *	vertices into it - a heap overrun during scene construction, before any
 *	frame.  The v0.0.9 dependency scan could not have caught it: these are
 *	plain integers, and nothing about reading one looks native.
 *
 *	This is a compatibility measure for v0.1.1, not a design.  The values the
 *	engine needs should be asked for through the renderer boundary rather than
 *	found in a global struct, and that migration is recorded as follow-up work
 *	in the validation record.
 */
void CRS2D3D12Backend::PublishCompatibilityState(){
	//	NOT the hardware limit.  Direct3D 12 has no equivalent of
	//	D3DCAPS8::MaxPrimitiveCount, and inventing one here would be a claim
	//	about the adapter that this backend is in no position to make.
	//
	//	What this number actually has to satisfy is the engine's own batching
	//	contract.  The three users divide it and then clamp the result:
	//
	//	    LINE_DUMP_MAX = /2, clamped to 32767
	//	    TRI_DUMP_MAX  = /3, clamped to 21845
	//	    QUAD_DUMP_MAX = /6, clamped to 10922
	//
	//	Those three clamps are 65535/2, 65535/3 and 65535/6.  The ceiling the
	//	engine already imposes on itself is a batch of 65535 vertices - the
	//	16-bit index range the dump buffers are written for - so that is the
	//	number, and every derived size lands exactly on the clamp the code
	//	applies anyway.
	sv3.capsMaxPrim = 65535;

	//	The engine reads these for aspect ratio, viewport arithmetic, capture
	//	geometry and cursor clipping.  Zero would divide by zero in several of
	//	them.
	sv3.width = (int)m_Width;
	sv3.height = (int)m_Height;
	sv3.fWindowed = m_Windowed ? TRUE : FALSE;

	//	[RS2EX] v0.2.0: and the display size the UI, the cursor and every
	//	g_DispWidth reader lay out against.  See RS2Display.h.
	RS2PublishDisplaySize((int)m_Width, (int)m_Height);

	//	The Direct3D 8 backend showed the window from its device creation, and
	//	this backend took the job over from it.  It is not a device matter,
	//	but nothing else shows the window.
	if(svw.hWnd){
		ShowWindow(svw.hWnd, SW_SHOW);
		UpdateWindow(svw.hWnd);
	}

	Debug("[RS2EX D3D12] compatibility state: %d x %d, %s, batch limit %lu\n",
		sv3.width, sv3.height, sv3.fWindowed ? "windowed" : "fullscreen",
		(unsigned long)sv3.capsMaxPrim);
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
	RS2D3D12_ResetTextureRuntimeStats();

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
			|| !m_TextureUpload.Create(m_Device, m_Queue)
			|| !m_Descriptors.Create(m_Device)
			|| !CreateSwapChain(svw.hWnd) || !CreateRenderTargets()
			|| !CreateDepthBuffer() || !CreatePipeline()){
		Shutdown();
		return false;
	}

	//	The whole target, until something asks for less.  Leaving this zeroed
	//	would clip every pass away and look exactly like a backend that draws
	//	nothing.
	SetViewport(0, 0, m_Width, m_Height, 0.0f, 1.0f);

	PublishCompatibilityState();
	s_Active = this;
	//	Profile draws reset their world transform from sv3.mtxFront.  D3D8
	//	initialises that matrix through InitMetrics(); D3D12 must do the same
	//	once the renderer is installed.  The bootstrap smoke also constructs
	//	this backend directly, without a renderer, so do not dispatch the
	//	neutral transform setters through its default D3D8 selection there.
	if(GetRS2Renderer().IsReady()
			&& GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12){
		InitMetrics();
		//	Direct3D 8 creates the live-text font here, at the height the rest
		//	of the UI uses (v0.1.6).
		RS2CreateTextFont(FONT_HEIGHT, 0xffffffff, false);
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

void CRS2D3D12Backend::CollectRetiredTextures(){
	if(!m_Fence) return;
	const UINT64 completed = m_Fence->GetCompletedValue();
	std::list<RetiredTexture>::iterator it = m_RetiredTextures.begin();
	while(it!=m_RetiredTextures.end()){
		if(it->fenceValue>completed){ ++it; continue; }
		m_Descriptors.Free(it->slot);
		RELEASE(it->resource);
		it = m_RetiredTextures.erase(it);
	}
}

void CRS2D3D12Backend::RetireTexture(
	ID3D12Resource *resource, const RS2D3D12SrvSlot &slot
){
	if(!resource) return;
	RetiredTexture retired;
	retired.resource = resource;
	retired.slot = slot;
	// A currently recorded draw has no fence value until Present submits it.
	// Do not guess the next value: another queue signal may occur meanwhile.
	retired.fenceValue = m_FrameRecording ? ~UINT64(0)
		: (m_NextFenceValue ? m_NextFenceValue-1 : 0);
	m_RetiredTextures.push_back(retired);
	CollectRetiredTextures();
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
	const bool sceneAudit = m_Device && CheckArguments("-dx12sceneaudit")!=FALSE;
	//	The live-text texture is retired through this backend, so it goes
	//	while this is still the active one.
	if(s_Active==this) RS2D3D12_DestroyTextFont();
	if(s_Active==this) s_Active = 0;
	RS2D3D12_ResetTextureBinding();

	WaitForGpu();
	CollectRetiredTextures();
	// A recording never presented during shutdown has no GPU work to await.
	while(!m_RetiredTextures.empty()){
		RetiredTexture &retired = m_RetiredTextures.front();
		m_Descriptors.Free(retired.slot);
		RELEASE(retired.resource);
		m_RetiredTextures.pop_front();
	}
	if(sceneAudit && m_Device){
		m_TextureUpload.WaitForAll();
		const RS2D3D12TextureRuntimeStats &stats =
			RS2D3D12_GetTextureRuntimeStats();
		const unsigned int attempts = stats.fileAttempts+stats.resourceAttempts;
		const unsigned int successes = stats.fileSuccesses+stats.resourceSuccesses;
		const unsigned int errors = HasDebugLayer()
			? CountDebugMessages(D3D12_MESSAGE_SEVERITY_ERROR) : 0;
		const unsigned int warnings = HasDebugLayer()
			? CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;
		Debug("RS2D3D12SCENE|backend=%s|windowed=%u|infoQueue=%u\n",
			GetName(),m_Windowed ? 1u : 0u,HasDebugLayer() ? 1u : 0u);
		Debug("RS2D3D12SCENE|texture attempts=%u success=%u failed=%u file=%u/%u resource=%u/%u\n",
			attempts,successes,attempts-successes,
			stats.fileSuccesses,stats.fileAttempts,
			stats.resourceSuccesses,stats.resourceAttempts);
		Debug("RS2D3D12SCENE|dds attempts=%u success=%u failed=%u bc1=%u bc3=%u mipShortfall=%u uploadBytes=%llu live=%u peak=%u texturedDraws=%u\n",
			stats.ddsAttempts,stats.ddsSuccesses,stats.ddsFailures,
			stats.ddsBC1,stats.ddsBC3,stats.ddsMipShortfalls,
			stats.ddsUploadBytes,RS2D3D12_GetLiveDDSTextureCount(),
			RS2D3D12_GetPeakDDSTextureCount(),RS2D3D12_GetDDSTexturedDrawCount());
		{
			const RS2D3D12LightingStats &light = RS2D3D12_GetLightingStats();

			Debug("RS2D3D12SCENE|lighting material=%u lighting=%u ambient=%u light=%u specular=%u diffuseSource=%u ambientSource=%u\n",
				light.materialCalls,light.lightingCalls,light.ambientCalls,light.lightCalls,
				light.specularCalls,light.diffuseSourceCalls,light.ambientSourceCalls);
			Debug("RS2D3D12SCENE|lit draws=%u unlit=%u litNormal=%u litNoNormal=%u specularDraws=%u\n",
				light.litDraws,light.unlitDraws,light.litNormalDraws,light.litNoNormalDraws,
				light.specularDraws);
		}
		{
			const RS2D3D12StageStats &st = RS2D3D12_GetStageStats();

			Debug("RS2D3D12SCENE|stage1 bind=%u unbind=%u point=%u linear=%u combine=%u environment=%u uvTransform=%u uvMatrix=%u\n",
				stats.stage1Binds,stats.stage1Unbinds,stats.stage1PointFilters,
				stats.stage1LinearFilters,st.combineCalls,st.environmentCalls,
				st.uvTransformCalls,st.uvMatrixCalls);
			Debug("RS2D3D12SCENE|stage1 draws=%u environmentDraws=%u uvTransformedDraws=%u skippedNoTexture=%u\n",
				st.stage1Draws,st.environmentDraws,st.uvTransformedDraws,st.stage1Skipped);
		}
		{
			const RS2D3D12StencilStats &sc = RS2D3D12_GetStencilStats();

			Debug("RS2D3D12SCENE|stencil calls test=%u func=%u ref=%u readMask=%u writeMask=%u fail=%u depthFail=%u pass=%u shade=%u flat=%u fog=%u\n",
				sc.calls[0],sc.calls[1],sc.calls[2],sc.calls[3],sc.calls[4],sc.calls[5],
				sc.calls[6],sc.calls[7],sc.shadeCalls,sc.flatCalls,sc.fogCalls);
			Debug("RS2D3D12SCENE|stencil draws=%u volume=%u overlay=%u refChanges=%u stencilPso=%u\n",
				sc.stencilDraws,sc.volumeDraws,sc.overlayDraws,sc.refChanges,
				m_Pipeline.GetStencilStateCount());
		}
		Debug("RS2D3D12SCENE|stage0 bind=%u unbind=%u rejected=%u otherStage=%u point=%u linear=%u rejectedFilter=%u\n",
			stats.stage0Binds,stats.stage0Unbinds,stats.rejectedBinds,
			stats.otherStageBinds,stats.pointFilters,stats.linearFilters,
			stats.rejectedFilters);
		Debug("RS2D3D12SCENE|textures live=%u peak=%u descriptors live=%u peak=%u/%u uploadBytes=%llu pending=%u\n",
			RS2D3D12_GetLiveTextureCount(),RS2D3D12_GetPeakTextureCount(),
			m_Descriptors.GetLive(),m_Descriptors.GetPeak(),m_Descriptors.GetCapacity(),
			(unsigned long long)m_TextureUpload.GetSubmittedBytes(),
			m_TextureUpload.GetPendingCount());
		{
			//	v0.1.6: the string texture and the live-text line.
			const RS2D3D12MutableStats &mu = RS2D3D12_GetMutableStats();

			Debug("RS2D3D12SCENE|mutable creates=%u failed=%u live=%u peak=%u locks=%u unlocks=%u\n",
				mu.creates,mu.createFailures,mu.live,mu.peak,mu.locks,mu.unlocks);
			Debug("RS2D3D12SCENE|mutable refusedLocks=%u unlocksWithoutLock=%u destroyedLocked=%u\n",
				mu.refusedLocks,mu.unlocksWithoutLock,mu.destroyedLocked);
			Debug("RS2D3D12SCENE|mutable uploads=%u coalesced=%u unchanged=%u refused=%u bytes=%llu largest=%u\n",
				mu.uploads,mu.coalescedUnlocks,mu.unchangedUnlocks,mu.refusedUploads,
				mu.uploadBytes,mu.largestUpload);
			Debug("RS2D3D12SCENE|mutable draws=%u liveText=%u\n",mu.draws,mu.liveTextDraws);
		}
		Debug("RS2D3D12SCENE|draws=%u textured=%u refused=%u pso=%u unsupported=%u errors=%u warnings=%u\n",
			RS2D3D12_GetDrawCount(),RS2D3D12_GetTexturedDrawCount(),
			RS2D3D12_GetRefusedDrawCount(),m_Pipeline.GetStateCount(),
			RS2D3D12UnsupportedCount(),errors,warnings);
	}
	m_TextureUpload.Destroy();
	m_Descriptors.Destroy();

	if(m_FenceEvent){
		CloseHandle(m_FenceEvent);
		m_FenceEvent = NULL;
	}

	//	What the run actually drew.  A renderer that presented every frame and
	//	submitted nothing looks identical from the outside to one that drew
	//	something invisible, and these two numbers tell them apart.
	if(m_Device)
		Debug("[RS2EX D3D12] draws submitted %u, refused %u, pipeline states %u\n",
			RS2D3D12_GetDrawCount(), RS2D3D12_GetRefusedDrawCount(),
			m_Pipeline.GetStateCount());

	//	Reported before it goes, because the size of these blocks is a guess
	//	until something measures it.
	if(m_Device && GetUploadPeak())
		Debug("[RS2EX D3D12] frame scratch peak %u KB of %u KB\n",
			GetUploadPeak()/1024, (unsigned)(RS2D3D12_UPLOAD_BYTES/1024));

	{
		unsigned int i;

		for(i = 0; i<RS2D3D12_FRAME_COUNT; i++) m_Upload[i].Destroy();
	}
	m_Pipeline.Destroy();

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

	//	The device goes last, and its final reference count is the cheapest
	//	leak check there is: everything created from it holds a reference, so
	//	anything above that was missed leaves this above zero.  The alternative
	//	- ReportLiveDeviceObjects - writes to the debugger rather than to a log
	//	anyone running the program can read.
	if(m_Device){
		const ULONG remaining = m_Device->Release();

		if(remaining)
			Debug("[RS2EX D3D12] device still has %lu references after shutdown\n",
				(unsigned long)remaining);
		m_Device = 0;
		m_DeviceReferencesAfterShutdown = (unsigned int)remaining;
	}
	if(sceneAudit)
		Debug("RS2D3D12SCENE|shutdownReferences=%u\n",
			m_DeviceReferencesAfterShutdown);

	RELEASE(m_Factory);

	m_NextFenceValue = 1;
	m_DeviceRemoved = false;
	m_FrameIndex = 0;
	m_Width = m_Height = 0;
	m_RtvStride = 0;
	m_DepthFormat = DXGI_FORMAT_UNKNOWN;
	m_HasStencil = false;
	g_StencilEnabled = false;
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
	CollectRetiredTextures();

	//	Safe here and nowhere else: the wait above is what says the GPU has
	//	finished reading everything this context handed it last time round.
	m_Upload[m_FrameIndex].Reset();

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

	//	Once the device is gone every call fails, so recording against it would
	//	only produce a log full of the same failure.
	if(m_DeviceRemoved) return false;

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

	//	[RS2EX] The Direct3D 8 backend submits the view matrix here, straight
	//	to the device, so it never went through the draw boundary and the
	//	Direct3D 12 backend never saw it - the scene drew at the world origin
	//	and went off screen, while the screen-space interface looked fine.
	//
	//	Same source, same moment, same effect.  Like the scalar state in
	//	PublishCompatibilityState, this is the engine keeping something in sv3
	//	that a backend has to go and fetch, and it belongs on the same list of
	//	things the boundary should be asking for instead.
	RS2D3D12_SetViewTransform(sv3.mtxView);

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

	if(FAILED(hr)) ReportDeviceFailure("Present", hr);

	//	Remember what this context has to finish before its allocator may be
	//	reset again, then move on to the buffer DXGI has just made current.
	m_FenceValue[m_FrameIndex] = m_NextFenceValue;
	m_Queue->Signal(m_Fence, m_NextFenceValue);
	for(std::list<RetiredTexture>::iterator it = m_RetiredTextures.begin();
			it!=m_RetiredTextures.end(); ++it)
		if(it->fenceValue==~UINT64(0)) it->fenceValue = m_NextFenceValue;
	m_NextFenceValue++;

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	m_FrameRecording = false;
	m_PassActive = false;
}

/*
 *	Report a failure, and say whether the device is gone.
 *
 *	what	: the operation that failed
 *	hr		: what it returned
 *
 *	A removed device makes every later call fail the same way, so the first
 *	report is the only one worth reading, and the removal reason is the only
 *	thing that says why - the HRESULT from the call that noticed is almost
 *	always just DXGI_ERROR_DEVICE_REMOVED again.
 *
 *	There is no recovery attempt.  Recreating a device means recreating
 *	everything built on it, and v0.1.0 has nothing built on it worth
 *	recreating; guessing at that now would be designing for a case nobody has
 *	yet had to handle.
 */
void CRS2D3D12Backend::ReportDeviceFailure(
	const char *what,	//	operation that failed
	long hr			//	its result
){
	Debug("[RS2EX D3D12] %s failed (0x%08lx)\n", what, (unsigned long)hr);

	if(hr!=DXGI_ERROR_DEVICE_REMOVED && hr!=DXGI_ERROR_DEVICE_RESET) return;
	if(!m_Device) return;

	const HRESULT reason = m_Device->GetDeviceRemovedReason();

	Debug("[RS2EX D3D12] the device is gone, reason 0x%08lx\n", (unsigned long)reason);
	m_DeviceRemoved = true;
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
		ReportDeviceFailure("ResizeBuffers", hr);
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

	//	The engine reads the size from sv3, so a resize that did not update it
	//	would leave every aspect-ratio and viewport calculation on the old one.
	PublishCompatibilityState();

	//	Direct3D 8 rebuilt the live-text font after every device reset, and
	//	rebuilt it at 16 rather than the start-up 12 (inherited behaviour).
	//	The same here keeps RS2GetTextHeight - and so the edit box's caret -
	//	what it was under Direct3D 8 (v0.1.6).
	if(s_Active==this && GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D12)
		RS2CreateTextFont(16, 0xffffffff, false);

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
