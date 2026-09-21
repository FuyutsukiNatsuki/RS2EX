//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	The one place Direct3D 12 and DXGI headers are included.
//
//	Include-path order matters here and is not obvious.  The DirectX 8.1 SDK
//	ships 2001-era copies of basetsd.h, dshow.h, strmif.h, ks.h and about
//	seventy other headers that also exist in the Windows 10/11 SDK.  If its
//	include directory comes first, its basetsd.h shadows the modern one, and
//	winnt.h then fails on PVOID64 before anything Direct3D 12 is even reached.
//	The build appends the DX8 directory rather than prepending it, so the
//	Windows SDK keeps priority and DX8 only fills in what is missing - d3d8*.h,
//	d3dx8*.h and their libraries, which the Direct3D 8 backend and the legacy
//	mesh importer still need.  Do not reorder it.
//
//	d3d12.lib and dxgi.lib are linked normally rather than resolved at run
//	time.  That makes the executable require Windows 10 even when it is running
//	the default Direct3D 8 backend.  It was an explicit decision - the
//	alternative preserved start-up on Windows 8.1 and earlier at the cost of
//	dynamic loading in the backend - and v0.1.0-validation.md records it.

#ifndef RS2D3D12_H_INCLUDED
#define RS2D3D12_H_INCLUDED

#include <d3d12.h>
#include <dxgi1_4.h>

//	Direct3D 12 has no fixed-function pipeline, so RailSim's legacy feature set
//	does not justify asking for more than the level the API itself requires.
#define RS2D3D12_MIN_FEATURE_LEVEL	D3D_FEATURE_LEVEL_11_0

#endif	//	RS2D3D12_H_INCLUDED
