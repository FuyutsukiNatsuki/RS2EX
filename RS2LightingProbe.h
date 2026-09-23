//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	A deterministic material / lighting probe that runs on either backend.
//
//	-lightingprobe draws a fixed grid of patches, each isolating one part of
//	the fixed-function lighting contract - a light direction, a colour source,
//	a specular power, a missing normal - through the public RS2 boundary only.
//
//	Run on Direct3D 8 it is the reference: the fixed-function pipeline says
//	what each patch should look like, measured rather than read from
//	documentation.  Run on Direct3D 12 the same grid shows whether the shader
//	agrees.  The drawing code is shared on purpose, so a difference between
//	the two images can only come from the backends.

#ifndef RS2LIGHTINGPROBE_H_INCLUDED
#define RS2LIGHTINGPROBE_H_INCLUDED

bool RS2LightingProbeRequested();

//	Draws, holds the final frame long enough to photograph, and returns
//	whether every frame was drawn.  Needs the renderer running.
bool RS2LightingProbeRun();

#endif	//	RS2LIGHTINGPROBE_H_INCLUDED
