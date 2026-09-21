//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	See RS2D3D12Draw.h.

#include "stdafx.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Backend.h"

////////////////////////////////////////////////////////////////////////////////
//	State the engine has set
////////////////////////////////////////////////////////////////////////////////

//	The engine's matrices, as last submitted.  Kept here rather than read back
//	from anywhere: the engine is authoritative and the backend only remembers.
static float s_World[16];
static float s_View[16];
static float s_Projection[16];

//	Everything the pipeline key needs.  Direct3D 12 has no mutable
//	fixed-function state, so the engine's settings are collected here and turned
//	into a pipeline state when a draw happens.
static bool s_DepthTest = true;
static bool s_DepthWrite = true;
static RS2CompareFunc s_DepthFunc = RS2_COMPARE_LESS_EQUAL;
static RS2CullMode s_CullMode = RS2_CULL_COUNTER_CLOCKWISE;
static RS2BlendMode s_BlendMode = RS2_BLEND_ALPHA;

static unsigned int s_DrawCount = 0;
static unsigned int s_RefusedCount = 0;

static void RS2D3D12_Identity(float *m){
	int i;

	for(i = 0; i<16; i++) m[i] = 0.0f;
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/*
 *	out = a * b, row-major, row-vector convention.
 *
 *	Written out rather than taken from D3DX so that the Direct3D 12 backend
 *	does not depend on the Direct3D 8 era maths library, and so the
 *	multiplication order is visible: the engine composes world, then view, then
 *	projection, and the shader does mul(vector, matrix) to match.
 */
static void RS2D3D12_Multiply(float *out, const float *a, const float *b){
	float r[16];
	int i, j;

	for(i = 0; i<4; i++){
		for(j = 0; j<4; j++){
			r[i*4+j] = a[i*4+0]*b[0*4+j]
				+ a[i*4+1]*b[1*4+j]
				+ a[i*4+2]*b[2*4+j]
				+ a[i*4+3]*b[3*4+j];
		}
	}
	for(i = 0; i<16; i++) out[i] = r[i];
}

static void RS2D3D12_Store(float *dst, const float *src){
	int i;

	if(!src){
		RS2D3D12_Identity(dst);
		return;
	}
	for(i = 0; i<16; i++) dst[i] = src[i];
}

void RS2D3D12_SetWorldTransform(const float *matrix){ RS2D3D12_Store(s_World, matrix); }
void RS2D3D12_SetViewTransform(const float *matrix){ RS2D3D12_Store(s_View, matrix); }
void RS2D3D12_SetProjectionTransform(const float *matrix){ RS2D3D12_Store(s_Projection, matrix); }

void RS2D3D12_SetDepthTest(bool enable){ s_DepthTest = enable; }
void RS2D3D12_SetDepthWrite(bool enable){ s_DepthWrite = enable; }
void RS2D3D12_SetDepthFunc(RS2CompareFunc func){ s_DepthFunc = func; }
void RS2D3D12_SetCullMode(RS2CullMode mode){ s_CullMode = mode; }
void RS2D3D12_SetBlend(RS2BlendMode mode){ s_BlendMode = mode; }

void RS2D3D12_ApplyInitialRenderState(){
	//	The same values RS2D3D8_ApplyInitialRenderState leaves behind, so the
	//	engine starts from the same place whichever backend is running.
	s_DepthTest = true;
	s_DepthWrite = true;
	s_DepthFunc = RS2_COMPARE_LESS_EQUAL;
	s_CullMode = RS2_CULL_COUNTER_CLOCKWISE;
	s_BlendMode = RS2_BLEND_ALPHA;

	RS2D3D12_Identity(s_World);
	RS2D3D12_Identity(s_View);
	RS2D3D12_Identity(s_Projection);
}

unsigned int RS2D3D12_GetDrawCount(){ return s_DrawCount; }
unsigned int RS2D3D12_GetRefusedDrawCount(){ return s_RefusedCount; }

////////////////////////////////////////////////////////////////////////////////
//	Topology
////////////////////////////////////////////////////////////////////////////////

/*
 *	The Direct3D 12 topology for an RS2 primitive type.
 *
 *	Triangle fans are missing on purpose.  Direct3D 12 has no fan topology at
 *	all - it was removed, not renamed - so a cast to the nearest enum would
 *	draw a strip and be wrong in a way that looks like a modelling error.  Fans
 *	are expanded into a list before they get here.
 */
static bool RS2D3D12_Topology(
	RS2PrimitiveType primitive,			//	what the caller asked for
	D3D12_PRIMITIVE_TOPOLOGY *topology,		//	the topology to set
	D3D12_PRIMITIVE_TOPOLOGY_TYPE *type		//	the class the pipeline needs
){
	switch(primitive){
	case RS2_PRIMITIVE_POINT_LIST:
		*topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		*type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		return true;

	case RS2_PRIMITIVE_LINE_LIST:
		*topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		*type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		return true;

	case RS2_PRIMITIVE_LINE_STRIP:
		*topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		*type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		return true;

	case RS2_PRIMITIVE_TRIANGLE_LIST:
		*topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		*type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		return true;

	case RS2_PRIMITIVE_TRIANGLE_STRIP:
		*topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		*type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		return true;

	case RS2_PRIMITIVE_TRIANGLE_FAN:
	default:
		return false;
	}
}

/*
 *	Expand a triangle fan into a triangle list, in place in scratch memory.
 *
 *	0 1 2 3 4 becomes 0 1 2, 0 2 3, 0 3 4 - the same triangles in the same
 *	winding, which is what keeps culling and the depth order unchanged.
 *
 *	returns	: vertices written, or 0 if it would not fit
 */
static unsigned int RS2D3D12_ExpandFan(
	CRS2D3D12Upload *upload,	//	where to put the result
	const void *vertices,		//	fan vertices
	unsigned int vertexCount,	//	how many
	unsigned int stride,		//	vertex size
	D3D12_GPU_VIRTUAL_ADDRESS *gpu	//	address of the expanded list
){
	if(vertexCount<3) return 0;

	const unsigned int triangles = vertexCount-2;
	const unsigned int written = triangles*3;
	unsigned char *out = 0;

	if(!upload->Allocate(written*stride, stride, (void **)&out, gpu)) return 0;

	const unsigned char *in = (const unsigned char *)vertices;
	unsigned int i;

	for(i = 0; i<triangles; i++){
		memcpy(out+(i*3+0)*stride, in, stride);
		memcpy(out+(i*3+1)*stride, in+(i+1)*stride, stride);
		memcpy(out+(i*3+2)*stride, in+(i+2)*stride, stride);
	}
	return written;
}

////////////////////////////////////////////////////////////////////////////////
//	Submission
////////////////////////////////////////////////////////////////////////////////

/*
 *	Say once why draws are being refused.
 *
 *	A renderer that draws nothing and says nothing is the failure this project
 *	has already shipped once.
 */
static void RS2D3D12_Refuse(const char *why){
	static const char *seen[16];
	static unsigned int count = 0;
	unsigned int i;

	s_RefusedCount++;

	for(i = 0; i<count; i++) if(seen[i]==why) return;
	if(count<16) seen[count++] = why;

	Debug("[RS2EX D3D12] draw refused: %s\n", why);
}

void RS2D3D12_DrawImmediate(
	const RS2MeshVertexLayout &layout,	//	vertex layout
	RS2PrimitiveType primitive,		//	what to draw
	const void *vertices,			//	vertex data
	unsigned int vertexCount		//	vertices
){
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();

	if(!backend || !backend->IsRecording()){
		RS2D3D12_Refuse("no frame is being recorded");
		return;
	}

	//	The same arithmetic the Direct3D 8 backend does, so a count that would
	//	not form whole primitives is refused here too rather than truncated.
	if(!RS2PrimitiveCount(primitive, vertexCount) || !vertices || !layout.stride){
		RS2D3D12_Refuse("the vertex count does not form whole primitives");
		return;
	}

	RS2D3D12PipelineKey key;

	ZeroMemory(&key, sizeof(key));
	if(!RS2D3D12_DescribeLayout(layout, &key)){
		RS2D3D12_Refuse("the vertex layout cannot be expressed");
		return;
	}

	CRS2D3D12Upload *upload = backend->GetUpload();
	D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0;
	unsigned int drawCount = vertexCount;

	D3D12_PRIMITIVE_TOPOLOGY topology;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType;

	if(primitive==RS2_PRIMITIVE_TRIANGLE_FAN){
		//	Expanded rather than approximated: see RS2D3D12_ExpandFan.
		drawCount = RS2D3D12_ExpandFan(
			upload, vertices, vertexCount, layout.stride, &vertexAddress);

		if(!drawCount){
			RS2D3D12_Refuse("a triangle fan would not fit in this frame");
			return;
		}
		topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}else{
		if(!RS2D3D12_Topology(primitive, &topology, &topologyType)){
			RS2D3D12_Refuse("the primitive type has no Direct3D 12 topology");
			return;
		}
		if(!upload->Write(vertices, vertexCount*layout.stride,
				layout.stride, &vertexAddress)){
			RS2D3D12_Refuse("the vertices would not fit in this frame");
			return;
		}
	}

	key.topology = (unsigned char)topologyType;
	key.depthTest = s_DepthTest ? 1 : 0;
	key.depthWrite = s_DepthWrite ? 1 : 0;
	key.depthFunc = (unsigned char)s_DepthFunc;
	key.cullMode = (unsigned char)s_CullMode;
	key.blendMode = (unsigned char)s_BlendMode;

	ID3D12PipelineState *state = backend->GetPipeline()->Get(key);

	if(!state){
		RS2D3D12_Refuse("no pipeline state for this combination");
		return;
	}

	//	Constants.  World, view and projection are combined here because the
	//	shader wants one matrix and the engine sets three, and because doing it
	//	on the GPU would mean three matrices in the constant buffer for no
	//	reason.
	RS2D3D12Constants constants;
	float worldView[16];

	RS2D3D12_Multiply(worldView, s_World, s_View);
	RS2D3D12_Multiply(constants.worldViewProj, worldView, s_Projection);

	unsigned int width = 0, height = 0;

	backend->GetViewportSize(&width, &height);
	constants.viewport[0] = (float)width;
	constants.viewport[1] = (float)height;
	constants.viewport[2] = width ? 1.0f/(float)width : 0.0f;
	constants.viewport[3] = height ? 1.0f/(float)height : 0.0f;

	D3D12_GPU_VIRTUAL_ADDRESS constantAddress = 0;

	//	Constant buffers must start on a 256-byte boundary.
	if(!upload->Write(&constants, sizeof(constants), 256, &constantAddress)){
		RS2D3D12_Refuse("the constants would not fit in this frame");
		return;
	}

	ID3D12GraphicsCommandList *list = backend->GetCommandList();

	D3D12_VERTEX_BUFFER_VIEW view;

	view.BufferLocation = vertexAddress;
	view.SizeInBytes = drawCount*layout.stride;
	view.StrideInBytes = layout.stride;

	list->SetGraphicsRootSignature(backend->GetPipeline()->GetRootSignature());
	list->SetPipelineState(state);
	list->SetGraphicsRootConstantBufferView(0, constantAddress);
	list->IASetPrimitiveTopology(topology);
	list->IASetVertexBuffers(0, 1, &view);
	list->DrawInstanced(drawCount, 1, 0, 0);

	s_DrawCount++;
}
