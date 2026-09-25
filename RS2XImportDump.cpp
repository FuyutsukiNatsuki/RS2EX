//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	-ximportdump: see RS2XImportDump.h.
//
//	Format (little-endian, one record per file, in sorted path order):
//
//		"RS2XDUMP" u32 version
//		per file:
//			u32 pathLength, path bytes (relative, as passed to the importer)
//			u32 ok
//			if ok:
//				f32 boundsMin[3], boundsMax[3]
//				u32 stride, i32 position, i32 normal, i32 diffuse
//				u32 texCoordCount, (i32 offset, u32 components) x texCoordCount
//				u32 vertexCount, vertex bytes (vertexCount * stride)
//				u32 faceCount, u32 indices[faceCount * 3], u32 faceMaterial[faceCount]
//				u32 subsetCount, (u32 material, u32 firstIndex, u32 primitives) x n
//				u32 materialCount, per material:
//					f32 diffuse[4], ambient[4], specular[4], emissive[4], power
//					i32 textureNameLength (-1 = none), bytes
//		"RS2XEND!"

#include "stdafx.h"
#include "RS2XImportDump.h"
#include "RS2LegacyXMeshImporter.h"

#include <stdio.h>

bool RS2XImportDumpRequested(){
	return CheckArguments("-ximportdump")!=FALSE;
}

static void RS2XDFind(const std::string &dir, std::vector<std::string> *out){
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir+"*").c_str(), &fd);

	if(h==INVALID_HANDLE_VALUE) return;
	do{
		const std::string name = fd.cFileName;

		if(name=="." || name=="..") continue;
		if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
			RS2XDFind(dir+name+"\\", out);
		}else if(name.size()>2 && _stricmp(name.c_str()+name.size()-2, ".x")==0){
			out->push_back(dir+name);
		}
	}while(FindNextFileA(h, &fd));
	FindClose(h);
}

static bool RS2XDLess(const std::string &a, const std::string &b){
	return _stricmp(a.c_str(), b.c_str())<0;
}

static void RS2XDU32(FILE *f, unsigned int v){ fwrite(&v, 4, 1, f); }
static void RS2XDI32(FILE *f, int v){ fwrite(&v, 4, 1, f); }
static void RS2XDF32(FILE *f, float v){ fwrite(&v, 4, 1, f); }

static void RS2XDColor(FILE *f, const RS2Color4 &c){
	RS2XDF32(f, c.r); RS2XDF32(f, c.g); RS2XDF32(f, c.b); RS2XDF32(f, c.a);
}

//	WP0 investigation only: the mesh D3DXLoadMeshFromX returns, before the
//	importer optimises it (-ximportraw <file>).
#include "RS2LegacyImportDevice.h"

static void RS2XDRaw(const char *path){
	IDirect3DDevice8 *device = RS2GetLegacyImportDevice();
	LPD3DXMESH mesh = 0;
	LPD3DXBUFFER adj = 0, mat = 0;
	DWORD nmat = 0;

	if(!device || FAILED(D3DXLoadMeshFromX((LPSTR)path, D3DXMESH_SYSTEMMEM, device, &adj, &mat, &nmat, &mesh))){
		Debug("RS2XIMPORTRAW|load failed|%s\n", path);
		return;
	}

	BYTE *pv = 0;
	const DWORD n = mesh->GetNumVertices(), stride = D3DXGetFVFVertexSize(mesh->GetFVF());

	Debug("RS2XIMPORTRAW|%s|vertices=%u|faces=%u|fvf=%x|stride=%u\n", path, n, mesh->GetNumFaces(), mesh->GetFVF(), stride);
	if(SUCCEEDED(mesh->LockVertexBuffer(D3DLOCK_READONLY, &pv))){
		D3DXVECTOR3 mn, mx;
		DWORD i;

		D3DXComputeBoundingBox(pv, n, mesh->GetFVF(), &mn, &mx);
		Debug("RS2XIMPORTRAW|bounds|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g\n", mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
		for(i = 0; i<n; i++){
			const float *p = (const float *)(pv+i*stride);

			Debug("RS2XIMPORTRAW|v|%u|%.9g|%.9g|%.9g\n", i, p[0], p[1], p[2]);
		}
		mesh->UnlockVertexBuffer();
	}
	RELEASE(adj);
	RELEASE(mat);
	RELEASE(mesh);
}

bool RS2XImportDumpRun(){
	{
		if(CheckArguments("-ximportraw")){
			RS2XDRaw("Env\\Default\\Landscape.x");
			return true;
		}
	}

	std::vector<std::string> files;

	RS2XDFind("", &files);
	std::sort(files.begin(), files.end(), RS2XDLess);

	FILE *f = fopen("ximport-dump.bin", "wb");

	if(!f){
		Debug("RS2XIMPORTDUMP|cannot write ximport-dump.bin|FAIL\n");
		return false;
	}
	fwrite("RS2XDUMP", 8, 1, f);
	RS2XDU32(f, 1);

	unsigned int imported = 0, failed = 0, vertices = 0, faces = 0;
	size_t i;

	for(i = 0; i<files.size(); i++){
		const std::string &path = files[i];
		CRS2MeshImportResult import;

		Debug("RS2XIMPORTDUMP|import|%s\n", path.c_str());
		const bool ok = RS2ImportLegacyXMesh(FALSE, path.c_str(), &import) && import.geometry.IsValid();

		RS2XDU32(f, (unsigned int)path.size());
		fwrite(path.c_str(), path.size(), 1, f);
		RS2XDU32(f, ok ? 1u : 0u);
		if(!ok){
			failed++;
			Debug("RS2XIMPORTDUMP|failed|%s\n", path.c_str());
			continue;
		}
		imported++;

		const CRS2MeshData &g = import.geometry;
		const RS2MeshVertexLayout &l = g.GetLayout();
		unsigned int k;

		RS2XDF32(f, import.boundsMin.x); RS2XDF32(f, import.boundsMin.y); RS2XDF32(f, import.boundsMin.z);
		RS2XDF32(f, import.boundsMax.x); RS2XDF32(f, import.boundsMax.y); RS2XDF32(f, import.boundsMax.z);
		RS2XDU32(f, l.stride);
		RS2XDI32(f, l.positionOffset);
		RS2XDI32(f, l.normalOffset);
		RS2XDI32(f, l.diffuseOffset);
		RS2XDU32(f, l.texCoordCount);
		for(k = 0; k<l.texCoordCount; k++){
			RS2XDI32(f, l.texCoord[k].offset);
			RS2XDU32(f, l.texCoord[k].components);
		}
		RS2XDU32(f, g.GetVertexCount());
		fwrite(g.GetVertexBytes(), g.GetVertexBytesSize(), 1, f);
		RS2XDU32(f, g.GetFaceCount());
		fwrite(g.GetIndices(), 4, g.GetIndexCount(), f);
		for(k = 0; k<g.GetFaceCount(); k++) RS2XDU32(f, g.GetFaceMaterialId(k));
		RS2XDU32(f, g.GetSubsetCount());
		for(k = 0; k<g.GetSubsetCount(); k++){
			const RS2MeshSubset &s = g.GetSubset(k);

			RS2XDU32(f, s.materialId);
			RS2XDU32(f, s.firstIndex);
			RS2XDU32(f, s.primitiveCount);
		}
		RS2XDU32(f, import.GetMaterialCount());
		for(k = 0; k<import.GetMaterialCount(); k++){
			const RS2ImportedMaterial &m = import.GetMaterial(k);

			RS2XDColor(f, m.material.Diffuse);
			RS2XDColor(f, m.material.Ambient);
			RS2XDColor(f, m.material.Specular);
			RS2XDColor(f, m.material.Emissive);
			RS2XDF32(f, m.material.Power);
			if(m.textureFileName){
				const int n = (int)strlen(m.textureFileName);

				RS2XDI32(f, n);
				fwrite(m.textureFileName, n, 1, f);
			}else{
				RS2XDI32(f, -1);
			}
		}
		fflush(f);
		vertices += g.GetVertexCount();
		faces += g.GetFaceCount();
	}
	fwrite("RS2XEND!", 8, 1, f);
	fclose(f);

	Debug("RS2XIMPORTDUMP|files=%u|imported=%u|failed=%u|vertices=%u|faces=%u\n",
		(unsigned int)files.size(), imported, failed, vertices, faces);
	Debug("RS2XIMPORTDUMP|%s\n", failed ? "FAIL" : "pass");
	return failed==0;
}
