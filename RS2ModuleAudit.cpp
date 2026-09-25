//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	-moduleaudit: list every module loaded in the process at shutdown.
//
//	v0.2.0 uses it as the runtime evidence for the Direct3D 8 / D3DX8
//	independence claim: the list shows which DirectX-family DLLs a real run
//	actually loaded, whether through the import table or LoadLibrary.

#include "stdafx.h"
#include "RS2ModuleAudit.h"

#include <psapi.h>
#pragma comment(lib, "psapi.lib")

void RS2ModuleAuditDump(){
	if(!CheckArguments("-moduleaudit")) return;

	HMODULE modules[1024];
	DWORD needed = 0;

	if(!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)){
		Debug("RS2MODULEAUDIT|EnumProcessModules failed\n");
		return;
	}

	const unsigned int count = needed/sizeof(HMODULE);
	unsigned int i;

	Debug("RS2MODULEAUDIT|begin|count=%u\n", count);
	for(i = 0; i<count && i<1024; i++){
		char path[MAX_PATH];

		if(GetModuleFileNameA(modules[i], path, MAX_PATH)){
			const char *name = strrchr(path, '\\');

			Debug("RS2MODULEAUDIT|module|%s\n", name ? name+1 : path);
		}
	}
	Debug("RS2MODULEAUDIT|end\n");
}
