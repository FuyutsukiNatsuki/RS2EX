//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	The first visible Direct3D 12 draw.
//
//	-dx12drawsmoke draws through the public RS2 boundary and presents, so that
//	"Direct3D 12 can draw" and "RS2 geometry can reach Direct3D 12" are the
//	same claim rather than two different ones.
//
//	It runs after the renderer is up, because it needs the Direct3D 12 backend
//	installed behind CRS2Renderer, and it exits without starting the game.

#ifndef RS2D3D12DRAWSMOKE_H_INCLUDED
#define RS2D3D12DRAWSMOKE_H_INCLUDED

/*
 *	Whether -dx12drawsmoke was given.
 */
bool RS2D3D12DrawSmokeRequested();

/*
 *	Draw and present.  Returns false if anything refused a draw.
 */
bool RS2D3D12DrawSmokeRun();

#endif	//	RS2D3D12DRAWSMOKE_H_INCLUDED
