//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//	Modified for RS2EX on 2026-09-23, 2026-09-24, 2026-09-25.
//
//	See RS2D3D12Draw.h.

#include "stdafx.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12Unsupported.h"

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
static bool s_AlphaTest = false;
static unsigned int s_AlphaRef = 0;
static RS2CompareFunc s_AlphaFunc = RS2_COMPARE_ALWAYS;

//	Stencil, v0.1.5.  The engine sets none of this before the shadow pass, so
//	it starts where Direct3D 8's device did - read from the device by
//	-shadowaudit: off, ALWAYS, reference 0, both masks all ones, every op
//	KEEP.  The shadow pass leaves its values behind for the next frame, as it
//	did on Direct3D 8; nothing here resets them in between.
static bool s_StencilTest = false;
static RS2CompareFunc s_StencilFunc = RS2_COMPARE_ALWAYS;
static unsigned int s_StencilRef = 0;
static unsigned int s_StencilReadMask = 0xffffffffu;
static unsigned int s_StencilWriteMask = 0xffffffffu;
static RS2StencilOp s_StencilFail = RS2_STENCIL_KEEP;
static RS2StencilOp s_StencilDepthFail = RS2_STENCIL_KEEP;
static RS2StencilOp s_StencilPass = RS2_STENCIL_KEEP;
static RS2ShadeMode s_ShadeMode = RS2_SHADE_GOURAUD;
static RS2D3D12StencilStats s_StencilStats;
static int s_LastStencilRef = -1;

//	Material and lighting.  The starting values are what Direct3D 8 began
//	with, read from the device by -lightingaudit rather than taken from
//	documentation: RS2D3D8_ApplyInitialRenderState sets lighting, specular and
//	ambient, and the rest - an all-zero material, diffuse from the vertex,
//	ambient from the material, no light - is the device's own default.
static RS2Material s_Material;
static bool s_Lighting = true;
static bool s_Specular = true;
static RS2PackedColor s_Ambient = 0xff808080;
static RS2ColorSource s_DiffuseSource = RS2_COLOR_FROM_VERTEX;
static RS2ColorSource s_AmbientSource = RS2_COLOR_FROM_MATERIAL;
static RS2DirectionalLight s_Light;
static bool s_LightEnabled = false;
static RS2D3D12LightingStats s_LightingStats;

//	Texture coordinate state for stages 0 and 1 - what Direct3D 8 kept as
//	D3DTSS_TEXTURETRANSFORMFLAGS, D3DTS_TEXTUREn and D3DTSS_TEXCOORDINDEX.
//	The matrix and the enable flag are separate, as they were: disabling a
//	transform keeps the matrix, and enabling it again uses the stored one.
enum RS2D3D12UVSource
{
	RS2D3D12_UV_TEXCOORD0,		//	PASSTHRU | 0
	RS2D3D12_UV_TEXCOORD1,		//	Direct3D 8's stage 1 default index
	RS2D3D12_UV_CAMERA_NORMAL	//	D3DTSS_TCI_CAMERASPACENORMAL
};

static bool s_UVTransform[2] = { false, false };
static float s_UVMatrix[2][16];
static RS2D3D12UVSource s_UVSource[2] = { RS2D3D12_UV_TEXCOORD0, RS2D3D12_UV_TEXCOORD1 };
static bool s_Combine1 = false;
static RS2D3D12StageStats s_StageStats;

//	The matrix RS2D3D8_SetEnvironmentMapping installs, value for value.  With
//	the camera-space normal as a three-component input its third row
//	multiplies nz - measured: u = 0.5 nx + 0.5 nz, v = -0.5 ny + 0.5 nz.
static const float RS2D3D12_ENV_MATRIX[16] = {
	0.5f,  0.0f, 0.0f, 0.0f,
	0.0f, -0.5f, 0.0f, 0.0f,
	0.5f,  0.5f, 1.0f, 0.0f,
	0.0f,  0.0f, 0.0f, 1.0f
};

static unsigned int s_DrawCount = 0;
static unsigned int s_TexturedDrawCount = 0;
static unsigned int s_DDSTexturedDrawCount = 0;

static void RS2D3D12_CountTextured(bool textured){
	if(!textured) return;
	s_TexturedDrawCount++;
	if(RS2D3D12_BoundTextureIsDDS()) s_DDSTexturedDrawCount++;
}
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
void RS2D3D12_SetAlphaTest(bool enable){ s_AlphaTest = enable; }
void RS2D3D12_SetAlphaRef(unsigned int ref){ s_AlphaRef = ref & 0xffu; }
void RS2D3D12_SetAlphaFunc(RS2CompareFunc func){
	if(func==RS2_COMPARE_ALWAYS || func==RS2_COMPARE_LESS_EQUAL
			|| func==RS2_COMPARE_GREATER) s_AlphaFunc = func;
	else Debug("[RS2EX D3D12] unsupported alpha comparison %d\n", (int)func);
}

void RS2D3D12_SetStencilTest(bool enable){
	s_StencilStats.calls[0]++;
	s_StencilTest = enable;
}

void RS2D3D12_SetStencilFunc(RS2CompareFunc func){
	s_StencilStats.calls[1]++;
	s_StencilFunc = func;
}

void RS2D3D12_SetStencilRef(unsigned int ref){
	s_StencilStats.calls[2]++;
	s_StencilRef = ref;
}

void RS2D3D12_SetStencilReadMask(unsigned int mask){
	s_StencilStats.calls[3]++;
	s_StencilReadMask = mask;
}

void RS2D3D12_SetStencilWriteMask(unsigned int mask){
	s_StencilStats.calls[4]++;
	s_StencilWriteMask = mask;
}

void RS2D3D12_SetStencilFailOp(RS2StencilOp op){
	s_StencilStats.calls[5]++;
	s_StencilFail = op;
}

void RS2D3D12_SetStencilDepthFailOp(RS2StencilOp op){
	s_StencilStats.calls[6]++;
	s_StencilDepthFail = op;
}

void RS2D3D12_SetStencilPassOp(RS2StencilOp op){
	s_StencilStats.calls[7]++;
	s_StencilPass = op;
}

/*
 *	Shade mode: recorded, not rendered.
 *
 *	The only FLAT in the program is CShadowVolume's, and every draw made under
 *	it is colour-preserving (Zero / One) - it writes stencil and no colour, so
 *	flat and Gouraud give the same picture (v0.1.5 audit: 3,006 of 3,006).  A
 *	flat-shading shader variant would be a renderer feature nothing can see.
 */
void RS2D3D12_SetShadeMode(RS2ShadeMode mode){
	s_StencilStats.shadeCalls++;
	if(mode==RS2_SHADE_FLAT) s_StencilStats.flatCalls++;
	s_ShadeMode = mode;
}

/*
 *	Fog: already off.  Direct3D 12 has no fog, and on Direct3D 8 nothing ever
 *	turned it on (FOGENABLE read as 0 at start-up and before every shadow
 *	pass), so the call is satisfied as it stands.
 */
void RS2D3D12_DisableFog(){
	s_StencilStats.fogCalls++;
}

const RS2D3D12StencilStats &RS2D3D12_GetStencilStats(){ return s_StencilStats; }

void RS2D3D12_ApplyInitialRenderState(){
	//	The same values RS2D3D8_ApplyInitialRenderState leaves behind, so the
	//	engine starts from the same place whichever backend is running.
	s_DepthTest = true;
	s_DepthWrite = true;
	s_DepthFunc = RS2_COMPARE_LESS_EQUAL;
	s_CullMode = RS2_CULL_COUNTER_CLOCKWISE;
	s_BlendMode = RS2_BLEND_ALPHA;
	s_AlphaTest = false;
	s_AlphaRef = 0;
	s_AlphaFunc = RS2_COMPARE_ALWAYS;

	//	The device defaults, as a Direct3D 8 device starts - see above.
	s_StencilTest = false;
	s_StencilFunc = RS2_COMPARE_ALWAYS;
	s_StencilRef = 0;
	s_StencilReadMask = 0xffffffffu;
	s_StencilWriteMask = 0xffffffffu;
	s_StencilFail = s_StencilDepthFail = s_StencilPass = RS2_STENCIL_KEEP;
	s_ShadeMode = RS2_SHADE_GOURAUD;

	ZeroMemory(&s_Material, sizeof(s_Material));
	s_Lighting = true;
	s_Specular = true;
	s_Ambient = 0xff808080;
	s_DiffuseSource = RS2_COLOR_FROM_VERTEX;
	s_AmbientSource = RS2_COLOR_FROM_MATERIAL;
	ZeroMemory(&s_Light, sizeof(s_Light));
	s_LightEnabled = false;

	//	Direct3D 8's own start: no transform, identity matrices, stage 1
	//	reading coordinate set 1, no secondary combine.  The engine sets all
	//	of these before stage 1 is used; nothing here invents a reset the
	//	legacy renderer did not do.
	s_UVTransform[0] = s_UVTransform[1] = false;
	RS2D3D12_Identity(s_UVMatrix[0]);
	RS2D3D12_Identity(s_UVMatrix[1]);
	s_UVSource[0] = RS2D3D12_UV_TEXCOORD0;
	s_UVSource[1] = RS2D3D12_UV_TEXCOORD1;
	s_Combine1 = false;

	RS2D3D12_Identity(s_World);
	RS2D3D12_Identity(s_View);
	RS2D3D12_Identity(s_Projection);
}

void RS2D3D12_SetMaterial(const RS2Material &material){
	s_Material = material;
	s_LightingStats.materialCalls++;
}

void RS2D3D12_SetLighting(bool enable){
	s_Lighting = enable;
	s_LightingStats.lightingCalls++;
}

void RS2D3D12_SetAmbientLight(RS2PackedColor color){
	s_Ambient = color;
	s_LightingStats.ambientCalls++;
}

void RS2D3D12_SetSpecular(bool enable){
	s_Specular = enable;
	s_LightingStats.specularCalls++;
}

void RS2D3D12_SetDiffuseColorSource(RS2ColorSource source){
	s_DiffuseSource = source;
	s_LightingStats.diffuseSourceCalls++;
}

void RS2D3D12_SetAmbientColorSource(RS2ColorSource source){
	s_AmbientSource = source;
	s_LightingStats.ambientSourceCalls++;
}

void RS2D3D12_SubmitDirectionalLight(const RS2DirectionalLight &light){
	s_Light = light;
	s_LightEnabled = true;
	s_LightingStats.lightCalls++;
}

const RS2D3D12LightingStats &RS2D3D12_GetLightingStats(){ return s_LightingStats; }

void RS2D3D12_SetSecondaryTextureCombine(unsigned int stage, bool enable){
	s_StageStats.combineCalls++;
	if(stage!=1){
		RS2D3D12Unsupported("RS2SetSecondaryTextureCombine(stage!=1)");
		return;
	}
	s_Combine1 = enable;
}

void RS2D3D12_SetUVTransform(unsigned int stage, bool enable){
	s_StageStats.uvTransformCalls++;
	if(stage>1){
		RS2D3D12Unsupported("RS2SetUVTransform(stage>1)");
		return;
	}
	s_UVTransform[stage] = enable;
}

void RS2D3D12_SetUVMatrix(unsigned int stage, const float *matrix){
	s_StageStats.uvMatrixCalls++;
	if(stage>1){
		RS2D3D12Unsupported("RS2SetUVMatrix(stage>1)");
		return;
	}
	if(!matrix) return;		//	Direct3D 8 ignored a null matrix too
	for(int i = 0; i<16; i++) s_UVMatrix[stage][i] = matrix[i];
}

/*
 *	The three things RS2D3D8_SetEnvironmentMapping does: the transform flag,
 *	the matrix (the environment matrix, or identity on the way out), and the
 *	coordinate source (camera-space normal, or passthrough of set 0 - not set
 *	1, which is what Direct3D 8 is left with after the first use too).
 */
void RS2D3D12_SetEnvironmentMapping(unsigned int stage, bool enable){
	s_StageStats.environmentCalls++;
	if(stage>1){
		RS2D3D12Unsupported("RS2SetEnvironmentMapping(stage>1)");
		return;
	}
	s_UVTransform[stage] = enable;
	if(enable) for(int i = 0; i<16; i++) s_UVMatrix[stage][i] = RS2D3D12_ENV_MATRIX[i];
	else RS2D3D12_Identity(s_UVMatrix[stage]);
	s_UVSource[stage] = enable ? RS2D3D12_UV_CAMERA_NORMAL : RS2D3D12_UV_TEXCOORD0;
}

const RS2D3D12StageStats &RS2D3D12_GetStageStats(){ return s_StageStats; }

static void RS2D3D12_Colour(float *out, const RS2Color4 &c){
	out[0] = c.r;
	out[1] = c.g;
	out[2] = c.b;
	out[3] = c.a;
}

/*
 *	The material and light constants for one draw.
 *
 *	The light is transformed into view space here, once per draw, because the
 *	engine keeps it in world space and the shader lights in view space.  The
 *	engine stores the direction the light travels; the shader wants the
 *	direction toward it, so it is negated.
 */
static void RS2D3D12_LightingConstants(
	RS2D3D12Constants *constants, const float *worldView, bool pipelineTransformed,
	bool hasNormal){
	int i;

	for(i = 0; i<16; i++) constants->worldView[i] = worldView[i];

	float toward[3] = { -s_Light.direction.x, -s_Light.direction.y, -s_Light.direction.z };
	float view[3];

	for(i = 0; i<3; i++)
		view[i] = toward[0]*s_View[0*4+i] + toward[1]*s_View[1*4+i] + toward[2]*s_View[2*4+i];

	const float length = (float)sqrt(view[0]*view[0] + view[1]*view[1] + view[2]*view[2]);

	for(i = 0; i<3; i++) constants->lightToward[i] = length>0.0f ? view[i]/length : 0.0f;
	constants->lightToward[3] = s_LightEnabled ? 1.0f : 0.0f;
	RS2D3D12_Colour(constants->lightColour, s_Light.color);

	constants->ambient[0] = (float)((s_Ambient>>16)&0xff)/255.0f;
	constants->ambient[1] = (float)((s_Ambient>>8)&0xff)/255.0f;
	constants->ambient[2] = (float)(s_Ambient&0xff)/255.0f;
	constants->ambient[3] = (float)((s_Ambient>>24)&0xff)/255.0f;

	RS2D3D12_Colour(constants->materialDiffuse, s_Material.Diffuse);
	RS2D3D12_Colour(constants->materialAmbient, s_Material.Ambient);
	RS2D3D12_Colour(constants->materialSpecular, s_Material.Specular);
	RS2D3D12_Colour(constants->materialEmissive, s_Material.Emissive);

	constants->lighting[0] = s_Lighting ? 1.0f : 0.0f;
	constants->lighting[1] = s_Specular ? 1.0f : 0.0f;
	constants->lighting[2] = s_DiffuseSource==RS2_COLOR_FROM_VERTEX ? 1.0f : 0.0f;
	constants->lighting[3] = s_AmbientSource==RS2_COLOR_FROM_VERTEX ? 1.0f : 0.0f;
	constants->power[0] = s_Material.Power;
	constants->power[1] = constants->power[2] = constants->power[3] = 0.0f;

	//	Screen-space vertices are never lit, as in Direct3D 8.
	if(s_Lighting && pipelineTransformed){
		s_LightingStats.litDraws++;
		if(hasNormal){
			s_LightingStats.litNormalDraws++;
			if(s_Specular) s_LightingStats.specularDraws++;
		}else{
			s_LightingStats.litNoNormalDraws++;
		}
	}else{
		s_LightingStats.unlitDraws++;
	}
}

unsigned int RS2D3D12_GetDrawCount(){ return s_DrawCount; }
unsigned int RS2D3D12_GetTexturedDrawCount(){ return s_TexturedDrawCount; }
unsigned int RS2D3D12_GetDDSTexturedDrawCount(){ return s_DDSTexturedDrawCount; }
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
	key->stencilTest = s_StencilTest ? 1 : 0;
	key->stencilFunc = s_StencilTest ? (unsigned char)s_StencilFunc : 0;
	key->stencilReadMask = s_StencilTest ? (unsigned char)(s_StencilReadMask&0xffu) : 0;
	key->stencilWriteMask = s_StencilTest ? (unsigned char)(s_StencilWriteMask&0xffu) : 0;
	key->stencilFail = s_StencilTest ? (unsigned char)s_StencilFail : 0;
	key->stencilDepthFail = s_StencilTest ? (unsigned char)s_StencilDepthFail : 0;
	key->stencilPass = s_StencilTest ? (unsigned char)s_StencilPass : 0;
	key->stencilPad = 0;
	RS2D3D12SrvSlot textureSlot;
	RS2TextureFilter filter = RS2_FILTER_POINT;
	// A bound texture without TEXCOORD0 remains an untextured draw. The PSO
	// key records only the shader variant, never the texture or sampler.
	key->textured = key->texCoordCount && RS2D3D12_GetBoundTexture(
		backend, &textureSlot, &filter) ? 1 : 0;

	//	Stage 1 only takes part when Direct3D 8 would have let it: combine on,
	//	a live stage 1 texture, and a textured stage 0 - with no base texture
	//	Direct3D 8 applied no stage 1 at all (measured).  Its presence is the
	//	one structural fact the key needs; which texture it is never is.
	RS2D3D12SrvSlot stage1Slot;
	RS2TextureFilter stage1Filter = RS2_FILTER_POINT;
	const bool stage1Texture = RS2D3D12_GetBoundStageTexture(
		backend, 1, &stage1Slot, &stage1Filter);

	key->stage1 = (s_Combine1 && stage1Texture && key->textured) ? 1 : 0;
	if(s_Combine1 && !key->stage1) s_StageStats.stage1Skipped++;

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
	constants.alphaTest[0] = s_AlphaTest ? 1.0f : 0.0f;
	constants.alphaTest[1] = (float)s_AlphaRef;
	constants.alphaTest[2] = (float)s_AlphaFunc;
	constants.alphaTest[3] = 0.0f;
	RS2D3D12_LightingConstants(&constants, worldView,
		key->positionSemantic!=RS2_POSITION_ALREADY_TRANSFORMED, key->hasNormal!=0);

	{
		int i;

		for(i = 0; i<16; i++){
			constants.uvMatrix0[i] = s_UVMatrix[0][i];
			constants.uvMatrix1[i] = s_UVMatrix[1][i];
		}
		constants.uvFlags[0] = s_UVTransform[0] ? 1.0f : 0.0f;
		constants.uvFlags[1] = s_UVTransform[1] ? 1.0f : 0.0f;
		constants.uvFlags[2] = (float)s_UVSource[1];
		constants.uvFlags[3] = 0.0f;

		const bool pipelineTransformed =
			key->positionSemantic!=RS2_POSITION_ALREADY_TRANSFORMED;

		if(key->stage1){
			s_StageStats.stage1Draws++;
			if(s_UVSource[1]==RS2D3D12_UV_CAMERA_NORMAL) s_StageStats.environmentDraws++;
		}
		if(key->textured && s_UVTransform[0] && pipelineTransformed)
			s_StageStats.uvTransformedDraws++;
	}

	D3D12_GPU_VIRTUAL_ADDRESS constantAddress = 0;

	//	Constant buffers must start on a 256-byte boundary.
	if(!backend->GetUpload()->Write(&constants, sizeof(constants), 256, &constantAddress)){
		RS2D3D12_Refuse("the constants would not fit in this frame");
		return false;
	}

	ID3D12GraphicsCommandList *list = backend->GetCommandList();

	list->SetGraphicsRootSignature(backend->GetPipeline()->GetRootSignature());
	list->SetPipelineState(state);

	//	The reference is command-list state, not pipeline state: set with every
	//	stencil draw, so a new command list or a changed reference never draws
	//	against a stale one, and a reference change never builds a pipeline.
	if(key->stencilTest){
		const int ref = (int)(s_StencilRef&0xffu);

		list->OMSetStencilRef((UINT)ref);
		s_StencilStats.stencilDraws++;
		if(s_BlendMode==RS2_BLEND_COLOR_PRESERVE) s_StencilStats.volumeDraws++;
		else s_StencilStats.overlayDraws++;
		if(ref!=s_LastStencilRef){
			s_StencilStats.refChanges++;
			s_LastStencilRef = ref;
		}
	}
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

		//	Stage 1 reuses the texture's own view and the shared samplers.
		//	Tables 3 and 4 are only set - and only read by the shader - when
		//	stage 1 takes part, so no draw can reach a stale descriptor.
		if(key->stage1){
			D3D12_GPU_DESCRIPTOR_HANDLE srv1, sampler1;

			if(!backend->GetDescriptors()->GetSrvHandles(stage1Slot, 0, &srv1)
					|| !backend->GetDescriptors()->GetSamplerHandle(stage1Filter, &sampler1)){
				RS2D3D12_Refuse("Stage 1 descriptors are unavailable");
				return false;
			}
			list->SetGraphicsRootDescriptorTable(3, srv1);
			list->SetGraphicsRootDescriptorTable(4, sampler1);
		}
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
	RS2D3D12_CountTextured(key.textured != 0);
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
	RS2D3D12_CountTextured(key.textured != 0);
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
	RS2D3D12_CountTextured(key.textured != 0);
}
