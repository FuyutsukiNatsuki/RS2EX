//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Mesh import: the result type and the entry point CMesh uses.
//	CRS2MeshImportResult moved here verbatim from RS2LegacyXMeshImporter.cpp.

#include "stdafx.h"
#include "RS2MeshImport.h"
#include "RS2LegacyXMeshImporter.h"

CRS2MeshImportResult::CRS2MeshImportResult()
	: m_Materials(0), m_TextureNames(0), m_MaterialCount(0)
{
	boundsMin = VEC3(0, 0, 0);
	boundsMax = VEC3(0, 0, 0);
}

CRS2MeshImportResult::~CRS2MeshImportResult(){
	Free();
}

void CRS2MeshImportResult::Free(){
	if(m_TextureNames){
		unsigned int i;
		for(i = 0; i<m_MaterialCount; i++) DELETE_A(m_TextureNames[i]);
	}
	DELETE_A(m_TextureNames);
	DELETE_A(m_Materials);
	m_MaterialCount = 0;
	geometry.Free();
}

bool CRS2MeshImportResult::AllocMaterials(unsigned int count){
	if(!count) return false;

	m_Materials = new RS2ImportedMaterial[count];
	m_TextureNames = new char *[count];
	m_MaterialCount = count;

	unsigned int i;
	for(i = 0; i<count; i++){
		m_TextureNames[i] = 0;
		m_Materials[i].textureFileName = 0;
	}
	return true;
}

void CRS2MeshImportResult::SetMaterial(
	unsigned int i, const RS2Material &mat, const char *textureFileName
){
	if(i>=m_MaterialCount) return;

	m_Materials[i].material = mat;

	if(textureFileName){
		const size_t n = strlen(textureFileName)+1;
		m_TextureNames[i] = new char[n];
		memcpy(m_TextureNames[i], textureFileName, n);
	}
	m_Materials[i].textureFileName = m_TextureNames[i];
}

bool RS2ImportMeshFile(BOOL fRes, const char *name, CRS2MeshImportResult *out){
	//	v0.2.0 WP2: the D3DX8 importer stays reachable, on request only, until
	//	the new one has been compared against it.  It goes in WP3.
	static int legacy = -1;

	if(legacy<0){
		legacy = CheckArguments("-ximportlegacy") ? 1 : 0;
		if(legacy) Debug("[RS2EX Import] -ximportlegacy: the D3DX8 importer is used\n");
	}
	if(legacy) return RS2ImportLegacyXMesh(fRes, name, out);
	if(fRes){
		Debug("[RS2EX Import] %s: resource meshes are not supported\n", name);
		return false;
	}
	return RS2ImportXMesh(name, out);
}
