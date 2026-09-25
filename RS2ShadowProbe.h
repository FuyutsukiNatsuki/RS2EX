//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	A deterministic stencil-shadow probe for either backend (-shadowprobe).
//
//	Twelve cells, each a receiver and a small shadow-volume case drawn with
//	the calls CShadowVolume makes - the RS2SetStencil* setters, position-only
//	RS2DrawImmediate volumes drawn twice with the culling reversed, and a
//	Fill2DRect overlay under a stencil compare.  The expected colour of every
//	region is logged, and the same drawing code runs on both backends, so the
//	Direct3D 8 capture checks the expectations and the Direct3D 12 capture is
//	checked against both.

#ifndef RS2SHADOWPROBE_H_INCLUDED
#define RS2SHADOWPROBE_H_INCLUDED

bool RS2ShadowProbeRequested();
bool RS2ShadowProbeRun();

#endif	//	RS2SHADOWPROBE_H_INCLUDED
