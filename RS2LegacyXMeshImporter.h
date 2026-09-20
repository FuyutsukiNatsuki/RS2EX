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

/*
 *	What one imported material carries into CMesh.
 *
 *	The material is an RS2Material from v0.0.7 on.  The texture file name is
 *	still a name rather than a reference, because acquiring it is CMesh's
 *	decision - the importer does not know the cache or the mip policy.
 */
struct RS2ImportedMaterial
{
	RS2Material material;
	const char *textureFileName;	//	owned by the import result, may be NULL
};

/*
 *	Result of one import.
 *
 *	Owns everything it returns.  Free() releases the material metadata; the
 *	geometry is owned by the CRS2MeshData the caller took.
 */
class CRS2MeshImportResult
{
private:
	RS2ImportedMaterial *m_Materials;
	char **m_TextureNames;
	unsigned int m_MaterialCount;

	CRS2MeshImportResult(const CRS2MeshImportResult &);
	CRS2MeshImportResult &operator=(const CRS2MeshImportResult &);

public:
	CRS2MeshData geometry;

	/*
	 *	Bounding box of the mesh *before* optimisation.
	 *
	 *	Deliberately not derived from the geometry above.  2.15 computed bounds
	 *	before D3DXMESHOPT_COMPACT ran, and COMPACT drops vertices no face
	 *	references - measured: one bundled mesh, Landscape.x, actually loses an
	 *	extreme vertex that way.  Recomputing afterwards would silently tighten
	 *	its bounds and change culling, so the original value is carried out.
	 */
	VEC3 boundsMin;
	VEC3 boundsMax;

	CRS2MeshImportResult();
	~CRS2MeshImportResult();

	void Free();

	bool AllocMaterials(unsigned int count);
	void SetMaterial(
		unsigned int i, const RS2Material &mat, const char *textureFileName);

	unsigned int GetMaterialCount() const{ return m_MaterialCount; }
	const RS2ImportedMaterial &GetMaterial(unsigned int i) const{ return m_Materials[i]; }
};

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
