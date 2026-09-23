//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-24.
//
//	See RS2StageAudit.h.  Records observations; never changes a value.

#include "stdafx.h"
#include "RS2StageAudit.h"
#include "RS2TextureResource.h"

#include <map>
#include <string>
#include <vector>
#include <stdio.h>

//	More than RailSim uses.  A stage past this is still counted, in "other".
enum { RS2_AUDIT_STAGES = 4 };

struct RS2StageAuditLayout
{
	bool normal, diffuse, screen;
	unsigned int texcoords;
};

struct RS2StageAuditStageState
{
	const CRS2TextureResource *texture;
	int filter;			//	-1 never set, else RS2TextureFilter
	int combine;		//	-1 never set, 0 / 1
	int environment;	//	-1 never set, 0 / 1
	int transform;		//	-1 never set, 0 / 1
	std::string matrix;	//	empty: never set
};

struct RS2StageAuditState
{
	RS2StageAuditStageState stage[RS2_AUDIT_STAGES];
	int lighting, alphaTest, blend;

	unsigned int binds[RS2_AUDIT_STAGES][2];		//	[stage][unbind, bind]
	unsigned int filters[RS2_AUDIT_STAGES][2];		//	[stage][point, linear]
	unsigned int otherFilters;
	unsigned int combines[RS2_AUDIT_STAGES][2];		//	[stage][off, on]
	unsigned int combineRedundant[RS2_AUDIT_STAGES];	//	same as before
	unsigned int environments[RS2_AUDIT_STAGES][2];
	unsigned int transforms[RS2_AUDIT_STAGES][2];
	unsigned int matrixCalls[RS2_AUDIT_STAGES];
	unsigned int otherStageCalls;			//	any call with stage >= RS2_AUDIT_STAGES

	std::map<const CRS2TextureResource *, std::string> sources;
	std::map<std::string, unsigned int> boundSources[RS2_AUDIT_STAGES];
	std::map<std::string, unsigned int> matrices[RS2_AUDIT_STAGES];
	std::map<std::string, unsigned int> drawKeys;
	std::map<std::string, unsigned int> stage1DrawSources;
	std::map<std::string, unsigned int> transformedDrawMatrices;
	std::map<const CRS2GeometryResource *, RS2StageAuditLayout> geometry;

	std::vector<std::string> sequence;
	unsigned int draws, stage1Draws, environmentDraws, transformedDraws, unknownGeometry;
	bool dumped;

	RS2StageAuditState()
		: lighting(-1), alphaTest(-1), blend(-1), otherFilters(0), otherStageCalls(0),
		  draws(0), stage1Draws(0), environmentDraws(0), transformedDraws(0),
		  unknownGeometry(0), dumped(false)
	{
		int i;

		for(i = 0; i<RS2_AUDIT_STAGES; i++){
			stage[i].texture = 0;
			stage[i].filter = stage[i].combine = stage[i].environment = stage[i].transform = -1;
		}
		ZeroMemory(binds, sizeof(binds));
		ZeroMemory(filters, sizeof(filters));
		ZeroMemory(combines, sizeof(combines));
		ZeroMemory(combineRedundant, sizeof(combineRedundant));
		ZeroMemory(environments, sizeof(environments));
		ZeroMemory(transforms, sizeof(transforms));
		ZeroMemory(matrixCalls, sizeof(matrixCalls));
	}
};

static RS2StageAuditState &RS2StageAuditGetState(){
	static RS2StageAuditState *state = new RS2StageAuditState;
	return *state;
}

bool RS2StageAuditEnabled(){
	static int enabled = -1;

	if(enabled<0){
		enabled = CheckArguments("-stageaudit") ? 1 : 0;
		if(enabled) Debug("RS2STAGEAUDIT|begin\n");
	}
	return enabled!=0;
}

static void RS2StageAuditEvent(const char *text){
	RS2StageAuditState &s = RS2StageAuditGetState();

	if(s.sequence.size()<96) s.sequence.push_back(text);
}

static bool RS2StageAuditStage(unsigned int stage){
	if(stage<RS2_AUDIT_STAGES) return true;
	RS2StageAuditGetState().otherStageCalls++;
	return false;
}

static std::string RS2StageAuditSource(const CRS2TextureResource *texture){
	if(!texture) return "(none)";

	std::map<const CRS2TextureResource *, std::string>::const_iterator it =
		RS2StageAuditGetState().sources.find(texture);

	return it==RS2StageAuditGetState().sources.end() ? "(unknown)" : it->second;
}

void RS2StageAuditTextureCreated(const CRS2TextureResource *texture, const char *source,
	bool fromResource){
	if(!RS2StageAuditEnabled() || !texture) return;

	char text[640];

	_snprintf(text, sizeof(text)-1, "%s%s %dx%d", fromResource ? "resource:" : "",
		source ? source : "?", texture->GetWidth(), texture->GetHeight());
	text[sizeof(text)-1] = 0;
	RS2StageAuditGetState().sources[texture] = text;
}

void RS2StageAuditTextureDestroyed(const CRS2TextureResource *texture){
	if(!RS2StageAuditEnabled() || !texture) return;
	RS2StageAuditGetState().sources.erase(texture);
}

void RS2StageAuditBind(unsigned int stage, const CRS2TextureResource *texture){
	if(!RS2StageAuditEnabled() || !RS2StageAuditStage(stage)) return;
	RS2StageAuditState &s = RS2StageAuditGetState();

	s.binds[stage][texture ? 1 : 0]++;
	s.stage[stage].texture = texture;
	if(texture) s.boundSources[stage][RS2StageAuditSource(texture)]++;
	if(stage>0){
		char text[64];

		sprintf(text, "bind %u %s", stage, texture ? "texture" : "none");
		RS2StageAuditEvent(text);
	}
}

void RS2StageAuditFilter(unsigned int stage, RS2TextureFilter filter){
	if(!RS2StageAuditEnabled() || !RS2StageAuditStage(stage)) return;
	RS2StageAuditState &s = RS2StageAuditGetState();

	if(filter==RS2_FILTER_POINT || filter==RS2_FILTER_LINEAR)
		s.filters[stage][filter==RS2_FILTER_LINEAR ? 1 : 0]++;
	else s.otherFilters++;
	s.stage[stage].filter = (int)filter;
	if(stage>0){
		char text[64];

		sprintf(text, "filter %u %s", stage, filter==RS2_FILTER_LINEAR ? "linear" : "point");
		RS2StageAuditEvent(text);
	}
}

void RS2StageAuditCombine(unsigned int stage, bool enable){
	if(!RS2StageAuditEnabled() || !RS2StageAuditStage(stage)) return;
	RS2StageAuditState &s = RS2StageAuditGetState();
	char text[64];

	s.combines[stage][enable ? 1 : 0]++;
	if(s.stage[stage].combine==(enable ? 1 : 0)) s.combineRedundant[stage]++;
	s.stage[stage].combine = enable ? 1 : 0;
	sprintf(text, "combine %u %s", stage, enable ? "on" : "off");
	RS2StageAuditEvent(text);
}

void RS2StageAuditEnvironment(unsigned int stage, bool enable){
	if(!RS2StageAuditEnabled() || !RS2StageAuditStage(stage)) return;
	RS2StageAuditState &s = RS2StageAuditGetState();
	char text[64];

	s.environments[stage][enable ? 1 : 0]++;
	s.stage[stage].environment = enable ? 1 : 0;
	sprintf(text, "environment %u %s", stage, enable ? "on" : "off");
	RS2StageAuditEvent(text);
}

static std::string RS2StageAuditMatrix(const float *m){
	char text[400];

	if(!m) return "(null)";
	sprintf(text, "%g,%g,%g,%g|%g,%g,%g,%g|%g,%g,%g,%g|%g,%g,%g,%g",
		m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
		m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
	return text;
}

void RS2StageAuditUVMatrix(unsigned int stage, const float *matrix){
	if(!RS2StageAuditEnabled() || !RS2StageAuditStage(stage)) return;
	RS2StageAuditState &s = RS2StageAuditGetState();
	const std::string m = RS2StageAuditMatrix(matrix);
	char text[480];

	s.matrixCalls[stage]++;
	s.matrices[stage][m]++;
	s.stage[stage].matrix = m;
	_snprintf(text, sizeof(text)-1, "uvmatrix %u %s", stage, m.c_str());
	text[sizeof(text)-1] = 0;
	RS2StageAuditEvent(text);
}

void RS2StageAuditUVTransform(unsigned int stage, bool enable){
	if(!RS2StageAuditEnabled() || !RS2StageAuditStage(stage)) return;
	RS2StageAuditState &s = RS2StageAuditGetState();
	char text[64];

	s.transforms[stage][enable ? 1 : 0]++;
	s.stage[stage].transform = enable ? 1 : 0;
	sprintf(text, "uvtransform %u %s", stage, enable ? "on" : "off");
	RS2StageAuditEvent(text);
}

void RS2StageAuditLighting(bool enable){
	if(!RS2StageAuditEnabled()) return;
	RS2StageAuditGetState().lighting = enable ? 1 : 0;
}

void RS2StageAuditAlphaTest(bool enable){
	if(!RS2StageAuditEnabled()) return;
	RS2StageAuditGetState().alphaTest = enable ? 1 : 0;
}

void RS2StageAuditBlend(RS2BlendMode mode){
	if(!RS2StageAuditEnabled()) return;
	RS2StageAuditGetState().blend = (int)mode;
}

void RS2StageAuditGeometryCreated(
	const CRS2GeometryResource *geometry, const RS2MeshVertexLayout &layout){
	if(!RS2StageAuditEnabled() || !geometry) return;
	RS2StageAuditLayout l;

	l.normal = layout.HasNormal();
	l.diffuse = layout.HasDiffuse();
	l.screen = layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED;
	l.texcoords = layout.texCoordCount;
	RS2StageAuditGetState().geometry[geometry] = l;
}

void RS2StageAuditGeometryDestroyed(const CRS2GeometryResource *geometry){
	if(!RS2StageAuditEnabled() || !geometry) return;
	RS2StageAuditGetState().geometry.erase(geometry);
}

static const char *RS2StageAuditTri(int v){
	return v<0 ? "default" : (v ? "on" : "off");
}

static void RS2StageAuditDraw(const RS2StageAuditLayout &l, const char *kind){
	RS2StageAuditState &s = RS2StageAuditGetState();
	const RS2StageAuditStageState &s0 = s.stage[0];
	const RS2StageAuditStageState &s1 = s.stage[1];
	const bool stage1 = s1.combine==1;
	char key[512];

	s.draws++;
	if(stage1){
		s.stage1Draws++;
		s.stage1DrawSources[RS2StageAuditSource(s1.texture)]++;
	}
	if(s1.environment==1 || s0.environment==1) s.environmentDraws++;
	if(s0.transform==1 || s1.transform==1){
		s.transformedDraws++;
		s.transformedDrawMatrices[s0.transform==1 ? s0.matrix : s1.matrix]++;
	}

	//	Only the draws where a stage beyond the base is doing something get a
	//	detailed key; everything else is summarised.
	if(!stage1 && s1.environment!=1 && s0.transform!=1 && s1.transform!=1
			&& s0.environment!=1){
		sprintf(key, "plain %s", kind);
	}else{
		sprintf(key, "%s uv=%u normal=%d diffuse=%d screen=%d lit=%s atest=%s blend=%d"
			" | s0 tex=%d xform=%s env=%s filter=%d"
			" | s1 tex=%d combine=%s env=%s xform=%s filter=%d",
			kind, l.texcoords, l.normal ? 1 : 0, l.diffuse ? 1 : 0, l.screen ? 1 : 0,
			RS2StageAuditTri(s.lighting), RS2StageAuditTri(s.alphaTest), s.blend,
			s0.texture ? 1 : 0, RS2StageAuditTri(s0.transform),
			RS2StageAuditTri(s0.environment), s0.filter,
			s1.texture ? 1 : 0, RS2StageAuditTri(s1.combine),
			RS2StageAuditTri(s1.environment), RS2StageAuditTri(s1.transform), s1.filter);
	}
	s.drawKeys[key]++;
}

void RS2StageAuditDrawImmediate(const RS2MeshVertexLayout &layout){
	if(!RS2StageAuditEnabled()) return;
	RS2StageAuditLayout l;

	l.normal = layout.HasNormal();
	l.diffuse = layout.HasDiffuse();
	l.screen = layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED;
	l.texcoords = layout.texCoordCount;
	RS2StageAuditDraw(l, "immediate");
}

void RS2StageAuditDrawGeometry(const CRS2GeometryResource *geometry, bool indexed){
	if(!RS2StageAuditEnabled()) return;
	RS2StageAuditState &s = RS2StageAuditGetState();
	std::map<const CRS2GeometryResource *, RS2StageAuditLayout>::const_iterator it =
		s.geometry.find(geometry);

	if(it==s.geometry.end()){
		s.unknownGeometry++;
		return;
	}
	RS2StageAuditDraw(it->second, indexed ? "indexed" : "buffered");
}

static void RS2StageAuditDumpMap(const char *tag, const std::map<std::string, unsigned int> &m){
	std::map<std::string, unsigned int>::const_iterator it;

	for(it = m.begin(); it!=m.end(); ++it)
		Debug("RS2STAGEAUDIT|%s|%u|%s\n", tag, it->second, it->first.c_str());
}

void RS2StageAuditDump(){
	if(!RS2StageAuditEnabled()) return;
	RS2StageAuditState &s = RS2StageAuditGetState();
	unsigned int i;

	if(s.dumped) return;
	s.dumped = true;

	for(i = 0; i<s.sequence.size(); i++)
		Debug("RS2STAGEAUDIT|sequence|%u|%s\n", i, s.sequence[i].c_str());

	for(i = 0; i<RS2_AUDIT_STAGES; i++){
		Debug("RS2STAGEAUDIT|stage|%u|bind=%u|unbind=%u|point=%u|linear=%u"
			"|combine_on=%u|combine_off=%u|combine_redundant=%u|env_on=%u|env_off=%u"
			"|xform_on=%u|xform_off=%u|matrix=%u\n", i,
			s.binds[i][1], s.binds[i][0], s.filters[i][0], s.filters[i][1],
			s.combines[i][1], s.combines[i][0], s.combineRedundant[i],
			s.environments[i][1], s.environments[i][0],
			s.transforms[i][1], s.transforms[i][0], s.matrixCalls[i]);
	}
	Debug("RS2STAGEAUDIT|other|stage_calls=%u|filters=%u\n", s.otherStageCalls, s.otherFilters);

	for(i = 1; i<RS2_AUDIT_STAGES; i++){
		char tag[32];

		sprintf(tag, "bound%u", i);
		RS2StageAuditDumpMap(tag, s.boundSources[i]);
	}
	for(i = 0; i<RS2_AUDIT_STAGES; i++){
		char tag[32];

		sprintf(tag, "matrix%u", i);
		RS2StageAuditDumpMap(tag, s.matrices[i]);
	}
	Debug("RS2STAGEAUDIT|draws|total=%u|stage1=%u|environment=%u|transformed=%u|unknown_geometry=%u\n",
		s.draws, s.stage1Draws, s.environmentDraws, s.transformedDraws, s.unknownGeometry);
	RS2StageAuditDumpMap("stage1source", s.stage1DrawSources);
	RS2StageAuditDumpMap("drawmatrix", s.transformedDrawMatrices);
	RS2StageAuditDumpMap("drawkey", s.drawKeys);
	Debug("RS2STAGEAUDIT|end\n");
}
