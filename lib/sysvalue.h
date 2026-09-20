//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-21.

#ifndef SYSVALUE_H
#define SYSVALUE_H

char g_errMsg[512];				//	for ASSERT()
char g_debugDest[_MAX_PATH];	//	デバッグ文字の出力先

int g_DispWidth = 640;	//	画面幅
int g_DispHeight = 480;	//	画面高

SYSVALUE_W svw;		//	Window
SYSVALUE_3D sv3;	//	3D
SYSVALUE_F svf;		//	Font
SYSVALUE_I svi;		//	Input
SYSVALUE_S svs;		//	Sound
//SYSVALUE_M svm;		//	Music
//SYSVALUE_V svv;		//	Video
SYSVALUE_C svc;     //	Comm

CFrame g_frame;

#endif
