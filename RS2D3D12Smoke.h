//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	The Direct3D 12 bootstrap smoke test.
//
//	v0.0.9 proved that a build can pass every static gate while the screen is
//	blank, so a backend being built in stages needs something that exercises
//	it directly rather than a dependency scan saying it looks right.
//
//	This is that: a fixed sequence run against whatever the backend can do so
//	far, with a pass or fail per step in the log.  It does not claim anything
//	about scene rendering, and it grows one step per work package.
//
//	-dx12smoke runs it and exits.  It never starts the game, so it cannot be
//	confused with -dx12, which selects the backend for a normal run.

#ifndef RS2D3D12SMOKE_H_INCLUDED
#define RS2D3D12SMOKE_H_INCLUDED

/*
 *	Whether -dx12smoke was given.
 */
bool RS2D3D12SmokeRequested();

/*
 *	Run the sequence and report.
 *
 *	window	: the application window, for the swap chain once there is one
 *	returns	: true when every step passed
 *
 *	Results go to the debug log with an RS2D3D12SMOKE prefix, so a caller that
 *	cannot see a console can still read the outcome from debug.txt.
 */
bool RS2D3D12SmokeRun(HWND window);

#endif	//	RS2D3D12SMOKE_H_INCLUDED
