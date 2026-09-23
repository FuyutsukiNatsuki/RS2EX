//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-24.
//
//	-dx12stage1smoke: stage 1, environment mapping and texture transforms
//	through the public boundary.
//
//	Every texture is created with RS2CreateTextureFromFile, bound with
//	RS2BindTexture and drawn with RS2DrawImmediate; every state change goes
//	through the RS2Set* functions CMesh::Render uses.  The expected pixel of
//	every patch is computed here from the contract Direct3D 8 was measured to
//	follow (v0.1.4 WP0) and logged, so the checker
//	(tools/stage1_smoke_check.py) only has to compare.
//
//	On Direct3D 12 it also checks what a screenshot cannot show: rebinding,
//	a foreign texture refused, a texture destroyed while bound, stages past 1
//	refused, and the draw / texture / descriptor counters back at baseline.
//	Without -dx12 it draws the same patches on Direct3D 8 and checks nothing
//	else - that run is how the expectations themselves are checked against
//	the reference renderer.

#ifndef RS2D3D12STAGE1SMOKE_H_INCLUDED
#define RS2D3D12STAGE1SMOKE_H_INCLUDED

bool RS2D3D12Stage1SmokeRequested();
bool RS2D3D12Stage1SmokeRun();

#endif	//	RS2D3D12STAGE1SMOKE_H_INCLUDED
