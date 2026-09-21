//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	CXFile and the D3DX load/optimise sequence below are inherited from the UDX
//	library (Copyright (c) 2002 Midikyou, lib/mesh.cpp) and moved here rather than
//	rewritten.  The load flags, the optimisation flags and the top-level-mesh
//	selection rule all decide what existing content looks like, so they are
//	preserved exactly.  What is new is everything after the optimisation: the
//	geometry is copied out into RS2-owned memory and the D3DX object is released.

#include "stdafx.h"

#include <rmxfguid.h>
#include <rmxftmpl.h>

#include "RS2LegacyXMeshImporter.h"
#include "RS2LegacyImportDevice.h"

////////////////////////////////////////////////////////////////////////////////
//	CXFile - inherited from lib/mesh.cpp
////////////////////////////////////////////////////////////////////////////////

CXFile::CXFile(){
	m_pXF = NULL;
	m_pPtr = NULL;
}

CXFile::~CXFile(){
	Close();
}

/*
 *	File or resource open
 */
BOOL CXFile::Open(LPCSTR strSrc, BOOL fRes){
	HRESULT hr;

	DirectXFileCreate(&m_pXF);
	m_pXF->RegisterTemplates((LPVOID)D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES);

	if(fRes){
		DXFILELOADRESOURCE res;

		res.hModule = NULL;
		res.lpName = strSrc;
		res.lpType = "X";

		hr = m_pXF->CreateEnumObject(
			(LPVOID)&res, DXFILELOAD_FROMRESOURCE, &m_pPtr);
	}else{
		hr = m_pXF->CreateEnumObject(
			(LPVOID)strSrc,	DXFILELOAD_FROMFILE, &m_pPtr);
	}
	return hr==DXFILE_OK;
}

BOOL CXFile::GetNextData(LPDIRECTXFILEDATA *ppDat){
	HRESULT hr;

	hr = m_pPtr->GetNextDataObject(ppDat);

	return hr==DXFILE_OK;
}

/*
 *	First top-level mesh in the file
 */
BOOL CXFile::GetTopMesh(LPDIRECTXFILEDATA *ppDat){
	const GUID *type;

	while(1){
		if(!GetNextData(ppDat)) return FALSE;

		(*ppDat)->GetType(&type);

		if(*type==TID_D3DRMMesh) break;
		else RELEASE(*ppDat);
	}
	return TRUE;
}

void CXFile::Close(){
	RELEASE(m_pPtr);
	RELEASE(m_pXF);
}

////////////////////////////////////////////////////////////////////////////////
//	Import result
////////////////////////////////////////////////////////////////////////////////

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

/*
 *	Convert one material as D3DX reports it.
 *
 *	D3DXLoadMeshFromX hands back D3DMATERIAL8, so this file is where that
 *	type legitimately appears - the .x format is a Direct3D format.  The
 *	values are copied unchanged; the import must not reinterpret content.
 */
static RS2Material RS2FromD3DMaterial(const D3DMATERIAL8 &src){
	RS2Material dst;

	dst.Diffuse = RS2MakeColor4(
		src.Diffuse.r, src.Diffuse.g, src.Diffuse.b, src.Diffuse.a);
	dst.Ambient = RS2MakeColor4(
		src.Ambient.r, src.Ambient.g, src.Ambient.b, src.Ambient.a);
	dst.Specular = RS2MakeColor4(
		src.Specular.r, src.Specular.g, src.Specular.b, src.Specular.a);
	dst.Emissive = RS2MakeColor4(
		src.Emissive.r, src.Emissive.g, src.Emissive.b, src.Emissive.a);
	dst.Power = src.Power;
	return dst;
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

////////////////////////////////////////////////////////////////////////////////
//	FVF decoding
////////////////////////////////////////////////////////////////////////////////

/*
 *	Translate a legacy FVF into an API-neutral vertex layout.
 *
 *	fvf		: the D3DX mesh FVF
 *	out		: receives the layout
 *
 *	returns	: false if the format cannot be described exactly
 *
 *	Plan section 14: an FVF that cannot be translated is reported and refused,
 *	never guessed at.  Dropping an attribute or inventing one changes
 *	fixed-function lighting, and a wrong stride corrupts every vertex.
 *
 *	Field order in a D3D8 FVF vertex is fixed: position, blend weights, normal,
 *	point size, diffuse, specular, then texture coordinates.
 */
static bool RS2DecodeFVF(DWORD fvf, RS2MeshVertexLayout *out){
	out->Clear();

	unsigned int offset = 0;

	//	Position.  Only untransformed XYZ is supported: XYZRHW means the vertex is
	//	already in screen space, and blend weights imply skinning, neither of which
	//	the mesh pipeline here has ever handled.
	const DWORD posType = fvf&D3DFVF_POSITION_MASK;
	if(posType!=D3DFVF_XYZ){
		Debug("[RS2EX Mesh] unsupported position format in fvf=%08x\n", fvf);
		return false;
	}
	out->positionOffset = offset;
	offset += sizeof(float)*3;

	if(fvf&D3DFVF_NORMAL){
		out->normalOffset = offset;
		offset += sizeof(float)*3;
	}
	if(fvf&D3DFVF_PSIZE) offset += sizeof(float);

	if(fvf&D3DFVF_DIFFUSE){
		out->diffuseOffset = offset;
		offset += sizeof(DWORD);
	}
	if(fvf&D3DFVF_SPECULAR) offset += sizeof(DWORD);

	const unsigned int texCount =
		(fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;

	if(texCount>RS2MeshVertexLayout::MAX_TEXCOORD){
		Debug("[RS2EX Mesh] %u texture coordinate sets in fvf=%08x\n", texCount, fvf);
		return false;
	}
	out->texCoordCount = texCount;

	unsigned int i;
	for(i = 0; i<texCount; i++){
		//	Two bits per set, starting at bit 16, say how many floats it has.
		const DWORD sizeCode = (fvf>>(i*2+16))&0x3;
		unsigned int components;

		switch(sizeCode){
		case D3DFVF_TEXTUREFORMAT1:	components = 1; break;
		case D3DFVF_TEXTUREFORMAT2:	components = 2; break;
		case D3DFVF_TEXTUREFORMAT3:	components = 3; break;
		case D3DFVF_TEXTUREFORMAT4:	components = 4; break;
		default:
			Debug("[RS2EX Mesh] bad texcoord size in fvf=%08x\n", fvf);
			return false;
		}
		out->texCoord[i].offset = offset;
		out->texCoord[i].components = components;
		offset += sizeof(float)*components;
	}

	out->stride = offset;

	//	Cross-check against D3DX.  A mismatch means this decoder has drifted from
	//	what the runtime actually packed, which must never be guessed past.
	const UINT d3dxStride = D3DXGetFVFVertexSize(fvf);
	if(d3dxStride!=out->stride){
		Debug("[RS2EX Mesh] stride mismatch for fvf=%08x: decoded %u, D3DX says %u\n",
			fvf, out->stride, d3dxStride);
		return false;
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////
//	Geometry extraction
////////////////////////////////////////////////////////////////////////////////

/*
 *	Copy an optimised D3DX mesh into RS2-owned data.
 *
 *	pMesh		: source, unchanged
 *	materialCount	: size of the material table the faces may refer to
 *	out		: receives the geometry
 *
 *	returns		: false on any failure, leaving out empty
 */
static bool RS2ExtractMeshData(
	LPD3DXMESH pMesh, unsigned int materialCount, CRS2MeshData *out
){
	if(!pMesh || !out) return false;

	RS2MeshVertexLayout layout;
	if(!RS2DecodeFVF(pMesh->GetFVF(), &layout)) return false;

	const DWORD vertexCount = pMesh->GetNumVertices();
	const DWORD faceCount = pMesh->GetNumFaces();

	if(!vertexCount || !faceCount){
		Debug("[RS2EX Mesh] empty mesh (v=%u f=%u)\n", vertexCount, faceCount);
		return false;
	}

	//	D3DXMESH_32BIT decides the index width; the bundled content is all 16-bit,
	//	but both are copied into one API-neutral width so callers never ask again.
	const bool index32 = (pMesh->GetOptions()&D3DXMESH_32BIT)!=0;

	unsigned char *vertexBytes = 0;
	unsigned int *indices = 0;
	unsigned int *faceMaterialIds = 0;

	BYTE *pV = 0;
	BYTE *pI = 0;
	DWORD *pA = 0;
	bool ok = false;

	do{
		if(FAILED(pMesh->LockVertexBuffer(D3DLOCK_READONLY, &pV))) break;
		if(FAILED(pMesh->LockIndexBuffer(D3DLOCK_READONLY, &pI))) break;

		//	A mesh with one material may have no attribute buffer at all; that is
		//	not an error, every face simply belongs to material 0.
		pMesh->LockAttributeBuffer(D3DLOCK_READONLY, &pA);

		const unsigned int vertexBytesSize = vertexCount*layout.stride;
		vertexBytes = new unsigned char[vertexBytesSize];
		memcpy(vertexBytes, pV, vertexBytesSize);

		const unsigned int indexCount = faceCount*3;
		indices = new unsigned int[indexCount];

		unsigned int i;
		if(index32){
			const DWORD *src = (const DWORD *)pI;
			for(i = 0; i<indexCount; i++) indices[i] = src[i];
		}else{
			const WORD *src = (const WORD *)pI;
			for(i = 0; i<indexCount; i++) indices[i] = src[i];
		}

		faceMaterialIds = new unsigned int[faceCount];
		for(i = 0; i<faceCount; i++) faceMaterialIds[i] = pA ? pA[i] : 0;

		ok = true;
	}while(false);

	if(pA) pMesh->UnlockAttributeBuffer();
	if(pI) pMesh->UnlockIndexBuffer();
	if(pV) pMesh->UnlockVertexBuffer();

	if(!ok){
		Debug("[RS2EX Mesh] could not lock source mesh\n");
		DELETE_A(vertexBytes);
		DELETE_A(indices);
		DELETE_A(faceMaterialIds);
		return false;
	}

	//	Build() adopts the buffers and validates them; it frees them on failure.
	return out->Build(vertexBytes, vertexCount, layout,
		indices, faceCount, faceMaterialIds, materialCount);
}

////////////////////////////////////////////////////////////////////////////////
//	.x import
////////////////////////////////////////////////////////////////////////////////

bool RS2ImportLegacyXMesh(BOOL fRes, const char *strName, CRS2MeshImportResult *out){
	if(!out) return false;
	out->Free();

	LPD3DXMESH pMesh = 0;
	LPD3DXBUFFER pBuf = 0;
	LPD3DXBUFFER pAdj = 0;
	DWORD numMat = 0;
	//	[RS2EX] The importer's own device, not the renderer's.  If it cannot
	//	be created the import fails, leaving the destination empty - the same
	//	outcome a missing or unreadable file already produces.
	IDirect3DDevice8 *device = RS2GetLegacyImportDevice();
	if(!device) return false;

	HRESULT hr;

	if(fRes){
		//	Resource path, inherited: the first top-level mesh object wins.
		CXFile xfile;
		if(!xfile.Open(strName, TRUE)){
			Debug("open failed.\n");
			return false;
		}
		LPDIRECTXFILEDATA pDat;
		if(!xfile.GetTopMesh(&pDat)){
			Debug("mesh is not found.\n");
			return false;
		}
		hr = D3DXLoadMeshFromXof(pDat, D3DXMESH_SYSTEMMEM,
			device, &pAdj, &pBuf, &numMat, &pMesh);
		RELEASE(pDat);
		xfile.Close();
	}else{
		hr = D3DXLoadMeshFromX(
			(LPSTR)strName, D3DXMESH_SYSTEMMEM,
			device, &pAdj, &pBuf, &numMat, &pMesh);
	}

	if(FAILED(hr)){
		Debug("failed.\n");
		RELEASE(pBuf);
		RELEASE(pAdj);
		RELEASE(pMesh);
		return false;
	}
	Debug("ok.\n");

	//	Legacy material metadata.  The values are passed through untouched;
	//	RailSim's own rules about them stay in CMesh.
	if(numMat && pBuf){
		const D3DXMATERIAL *pMat = (const D3DXMATERIAL *)pBuf->GetBufferPointer();
		out->AllocMaterials(numMat);

		DWORD i;
		for(i = 0; i<numMat; i++)
			out->SetMaterial(
				i, RS2FromD3DMaterial(pMat[i].MatD3D), pMat[i].pTextureFilename);
	}
	RELEASE(pBuf);

	/*
	 *	Bounds, computed here and not from the extracted geometry.
	 *
	 *	2.15 measured the mesh before optimising it, and COMPACT removes vertices
	 *	that no face uses.  Keeping the original call and the original moment
	 *	keeps the numbers identical.
	 */
	{
		BYTE *pv = 0;
		if(SUCCEEDED(pMesh->LockVertexBuffer(D3DLOCK_READONLY, &pv))){
			D3DXComputeBoundingBox((VOID *)pv, pMesh->GetNumVertices(),
				pMesh->GetFVF(), &out->boundsMin, &out->boundsMax);
			pMesh->UnlockVertexBuffer();
		}
	}
	//	Same optimisation as 2.15, and for the same reason: the geometry extracted
	//	below must be the geometry the game used to render, pick and shadow.
	if(pAdj){
		LPD3DXMESH pMeshOpt = NULL;
		DWORD *pAdjBuf = (DWORD *)pAdj->GetBufferPointer();

		hr = pMesh->Optimize(
			D3DXMESHOPT_ATTRSORT|D3DXMESHOPT_COMPACT|D3DXMESHOPT_VERTEXCACHE,
			pAdjBuf, NULL, NULL, NULL, &pMeshOpt);

		if(SUCCEEDED(hr)){
			pMesh->Release();
			pMesh = pMeshOpt;
		}else{
			Debug("optimization failed (%x).\n", hr);
		}
	}
	RELEASE(pAdj);

	const bool extracted = RS2ExtractMeshData(pMesh, numMat, &out->geometry);

	//	The whole point: D3DX does not outlive this function.
	RELEASE(pMesh);

	if(!extracted){
		out->Free();
		return false;
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////
//	Primitive generation
////////////////////////////////////////////////////////////////////////////////

/*
 *	Extract and release a freshly generated D3DX primitive.
 *
 *	Primitives always carry exactly one material.
 */
static bool RS2FinishPrimitive(LPD3DXMESH pMesh, CRS2MeshData *out){
	if(!pMesh) return false;

	const bool extracted = RS2ExtractMeshData(pMesh, 1, out);
	pMesh->Release();
	return extracted;
}

bool RS2ImportLegacySphere(float r, UINT sl, UINT st, CRS2MeshData *out){
	LPD3DXMESH pMesh = NULL;
	//	[RS2EX] The importer's own device, not the renderer's.  If it cannot
	//	be created the import fails, leaving the destination empty - the same
	//	outcome a missing or unreadable file already produces.
	IDirect3DDevice8 *device = RS2GetLegacyImportDevice();
	if(!device) return false;

	if(FAILED(D3DXCreateSphere(device, r, sl, st, &pMesh, NULL))){
		Debug("D3DXCreateSphere\n");
		return false;
	}
	return RS2FinishPrimitive(pMesh, out);
}

bool RS2ImportLegacyBox(float x, float y, float z, CRS2MeshData *out){
	LPD3DXMESH pMesh = NULL;
	//	[RS2EX] The importer's own device, not the renderer's.  If it cannot
	//	be created the import fails, leaving the destination empty - the same
	//	outcome a missing or unreadable file already produces.
	IDirect3DDevice8 *device = RS2GetLegacyImportDevice();
	if(!device) return false;

	if(FAILED(D3DXCreateBox(device, x, y, z, &pMesh, NULL))){
		//	[RS2EX] 2.15 logged "D3DXCreateSphere" here; kept verbatim so the
		//	existing debug output does not change.  Recorded as a known issue.
		Debug("D3DXCreateSphere\n");
		return false;
	}
	return RS2FinishPrimitive(pMesh, out);
}

bool RS2ImportLegacyTeapot(CRS2MeshData *out){
	LPD3DXMESH pMesh = NULL;
	//	[RS2EX] The importer's own device, not the renderer's.  If it cannot
	//	be created the import fails, leaving the destination empty - the same
	//	outcome a missing or unreadable file already produces.
	IDirect3DDevice8 *device = RS2GetLegacyImportDevice();
	if(!device) return false;

	if(FAILED(D3DXCreateTeapot(device, &pMesh, NULL))){
		Debug("D3DXCreateTeapot\n");
		return false;
	}
	return RS2FinishPrimitive(pMesh, out);
}
