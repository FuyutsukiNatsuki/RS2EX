//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	A deterministic mutable-texture / text probe for either backend
//	(-mutableprobe; -dx12mutablesmoke is the same probe on Direct3D 12).
//
//	Everything goes through the calls the string texture uses:
//	RS2CreateMutableTexture, Lock / Unlock with 16-bit A4R4G4B4 texels written
//	by hand, RS2BindTexture and TexMap2DRect.  Known texels - every channel,
//	alpha 0 / 1 / 7 / 8 / 14 / 15, a gradient for orientation and pitch - are
//	drawn the way strings are drawn, their expected colours are logged, and
//	the update-ordering cases the audit found (update then draw in one frame,
//	update / draw / update / draw of one region, back-to-back updates, a
//	partial update over older content) are drawn beside them.  Strings through
//	CStringTexture and through RS2DrawText give the checker text to compare.
//
//	Run on Direct3D 8 the result checks the expectations; on Direct3D 12 it
//	also checks resource, descriptor and upload counters and the InfoQueue.

#ifndef RS2MUTABLEPROBE_H_INCLUDED
#define RS2MUTABLEPROBE_H_INCLUDED

bool RS2MutableProbeRequested();
bool RS2MutableProbeRun();

#endif	//	RS2MUTABLEPROBE_H_INCLUDED
