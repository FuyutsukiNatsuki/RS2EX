//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	Can this machine run a Direct3D 12 backend at all?
//
//	Asked before anything is built, so that -dx12 can fail with a reason
//	instead of failing somewhere inside device creation.  It answers only the
//	question in its name: a true answer does not mean a backend exists yet.
//
//	No Direct3D type appears here.  Callers are selection code, not renderer
//	code, and they should not have to include d3d12.h to ask.

#ifndef RS2D3D12AVAILABILITY_H_INCLUDED
#define RS2D3D12AVAILABILITY_H_INCLUDED

/*
 *	Whether a hardware adapter can create a Direct3D 12 device at the minimum
 *	feature level.
 *
 *	Nothing is created: this uses the documented null-device probe, so asking
 *	costs a DXGI factory and no GPU resources.  Software adapters are skipped -
 *	answering yes because WARP exists would hide exactly the adapter and driver
 *	failures this is meant to report.
 *
 *	Logs what it found, including why it said no.
 */
bool RS2D3D12IsRuntimeUsable();

#endif	//	RS2D3D12AVAILABILITY_H_INCLUDED
