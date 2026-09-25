//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Mesh import: what an importer hands CMesh, and the entry point CMesh uses.
//
//	The result types moved here from RS2LegacyXMeshImporter.h in v0.2.0, when
//	the .x importer stopped being D3DX8's: the result never depended on D3DX,
//	and it outlives the importer that used to fill it.

#ifndef RS2MESHIMPORT_H_INCLUDED
#define RS2MESHIMPORT_H_INCLUDED

#include "RS2MeshData.h"
#include "RS2Material.h"

/*
 *	What one imported material carries into CMesh.
 *
 *	The texture file name is still a name rather than a reference, because
 *	acquiring it is CMesh's decision - the importer does not know the cache or
 *	the mip policy.
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
	 *	Bounding box as RailSim II 2.15 measured it, which is not the box of
	 *	the geometry above: D3DX8's D3DXComputeBoundingBox ran on the mesh
	 *	before optimisation and never read its last vertex (v0.2.0 WP0).  The
	 *	importer reproduces that number, so culling and the bounding sphere
	 *	stay where they were.
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
 *	Import a text .x file (v0.2.0).
 *
 *	The contract - the subset of the format, and every rule that makes the
 *	result the same as the D3DX8 importer's - is docs v0.2.0-x-import-contract.
 *	No Direct3D object is created or needed.  On failure the log names the
 *	file and the reason and nothing is retained.
 */
bool RS2ImportXMesh(const char *path, CRS2MeshImportResult *out);

/*
 *	The import CMesh::Load makes.
 *
 *	fRes	: TRUE to read a Win32 resource (no caller does; refused)
 */
bool RS2ImportMeshFile(BOOL fRes, const char *name, CRS2MeshImportResult *out);

#endif	//	RS2MESHIMPORT_H_INCLUDED
