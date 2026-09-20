//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	The scene's directional light, owned by RailSim.
//
//	RailSim II 2.15 kept a D3DLIGHT8 in a global and used it for two unrelated
//	jobs: it was the structure handed to the device, and it was where the CPU
//	read the light direction from when building shadow volumes and orienting
//	the sun object.  The second job is not a renderer concern at all, and it is
//	the reason a Direct3D structure had to stay reachable from scene code.
//
//	RS2DirectionalLight separates them.  The engine owns the direction and
//	colour; the backend assembles whatever its API needs when it submits.  CPU
//	shadow generation and GPU submission therefore read the same numbers, which
//	was true before only because they shared a struct.
//
//	This header deliberately does not include d3d8.h.

#ifndef RS2LIGHTING_H_INCLUDED
#define RS2LIGHTING_H_INCLUDED

#include "RS2Material.h"	//	RS2Color4

/*
 *	A direction in world space.
 *
 *	Three floats rather than VEC3, so this header does not need D3DX.  The
 *	conversion at the few call sites that hold a VEC3 is explicit on purpose.
 */
struct RS2Direction
{
	float x, y, z;
};

inline RS2Direction RS2MakeDirection(float x, float y, float z){
	RS2Direction d;

	d.x = x;
	d.y = y;
	d.z = z;
	return d;
}

/*
 *	The single directional light this program has.
 *
 *	Slot 0, always enabled.  Turning the camera light "off" does not disable
 *	the slot - it submits a black light and a dim ambient - so an enable flag
 *	here would invite a change that looks equivalent and is not.
 */
struct RS2DirectionalLight
{
	RS2Direction direction;	//	normalised
	RS2Color4 color;		//	used for both diffuse and specular
};

/*
 *	Set the scene light and submit it.
 *
 *	direction	: normalised here, as SetDirLight always did
 *	color		: becomes both the diffuse and the specular colour
 *
 *	Enables slot 0 as a side effect.  That is existing behaviour and the camera
 *	path depends on it.
 */
void RS2SetDirectionalLight(const RS2Direction &direction, const RS2Color4 &color);

/*
 *	The current light, for CPU work.
 *
 *	Shadow volume construction and the sun object read this.  It is the engine
 *	value, not a read-back from the device.
 */
const RS2DirectionalLight &RS2GetDirectionalLight();

/*
 *	Whether slot 0 is on.  Always true once the light has been set; kept
 *	because the legacy flag it replaces was readable.
 */
bool RS2IsDirectionalLightEnabled();

#endif	//	RS2LIGHTING_H_INCLUDED
