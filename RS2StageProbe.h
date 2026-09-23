//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-24.
//
//	A deterministic texture-stage / environment / UV probe for either backend.
//
//	-stageprobe draws a fixed grid of patches through the public RS2 boundary:
//	secondary combine on, off and without a texture, the order against
//	specular and alpha, environment-mapped normals in every axis direction,
//	stage 0 texture transforms, and point / linear on stage 1.  Run on
//	Direct3D 8 the result is the reference; run on Direct3D 12 it is checked
//	against it.  The drawing code is shared, so a difference can only come
//	from the backends.

#ifndef RS2STAGEPROBE_H_INCLUDED
#define RS2STAGEPROBE_H_INCLUDED

bool RS2StageProbeRequested();
bool RS2StageProbeRun();

#endif	//	RS2STAGEPROBE_H_INCLUDED
