//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	See RS2Display.h.

#include "stdafx.h"
#include "RS2Display.h"

static unsigned int s_Generation = 0;

void RS2PublishDisplaySize(int width, int height){
	if(width<=0 || height<=0) return;
	if(s_Generation && width==g_DispWidth && height==g_DispHeight) return;

	Debug("[RS2EX] display %d x %d (was %d x %d)\n",
		width, height, g_DispWidth, g_DispHeight);
	g_DispWidth = width;
	g_DispHeight = height;
	s_Generation++;
}

int RS2GetDisplayWidth(){ return g_DispWidth; }
int RS2GetDisplayHeight(){ return g_DispHeight; }

float RS2GetDisplayAspect(){
	return g_DispHeight>0 ? (float)g_DispWidth/g_DispHeight : 1.0f;
}

unsigned int RS2GetDisplayGeneration(){ return s_Generation; }
