//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//	Modified for RS2EX on 2026-09-26.
//
//	The one place Direct3D 12 and DXGI headers are included.
//
//	v0.2.0: the build no longer uses the DirectX 8.1 SDK at all.  Up to v0.1.6
//	its include directory had to be appended after the Windows SDK's, because
//	its 2001-era basetsd.h and friends shadowed the modern ones and broke
//	winnt.h; that ordering problem went away with the SDK.
//
//	d3d12.lib and dxgi.lib are linked normally rather than resolved at run
//	time, so the executable requires Windows 10.  It was an explicit decision
//	in v0.1.0, and since v0.2.0 Direct3D 12 is the only renderer anyway.
//
//	Comments in the Direct3D 12 files that name "the Direct3D 8 backend" or
//	its RS2D3D8_* functions describe the v0.1.6 behaviour this port matches.
//	That code was removed in v0.2.0; the tagged v0.1.6 build is the reference.

#ifndef RS2D3D12_H_INCLUDED
#define RS2D3D12_H_INCLUDED

#include <d3d12.h>
#include <dxgi1_4.h>

//	Direct3D 12 has no fixed-function pipeline, so RailSim's legacy feature set
//	does not justify asking for more than the level the API itself requires.
#define RS2D3D12_MIN_FEATURE_LEVEL	D3D_FEATURE_LEVEL_11_0

#endif	//	RS2D3D12_H_INCLUDED
