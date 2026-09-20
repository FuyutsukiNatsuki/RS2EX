//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.

#include "stdafx.h"
#include "RS2MeshData.h"

void RS2MeshVertexLayout::Clear(){
	stride = 0;
	positionOffset = NOT_PRESENT;
	normalOffset = NOT_PRESENT;
	diffuseOffset = NOT_PRESENT;
	texCoordCount = 0;

	unsigned int i;
	for(i = 0; i<MAX_TEXCOORD; i++){
		texCoord[i].offset = NOT_PRESENT;
		texCoord[i].components = 0;
	}
}

CRS2MeshData::CRS2MeshData()
	: m_VertexBytes(0),
	  m_VertexCount(0),
	  m_Indices(0),
	  m_FaceCount(0),
	  m_FaceMaterialIds(0),
	  m_Subsets(0),
	  m_SubsetCount(0),
	  m_MaterialCount(0)
{
}

CRS2MeshData::~CRS2MeshData(){
	Free();
}

void CRS2MeshData::Free(){
	DELETE_A(m_VertexBytes);
	DELETE_A(m_Indices);
	DELETE_A(m_FaceMaterialIds);
	DELETE_A(m_Subsets);

	m_VertexCount = 0;
	m_FaceCount = 0;
	m_SubsetCount = 0;
	m_MaterialCount = 0;
	m_Layout.Clear();
}

/*
 *	Adopt imported geometry and derive the subset table.
 *
 *	Everything is validated before anything is kept.  A mesh that fails here is
 *	not drawn at all, which is the point: an index past the end of the vertex
 *	buffer is a GPU crash, not a cosmetic problem.
 */
bool CRS2MeshData::Build(
	unsigned char *vertexBytes,
	unsigned int vertexCount,
	const RS2MeshVertexLayout &layout,
	unsigned int *indices,
	unsigned int faceCount,
	unsigned int *faceMaterialIds,
	unsigned int materialCount
){
	Free();

	//	Take ownership immediately so every early return frees, not leaks.
	m_VertexBytes = vertexBytes;
	m_Indices = indices;
	m_FaceMaterialIds = faceMaterialIds;

	if(!vertexBytes || !indices || !faceMaterialIds){
		Debug("[RS2EX Mesh] build: missing buffer\n");
		Free();
		return false;
	}

	//	Plan section 105: a vertex must at least have a position and a size.
	if(!vertexCount || !layout.stride || !layout.HasPosition()){
		Debug("[RS2EX Mesh] build: bad vertex layout (count=%u stride=%u pos=%d)\n",
			vertexCount, layout.stride, layout.positionOffset);
		Free();
		return false;
	}
	if((unsigned int)layout.positionOffset+sizeof(VEC3)>layout.stride){
		Debug("[RS2EX Mesh] build: position does not fit the stride\n");
		Free();
		return false;
	}
	if(!faceCount){
		Debug("[RS2EX Mesh] build: no faces\n");
		Free();
		return false;
	}

	//	Plan section 104: never hand a corrupt index buffer to the GPU.
	unsigned int i;
	const unsigned int indexCount = faceCount*3;
	for(i = 0; i<indexCount; i++){
		if(indices[i]<vertexCount) continue;
		Debug("[RS2EX Mesh] build: index %u out of range (%u >= %u)\n",
			i, indices[i], vertexCount);
		Free();
		return false;
	}

	//	Plan section 106: a face may not point at a material that does not exist.
	//	Remapping it silently would move geometry onto another customizer target.
	for(i = 0; i<faceCount; i++){
		if(faceMaterialIds[i]<materialCount) continue;
		Debug("[RS2EX Mesh] build: face %u has material %u of %u\n",
			i, faceMaterialIds[i], materialCount);
		Free();
		return false;
	}

	m_VertexCount = vertexCount;
	m_Layout = layout;
	m_FaceCount = faceCount;
	m_MaterialCount = materialCount;

	/*
	 *	Derive subset ranges from the face material IDs.
	 *
	 *	One range per run of consecutive faces sharing a material, so a material
	 *	that appears in several places still draws completely.  After
	 *	D3DXMESHOPT_ATTRSORT there is normally one run per material, but the
	 *	representation does not rely on that.
	 */
	unsigned int runs = 1;
	for(i = 1; i<faceCount; i++)
		if(m_FaceMaterialIds[i]!=m_FaceMaterialIds[i-1]) runs++;

	m_Subsets = new RS2MeshSubset[runs];
	m_SubsetCount = 0;

	unsigned int runStart = 0;
	for(i = 1; i<=faceCount; i++){
		if(i<faceCount && m_FaceMaterialIds[i]==m_FaceMaterialIds[runStart]) continue;

		RS2MeshSubset &s = m_Subsets[m_SubsetCount++];
		s.materialId = m_FaceMaterialIds[runStart];
		s.firstIndex = runStart*3;
		s.primitiveCount = i-runStart;
		runStart = i;
	}
	return true;
}

/*
 *	Read one vertex position.
 *
 *	vertexIndex	: 0..GetVertexCount()-1
 *	out			: receives the position
 *
 *	returns		: false if the mesh is empty or the index is out of range
 */
bool CRS2MeshData::GetPosition(unsigned int vertexIndex, VEC3 *out) const{
	if(!out) return false;
	if(!m_VertexBytes || vertexIndex>=m_VertexCount) return false;

	*out = *(const VEC3 *)(m_VertexBytes
		+vertexIndex*m_Layout.stride+m_Layout.positionOffset);
	return true;
}

/*
 *	Total CPU bytes held, for the memory diagnostics the plan asks for.
 */
unsigned int CRS2MeshData::GetCpuByteSize() const{
	return m_VertexCount*m_Layout.stride
		+m_FaceCount*3*sizeof(unsigned int)
		+m_FaceCount*sizeof(unsigned int)
		+m_SubsetCount*sizeof(RS2MeshSubset);
}
