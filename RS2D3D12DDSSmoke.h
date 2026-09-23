//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	-dx12ddssmoke: DDS base textures through the public boundary.
//
//	Every texture is a DDS written by the test from known blocks, created with
//	RS2CreateTextureFromFile, bound with RS2BindTexture(0) and drawn with
//	RS2DrawImmediate - the path real content takes, nothing reached around.
//	The expected pixel of every panel is computed here and logged, so the
//	checker (tools/dds_smoke_check.py) only has to compare.

#ifndef RS2D3D12DDSSMOKE_H_INCLUDED
#define RS2D3D12DDSSMOKE_H_INCLUDED

bool RS2D3D12DDSSmokeRequested();
bool RS2D3D12DDSSmokeRun();

#endif	//	RS2D3D12DDSSMOKE_H_INCLUDED
