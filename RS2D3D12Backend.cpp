//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	See RS2D3D12Backend.h.

#include "stdafx.h"
#include "RS2D3D12Backend.h"

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

	if(!CreateDevice() || !CreateCommandObjects() || !CreateFence()){
		Shutdown();
		return false;
	}

	m_Width = (width>0) ? (unsigned int)width : 0;
	m_Height = (height>0) ? (unsigned int)height : 0;
	m_FrameIndex = 0;

	Debug("[RS2EX D3D12] device, queue, %d frame contexts and fence ready\n",
		RS2D3D12_FRAME_COUNT);
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
