//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	See RS2D3D12Unsupported.h.

#include "stdafx.h"
#include "RS2D3D12Unsupported.h"

//	More than the boundary has functions, so the list cannot overflow while
//	there is anything new to say.
#define RS2D3D12_UNSUPPORTED_MAX	96

//	Compared by pointer, not by strcmp.  Every call site passes its own string
//	literal and passes the same one every time, so pointer identity is exactly
//	"have I already reported this call site" - and it costs nothing on a path
//	that a renderer which cannot draw will reach very often.
static const char *s_Seen[RS2D3D12_UNSUPPORTED_MAX];
static unsigned int s_Count = 0;

void RS2D3D12Unsupported(
	const char *name	//	the public function's name
){
	unsigned int i;

	for(i = 0; i<s_Count; i++) if(s_Seen[i]==name) return;

	if(s_Count<RS2D3D12_UNSUPPORTED_MAX) s_Seen[s_Count++] = name;

	Debug("[RS2EX D3D12] %s has no Direct3D 12 implementation yet\n", name);
}

unsigned int RS2D3D12UnsupportedCount(){
	return s_Count;
}
