//	Modified for RS2EX on 2026-09-26.
//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Legacy .x / D3DX mesh importer.
//
//	This is the one place in the program still allowed to touch ID3DXMesh, and it
//	only does so for the duration of a call.  D3DX loads and optimises the file, the
//	geometry is copied into RS2-owned data, and the D3DX object is released before
//	the importer returns.  After that no gameplay, render, picking or shadow code
//	needs Direct3D to answer a question about a mesh.
//
//	The D3DX import itself is deliberately unchanged: the same load flags, the same
//	optimisation flags and the same material handling as RailSim II 2.15, because
//	those decide what the content looks like.

#ifndef RS2LEGACYXMESHIMPORTER_H_INCLUDED
#define RS2LEGACYXMESHIMPORTER_H_INCLUDED

#include "RS2MeshData.h"
#include "RS2MeshImport.h"

/*
 *	X file reader
 *
 *	[RS2EX] Moved verbatim from lib/mesh.h (Copyright (c) 2002 Midikyou).
 *	Only the resource-based .x path uses it, and that path now lives entirely
 *	inside the importer.
 */
class CXFile{
	LPDIRECTXFILE			m_pXF;
	LPDIRECTXFILEENUMOBJECT	m_pPtr;

public:
	CXFile();
	~CXFile();
	BOOL Open(LPCSTR strSrc, BOOL fRes = FALSE);
	BOOL GetNextData(LPDIRECTXFILEDATA *ppDat);
	BOOL GetTopMesh(LPDIRECTXFILEDATA *ppDat);
	void Close();
};

//	[RS2EX] v0.2.0: RS2ImportedMaterial and CRS2MeshImportResult moved to
//	RS2MeshImport.h.

/*
 *	Import a .x mesh.
 *
 *	fRes		: TRUE to read from a Win32 resource instead of a file
 *	strName	: file or resource name
 *	out		: receives geometry and legacy material metadata
 *
 *	returns	: false on any failure, with nothing retained
 */
bool RS2ImportLegacyXMesh(BOOL fRes, const char *strName, CRS2MeshImportResult *out);

/*
 *	Legacy D3DX primitive generators.
 *
 *	Same extraction path as the .x importer, so primitives are RS2-owned geometry
 *	too and no runtime ID3DXMesh survives them.  Each produces geometry only; the
 *	single synthetic material stays CMesh's business.
 */
bool RS2ImportLegacySphere(float r, UINT sl, UINT st, CRS2MeshData *out);
bool RS2ImportLegacyBox(float x, float y, float z, CRS2MeshData *out);
bool RS2ImportLegacyTeapot(CRS2MeshData *out);

#endif	//	RS2LEGACYXMESHIMPORTER_H_INCLUDED
