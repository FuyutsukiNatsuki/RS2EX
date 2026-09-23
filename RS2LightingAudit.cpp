//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	See RS2LightingAudit.h.  Records observations; never changes a value.

#include "stdafx.h"
#include "RS2LightingAudit.h"

#include <map>
#include <string>
#include <vector>
#include <math.h>
#include <stdio.h>

//	What a world matrix does to a normal.  Only the upper 3x3 matters, and the
//	engine builds its matrices for the row-vector convention, so the rows are
//	the transformed basis vectors.
enum RS2AuditWorldClass
{
	RS2_AUDIT_WORLD_UNIT,		//	rotation only, lengths 1
	RS2_AUDIT_WORLD_UNIFORM,	//	rotation and one scale factor
	RS2_AUDIT_WORLD_NONUNIFORM,	//	orthogonal axes, different lengths
	RS2_AUDIT_WORLD_SKEW,		//	axes not orthogonal
	RS2_AUDIT_WORLD_DEGENERATE,	//	an axis of (almost) zero length
	RS2_AUDIT_WORLD_COUNT
};

static const char *const RS2_AUDIT_WORLD_NAME[RS2_AUDIT_WORLD_COUNT] = {
	"unit", "uniform", "nonuniform", "skew", "degenerate"
};

//	Relative tolerance.  Matrices here come out of float arithmetic on angles,
//	so exact equality would call every rotated object "non-uniform".
static const float RS2_AUDIT_EPSILON = 1.0e-3f;

struct RS2AuditRange
{
	float lo[4], hi[4];
	bool seen;

	RS2AuditRange() : seen(false){}

	void Add(const float *v, int n){
		int i;

		for(i = 0; i<n; i++){
			if(!seen || v[i]<lo[i]) lo[i] = v[i];
			if(!seen || v[i]>hi[i]) hi[i] = v[i];
		}
		seen = true;
	}
};

struct RS2AuditLayout
{
	bool normal, diffuse, texcoord, screen;
};

struct RS2LightingAuditState
{
	//	Current state, as the engine last set it.  "set" false means the
	//	engine never set it and the backend's own default is in force.
	bool lighting, lightingSet;
	bool specular, specularSet;
	RS2ColorSource diffuseSource, ambientSource;
	bool diffuseSourceSet, ambientSourceSet;
	RS2PackedColor ambient;
	bool ambientSet;
	bool texture0;
	RS2AuditWorldClass world;
	bool mirrored;
	RS2Material material;
	bool materialSet;

	//	Call counts.
	unsigned int materialCalls;
	unsigned int lightingCalls[2], lightingTransitions;
	unsigned int specularCalls[2];
	unsigned int diffuseSourceCalls[2], ambientSourceCalls[2];
	unsigned int normalizeCalls[2];
	unsigned int ambientCalls, lightCalls, worldCalls;
	unsigned int worldClassCalls[RS2_AUDIT_WORLD_COUNT];
	unsigned int mirroredWorldCalls;

	//	Material values.
	RS2AuditRange diffuse, ambientRange, specularRange, emissive, power;
	unsigned int outOfRange[4];	//	diffuse, ambient, specular, emissive
	unsigned int negative[4];
	std::map<std::string, unsigned int> materials;

	//	Light values.
	std::map<RS2PackedColor, unsigned int> ambients;
	std::map<std::string, unsigned int> lights;

	//	Draws.
	unsigned int draws, litDraws, unlitDraws;
	unsigned int litNormalDraws, litNoNormalDraws;
	unsigned int litEmissiveDraws, litSpecularCandidateDraws;
	unsigned int litNonUniformNormalDraws;
	std::map<std::string, unsigned int> drawKeys;
	std::map<std::string, unsigned int> litMaterials;
	std::map<std::string, float> nonUniformRatio;

	std::map<const CRS2GeometryResource *, RS2AuditLayout> geometry;
	unsigned int unknownGeometryDraws;

	//	The order the first state arrives in, so the initial and inherited
	//	state can be read off rather than reconstructed.
	std::vector<std::string> sequence;
	bool firstDrawLogged;
	bool dumped;

	float worldRatio;

	RS2LightingAuditState()
		: lighting(false), lightingSet(false),
		  specular(false), specularSet(false),
		  diffuseSource(RS2_COLOR_FROM_VERTEX), ambientSource(RS2_COLOR_FROM_MATERIAL),
		  diffuseSourceSet(false), ambientSourceSet(false),
		  ambient(0), ambientSet(false), texture0(false),
		  world(RS2_AUDIT_WORLD_UNIT), mirrored(false), materialSet(false),
		  materialCalls(0), lightingTransitions(0),
		  ambientCalls(0), lightCalls(0), worldCalls(0), mirroredWorldCalls(0),
		  draws(0), litDraws(0), unlitDraws(0),
		  litNormalDraws(0), litNoNormalDraws(0),
		  litEmissiveDraws(0), litSpecularCandidateDraws(0),
		  litNonUniformNormalDraws(0), unknownGeometryDraws(0),
		  firstDrawLogged(false), dumped(false), worldRatio(1.0f)
	{
		ZeroMemory(lightingCalls, sizeof(lightingCalls));
		ZeroMemory(specularCalls, sizeof(specularCalls));
		ZeroMemory(diffuseSourceCalls, sizeof(diffuseSourceCalls));
		ZeroMemory(ambientSourceCalls, sizeof(ambientSourceCalls));
		ZeroMemory(normalizeCalls, sizeof(normalizeCalls));
		ZeroMemory(worldClassCalls, sizeof(worldClassCalls));
		ZeroMemory(outOfRange, sizeof(outOfRange));
		ZeroMemory(negative, sizeof(negative));
		ZeroMemory(&material, sizeof(material));
	}
};

static RS2LightingAuditState &RS2LightingAuditGetState(){
	//	Dumped explicitly before shutdown, so static-destruction order is not
	//	part of the audit.
	static RS2LightingAuditState *state = new RS2LightingAuditState;
	return *state;
}

bool RS2LightingAuditEnabled(){
	static int enabled = -1;

	if(enabled<0){
		enabled = CheckArguments("-lightingaudit") ? 1 : 0;
		if(enabled) Debug("RS2LIGHTAUDIT|begin\n");
	}
	return enabled!=0;
}

static void RS2LightingAuditSequence(const char *text){
	RS2LightingAuditState &s = RS2LightingAuditGetState();

	if(s.sequence.size()<64 && !s.firstDrawLogged) s.sequence.push_back(text);
}

static std::string RS2LightingAuditFormatMaterial(const RS2Material &m){
	char text[512];

	sprintf(text,
		"D=%g,%g,%g,%g A=%g,%g,%g,%g S=%g,%g,%g,%g E=%g,%g,%g,%g P=%g",
		m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a,
		m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Ambient.a,
		m.Specular.r, m.Specular.g, m.Specular.b, m.Specular.a,
		m.Emissive.r, m.Emissive.g, m.Emissive.b, m.Emissive.a,
		m.Power);
	return text;
}

static void RS2LightingAuditColourRange(
	const RS2Color4 &c, RS2AuditRange &range, unsigned int field){
	RS2LightingAuditState &s = RS2LightingAuditGetState();
	const float v[4] = { c.r, c.g, c.b, c.a };
	int i;
	bool out = false, neg = false;

	range.Add(v, 4);
	for(i = 0; i<4; i++){
		if(v[i]<0.0f || v[i]>1.0f) out = true;
		if(v[i]<0.0f) neg = true;
	}
	if(out) s.outOfRange[field]++;
	if(neg) s.negative[field]++;
}

void RS2LightingAuditMaterial(const RS2Material &material){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();

	s.materialCalls++;
	s.material = material;
	s.materialSet = true;

	RS2LightingAuditColourRange(material.Diffuse, s.diffuse, 0);
	RS2LightingAuditColourRange(material.Ambient, s.ambientRange, 1);
	RS2LightingAuditColourRange(material.Specular, s.specularRange, 2);
	RS2LightingAuditColourRange(material.Emissive, s.emissive, 3);
	s.power.Add(&material.Power, 1);

	s.materials[RS2LightingAuditFormatMaterial(material)]++;
	RS2LightingAuditSequence("material");
}

void RS2LightingAuditLighting(bool enable){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();

	s.lightingCalls[enable ? 1 : 0]++;
	if(s.lightingSet && s.lighting!=enable) s.lightingTransitions++;
	s.lighting = enable;
	s.lightingSet = true;
	RS2LightingAuditSequence(enable ? "lighting=on" : "lighting=off");
}

void RS2LightingAuditAmbient(RS2PackedColor color){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();
	char text[48];

	s.ambientCalls++;
	s.ambient = color;
	s.ambientSet = true;
	s.ambients[color]++;
	sprintf(text, "ambient=%08lx", (unsigned long)color);
	RS2LightingAuditSequence(text);
}

void RS2LightingAuditSpecular(bool enable){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();

	s.specularCalls[enable ? 1 : 0]++;
	s.specular = enable;
	s.specularSet = true;
	RS2LightingAuditSequence(enable ? "specular=on" : "specular=off");
}

void RS2LightingAuditDiffuseSource(RS2ColorSource source){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();
	const bool material = source==RS2_COLOR_FROM_MATERIAL;

	s.diffuseSourceCalls[material ? 1 : 0]++;
	s.diffuseSource = source;
	s.diffuseSourceSet = true;
	RS2LightingAuditSequence(material ? "diffusesource=material" : "diffusesource=vertex");
}

void RS2LightingAuditAmbientSource(RS2ColorSource source){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();
	const bool material = source==RS2_COLOR_FROM_MATERIAL;

	s.ambientSourceCalls[material ? 1 : 0]++;
	s.ambientSource = source;
	s.ambientSourceSet = true;
	RS2LightingAuditSequence(material ? "ambientsource=material" : "ambientsource=vertex");
}

void RS2LightingAuditNormalize(bool enable){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();

	s.normalizeCalls[enable ? 1 : 0]++;
	RS2LightingAuditSequence(enable ? "normalize=on" : "normalize=off");
}

void RS2LightingAuditDirectionalLight(const RS2DirectionalLight &light){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();
	char text[256];

	s.lightCalls++;
	sprintf(text, "dir=%.4f,%.4f,%.4f col=%.4f,%.4f,%.4f,%.4f",
		light.direction.x, light.direction.y, light.direction.z,
		light.color.r, light.color.g, light.color.b, light.color.a);
	s.lights[text]++;
	RS2LightingAuditSequence("light");
}

static float RS2LightingAuditLength(const float *row){
	return (float)sqrt(row[0]*row[0] + row[1]*row[1] + row[2]*row[2]);
}

void RS2LightingAuditWorld(const float *matrix){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();

	s.worldCalls++;

	if(!matrix){
		s.world = RS2_AUDIT_WORLD_UNIT;
		s.mirrored = false;
		s.worldRatio = 1.0f;
		s.worldClassCalls[s.world]++;
		return;
	}

	const float *r0 = matrix, *r1 = matrix+4, *r2 = matrix+8;
	const float l0 = RS2LightingAuditLength(r0);
	const float l1 = RS2LightingAuditLength(r1);
	const float l2 = RS2LightingAuditLength(r2);
	float lo = l0, hi = l0;

	if(l1<lo) lo = l1;
	if(l2<lo) lo = l2;
	if(l1>hi) hi = l1;
	if(l2>hi) hi = l2;

	const float det =
		r0[0]*(r1[1]*r2[2]-r1[2]*r2[1])
		- r0[1]*(r1[0]*r2[2]-r1[2]*r2[0])
		+ r0[2]*(r1[0]*r2[1]-r1[1]*r2[0]);

	s.mirrored = det<0.0f;
	if(s.mirrored) s.mirroredWorldCalls++;
	s.worldRatio = 1.0f;

	if(lo<RS2_AUDIT_EPSILON){
		s.world = RS2_AUDIT_WORLD_DEGENERATE;
	}else{
		const float d01 = (r0[0]*r1[0]+r0[1]*r1[1]+r0[2]*r1[2])/(l0*l1);
		const float d02 = (r0[0]*r2[0]+r0[1]*r2[1]+r0[2]*r2[2])/(l0*l2);
		const float d12 = (r1[0]*r2[0]+r1[1]*r2[1]+r1[2]*r2[2])/(l1*l2);

		if(fabs(d01)>RS2_AUDIT_EPSILON || fabs(d02)>RS2_AUDIT_EPSILON
			|| fabs(d12)>RS2_AUDIT_EPSILON){
			s.world = RS2_AUDIT_WORLD_SKEW;
		}else if(hi/lo-1.0f>RS2_AUDIT_EPSILON){
			s.world = RS2_AUDIT_WORLD_NONUNIFORM;
			s.worldRatio = hi/lo;
		}else if(fabs(l0-1.0f)>RS2_AUDIT_EPSILON){
			s.world = RS2_AUDIT_WORLD_UNIFORM;
		}else{
			s.world = RS2_AUDIT_WORLD_UNIT;
		}
	}
	s.worldClassCalls[s.world]++;
}

void RS2LightingAuditTexture(unsigned int stage, bool bound){
	if(!RS2LightingAuditEnabled()) return;
	if(stage==0) RS2LightingAuditGetState().texture0 = bound;
}

void RS2LightingAuditGeometryCreated(
	const CRS2GeometryResource *geometry, const RS2MeshVertexLayout &layout){
	if(!RS2LightingAuditEnabled() || !geometry) return;
	RS2AuditLayout l;

	l.normal = layout.HasNormal();
	l.diffuse = layout.HasDiffuse();
	l.texcoord = layout.texCoordCount>0;
	l.screen = layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED;
	RS2LightingAuditGetState().geometry[geometry] = l;
}

void RS2LightingAuditGeometryDestroyed(const CRS2GeometryResource *geometry){
	if(!RS2LightingAuditEnabled() || !geometry) return;
	RS2LightingAuditGetState().geometry.erase(geometry);
}

static const char *RS2LightingAuditSourceName(bool set, RS2ColorSource source){
	if(!set) return "default";
	return source==RS2_COLOR_FROM_MATERIAL ? "material" : "vertex";
}

static bool RS2LightingAuditNonZero(const RS2Color4 &c){
	return c.r!=0.0f || c.g!=0.0f || c.b!=0.0f;
}

static void RS2LightingAuditDraw(const RS2AuditLayout &l, const char *kind){
	RS2LightingAuditState &s = RS2LightingAuditGetState();

	//	Lighting that was never set is whatever the backend starts with.  The
	//	audit reports it separately rather than assuming a value.
	const bool lit = s.lightingSet ? s.lighting : true;
	char key[256];

	if(!s.firstDrawLogged){
		char text[256];

		sprintf(text, "firstdraw lighting=%s specular=%s diffusesource=%s ambientsource=%s"
			" ambient=%s material=%s",
			s.lightingSet ? (s.lighting ? "on" : "off") : "default",
			s.specularSet ? (s.specular ? "on" : "off") : "default",
			RS2LightingAuditSourceName(s.diffuseSourceSet, s.diffuseSource),
			RS2LightingAuditSourceName(s.ambientSourceSet, s.ambientSource),
			s.ambientSet ? "set" : "default",
			s.materialSet ? "set" : "default");
		s.sequence.push_back(text);
		s.firstDrawLogged = true;
	}

	s.draws++;
	if(l.screen || !lit) s.unlitDraws++;
	else{
		s.litDraws++;
		if(l.normal) s.litNormalDraws++;
		else s.litNoNormalDraws++;

		if(l.normal && (s.world==RS2_AUDIT_WORLD_NONUNIFORM || s.world==RS2_AUDIT_WORLD_SKEW)){
			s.litNonUniformNormalDraws++;
			char ratio[32];

			sprintf(ratio, "%s", RS2_AUDIT_WORLD_NAME[s.world]);
			float &worst = s.nonUniformRatio[ratio];
			if(s.worldRatio>worst) worst = s.worldRatio;
		}

		if(s.materialSet){
			if(RS2LightingAuditNonZero(s.material.Emissive)) s.litEmissiveDraws++;
			if(l.normal && (s.specularSet ? s.specular : true)
				&& RS2LightingAuditNonZero(s.material.Specular))
				s.litSpecularCandidateDraws++;
			s.litMaterials[RS2LightingAuditFormatMaterial(s.material)]++;
		}
	}

	//	Screen-space vertices bypass fixed-function lighting in Direct3D 8
	//	whatever the render state says, so they are keyed on their own.
	sprintf(key, "%s lit=%s normal=%d diffuse=%d uv=%d screen=%d tex0=%d"
		" dsrc=%s asrc=%s spec=%s world=%s%s",
		kind,
		s.lightingSet ? (s.lighting ? "on" : "off") : "default",
		l.normal ? 1 : 0, l.diffuse ? 1 : 0, l.texcoord ? 1 : 0, l.screen ? 1 : 0,
		s.texture0 ? 1 : 0,
		RS2LightingAuditSourceName(s.diffuseSourceSet, s.diffuseSource),
		RS2LightingAuditSourceName(s.ambientSourceSet, s.ambientSource),
		s.specularSet ? (s.specular ? "on" : "off") : "default",
		RS2_AUDIT_WORLD_NAME[s.world], s.mirrored ? "/mirror" : "");
	s.drawKeys[key]++;
}

void RS2LightingAuditDrawImmediate(
	const RS2MeshVertexLayout &layout, RS2PrimitiveType primitive, unsigned int vertexCount){
	if(!RS2LightingAuditEnabled()) return;
	RS2AuditLayout l;

	(void)primitive;
	(void)vertexCount;
	l.normal = layout.HasNormal();
	l.diffuse = layout.HasDiffuse();
	l.texcoord = layout.texCoordCount>0;
	l.screen = layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED;
	RS2LightingAuditDraw(l, "immediate");
}

void RS2LightingAuditDrawGeometry(
	const CRS2GeometryResource *geometry, RS2PrimitiveType primitive,
	unsigned int count, bool indexed){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();
	std::map<const CRS2GeometryResource *, RS2AuditLayout>::const_iterator it =
		s.geometry.find(geometry);

	(void)primitive;
	(void)count;
	if(it==s.geometry.end()){
		s.unknownGeometryDraws++;
		return;
	}
	RS2LightingAuditDraw(it->second, indexed ? "indexed" : "buffered");
}

static void RS2LightingAuditDumpRange(const char *name, const RS2AuditRange &r, int n){
	if(!r.seen){
		Debug("RS2LIGHTAUDIT|range|%s|none\n", name);
		return;
	}
	if(n==1){
		Debug("RS2LIGHTAUDIT|range|%s|%g..%g\n", name, r.lo[0], r.hi[0]);
		return;
	}
	Debug("RS2LIGHTAUDIT|range|%s|r=%g..%g|g=%g..%g|b=%g..%g|a=%g..%g\n", name,
		r.lo[0], r.hi[0], r.lo[1], r.hi[1], r.lo[2], r.hi[2], r.lo[3], r.hi[3]);
}

void RS2LightingAuditDump(){
	if(!RS2LightingAuditEnabled()) return;
	RS2LightingAuditState &s = RS2LightingAuditGetState();
	unsigned int i;

	if(s.dumped) return;
	s.dumped = true;

	for(i = 0; i<s.sequence.size(); i++)
		Debug("RS2LIGHTAUDIT|sequence|%u|%s\n", i, s.sequence[i].c_str());

	Debug("RS2LIGHTAUDIT|calls|material=%u|lighting_on=%u|lighting_off=%u|transitions=%u"
		"|specular_on=%u|specular_off=%u|dsrc_vertex=%u|dsrc_material=%u"
		"|asrc_vertex=%u|asrc_material=%u|normalize_on=%u|normalize_off=%u"
		"|ambient=%u|light=%u|world=%u\n",
		s.materialCalls, s.lightingCalls[1], s.lightingCalls[0], s.lightingTransitions,
		s.specularCalls[1], s.specularCalls[0],
		s.diffuseSourceCalls[0], s.diffuseSourceCalls[1],
		s.ambientSourceCalls[0], s.ambientSourceCalls[1],
		s.normalizeCalls[1], s.normalizeCalls[0],
		s.ambientCalls, s.lightCalls, s.worldCalls);

	RS2LightingAuditDumpRange("diffuse", s.diffuse, 4);
	RS2LightingAuditDumpRange("ambient", s.ambientRange, 4);
	RS2LightingAuditDumpRange("specular", s.specularRange, 4);
	RS2LightingAuditDumpRange("emissive", s.emissive, 4);
	RS2LightingAuditDumpRange("power", s.power, 1);

	Debug("RS2LIGHTAUDIT|outofrange|diffuse=%u|ambient=%u|specular=%u|emissive=%u\n",
		s.outOfRange[0], s.outOfRange[1], s.outOfRange[2], s.outOfRange[3]);
	Debug("RS2LIGHTAUDIT|negative|diffuse=%u|ambient=%u|specular=%u|emissive=%u\n",
		s.negative[0], s.negative[1], s.negative[2], s.negative[3]);

	Debug("RS2LIGHTAUDIT|materials|unique=%u|lit_unique=%u\n",
		(unsigned int)s.materials.size(), (unsigned int)s.litMaterials.size());
	{
		std::map<std::string, unsigned int>::const_iterator it;

		for(it = s.materials.begin(); it!=s.materials.end(); ++it)
			Debug("RS2LIGHTAUDIT|material|%u|%s\n", it->second, it->first.c_str());
	}

	{
		std::map<RS2PackedColor, unsigned int>::const_iterator it;

		for(it = s.ambients.begin(); it!=s.ambients.end(); ++it)
			Debug("RS2LIGHTAUDIT|ambientvalue|%08lx|%u\n",
				(unsigned long)it->first, it->second);
	}
	{
		std::map<std::string, unsigned int>::const_iterator it;

		for(it = s.lights.begin(); it!=s.lights.end(); ++it)
			Debug("RS2LIGHTAUDIT|lightvalue|%u|%s\n", it->second, it->first.c_str());
	}

	for(i = 0; i<RS2_AUDIT_WORLD_COUNT; i++)
		Debug("RS2LIGHTAUDIT|worldclass|%s|%u\n", RS2_AUDIT_WORLD_NAME[i], s.worldClassCalls[i]);
	Debug("RS2LIGHTAUDIT|worldmirrored|%u\n", s.mirroredWorldCalls);

	Debug("RS2LIGHTAUDIT|draws|total=%u|lit=%u|unlit=%u|lit_normal=%u|lit_no_normal=%u"
		"|lit_emissive=%u|lit_specular_candidate=%u|lit_nonuniform_normal=%u|unknown_geometry=%u\n",
		s.draws, s.litDraws, s.unlitDraws, s.litNormalDraws, s.litNoNormalDraws,
		s.litEmissiveDraws, s.litSpecularCandidateDraws, s.litNonUniformNormalDraws,
		s.unknownGeometryDraws);
	{
		std::map<std::string, float>::const_iterator it;

		for(it = s.nonUniformRatio.begin(); it!=s.nonUniformRatio.end(); ++it)
			Debug("RS2LIGHTAUDIT|nonuniform|%s|worst_ratio=%g\n", it->first.c_str(), it->second);
	}
	{
		std::map<std::string, unsigned int>::const_iterator it;

		for(it = s.drawKeys.begin(); it!=s.drawKeys.end(); ++it)
			Debug("RS2LIGHTAUDIT|drawkey|%u|%s\n", it->second, it->first.c_str());
	}
	Debug("RS2LIGHTAUDIT|end\n");
}
