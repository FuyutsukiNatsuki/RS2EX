//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23.
//
//	The Direct3D 12 pipeline: root signature, shaders, and pipeline states.
//
//	Direct3D 8 had mutable fixed-function state - set a blend mode, set a cull
//	mode, draw, change one of them, draw again.  Direct3D 12 has none of that.
//	Everything that was a switch is baked into an immutable pipeline state
//	object, so the engine's state has to be gathered up and turned into one.
//
//	States are built when they are first asked for and kept.  Building every
//	combination in advance would be thousands of objects for a scene that uses
//	a handful, and most of the key space is unreachable anyway.
//
//	v0.1.2 adds Stage 0 texture sampling. Untextured draws still use the
//	interpolated vertex colour; lighting remains outside this pipeline.

#ifndef RS2D3D12PIPELINE_H_INCLUDED
#define RS2D3D12PIPELINE_H_INCLUDED

#include "RS2D3D12.h"
#include "RS2MeshData.h"
#include "RS2Draw.h"
#include "RS2RenderState.h"

/*
 *	What the shaders are given.
 *
 *	One constant buffer, one draw.  The matrix is row-major and the shader uses
 *	mul(vector, matrix), which is the Direct3D 8 row-vector convention the
 *	engine's matrices are built for - see RS2D3D12Pipeline.cpp for why that is
 *	stated rather than left to HLSL defaults.
 */
struct RS2D3D12Constants
{
	float worldViewProj[16];

	//	x, y: viewport size.  z, w: its reciprocal.  Only the
	//	already-transformed path uses these, to turn screen coordinates into
	//	clip space the way the fixed-function pipeline used to.
	float viewport[4];
	// x: enabled, y: 8-bit reference, z: RS2CompareFunc, w: reserved.
	// Kept per draw so alpha-state changes never create another PSO.
	float alphaTest[4];

	//	Material and lighting, v0.1.3.  All of it is per-draw data: nothing
	//	here is in the pipeline key, so a material or light change never
	//	creates a pipeline state.  Lighting is computed in view space, as the
	//	fixed-function pipeline did, so this carries world * view as well.
	float worldView[16];

	//	xyz: the direction toward the light, view space, normalised.
	//	w: 1 while a light has been submitted, as Direct3D 8's slot 0 was
	//	disabled until the first SetLight.
	float lightToward[4];
	float lightColour[4];		//	used for diffuse and specular alike
	float ambient[4];		//	global ambient, from the packed colour

	float materialDiffuse[4];
	float materialAmbient[4];
	float materialSpecular[4];
	float materialEmissive[4];

	//	x: lighting on, y: specular on, z: diffuse from the vertex colour,
	//	w: ambient from the vertex colour.
	float lighting[4];
	float power[4];			//	x: material Power
};

/*
 *	Everything that decides which pipeline state a draw needs.
 *
 *	Compared as bytes, so it is packed and fully initialised - a padding byte
 *	left over from the stack would make two identical keys miss each other and
 *	build the same state twice.
 */
struct RS2D3D12PipelineKey
{
	//	The vertex layout, reduced to what the input layout needs.  Offsets are
	//	part of the key because they are part of the input layout: two layouts
	//	with the same fields in different places are different pipelines.
	unsigned short stride;
	unsigned short positionOffset;
	unsigned short normalOffset;
	unsigned short diffuseOffset;
	unsigned short texCoordOffset[8];

	unsigned char hasNormal;
	unsigned char hasDiffuse;
	unsigned char texCoordCount;
	unsigned char texCoordComponents[8];

	unsigned char positionSemantic;
	unsigned char topology;			//	D3D12_PRIMITIVE_TOPOLOGY_TYPE

	unsigned char depthTest;
	unsigned char depthWrite;
	unsigned char depthFunc;		//	RS2CompareFunc
	unsigned char cullMode;			//	RS2CullMode
	unsigned char blendMode;		//	RS2BlendMode

	unsigned char textured;		// Stage 0 shader variant, never texture identity
	unsigned char pad[1];
};

class CRS2D3D12Pipeline
{
private:
	ID3D12Device *m_Device;
	ID3D12RootSignature *m_RootSignature;

	//	[position semantic][vertex has a colour][textured][vertex has a normal].
	//	A vertex shader has to declare exactly the inputs the layout supplies -
	//	an input the layout does not provide is not ignored, it fails pipeline
	//	creation - so each of these needs its own variant.  The normal only
	//	exists for pipeline-transformed vertices; screen-space vertices never
	//	carry one, so those four slots stay empty.  Whether a layout has a
	//	normal was already part of the key, so this adds no pipeline states.
	ID3DBlob *m_Vertex[2][2][2][2];
	ID3DBlob *m_Pixel[2];

	//	Small and linear on purpose: a scene reaches a handful of states, and a
	//	linear scan over a handful is faster than anything with a hash in it.
	enum { RS2D3D12_MAX_PIPELINES = 64 };

	RS2D3D12PipelineKey m_Key[RS2D3D12_MAX_PIPELINES];
	ID3D12PipelineState *m_State[RS2D3D12_MAX_PIPELINES];
	unsigned int m_Count;

	bool m_Full;

	bool CompileShaders();
	ID3D12PipelineState *Build(const RS2D3D12PipelineKey &key);

public:
	CRS2D3D12Pipeline();
	~CRS2D3D12Pipeline();

	bool Create(ID3D12Device *device);
	void Destroy();

	ID3D12RootSignature *GetRootSignature() const{ return m_RootSignature; }

	/*
	 *	The pipeline state for this key, building it if it is new.
	 *
	 *	Returns 0 when the state cannot be built, which a caller must treat as
	 *	"do not draw" rather than "draw with something else".
	 */
	ID3D12PipelineState *Get(const RS2D3D12PipelineKey &key);

	unsigned int GetStateCount() const{ return m_Count; }
};

/*
 *	Fill in the layout half of a key from an RS2 vertex layout.
 *
 *	returns	: false if the layout cannot be expressed
 */
bool RS2D3D12_DescribeLayout(
	const RS2MeshVertexLayout &layout,
	RS2D3D12PipelineKey *key);

#endif	//	RS2D3D12PIPELINE_H_INCLUDED
