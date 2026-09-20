//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.

#include "stdafx.h"
#include "RS2D3D8MeshResource.h"
#include "RS2D3D8Resources.h"

static unsigned int s_LiveMeshResources = 0;
static unsigned int s_MeshVertexBytes = 0;
static unsigned int s_MeshIndexBytes = 0;

unsigned int RS2D3D8_GetLiveMeshResourceCount(){ return s_LiveMeshResources; }
unsigned int RS2D3D8_GetMeshVertexBytes(){ return s_MeshVertexBytes; }
unsigned int RS2D3D8_GetMeshIndexBytes(){ return s_MeshIndexBytes; }

/*
 *	Translate an API-neutral layout back into a legacy FVF.
 *
 *	This is the backend's job and nobody else's.  The generic layer describes
 *	where the fields are; turning that into D3D8's bit flags belongs here, so a
 *	future backend can describe the same vertices its own way.
 *
 *	returns	: false if the layout cannot be expressed as an FVF
 */
static bool RS2LayoutToFVF(const RS2MeshVertexLayout &layout, DWORD *outFvf){
	if(!layout.HasPosition()) return false;

	DWORD fvf = D3DFVF_XYZ;

	if(layout.HasNormal()) fvf |= D3DFVF_NORMAL;
	if(layout.HasDiffuse()) fvf |= D3DFVF_DIFFUSE;

	if(layout.texCoordCount>8) return false;
	fvf |= layout.texCoordCount<<D3DFVF_TEXCOUNT_SHIFT;

	unsigned int i;
	for(i = 0; i<layout.texCoordCount; i++){
		switch(layout.texCoord[i].components){
		case 1:	fvf |= D3DFVF_TEXTUREFORMAT1<<(i*2+16); break;
		case 2:	/* D3DFVF_TEXTUREFORMAT2 is zero */ break;
		case 3:	fvf |= D3DFVF_TEXTUREFORMAT3<<(i*2+16); break;
		case 4:	fvf |= D3DFVF_TEXTUREFORMAT4<<(i*2+16); break;
		default:	return false;
		}
	}

	//	If the round trip does not reproduce the stride the layout was built from,
	//	the vertex bytes would be reinterpreted wrongly.  Refuse instead.
	if(D3DXGetFVFVertexSize(fvf)!=layout.stride){
		Debug("[RS2EX Mesh] layout stride %u does not match fvf %08x\n",
			layout.stride, fvf);
		return false;
	}
	*outFvf = fvf;
	return true;
}

CRS2D3D8MeshResource::CRS2D3D8MeshResource()
	: m_pVB(0), m_pIB(0), m_Fvf(0), m_Stride(0), m_VertexCount(0), m_IndexCount(0)
{
}

CRS2D3D8MeshResource::~CRS2D3D8MeshResource(){
	Free();
}

void CRS2D3D8MeshResource::Free(){
	if(m_pVB || m_pIB){
		s_MeshVertexBytes -= GetVertexBytes();
		s_MeshIndexBytes -= GetIndexBytes();
		if(s_LiveMeshResources) s_LiveMeshResources--;
	}
	RS2D3D8_ReleaseVertexBuffer(&m_pVB);
	RS2D3D8_ReleaseIndexBuffer(&m_pIB);

	m_Fvf = 0;
	m_Stride = 0;
	m_VertexCount = 0;
	m_IndexCount = 0;
}

/*
 *	Upload geometry.
 *
 *	Indices are narrowed to 16 bits.  Every mesh in the measured content is
 *	already 16-bit, and CRS2MeshData has validated that no index exceeds the
 *	vertex count, so a mesh that would not fit is refused rather than truncated.
 */
bool CRS2D3D8MeshResource::Create(const CRS2MeshData &data){
	Free();

	if(!data.IsValid()) return false;

	const RS2MeshVertexLayout &layout = data.GetLayout();
	if(!RS2LayoutToFVF(layout, &m_Fvf)) return false;

	const unsigned int vertexCount = data.GetVertexCount();
	const unsigned int indexCount = data.GetIndexCount();

	if(vertexCount>0xffff){
		Debug("[RS2EX Mesh] %u vertices exceeds the 16-bit index range\n", vertexCount);
		m_Fvf = 0;
		return false;
	}

	m_Stride = layout.stride;
	m_VertexCount = vertexCount;
	m_IndexCount = indexCount;

	if(!RS2D3D8_CreateVertexBuffer(data.GetVertexBytesSize(), m_Fvf, &m_pVB)){
		Free();
		return false;
	}
	if(!RS2D3D8_UploadVertexBuffer(m_pVB, data.GetVertexBytes(), data.GetVertexBytesSize())){
		Free();
		return false;
	}

	if(!RS2D3D8_CreateIndexBuffer(indexCount*sizeof(WORD), &m_pIB)){
		Free();
		return false;
	}

	WORD *dst = 0;
	if(!RS2D3D8_LockIndexBuffer(m_pIB, (void **)&dst)){
		Free();
		return false;
	}
	{
		const unsigned int *src = data.GetIndices();
		unsigned int i;
		for(i = 0; i<indexCount; i++) dst[i] = (WORD)src[i];
	}
	RS2D3D8_UnlockIndexBuffer(m_pIB);

	s_LiveMeshResources++;
	s_MeshVertexBytes += GetVertexBytes();
	s_MeshIndexBytes += GetIndexBytes();
	return true;
}

/*
 *	Draw one run of triangles.
 *
 *	firstIndex		: first index of the run
 *	primitiveCount	: number of triangles
 *
 *	An empty run draws nothing, which is normal: a material may own no faces.
 */
void CRS2D3D8MeshResource::DrawRange(
	unsigned int firstIndex, unsigned int primitiveCount
) const{
	if(!m_pVB || !m_pIB || !primitiveCount) return;

	sv3.pDev->SetStreamSource(0, m_pVB, m_Stride);
	sv3.pDev->SetIndices(m_pIB, 0);
	sv3.pDev->SetVertexShader(m_Fvf);
	sv3.pDev->DrawIndexedPrimitive(
		D3DPT_TRIANGLELIST, 0, m_VertexCount, firstIndex, primitiveCount);
}
