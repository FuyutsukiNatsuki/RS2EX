//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	See RS2LegacyImportDevice.h for why this exists.

#include "stdafx.h"
#include "RS2LegacyImportDevice.h"

#include <stdlib.h>	//	atexit

static IDirect3D8 *s_D3D = 0;
static IDirect3DDevice8 *s_Device = 0;
static HWND s_Window = 0;
static bool s_Failed = false;	//	do not retry every mesh

/*
 *	A window for the device to be created against.
 *
 *	Hidden and never shown.  Direct3D 8 needs a focus window even for a device
 *	that will only ever hold a 1x1 swap chain it does not present.
 */
static HWND RS2CreateImportWindow(){
	static const char *CLASS_NAME = "RS2EXLegacyImport";

	WNDCLASSA wc;

	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = CLASS_NAME;
	RegisterClassA(&wc);

	return CreateWindowA(CLASS_NAME, CLASS_NAME, WS_OVERLAPPED,
		0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
}

IDirect3DDevice8 *RS2GetLegacyImportDevice(){
	if(s_Device) return s_Device;
	if(s_Failed) return 0;

	s_Failed = true;	//	cleared on success

	s_D3D = Direct3DCreate8(D3D_SDK_VERSION);
	if(!s_D3D){
		Debug("[RS2EX Import] Direct3DCreate8 failed\n");
		return 0;
	}

	s_Window = RS2CreateImportWindow();
	if(!s_Window){
		Debug("[RS2EX Import] could not create the import window\n");
		RS2ReleaseLegacyImportDevice();
		return 0;
	}

	D3DDISPLAYMODE mode;
	if(FAILED(s_D3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode))){
		Debug("[RS2EX Import] GetAdapterDisplayMode failed\n");
		RS2ReleaseLegacyImportDevice();
		return 0;
	}

	//	The smallest windowed swap chain the runtime will accept.  Nothing is
	//	ever drawn into it; meshes are created in system memory.
	D3DPRESENT_PARAMETERS pp;

	ZeroMemory(&pp, sizeof(pp));
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = mode.Format;
	pp.BackBufferWidth = 1;
	pp.BackBufferHeight = 1;
	pp.hDeviceWindow = s_Window;

	HRESULT hr = s_D3D->CreateDevice(
		D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_Window,
		D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &s_Device);

	if(FAILED(hr)){
		Debug("[RS2EX Import] import device creation failed (0x%08lx)\n",
			(unsigned long)hr);
		RS2ReleaseLegacyImportDevice();
		return 0;
	}

	s_Failed = false;

	//	The importer owns this device, so it releases it too.  Hanging the
	//	release off the renderer's shutdown would be the coupling this module
	//	exists to remove.
	static bool registered = false;
	if(!registered){
		atexit(RS2ReleaseLegacyImportDevice);
		registered = true;
	}
	Debug("[RS2EX Import] legacy import device created\n");
	return s_Device;
}

void RS2ReleaseLegacyImportDevice(){
	RELEASE(s_Device);
	RELEASE(s_D3D);

	if(s_Window){
		DestroyWindow(s_Window);
		s_Window = 0;
	}
}
