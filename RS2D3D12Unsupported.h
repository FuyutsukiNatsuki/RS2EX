//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	What a Direct3D 12 renderer says when it is asked for something v0.1.0 has
//	not built yet.
//
//	v0.1.0 is a backend bootstrap: it owns a device, a swap chain and a frame,
//	and it can clear and present.  It cannot draw, bind a texture, set render
//	state or render text, and the boundary those go through has to answer
//	something when Direct3D 12 is running.
//
//	It answers here rather than falling through to the Direct3D 8
//	implementation.  A fall-through would reach a device this backend does not
//	own, and it would work - for as long as both backends happened to be
//	initialised - which is exactly the kind of accidental dependency the last
//	six releases were spent removing.

#ifndef RS2D3D12UNSUPPORTED_H_INCLUDED
#define RS2D3D12UNSUPPORTED_H_INCLUDED

/*
 *	Report that a boundary function has no Direct3D 12 implementation.
 *
 *	name	: the public function's name, as a string literal
 *
 *	Logged once per call site.  A renderer that cannot draw would otherwise
 *	fill the log at frame rate, and the one thing worth knowing - which parts
 *	of the boundary a real scene actually reaches - would be buried in it.
 */
void RS2D3D12Unsupported(const char *name);

/*
 *	How many distinct boundary functions have reported that.
 *
 *	The smoke test reads it: the list of what a running scene asks for is the
 *	work list for the next release, and a count of zero after a frame would
 *	mean the dispatch is not being reached at all.
 */
unsigned int RS2D3D12UnsupportedCount();

#endif	//	RS2D3D12UNSUPPORTED_H_INCLUDED
