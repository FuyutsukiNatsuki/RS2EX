//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Pointer-width and narrowing contracts (v0.3.0, x64).
//
//	RailSim II was written for a 32-bit process and carried pointers in DWORD
//	fields wherever one slot had to hold "some object".  On x64 a pointer is
//	8 bytes and a DWORD is still 4, so those slots cut addresses in half.
//	This header names the two things that replace the habit:
//
//	RS2OpaqueData - a slot that carries an object pointer of some type, or a
//	    small integer, chosen by whoever filled it (list elements, drag data,
//	    popup-menu dispatch).  It is pointer-sized on every architecture.  Use
//	    a real typed pointer instead wherever a slot only ever holds one type.
//
//	RS2SizeToInt / RS2SizeToDword / RS2DiffToInt - the places where a
//	    size_t or pointer difference really does go into an int or a DWORD
//	    (a string length, the number of switches in a plugin, a count handed to
//	    a 32-bit API).  The value is checked, not just cast: out of range is
//	    reported once in debug.txt and clamped, instead of wrapping silently.
//	    Real inputs are far inside the range; the check is what makes that a
//	    stated fact rather than an assumption.
//
//	Do not widen DWORD values that are 32-bit by definition (colours, flags,
//	timeGetTime counters, DirectInput / DirectSound fields).

#ifndef RS2WIDTH_H_INCLUDED
#define RS2WIDTH_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

typedef uintptr_t RS2OpaqueData;

void RS2WidthNarrowingFailed(const char *what, unsigned long long value);

inline int RS2SizeToInt(size_t v){
	if(v>(size_t)INT_MAX){
		RS2WidthNarrowingFailed("size_t -> int", (unsigned long long)v);
		return INT_MAX;
	}
	return (int)v;
}

inline unsigned long RS2SizeToDword(size_t v){
	if(v>(size_t)0xffffffffu){
		RS2WidthNarrowingFailed("size_t -> DWORD", (unsigned long long)v);
		return 0xffffffffu;
	}
	return (unsigned long)v;
}

inline int RS2DiffToInt(ptrdiff_t v){
	if(v>(ptrdiff_t)INT_MAX || v<(ptrdiff_t)INT_MIN){
		RS2WidthNarrowingFailed("ptrdiff_t -> int", (unsigned long long)v);
		return v<0 ? INT_MIN : INT_MAX;
	}
	return (int)v;
}

#endif	//	RS2WIDTH_H_INCLUDED
