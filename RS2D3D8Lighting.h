//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	Direct3D 8 submission for the scene light.
//
//	Separated from RS2Lighting so that the engine half stays free of d3d8.h.
//	Nothing above the backend includes this.

#ifndef RS2D3D8LIGHTING_H_INCLUDED
#define RS2D3D8LIGHTING_H_INCLUDED

struct RS2DirectionalLight;

/*
 *	Send the light to slot 0 and enable it.
 */
void RS2D3D8_SubmitDirectionalLight(const RS2DirectionalLight &light);

#endif	//	RS2D3D8LIGHTING_H_INCLUDED
