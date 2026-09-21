//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	See RS2D3D12Availability.h.

#include "stdafx.h"
#include "RS2D3D12.h"
#include "RS2D3D12Availability.h"

/*
 *	Describe an adapter for the log without assuming the description is short
 *	or that the console can print wide characters.
 */
static void RS2D3D12_DescribeAdapter(
	const DXGI_ADAPTER_DESC1 &desc,	//	adapter to describe
	char *out,			//	buffer to fill
	int bytes			//	its size
){
	const int written = WideCharToMultiByte(
		CP_ACP, 0, desc.Description, -1, out, bytes, NULL, NULL);

	//	A description that will not convert is not a reason to refuse an
	//	adapter, so say so and carry on.
	if(written<=0){
		lstrcpynA(out, "(description unavailable)", bytes);
	}
}

bool RS2D3D12IsRuntimeUsable(){
	IDXGIFactory4 *factory = 0;

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	if(FAILED(hr)){
		Debug("[RS2EX D3D12] no DXGI factory (0x%08lx)\n", (unsigned long)hr);
		return false;
	}

	bool usable = false;
	UINT index;

	for(index = 0; ; index++){
		IDXGIAdapter1 *adapter = 0;

		if(factory->EnumAdapters1(index, &adapter)==DXGI_ERROR_NOT_FOUND) break;
		if(!adapter) continue;

		DXGI_ADAPTER_DESC1 desc;

		if(FAILED(adapter->GetDesc1(&desc))){
			adapter->Release();
			continue;
		}

		//	WARP would answer yes on a machine whose real adapter or driver
		//	cannot run Direct3D 12 at all, which is the case worth hearing
		//	about.  A deliberate software path can be its own switch later.
		if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE){
			adapter->Release();
			continue;
		}

		//	A null device pointer asks whether creation would succeed without
		//	creating anything.
		hr = D3D12CreateDevice(
			adapter, RS2D3D12_MIN_FEATURE_LEVEL, __uuidof(ID3D12Device), NULL);

		char name[256];

		RS2D3D12_DescribeAdapter(desc, name, sizeof(name));

		if(SUCCEEDED(hr)){
			Debug("[RS2EX D3D12] adapter %u usable: %s (vendor %04x device %04x)\n",
				index, name, (unsigned)desc.VendorId, (unsigned)desc.DeviceId);
			usable = true;
			adapter->Release();
			break;
		}

		Debug("[RS2EX D3D12] adapter %u cannot create a device (0x%08lx): %s\n",
			index, (unsigned long)hr, name);
		adapter->Release();
	}

	if(!usable)
		Debug("[RS2EX D3D12] no hardware adapter can create a device at the minimum feature level\n");

	factory->Release();
	return usable;
}
