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

/*
 *	Finish a pipeline key, bind the state and the constants.
 *
 *	The constants are the same for every draw in a pass, but they are written
 *	per draw because the engine can change a transform between two of them and
 *	the scratch allocator makes a copy cheap.  Deduplicating that would mean
 *	tracking what changed, which is a cache to get wrong for a memcpy.
 *
 *	returns	: false having already said why not
 */
static bool RS2D3D12_BindPipeline(
	CRS2D3D12Backend *backend,			//	backend to record into
	RS2D3D12PipelineKey *key,			//	layout half filled in
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType,	//	class for the state
	D3D12_PRIMITIVE_TOPOLOGY topology		//	topology for the assembler
){
	key->topology = (unsigned char)topologyType;
	key->depthTest = s_DepthTest ? 1 : 0;
	key->depthWrite = s_DepthWrite ? 1 : 0;
	key->depthFunc = (unsigned char)s_DepthFunc;
	key->cullMode = (unsigned char)s_CullMode;
	key->blendMode = (unsigned char)s_BlendMode;
	RS2D3D12SrvSlot textureSlot;
	RS2TextureFilter filter = RS2_FILTER_POINT;
	// A bound texture without TEXCOORD0 remains an untextured draw. The PSO
	// key records only the shader variant, never the texture or sampler.
	key->textured = key->texCoordCount && RS2D3D12_GetBoundTexture(
		backend, &textureSlot, &filter) ? 1 : 0;

	ID3D12PipelineState *state = backend->GetPipeline()->Get(*key);

	if(!state){
		RS2D3D12_Refuse("no pipeline state for this combination");
		return false;
	}

	//	World, view and projection are combined here because the shader wants
	//	one matrix and the engine sets three.
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
	if(!backend->GetUpload()->Write(&constants, sizeof(constants), 256, &constantAddress)){
		RS2D3D12_Refuse("the constants would not fit in this frame");
		return false;
	}

	ID3D12GraphicsCommandList *list = backend->GetCommandList();

	list->SetGraphicsRootSignature(backend->GetPipeline()->GetRootSignature());
	list->SetPipelineState(state);
	list->SetGraphicsRootConstantBufferView(0, constantAddress);
	if(key->textured){
		D3D12_GPU_DESCRIPTOR_HANDLE srv, sampler;
		if(!backend->GetDescriptors()->GetSrvHandles(textureSlot, 0, &srv)
				|| !backend->GetDescriptors()->GetSamplerHandle(filter, &sampler)){
			RS2D3D12_Refuse("Stage 0 descriptors are unavailable");
			return false;
		}
		backend->GetDescriptors()->BindHeaps(list);
		list->SetGraphicsRootDescriptorTable(1, srv);
		list->SetGraphicsRootDescriptorTable(2, sampler);
	}
	list->IASetPrimitiveTopology(topology);
	return true;
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

	if(!RS2D3D12_BindPipeline(backend, &key, topologyType, topology)) return;

	D3D12_VERTEX_BUFFER_VIEW view;

	view.BufferLocation = vertexAddress;
	view.SizeInBytes = drawCount*layout.stride;
	view.StrideInBytes = layout.stride;

	backend->GetCommandList()->IASetVertexBuffers(0, 1, &view);
	backend->GetCommandList()->DrawInstanced(drawCount, 1, 0, 0);

	s_DrawCount++;
}

////////////////////////////////////////////////////////////////////////////////
//	Geometry resources
////////////////////////////////////////////////////////////////////////////////

/*
 *	What this backend keeps behind a geometry resource.
 *
 *	The layout key is stored because a buffered or indexed draw is given only
 *	the resource.  Direct3D 8 kept an FVF for the same reason; Direct3D 12
 *	needs the whole input layout, and the key is exactly that.
 */
struct RS2D3D12Geometry
{
	ID3D12Resource *vertices;
	ID3D12Resource *indices;

	D3D12_VERTEX_BUFFER_VIEW vertexView;
	D3D12_INDEX_BUFFER_VIEW indexView;

	RS2D3D12PipelineKey layout;
};

static RS2D3D12Geometry *RS2D3D12_Payload(const CRS2GeometryResource *geometry){
	return geometry ? (RS2D3D12Geometry *)geometry->payload : 0;
}

/*
 *	A buffer on the upload heap, filled once.
 *
 *	v0.1.1 keeps static geometry in upload memory rather than copying it to a
 *	default heap through a staging buffer.  That is slower for the GPU to read
 *	and it is written down as temporary, because the question this release
 *	answers is whether RS2 geometry reaches the Direct3D 12 pipeline at all,
 *	not how fast it gets there.  A staging copy needs a copy queue or a
 *	one-shot command list and a fence wait per resource, which is a second
 *	lifetime problem to get right for no benefit yet.
 */
static ID3D12Resource *RS2D3D12_CreateFilledBuffer(
	ID3D12Device *device,	//	device to allocate from
	const void *data,	//	what to put in it
	unsigned int bytes	//	how much
){
	D3D12_HEAP_PROPERTIES props;

	ZeroMemory(&props, sizeof(props));
	props.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC desc;

	ZeroMemory(&desc, sizeof(desc));
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = bytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *buffer = 0;

	if(FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&buffer))))
		return 0;

	void *cpu = 0;
	D3D12_RANGE none;

	none.Begin = 0;
	none.End = 0;

	if(FAILED(buffer->Map(0, &none, &cpu))){
		buffer->Release();
		return 0;
	}

	memcpy(cpu, data, bytes);
	buffer->Unmap(0, NULL);
	return buffer;
}

void RS2D3D12_DestroyGeometry(CRS2GeometryResource *geometry){
	RS2D3D12Geometry *payload = RS2D3D12_Payload(geometry);

	if(!payload) return;

	RELEASE(payload->indices);
	RELEASE(payload->vertices);

	delete payload;
	geometry->payload = 0;
}

static bool RS2D3D12_BuildVertices(
	CRS2GeometryResource *geometry,		//	resource to fill
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices			//	vertex data
){
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();

	if(!backend) return false;

	RS2D3D12PipelineKey key;

	ZeroMemory(&key, sizeof(key));
	if(!RS2D3D12_DescribeLayout(layout, &key)){
		Debug("[RS2EX D3D12] geometry layout cannot be expressed\n");
		return false;
	}

	RS2D3D12Geometry *payload = new RS2D3D12Geometry;

	ZeroMemory(payload, sizeof(*payload));
	payload->layout = key;
	geometry->payload = payload;

	const unsigned int bytes = geometry->stride*geometry->vertexCount;

	payload->vertices = RS2D3D12_CreateFilledBuffer(
		backend->GetDevice(), vertices, bytes);

	if(!payload->vertices){
		Debug("[RS2EX D3D12] %u vertex bytes could not be allocated\n", bytes);
		return false;
	}

	payload->vertexView.BufferLocation = payload->vertices->GetGPUVirtualAddress();
	payload->vertexView.SizeInBytes = bytes;
	payload->vertexView.StrideInBytes = geometry->stride;
	return true;
}

bool RS2D3D12_CreateGeometry(
	CRS2GeometryResource *geometry,		//	resource to fill
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices			//	vertex data
){
	return RS2D3D12_BuildVertices(geometry, layout, vertices);
}

bool RS2D3D12_CreateIndexedGeometry(
	CRS2GeometryResource *geometry,		//	resource to fill
	const RS2MeshVertexLayout &layout,	//	vertex layout
	const void *vertices,			//	vertex data
	const unsigned int *indices		//	index data
){
	//	Sixteen-bit indices, the same contract CMesh has always had.  A mesh
	//	that needs more is refused rather than silently wrapping, and an index
	//	outside the vertices is refused rather than read.
	if(geometry->vertexCount>0xffff){
		Debug("[RS2EX D3D12] %u vertices exceeds the 16-bit index range\n",
			geometry->vertexCount);
		return false;
	}

	if(!RS2D3D12_BuildVertices(geometry, layout, vertices)) return false;

	RS2D3D12Geometry *payload = RS2D3D12_Payload(geometry);
	WORD *narrowed = new WORD[geometry->indexCount];
	unsigned int i;

	for(i = 0; i<geometry->indexCount; i++){
		if(indices[i]>=geometry->vertexCount){
			Debug("[RS2EX D3D12] index %u is outside %u vertices\n",
				indices[i], geometry->vertexCount);
			delete [] narrowed;
			return false;
		}
		narrowed[i] = (WORD)indices[i];
	}

	const unsigned int bytes = geometry->indexCount*(unsigned int)sizeof(WORD);

	payload->indices = RS2D3D12_CreateFilledBuffer(
		RS2D3D12GetActiveBackend()->GetDevice(), narrowed, bytes);
	delete [] narrowed;

	if(!payload->indices){
		Debug("[RS2EX D3D12] %u index bytes could not be allocated\n", bytes);
		return false;
	}

	payload->indexView.BufferLocation = payload->indices->GetGPUVirtualAddress();
	payload->indexView.SizeInBytes = bytes;
	payload->indexView.Format = DXGI_FORMAT_R16_UINT;
	return true;
}

/*
 *	The part of a buffered or indexed draw that is the same either way.
 *
 *	returns	: the payload, or 0 having already said why not
 */
static RS2D3D12Geometry *RS2D3D12_PrepareBufferedDraw(
	const CRS2GeometryResource *geometry,	//	resource being drawn
	RS2PrimitiveType primitive,		//	what to draw
	CRS2D3D12Backend **backendOut		//	the backend, for the caller
){
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();

	if(!backend || !backend->IsRecording()){
		RS2D3D12_Refuse("no frame is being recorded");
		return 0;
	}

	RS2D3D12Geometry *payload = RS2D3D12_Payload(geometry);

	if(!payload || !payload->vertices){
		RS2D3D12_Refuse("the geometry has no vertices");
		return 0;
	}

	//	Fans out of a buffer would have to be expanded with an index list built
	//	per draw.  Nothing in the engine draws a buffered fan today, so this
	//	says so rather than guessing at an implementation nobody needs yet.
	if(primitive==RS2_PRIMITIVE_TRIANGLE_FAN){
		RS2D3D12_Refuse("a buffered triangle fan is not implemented");
		return 0;
	}

	*backendOut = backend;
	return payload;
}

void RS2D3D12_DrawBuffered(
	const CRS2GeometryResource *geometry,	//	resource to draw from
	RS2PrimitiveType primitive,		//	what to draw
	unsigned int firstVertex,		//	first vertex
	unsigned int vertexCount		//	vertices
){
	CRS2D3D12Backend *backend = 0;
	RS2D3D12Geometry *payload =
		RS2D3D12_PrepareBufferedDraw(geometry, primitive, &backend);

	if(!payload) return;
	if(firstVertex+vertexCount>geometry->vertexCount){
		RS2D3D12_Refuse("the vertex range is outside the buffer");
		return;
	}
	if(!RS2PrimitiveCount(primitive, vertexCount)){
		RS2D3D12_Refuse("the vertex count does not form whole primitives");
		return;
	}

	D3D12_PRIMITIVE_TOPOLOGY topology;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType;

	if(!RS2D3D12_Topology(primitive, &topology, &topologyType)){
		RS2D3D12_Refuse("the primitive type has no Direct3D 12 topology");
		return;
	}

	RS2D3D12PipelineKey key = payload->layout;

	if(!RS2D3D12_BindPipeline(backend, &key, topologyType, topology)) return;

	backend->GetCommandList()->IASetVertexBuffers(0, 1, &payload->vertexView);
	backend->GetCommandList()->DrawInstanced(vertexCount, 1, firstVertex, 0);

	s_DrawCount++;
}

void RS2D3D12_DrawIndexed(
	const CRS2GeometryResource *geometry,	//	resource to draw from
	RS2PrimitiveType primitive,		//	what to draw
	unsigned int firstIndex,		//	first index
	unsigned int indexCount			//	indices
){
	CRS2D3D12Backend *backend = 0;
	RS2D3D12Geometry *payload =
		RS2D3D12_PrepareBufferedDraw(geometry, primitive, &backend);

	if(!payload) return;
	if(!payload->indices){
		RS2D3D12_Refuse("the geometry has no indices");
		return;
	}
	if(firstIndex+indexCount>geometry->indexCount){
		RS2D3D12_Refuse("the index range is outside the buffer");
		return;
	}
	if(!RS2PrimitiveCount(primitive, indexCount)){
		RS2D3D12_Refuse("the index count does not form whole primitives");
		return;
	}

	D3D12_PRIMITIVE_TOPOLOGY topology;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType;

	if(!RS2D3D12_Topology(primitive, &topology, &topologyType)){
		RS2D3D12_Refuse("the primitive type has no Direct3D 12 topology");
		return;
	}

	RS2D3D12PipelineKey key = payload->layout;

	if(!RS2D3D12_BindPipeline(backend, &key, topologyType, topology)) return;

	backend->GetCommandList()->IASetVertexBuffers(0, 1, &payload->vertexView);
	backend->GetCommandList()->IASetIndexBuffer(&payload->indexView);

	//	The whole vertex buffer stays addressable: a subset draws its own index
	//	range but shares vertices with the others, as it does on Direct3D 8.
	backend->GetCommandList()->DrawIndexedInstanced(indexCount, 1, firstIndex, 0, 0);

	s_DrawCount++;
}
