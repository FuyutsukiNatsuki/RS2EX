//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	See RS2D3D12Pipeline.h.

#include "stdafx.h"
#include "RS2D3D12Pipeline.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

/*
 *	The shaders.
 *
 *	Compiled at run time with D3DCompile, from source held here.  The choice is
 *	recorded in the validation document, and the reasoning is short: it needs
 *	no build step, no shader assets to ship alongside the executable and no
 *	extra redistributable, because d3dcompiler_47.dll is part of Windows 10 and
 *	v0.1.0 already made Windows 10 the floor.  DXC would mean shipping
 *	dxcompiler.dll, and offline compilation would mean generated headers or
 *	.cso files in the build - both are reasonable later, and neither is what
 *	this release is about.
 *
 *	Two things are stated rather than left to defaults, because both have a
 *	silent wrong answer:
 *
 *	row_major.  HLSL packs constant-buffer matrices column-major unless told
 *	otherwise, and the engine's matrices are D3DXMATRIX, which is row-major.
 *	Left alone this transposes every matrix and looks like a camera bug.
 *
 *	mul(vector, matrix).  Direct3D 8 fixed function treats positions as row
 *	vectors, so the engine composes world * view * projection in that order.
 *	Writing mul(matrix, vector) would apply them backwards.
 */
static const char *RS2D3D12_SHADER_SOURCE =
"cbuffer RS2Constants : register(b0)\n"
"{\n"
"	row_major float4x4 g_WorldViewProj;\n"
"	float4 g_Viewport;	// xy size, zw reciprocal\n"
"};\n"
"\n"
"//	Geometry with no vertex colour draws white.  It is not a material - there\n"
"//	are no materials yet - but it is visible and unmistakably unfinished,\n"
"//	which is what a bootstrap should look like rather than black.\n"
"#if RS2_HAS_DIFFUSE\n"
"#define RS2_VERTEX_COLOUR(v)	((v).col)\n"
"#else\n"
"#define RS2_VERTEX_COLOUR(v)	float4(1.0f, 1.0f, 1.0f, 1.0f)\n"
"#endif\n"
"\n"
"struct VSIn\n"
"{\n"
"	float4 pos : POSITION;\n"
"#if RS2_HAS_DIFFUSE\n"
"	float4 col : COLOR0;\n"
"#endif\n"
"#if RS2_TEXTURED\n"
"	float2 uv : TEXCOORD0;\n"
"#endif\n"
"};\n"
"\n"
"struct VSOut\n"
"{\n"
"	float4 pos : SV_POSITION;\n"
"	float4 col : COLOR0;\n"
"#if RS2_TEXTURED\n"
"	float2 uv : TEXCOORD0;\n"
"#endif\n"
"};\n"
"\n"
"//	Ordinary geometry: the engine's matrices do the work.\n"
"VSOut VSPipeline(VSIn input)\n"
"{\n"
"	VSOut output;\n"
"	output.pos = mul(float4(input.pos.xyz, 1.0f), g_WorldViewProj);\n"
"	output.col = RS2_VERTEX_COLOUR(input);\n"
"#if RS2_TEXTURED\n"
"	output.uv = input.uv;\n"
"#endif\n"
"	return output;\n"
"}\n"
"\n"
"//	Already-transformed geometry, the XYZRHW path.  Direct3D 8 took screen\n"
"//	pixels with a reciprocal w; Direct3D 12 has no such path, so the same\n"
"//	mapping is written out: pixels to normalised device coordinates, y down to\n"
"//	y up, and w restored from the reciprocal so the hardware divide undoes it.\n"
"VSOut VSScreen(VSIn input)\n"
"{\n"
"	VSOut output;\n"
"	float w = (input.pos.w != 0.0f) ? (1.0f/input.pos.w) : 1.0f;\n"
"	float2 ndc;\n"
"	ndc.x = input.pos.x*g_Viewport.z*2.0f - 1.0f;\n"
"	ndc.y = 1.0f - input.pos.y*g_Viewport.w*2.0f;\n"
"	output.pos = float4(ndc.x*w, ndc.y*w, input.pos.z*w, w);\n"
"	output.col = RS2_VERTEX_COLOUR(input);\n"
"#if RS2_TEXTURED\n"
"	output.uv = input.uv;\n"
"#endif\n"
"	return output;\n"
"}\n"
"\n"
"#if RS2_TEXTURED\n"
"Texture2D g_Texture : register(t0);\n"
"SamplerState g_Sampler : register(s0);\n"
"#endif\n"
"float4 PSMain(VSOut input) : SV_TARGET\n"
"{\n"
"#if RS2_TEXTURED\n"
"	return g_Texture.Sample(g_Sampler, input.uv)*input.col;\n"
"#else\n"
"	return input.col;\n"
"#endif\n"
"}\n";

static ID3DBlob *RS2D3D12_Compile(
	const char *entry,	//	function to compile
	const char *target,	//	shader model
	bool hasDiffuse,		//	whether the layout supplies a vertex colour
	bool textured		//	whether Stage 0 sampling is enabled
){
	ID3DBlob *code = 0;
	ID3DBlob *errors = 0;

	const D3D_SHADER_MACRO macros[3] = {
		{ "RS2_HAS_DIFFUSE", hasDiffuse ? "1" : "0" },
		{ "RS2_TEXTURED", textured ? "1" : "0" },
		{ NULL, NULL }
	};

	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;

#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	const HRESULT hr = D3DCompile(
		RS2D3D12_SHADER_SOURCE, strlen(RS2D3D12_SHADER_SOURCE),
		"RS2Basic", macros, NULL, entry, target, flags, 0, &code, &errors);

	if(errors){
		//	Warnings arrive here too, so this is reported whether or not the
		//	compile succeeded.
		Debug("[RS2EX D3D12] shader %s: %s\n", entry, (const char *)errors->GetBufferPointer());
		errors->Release();
	}

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] %s would not compile (0x%08lx)\n", entry, (unsigned long)hr);
		if(code) code->Release();
		return 0;
	}
	return code;
}

CRS2D3D12Pipeline::CRS2D3D12Pipeline()
	: m_Device(0),
	  m_RootSignature(0),
	  m_Count(0),
	  m_Full(false)
{
	unsigned int i;

	for(i = 0; i<RS2D3D12_MAX_PIPELINES; i++) m_State[i] = 0;
	ZeroMemory(m_Vertex, sizeof(m_Vertex));
	ZeroMemory(m_Pixel, sizeof(m_Pixel));
	ZeroMemory(m_Key, sizeof(m_Key));
}

CRS2D3D12Pipeline::~CRS2D3D12Pipeline(){
	Destroy();
}

bool CRS2D3D12Pipeline::CompileShaders(){
	unsigned int diffuse, textured;

	for(diffuse = 0; diffuse<2; diffuse++) for(textured = 0; textured<2; textured++){
		m_Vertex[0][diffuse][textured] = RS2D3D12_Compile(
			"VSPipeline", "vs_5_0", diffuse!=0, textured!=0);
		m_Vertex[1][diffuse][textured] = RS2D3D12_Compile(
			"VSScreen", "vs_5_0", diffuse!=0, textured!=0);

		if(!m_Vertex[0][diffuse][textured] || !m_Vertex[1][diffuse][textured])
			return false;
	}

	for(textured = 0; textured<2; textured++){
		m_Pixel[textured] = RS2D3D12_Compile("PSMain", "ps_5_0", true, textured!=0);
		if(!m_Pixel[textured]) return false;
	}
	return true;
}

bool CRS2D3D12Pipeline::Create(
	ID3D12Device *device	//	device to build against
){
	Destroy();
	m_Device = device;

	// b0 is a root CBV; t0 and s0 are one-descriptor tables supplied by WP4.
	D3D12_DESCRIPTOR_RANGE ranges[2];
	D3D12_ROOT_PARAMETER parameters[3];
	ZeroMemory(ranges, sizeof(ranges));
	ZeroMemory(parameters, sizeof(parameters));
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].NumDescriptors = 1;
	ranges[0].BaseShaderRegister = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	ranges[1].NumDescriptors = 1;
	ranges[1].BaseShaderRegister = 0;
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[0].Descriptor.ShaderRegister = 0;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	for(unsigned int i = 0; i<2; i++){
		parameters[i+1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[i+1].DescriptorTable.NumDescriptorRanges = 1;
		parameters[i+1].DescriptorTable.pDescriptorRanges = &ranges[i];
		parameters[i+1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	D3D12_ROOT_SIGNATURE_DESC desc;

	ZeroMemory(&desc, sizeof(desc));
	desc.NumParameters = 3;
	desc.pParameters = parameters;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob *serialised = 0;
	ID3DBlob *errors = 0;

	HRESULT hr = D3D12SerializeRootSignature(
		&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialised, &errors);

	if(errors){
		Debug("[RS2EX D3D12] root signature: %s\n", (const char *)errors->GetBufferPointer());
		errors->Release();
	}

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] root signature would not serialise (0x%08lx)\n",
			(unsigned long)hr);
		return false;
	}

	hr = m_Device->CreateRootSignature(0,
		serialised->GetBufferPointer(), serialised->GetBufferSize(),
		IID_PPV_ARGS(&m_RootSignature));
	serialised->Release();

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] root signature creation failed (0x%08lx)\n",
			(unsigned long)hr);
		return false;
	}

	if(!CompileShaders()) return false;

	Debug("[RS2EX D3D12] root signature and shaders ready\n");
	return true;
}

void CRS2D3D12Pipeline::Destroy(){
	unsigned int i;

	for(i = 0; i<RS2D3D12_MAX_PIPELINES; i++) RELEASE(m_State[i]);
	m_Count = 0;
	m_Full = false;

	for(i = 0; i<2; i++) RELEASE(m_Pixel[i]);

	{
		unsigned int semantic, diffuse, textured;

		for(semantic = 0; semantic<2; semantic++)
			for(diffuse = 0; diffuse<2; diffuse++)
				for(textured = 0; textured<2; textured++)
					RELEASE(m_Vertex[semantic][diffuse][textured]);
	}
	RELEASE(m_RootSignature);

	m_Device = 0;
	ZeroMemory(m_Key, sizeof(m_Key));
}

bool RS2D3D12_DescribeLayout(
	const RS2MeshVertexLayout &layout,	//	layout to describe
	RS2D3D12PipelineKey *key		//	key to fill in
){
	if(!key || !layout.HasPosition()) return false;
	if(layout.texCoordCount>8) return false;

	//	XYZRHW and a normal together is not a thing Direct3D 8 allowed either,
	//	and the screen-space shader has nothing to do with a normal.
	if(layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED && layout.HasNormal())
		return false;

	key->hasNormal = layout.HasNormal() ? 1 : 0;
	key->hasDiffuse = layout.HasDiffuse() ? 1 : 0;
	key->texCoordCount = (unsigned char)layout.texCoordCount;
	key->stride = (unsigned short)layout.stride;
	key->positionSemantic = (unsigned char)layout.positionSemantic;
	key->positionOffset = (unsigned short)layout.positionOffset;
	key->normalOffset = key->hasNormal ? (unsigned short)layout.normalOffset : 0;
	key->diffuseOffset = key->hasDiffuse ? (unsigned short)layout.diffuseOffset : 0;

	unsigned int i;

	for(i = 0; i<8; i++){
		const bool present = i<layout.texCoordCount;

		key->texCoordComponents[i] = present
			? (unsigned char)layout.texCoord[i].components : 0;
		key->texCoordOffset[i] = present
			? (unsigned short)layout.texCoord[i].offset : 0;
	}

	for(i = 0; i<layout.texCoordCount; i++){
		const unsigned char components = key->texCoordComponents[i];

		if(components<1 || components>4) return false;
	}
	return true;
}


/*
 *	The engine's three comparison functions.
 *
 *	Three, not eight: RS2CompareFunc only ever had the ones RailSim uses, and
 *	inventing the rest here would be adding a contract nothing asked for.
 */
static D3D12_COMPARISON_FUNC RS2D3D12_Compare(unsigned char func){
	switch((RS2CompareFunc)func){
	case RS2_COMPARE_LESS_EQUAL:	return D3D12_COMPARISON_FUNC_LESS_EQUAL;
	case RS2_COMPARE_GREATER:	return D3D12_COMPARISON_FUNC_GREATER;
	case RS2_COMPARE_ALWAYS:
	default:			return D3D12_COMPARISON_FUNC_ALWAYS;
	}
}

/*
 *	Culling, matching what the Direct3D 8 backend asks for exactly.
 *
 *	Direct3D 8 names the winding it culls: D3DCULL_CCW culls counter-clockwise
 *	triangles, so front faces are clockwise.  Direct3D 12 names the winding
 *	that is front-facing and then culls front or back, so both halves have to
 *	be stated - and FrontCounterClockwise defaults to FALSE, which happens to
 *	be right here, but is written down rather than relied on.
 */
static D3D12_CULL_MODE RS2D3D12_Cull(unsigned char mode){
	switch((RS2CullMode)mode){
	case RS2_CULL_NONE:		return D3D12_CULL_MODE_NONE;
	case RS2_CULL_CLOCKWISE:	return D3D12_CULL_MODE_FRONT;
	case RS2_CULL_COUNTER_CLOCKWISE:
	default:			return D3D12_CULL_MODE_BACK;
	}
}

/*
 *	Blending, with the same source and destination factors the Direct3D 8
 *	backend sets for each mode.
 */
static void RS2D3D12_Blend(unsigned char mode, D3D12_RENDER_TARGET_BLEND_DESC *out){
	ZeroMemory(out, sizeof(*out));
	out->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	out->BlendOp = D3D12_BLEND_OP_ADD;
	out->BlendOpAlpha = D3D12_BLEND_OP_ADD;
	out->SrcBlendAlpha = D3D12_BLEND_ONE;
	out->DestBlendAlpha = D3D12_BLEND_ZERO;

	if((RS2BlendMode)mode==RS2_BLEND_DISABLED){
		out->BlendEnable = FALSE;
		out->SrcBlend = D3D12_BLEND_ONE;
		out->DestBlend = D3D12_BLEND_ZERO;
		return;
	}

	out->BlendEnable = TRUE;

	switch((RS2BlendMode)mode){
	case RS2_BLEND_ALPHA_ADD:
		out->SrcBlend = D3D12_BLEND_SRC_ALPHA;
		out->DestBlend = D3D12_BLEND_ONE;
		return;

	case RS2_BLEND_COLOR_PRESERVE:
		out->SrcBlend = D3D12_BLEND_ZERO;
		out->DestBlend = D3D12_BLEND_ONE;
		return;

	case RS2_BLEND_ALPHA:
	default:
		out->SrcBlend = D3D12_BLEND_SRC_ALPHA;
		out->DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		return;
	}
}

static DXGI_FORMAT RS2D3D12_TexCoordFormat(unsigned char components){
	switch(components){
	case 1:	return DXGI_FORMAT_R32_FLOAT;
	case 2:	return DXGI_FORMAT_R32G32_FLOAT;
	case 3:	return DXGI_FORMAT_R32G32B32_FLOAT;
	case 4:	return DXGI_FORMAT_R32G32B32A32_FLOAT;
	}
	return DXGI_FORMAT_UNKNOWN;
}

ID3D12PipelineState *CRS2D3D12Pipeline::Build(
	const RS2D3D12PipelineKey &key	//	what the draw needs
){
	if(key.textured && !key.texCoordCount) return 0;
	D3D12_INPUT_ELEMENT_DESC elements[2+RS2MeshVertexLayout::MAX_TEXCOORD];
	UINT count = 0;

	//	Position.  The already-transformed path needs all four components: the
	//	fourth is the reciprocal w the shader restores.
	elements[count].SemanticName = "POSITION";
	elements[count].SemanticIndex = 0;
	elements[count].Format = (key.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED)
		? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
	elements[count].InputSlot = 0;
	elements[count].AlignedByteOffset = key.positionOffset;
	elements[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	elements[count].InstanceDataStepRate = 0;
	count++;

	if(key.hasDiffuse){
		//	The engine's packed colour is 0xAARRGGBB in a 32-bit word, which on
		//	a little-endian machine is B, G, R, A in memory - so BGRA is the
		//	format that reads it without swizzling.  Measured rather than
		//	assumed; see the validation record.
		elements[count].SemanticName = "COLOR";
		elements[count].SemanticIndex = 0;
		elements[count].Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		elements[count].InputSlot = 0;
		elements[count].AlignedByteOffset = key.diffuseOffset;
		elements[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[count].InstanceDataStepRate = 0;
		count++;
	}

	// The textured shader consumes TEXCOORD0; unused layout elements remain
	// described so the vertex byte offsets match the public draw contract.
	if(key.hasNormal){
		elements[count].SemanticName = "NORMAL";
		elements[count].SemanticIndex = 0;
		elements[count].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		elements[count].InputSlot = 0;
		elements[count].AlignedByteOffset = key.normalOffset;
		elements[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[count].InstanceDataStepRate = 0;
		count++;
	}

	unsigned int i;

	for(i = 0; i<key.texCoordCount; i++){
		const DXGI_FORMAT format = RS2D3D12_TexCoordFormat(key.texCoordComponents[i]);

		if(format==DXGI_FORMAT_UNKNOWN) return 0;

		elements[count].SemanticName = "TEXCOORD";
		elements[count].SemanticIndex = i;
		elements[count].Format = format;
		elements[count].InputSlot = 0;
		elements[count].AlignedByteOffset = key.texCoordOffset[i];
		elements[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[count].InstanceDataStepRate = 0;
		count++;
	}

	ID3DBlob *vertex = m_Vertex
		[(key.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED) ? 1 : 0]
		[key.hasDiffuse ? 1 : 0][key.textured ? 1 : 0];

	if(!vertex || !m_Pixel[key.textured ? 1 : 0]) return 0;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;

	ZeroMemory(&desc, sizeof(desc));
	desc.pRootSignature = m_RootSignature;
	desc.VS.pShaderBytecode = vertex->GetBufferPointer();
	desc.VS.BytecodeLength = vertex->GetBufferSize();
	desc.PS.pShaderBytecode = m_Pixel[key.textured ? 1 : 0]->GetBufferPointer();
	desc.PS.BytecodeLength = m_Pixel[key.textured ? 1 : 0]->GetBufferSize();
	desc.InputLayout.pInputElementDescs = elements;
	desc.InputLayout.NumElements = count;
	desc.PrimitiveTopologyType = (D3D12_PRIMITIVE_TOPOLOGY_TYPE)key.topology;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = UINT_MAX;

	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = RS2D3D12_Cull(key.cullMode);
	desc.RasterizerState.DepthClipEnable = TRUE;

	//	Direct3D 8 treated clockwise as front-facing; Direct3D 12 defaults to
	//	counter-clockwise, so this is stated rather than left alone.
	desc.RasterizerState.FrontCounterClockwise = FALSE;

	desc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
	desc.DepthStencilState.DepthWriteMask = key.depthWrite
		? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = RS2D3D12_Compare(key.depthFunc);
	desc.DepthStencilState.StencilEnable = FALSE;

	RS2D3D12_Blend(key.blendMode, &desc.BlendState.RenderTarget[0]);

	ID3D12PipelineState *state = 0;
	const HRESULT hr = m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&state));

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] pipeline state creation failed (0x%08lx)\n",
			(unsigned long)hr);
		return 0;
	}
	return state;
}

ID3D12PipelineState *CRS2D3D12Pipeline::Get(
	const RS2D3D12PipelineKey &key	//	what the draw needs
){
	unsigned int i;

	for(i = 0; i<m_Count; i++)
		if(memcmp(&m_Key[i], &key, sizeof(key))==0) return m_State[i];

	if(m_Count>=RS2D3D12_MAX_PIPELINES){
		if(!m_Full){
			m_Full = true;
			Debug("[RS2EX D3D12] pipeline cache is full at %d states\n",
				RS2D3D12_MAX_PIPELINES);
		}
		return 0;
	}

	ID3D12PipelineState *state = Build(key);

	if(!state) return 0;

	m_Key[m_Count] = key;
	m_State[m_Count] = state;
	m_Count++;

	Debug("[RS2EX D3D12] pipeline state %u built\n", m_Count);
	return state;
}
