//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	See RS2Width.h.

#include "stdafx.h"
#include "RS2Width.h"

void RS2WidthNarrowingFailed(const char *what, unsigned long long value){
	//	Once per kind is enough to find the caller; a loop over a huge
	//	container would otherwise fill debug.txt.
	static bool reported[3];
	const int k = what[0]=='p' ? 2 : (strstr(what, "DWORD") ? 1 : 0);

	if(reported[k]) return;
	reported[k] = true;
	Debug("[RS2EX] narrowing out of range (%s): %llu, clamped\n", what, value);
}
