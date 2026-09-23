// RS2EX - public-boundary D3D12 Stage 0 texture validation for WP8.
#include "stdafx.h"
#include "RS2D3D12TextureSmoke.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12Draw.h"
#include "RS2D3D12Texture.h"
#include "RS2DecodedImage.h"
#include "RS2Draw.h"
#include "RS2MaterialBinding.h"
#include "RS2MeshData.h"
#include "RS2RenderState.h"
#include "RS2Renderer.h"
#include "RS2TextureResource.h"

static const int RS2_TEXTURE_FRAMES = 300;
static const DWORD RS2_TEXTURE_CAPTURE_HOLD_MS = 8000;
static const unsigned int RS2_TEXTURE_CLEAR = 0x00102030;

struct RS2TextureSmokeVertex{
	float x, y, z, rhw;
	unsigned int diffuse;
	float u, v;
};

struct RS2TextureSmokeSolidVertex{
	float x, y, z, rhw;
	unsigned int diffuse;
};

bool RS2D3D12TextureSmokeRequested(){
	return CheckArguments("-dx12texturesmoke")!=FALSE;
}

static void RS2TextureSmokeIdentity(float *matrix){
	for(int i=0; i<16; i++) matrix[i]=0.0f;
	matrix[0]=matrix[5]=matrix[10]=matrix[15]=1.0f;
}

static void RS2TextureSmokePanel(
	const RS2MeshVertexLayout &layout, unsigned int column, unsigned int row,
	float u, float v, unsigned int diffuse
){
	const float x=(float)(95+column*150);
	const float y=(float)(90+row*140);
	const RS2TextureSmokeVertex quad[4]={
		{x-32,y-32,0.5f,1.0f,diffuse,u,v},
		{x+32,y-32,0.5f,1.0f,diffuse,u,v},
		{x+32,y+32,0.5f,1.0f,diffuse,u,v},
		{x-32,y+32,0.5f,1.0f,diffuse,u,v}
	};
	RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, quad, 4);
}

static void RS2TextureSmokeSolidPanel(
	const RS2MeshVertexLayout &layout, unsigned int column, unsigned int row,
	unsigned int diffuse
){
	const float x=(float)(95+column*150);
	const float y=(float)(90+row*140);
	const RS2TextureSmokeSolidVertex quad[4]={
		{x-32,y-32,0.5f,1.0f,diffuse},
		{x+32,y-32,0.5f,1.0f,diffuse},
		{x+32,y+32,0.5f,1.0f,diffuse},
		{x-32,y+32,0.5f,1.0f,diffuse}
	};
	RS2DrawImmediate(layout, RS2_PRIMITIVE_TRIANGLE_FAN, quad, 4);
}

static void RS2TextureSmokeStep(const char *name, bool passed, bool *allPassed){
	Debug("RS2D3D12TEXTURE|%-28s|%s\n", name, passed ? "pass" : "FAIL");
	if(!passed) *allPassed=false;
}

bool RS2D3D12TextureSmokeRun(){
	if(GetRS2Renderer().GetBackendType()!=RS2_RENDERER_D3D12){
		Debug("RS2D3D12TEXTURE|this test needs -dx12\n");
		return false;
	}
	CRS2D3D12Backend *backend=RS2D3D12GetActiveBackend();
	if(!backend) return false;
	unsigned int width=0, height=0;
	GetRS2Renderer().GetViewportSize(&width,&height);
	if(width!=640 || height!=480){
		Debug("RS2D3D12TEXTURE|expected 640 x 480; got %u x %u\n",width,height);
		return false;
	}
	if(sv3.fWindowed)
		SetWindowPos(svw.hWnd,HWND_TOPMOST,0,0,0,0,
			SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

	char directory[MAX_PATH], alphaPath[MAX_PATH], keyPath[MAX_PATH];
	if(!GetTempPathA(MAX_PATH,directory)) return false;
	_snprintf(alphaPath,MAX_PATH-1,"%sRS2EX-texture-alpha-%lu.png",
		directory,(unsigned long)GetCurrentProcessId());
	_snprintf(keyPath,MAX_PATH-1,"%sRS2EX-texture-key-%lu.png",
		directory,(unsigned long)GetCurrentProcessId());
	alphaPath[MAX_PATH-1]=keyPath[MAX_PATH-1]=0;
	if(!RS2WriteKnownAlphaPngFixture(alphaPath)
		|| !RS2WriteColourKeyPngFixture(keyPath)){
		DeleteFileA(alphaPath);
		DeleteFileA(keyPath);
		Debug("RS2D3D12TEXTURE|fixture write failed\n");
		return false;
	}

	const unsigned int textureBaseline=RS2D3D12_GetLiveTextureCount();
	const unsigned int descriptorBaseline=backend->GetDescriptors()->GetLive();
	const UINT64 uploadBaseline=backend->GetTextureUpload()->GetSubmittedBytes();
	bool ok=true;
	CRS2TextureResource *invalidFile=RS2CreateTextureFromFile(
		"RS2EX-WP8-this-file-does-not-exist.png",0,1);
	CRS2TextureResource *invalidResource=RS2CreateTextureFromResource(
		"RS2EX_WP8_MISSING_RESOURCE",0,1);
	const bool invalidOk=!invalidFile && !invalidResource
		&& RS2D3D12_GetLiveTextureCount()==textureBaseline
		&& backend->GetDescriptors()->GetLive()==descriptorBaseline;
	RS2TextureSmokeStep("invalid file/resource",invalidOk,&ok);
	RS2DestroyTexture(invalidFile);
	RS2DestroyTexture(invalidResource);

	// More than the 128-slot heap capacity, entirely through the public API.
	unsigned int cycles=0;
	for(;cycles<backend->GetDescriptors()->GetCapacity()+2;cycles++){
		CRS2TextureResource *temporary=RS2CreateTextureFromFile(alphaPath,0,1);
		if(!temporary || !temporary->IsValid()){
			RS2DestroyTexture(temporary);
			break;
		}
		RS2DestroyTexture(temporary);
		if(RS2D3D12_GetLiveTextureCount()!=textureBaseline
			|| backend->GetDescriptors()->GetLive()!=descriptorBaseline) break;
	}
	const bool cyclesOk=cycles==backend->GetDescriptors()->GetCapacity()+2;
	RS2TextureSmokeStep("create/destroy + descriptor reuse",cyclesOk,&ok);

	CRS2TextureResource *alpha=RS2CreateTextureFromFile(alphaPath,0,1);
	CRS2TextureResource *keyed=RS2CreateTextureFromFile(keyPath,0xff000000,1);
	CRS2TextureResource *unkeyed=RS2CreateTextureFromFile(keyPath,0,1);
	CRS2TextureResource *resource=RS2CreateTextureFromResource("OPENING",0,1);
	DeleteFileA(alphaPath);
	DeleteFileA(keyPath);
	const bool created=alpha && alpha->IsValid() && alpha->GetWidth()==2
		&& alpha->GetHeight()==2 && keyed && keyed->IsValid()
		&& keyed->GetWidth()==4 && keyed->GetHeight()==2
		&& unkeyed && unkeyed->IsValid()
		&& resource && resource->IsValid() && resource->GetWidth()==128
		&& resource->GetHeight()==128
		&& RS2D3D12_GetLiveTextureCount()==textureBaseline+4
		&& backend->GetDescriptors()->GetLive()==descriptorBaseline+4;
	RS2TextureSmokeStep("file/resource textures",created,&ok);

	RS2MeshVertexLayout solidLayout;
	solidLayout.Clear();
	solidLayout.stride=sizeof(RS2TextureSmokeSolidVertex);
	solidLayout.positionOffset=0;
	solidLayout.positionSemantic=RS2_POSITION_ALREADY_TRANSFORMED;
	solidLayout.diffuseOffset=sizeof(float)*4;
	RS2MeshVertexLayout uvLayout=solidLayout;
	uvLayout.stride=sizeof(RS2TextureSmokeVertex);
	uvLayout.texCoordCount=1;
	uvLayout.texCoord[0].offset=sizeof(RS2TextureSmokeSolidVertex);
	uvLayout.texCoord[0].components=2;
	float identity[16];
	RS2TextureSmokeIdentity(identity);
	const unsigned int drawBaseline=RS2D3D12_GetDrawCount();
	const unsigned int refusedBaseline=RS2D3D12_GetRefusedDrawCount();
	bool framesOk=created && cyclesOk && invalidOk;
	bool deferredOk=false;
	for(int frame=0;frame<RS2_TEXTURE_FRAMES && framesOk;frame++){
		if(!GetRS2Renderer().BeginRenderPass(RS2_TEXTURE_CLEAR,true)){
			framesOk=false;
			break;
		}
		RS2SetWorldTransform(identity);
		RS2SetViewTransform(identity);
		RS2SetProjectionTransform(identity);
		RS2SetDepthTest(false);
		RS2SetDepthWrite(false);
		RS2SetCullMode(RS2_CULL_NONE);
		RS2SetBlend(RS2_BLEND_DISABLED);
		RS2SetBaseTextureCombine();
		RS2SetAlphaTest(false);
		RS2SetTextureFilter(0,RS2_FILTER_POINT);
		RS2BindTexture(0,alpha->GetRef());
		RS2TextureSmokePanel(uvLayout,0,0,0.25f,0.25f,0xffffffff);
		RS2TextureSmokePanel(uvLayout,1,0,0.75f,0.25f,0xffffffff);
		RS2TextureSmokePanel(uvLayout,2,0,0.49f,0.25f,0xffffffff);
		RS2SetTextureFilter(0,RS2_FILTER_LINEAR);
		RS2TextureSmokePanel(uvLayout,3,0,0.5f,0.25f,0xffffffff);
		RS2SetTextureFilter(0,RS2_FILTER_POINT);
		RS2TextureSmokePanel(uvLayout,0,1,0.25f,0.75f,0xffffffff);
		RS2SetAlphaTest(true);
		RS2SetAlphaRef(0);
		RS2SetAlphaFunc(RS2_COMPARE_GREATER);
		RS2TextureSmokePanel(uvLayout,1,1,0.25f,0.75f,0xffffffff);
		RS2TextureSmokePanel(uvLayout,2,1,0.75f,0.25f,0xffffffff);
		RS2SetAlphaTest(false);
		RS2BindTexture(0,RS2TextureRef());
		RS2TextureSmokePanel(uvLayout,3,1,0.5f,0.5f,0xff20c0e0);
		RS2BindTexture(0,alpha->GetRef());
		RS2TextureSmokeSolidPanel(solidLayout,0,2,0xffe020c0);
		RS2SetAlphaTest(true);
		RS2BindTexture(0,keyed->GetRef());
		RS2TextureSmokePanel(uvLayout,1,2,0.125f,0.75f,0xffffffff);
		RS2BindTexture(0,unkeyed->GetRef());
		RS2TextureSmokePanel(uvLayout,2,2,0.125f,0.75f,0xffffffff);
		RS2SetAlphaTest(false);
		RS2BindTexture(0,resource->GetRef());
		RS2TextureSmokePanel(uvLayout,3,2,20.5f/128.0f,2.5f/128.0f,0xffffffff);

		// A drawn texture's descriptor stays live until this frame's fence.
		if(frame==0){
			CRS2TextureResource *transient=RS2CreateTextureFromResource("OPENING",0,1);
			if(transient && transient->IsValid()){
				RS2BindTexture(0,transient->GetRef());
				RS2TextureSmokePanel(uvLayout,3,2,20.5f/128.0f,2.5f/128.0f,0xffffffff);
				RS2DestroyTexture(transient);
				deferredOk=backend->GetDescriptors()->GetLive()==descriptorBaseline+5
					&& RS2D3D12_GetLiveTextureCount()==textureBaseline+4;
			}else RS2DestroyTexture(transient);
		}
		RS2BindTexture(0,RS2TextureRef());
		RS2SetAlphaTest(false);
		GetRS2Renderer().EndRenderPass();
		GetRS2Renderer().Present();
		if(frame==0){
			backend->WaitForGpu();
			backend->CollectRetiredTextures();
			deferredOk=deferredOk
				&& backend->GetDescriptors()->GetLive()==descriptorBaseline+4;
			RS2TextureSmokeStep("in-flight descriptor retirement",deferredOk,&ok);
		}
	}
	RS2BindTexture(0,RS2TextureRef());
	RS2SetDepthTest(true);
	RS2SetDepthWrite(true);
	RS2SetCullMode(RS2_CULL_COUNTER_CLOCKWISE);
	RS2SetBlend(RS2_BLEND_ALPHA);
	RS2DestroyTexture(resource);
	RS2DestroyTexture(unkeyed);
	RS2DestroyTexture(keyed);
	RS2DestroyTexture(alpha);
	backend->WaitForGpu();
	backend->CollectRetiredTextures();
	const bool uploadsDone=backend->GetTextureUpload()->WaitForAll();
	const unsigned int submitted=RS2D3D12_GetDrawCount()-drawBaseline;
	const unsigned int refused=RS2D3D12_GetRefusedDrawCount()-refusedBaseline;
	const unsigned int liveTextures=RS2D3D12_GetLiveTextureCount();
	const unsigned int liveDescriptors=backend->GetDescriptors()->GetLive();
	const unsigned int descriptorPeak=backend->GetDescriptors()->GetPeak();
	const unsigned int uploadPending=backend->GetTextureUpload()->GetPendingCount();
	const UINT64 uploadBytes=backend->GetTextureUpload()->GetSubmittedBytes()-uploadBaseline;
	const unsigned int warnings=backend->HasDebugLayer()
		? backend->CountDebugMessages(D3D12_MESSAGE_SEVERITY_WARNING) : 0;
	const bool countersOk=framesOk && deferredOk
		&& submitted==RS2_TEXTURE_FRAMES*12+1 && refused==0
		&& liveTextures==textureBaseline && liveDescriptors==descriptorBaseline
		&& uploadsDone && uploadPending==0 && uploadBytes>0 && warnings==0
		&& !backend->IsDeviceRemoved();
	RS2TextureSmokeStep("draw/texture/descriptor baselines",countersOk,&ok);
	Debug("RS2D3D12TEXTURE|draws=%u refused=%u cycles=%u\n",
		submitted,refused,cycles);
	Debug("RS2D3D12TEXTURE|textures=%u->%u descriptors=%u->%u peak=%u/%u\n",
		textureBaseline,liveTextures,descriptorBaseline,liveDescriptors,
		descriptorPeak,backend->GetDescriptors()->GetCapacity());
	Debug("RS2D3D12TEXTURE|uploadBytes=%llu pending=%u warnings=%u debug=%u\n",
		(unsigned long long)uploadBytes,uploadPending,warnings,
		backend->HasDebugLayer() ? 1 : 0);
	Debug("RS2D3D12TEXTURE|%s\n",ok ? "pass" : "FAIL");
	if(ok) Sleep(RS2_TEXTURE_CAPTURE_HOLD_MS);
	return ok;
}
