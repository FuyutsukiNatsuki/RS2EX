//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Direct3D 8 indexed mesh GPU resource.
//
//	Extends the v0.0.5 resource boundary with the one thing it lacked: an indexed
//	mesh.  It owns a vertex buffer, an index buffer and the FVF translated from the
//	API-neutral layout - and nothing else.  It is told "draw this range of indexed
//	geometry", never "render this RailSim material": material state stays in CMesh,
//	where the customizers can reach it.
//
//	Both buffers are D3DPOOL_MANAGED, so static mesh geometry needs no reset
//	participation - see docs/v0.0.6-mesh-inventory.md section 8.

#ifndef RS2D3D8MESHRESOURCE_H_INCLUDED
#define RS2D3D8MESHRESOURCE_H_INCLUDED

#include "RS2MeshData.h"

class CRS2D3D8MeshResource
{
private:
	LPDIRECT3DVERTEXBUFFER8 m_pVB;
	LPDIRECT3DINDEXBUFFER8 m_pIB;

	DWORD m_Fvf;
	unsigned int m_Stride;
	unsigned int m_VertexCount;
	unsigned int m_IndexCount;

	CRS2D3D8MeshResource(const CRS2D3D8MeshResource &);
	CRS2D3D8MeshResource &operator=(const CRS2D3D8MeshResource &);

public:
	CRS2D3D8MeshResource();
	~CRS2D3D8MeshResource();

	/*
	 *	Upload RS2-owned geometry.
	 *
	 *	returns	: false if the layout cannot be expressed as an FVF or a
	 *			  buffer could not be created.  Nothing partial is kept.
	 */
	bool Create(const CRS2MeshData &data);
	void Free();

	bool IsValid() const{ return m_pVB!=0 && m_pIB!=0; }

	//	Draw one run of triangles.  Equivalent to the old DrawSubset() for the
	//	faces of a single material.
	void DrawRange(unsigned int firstIndex, unsigned int primitiveCount) const;

	unsigned int GetVertexBytes() const{ return m_VertexCount*m_Stride; }
	unsigned int GetIndexBytes() const{ return m_IndexCount*sizeof(WORD); }
};

//	Live-resource counters, for validation rather than gameplay.
unsigned int RS2D3D8_GetLiveMeshResourceCount();
unsigned int RS2D3D8_GetMeshVertexBytes();
unsigned int RS2D3D8_GetMeshIndexBytes();

#endif	//	RS2D3D8MESHRESOURCE_H_INCLUDED
