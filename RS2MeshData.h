//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	RS2-owned CPU mesh geometry.
//
//	RailSim II 2.15 let one ID3DXMesh be everything at once: the import result, the
//	CPU geometry that picking and shadow generation read, the GPU draw resource and
//	the owner of the face-to-material mapping.  A renderer that is not Direct3D 8
//	cannot become authoritative while that is true.
//
//	This class owns the geometry instead.  It holds no Direct3D type, needs no
//	device, and survives independently of whatever backend happens to be drawing.
//
//	Vertex bytes are kept exactly as imported rather than converted to one canonical
//	struct.  The measured bundled content already uses four different vertex layouts,
//	one of which has no normal at all, and synthesising attributes that were absent
//	would change fixed-function lighting.  The layout below describes those bytes
//	instead of reshaping them.

#ifndef RS2MESHDATA_H_INCLUDED
#define RS2MESHDATA_H_INCLUDED

/*
 *	Where one texture coordinate set lives inside a vertex.
 */
struct RS2TexCoordLayout
{
	int offset;		//	bytes from the start of the vertex
	unsigned int components;	//	1..4 floats
};

/*
 *	API-neutral description of a vertex.
 *
 *	Offsets are byte offsets from the start of the vertex, or NOT_PRESENT.
 *	No D3DFVF_*, no D3DVERTEXELEMENT, no Direct3D pointer: translating this into
 *	whatever a backend wants is the backend's job.
 */
struct RS2MeshVertexLayout
{
	enum { NOT_PRESENT = -1, MAX_TEXCOORD = 8 };

	unsigned int stride;

	int positionOffset;	//	always present in a valid layout
	int normalOffset;
	int diffuseOffset;	//	packed 32-bit colour

	unsigned int texCoordCount;
	RS2TexCoordLayout texCoord[MAX_TEXCOORD];

	RS2MeshVertexLayout(){ Clear(); }
	void Clear();

	bool HasPosition() const{ return positionOffset!=NOT_PRESENT; }
	bool HasNormal() const{ return normalOffset!=NOT_PRESENT; }
	bool HasDiffuse() const{ return diffuseOffset!=NOT_PRESENT; }
};

/*
 *	A run of indices belonging to one material.
 *
 *	A material can own more than one range.  D3DXMESHOPT_ATTRSORT normally makes
 *	each one contiguous, but depending on that accidentally would make the
 *	representation wrong the moment the importer changes.
 */
struct RS2MeshSubset
{
	unsigned int materialId;
	unsigned int firstIndex;
	unsigned int primitiveCount;	//	triangles
};

/*
 *	RS2-owned CPU mesh.
 *
 *	Immutable once built.  Per-pass material scratch state stays in CMesh: this is
 *	shared between every instance of a model, so nothing here may depend on who is
 *	drawing it.
 */
class CRS2MeshData
{
private:
	unsigned char *m_VertexBytes;
	unsigned int m_VertexCount;
	RS2MeshVertexLayout m_Layout;

	unsigned int *m_Indices;	//	3 per face
	unsigned int m_FaceCount;

	unsigned int *m_FaceMaterialIds;	//	exactly one per face

	RS2MeshSubset *m_Subsets;
	unsigned int m_SubsetCount;

	unsigned int m_MaterialCount;

	//	Not copyable: it owns raw buffers and is shared by pointer.
	CRS2MeshData(const CRS2MeshData &);
	CRS2MeshData &operator=(const CRS2MeshData &);

public:
	CRS2MeshData();
	~CRS2MeshData();

	void Free();
	bool IsValid() const{ return m_VertexBytes!=0 && m_Indices!=0 && m_FaceCount!=0; }

	/*
	 *	Take ownership of imported geometry.
	 *
	 *	The arrays must come from new[]; this object frees them.  On failure
	 *	nothing is retained and the object stays empty, so a half-built mesh is
	 *	never observable.
	 */
	bool Build(
		unsigned char *vertexBytes,	//	adopted
		unsigned int vertexCount,
		const RS2MeshVertexLayout &layout,
		unsigned int *indices,		//	adopted, 3 per face
		unsigned int faceCount,
		unsigned int *faceMaterialIds,	//	adopted, 1 per face
		unsigned int materialCount);

	/*
	 *	Take over another instance's buffers.
	 *
	 *	An explicit move, because this class is deliberately not copyable: the
	 *	importer builds geometry into a temporary and the mesh adopts it whole,
	 *	without duplicating megabytes of vertices.  src is left empty.
	 */
	void AdoptFrom(CRS2MeshData &src);

	//	--- geometry ---
	unsigned int GetVertexCount() const{ return m_VertexCount; }
	unsigned int GetFaceCount() const{ return m_FaceCount; }
	unsigned int GetIndexCount() const{ return m_FaceCount*3; }
	unsigned int GetMaterialCount() const{ return m_MaterialCount; }

	const RS2MeshVertexLayout &GetLayout() const{ return m_Layout; }

	//	Raw bytes, for upload to a backend.  Read-only on purpose.
	const void *GetVertexBytes() const{ return m_VertexBytes; }
	unsigned int GetVertexBytesSize() const{ return m_VertexCount*m_Layout.stride; }
	const unsigned int *GetIndices() const{ return m_Indices; }

	/*
	 *	Index of one corner of one face.
	 *
	 *	face		: 0..GetFaceCount()-1
	 *	corner	: 0..2
	 */
	unsigned int GetIndex(unsigned int face, unsigned int corner) const{
		return m_Indices[face*3+corner];
	}

	//	Material this face belongs to.  Drives both subset drawing and the
	//	NoCastShadow test, so it is not an optimisation hint.
	unsigned int GetFaceMaterialId(unsigned int face) const{
		return m_FaceMaterialIds[face];
	}

	/*
	 *	Position of one vertex.
	 *
	 *	returns	: false if the index or the layout does not allow it
	 */
	bool GetPosition(unsigned int vertexIndex, VEC3 *out) const;

	//	Address of a vertex, for callers walking geometry in a tight loop.
	const void *GetVertex(unsigned int vertexIndex) const{
		return m_VertexBytes+vertexIndex*m_Layout.stride;
	}

	//	--- subsets ---
	unsigned int GetSubsetCount() const{ return m_SubsetCount; }
	const RS2MeshSubset &GetSubset(unsigned int i) const{ return m_Subsets[i]; }

	//	--- diagnostics ---
	unsigned int GetCpuByteSize() const;
};

#endif	//	RS2MESHDATA_H_INCLUDED
