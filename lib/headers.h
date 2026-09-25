//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-26.

#include <io.h>
#include <direct.h>
#include <windows.h>
#include <mmsystem.h>	//	timeGetTime()
#include <imm.h>			//	IME関連
#include <stdio.h>
#include <math.h>
#include <string>		//	for std::string
#include <mbstring.h>	//	文字判別
#include <time.h>		//	time()

#include <process.h>

#if defined(__BORLANDC__)	//	for BC++
	#include "bcc.h"
#endif

#if defined(_MSC_VER) && (_MSC_VER<1200)	//	for VC++6.0未満
	//	dinput.hの'UINT_PTR'未定義エラーが回避できるはず・・・（謎）
	#ifdef _WIN64
	typedef unsigned __int64 UINT_PTR, *PUINT_PTR;
	#else
	typedef unsigned long UINT_PTR, *PUINT_PTR;
	#endif
#endif

#ifndef __int3264
typedef unsigned long *DWORD_PTR;
#endif

#ifndef DIRECTINPUT_VERSION
	#define DIRECTINPUT_VERSION 0x0800
#endif

//	[RS2EX] v0.2.0: no Direct3D 8 / D3DX8 / DirectXFile / DirectMusic
//	headers.  The math types are RS2's (RS2Math.h); DirectInput and
//	DirectSound come from the Windows SDK.
#include <dinput.h>
#include <dsound.h>
#include "..\RS2Math.h"

/*
 *	型名変更
 */
typedef RS2Vector2		VEC2;
typedef RS2Vector3		VEC3;
typedef RS2Vector4		VEC4;
typedef RS2Matrix		MTX4;
//	[RS2EX] MAT8 removed in v0.0.7.  Materials are RS2Material, below;
//	D3DMATERIAL8 survives only where it legitimately arrives or departs:
//	the .x importer and the Direct3D 8 binder.
typedef RS2Quaternion	QUAT;

//	[RS2EX] RailSim-owned material values, replacing MAT8.
#include "..\RS2Material.h"

typedef LPDIRECTSOUNDBUFFER		LPSNDBUF;
typedef LPDIRECTSOUNDBUFFER8	LPSNDBUF8;
typedef LPDIRECTSOUND3DBUFFER	LP3DBUF;
typedef LPDIRECTSOUND3DLISTENER	LP3DLISTENER;
typedef LPDIRECTSOUNDNOTIFY		LPSNDNOTIFY;
