//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	See RS2ShadowAudit.h.  Records observations; never changes a value.

#include "stdafx.h"
#include "RS2ShadowAudit.h"

#include <map>
#include <string>
#include <vector>
#include <stdio.h>

static const char *const s_FieldNames[RS2_SA_FIELDS] = {
	"depthTest", "depthWrite", "depthFunc", "cull", "blend", "shade",
	"stencil", "func", "ref", "readMask", "writeMask", "fail", "depthFail", "pass",
	"disableFog", "baseCombine", "clearDepth"
};

//	The shadow sequences written out in full: the first, and one long after
//	it, so a sequence that changes once the scene has settled shows up.
static const unsigned int RS2_SA_SEQUENCE_A = 1;
static const unsigned int RS2_SA_SEQUENCE_B = 100;
static const size_t RS2_SA_SEQUENCE_MAX = 120;
static const unsigned int RS2_SA_TAIL = 12;	//	events kept after a sequence ends

struct RS2ShadowAuditState
{
	unsigned int current[RS2_SA_FIELDS];
	bool known[RS2_SA_FIELDS];			//	false: never set
	std::map<std::string, unsigned int> sets;
	std::map<std::string, unsigned int> drawKeys;	//	draws under stencil / preserve / flat
	std::map<std::string, unsigned int> drawVertices;
	std::map<std::string, unsigned int> overlayColours;
	std::map<unsigned int, unsigned int> sequencesPerPass;
	std::map<unsigned int, unsigned int> passesPerPresent;

	std::vector<std::string> recent;	//	before the first sequence
	std::vector<std::string> captured[2];
	std::string lastDraw;
	unsigned int lastDrawRepeat;

	unsigned int draws, stencilDraws, preserveDraws, flatDraws;
	unsigned int refChanges, enables, sequences, disablesInSequence, tail;
	unsigned int passes, presents, sequencesThisPass, passesThisPresent;
	bool capturing, firstEnable, dumped;
	int captureSlot;

	RS2ShadowAuditState()
		: lastDrawRepeat(0), draws(0), stencilDraws(0), preserveDraws(0), flatDraws(0),
		  refChanges(0), enables(0), sequences(0), disablesInSequence(0), tail(0),
		  passes(0), presents(0), sequencesThisPass(0), passesThisPresent(0),
		  capturing(false), firstEnable(true), dumped(false), captureSlot(-1)
	{
		for(int i = 0; i<RS2_SA_FIELDS; i++){
			current[i] = 0;
			known[i] = false;
		}
	}
};

static RS2ShadowAuditState &RS2ShadowAuditGetState(){
	static RS2ShadowAuditState *state = new RS2ShadowAuditState;
	return *state;
}

bool RS2ShadowAuditEnabled(){
	static int enabled = -1;

	if(enabled<0){
		enabled = CheckArguments("-shadowaudit") ? 1 : 0;
		if(enabled) Debug("RS2SHADOWAUDIT|begin\n");
	}
	return enabled!=0;
}

static std::string RS2ShadowAuditValue(RS2ShadowAuditField field, unsigned int value){
	char buffer[32];

	switch(field){
	case RS2_SA_DEPTH_FUNC:
	case RS2_SA_STENCIL_FUNC:
		switch(value){
		case RS2_COMPARE_ALWAYS: return "ALWAYS";
		case RS2_COMPARE_LESS_EQUAL: return "LESS_EQUAL";
		case RS2_COMPARE_GREATER: return "GREATER";
		}
		break;
	case RS2_SA_CULL:
		switch(value){
		case RS2_CULL_NONE: return "NONE";
		case RS2_CULL_COUNTER_CLOCKWISE: return "CCW";
		case RS2_CULL_CLOCKWISE: return "CW";
		}
		break;
	case RS2_SA_BLEND:
		switch(value){
		case RS2_BLEND_DISABLED: return "DISABLED";
		case RS2_BLEND_ALPHA: return "ALPHA";
		case RS2_BLEND_ALPHA_ADD: return "ALPHA_ADD";
		case RS2_BLEND_COLOR_PRESERVE: return "PRESERVE";
		}
		break;
	case RS2_SA_SHADE:
		return value==RS2_SHADE_FLAT ? "FLAT" : "GOURAUD";
	case RS2_SA_STENCIL_FAIL:
	case RS2_SA_STENCIL_DEPTH_FAIL:
	case RS2_SA_STENCIL_PASS:
		switch(value){
		case RS2_STENCIL_KEEP: return "KEEP";
		case RS2_STENCIL_INCREMENT: return "INCR";
		case RS2_STENCIL_DECREMENT: return "DECR";
		}
		break;
	case RS2_SA_STENCIL_READ_MASK:
	case RS2_SA_STENCIL_WRITE_MASK:
		_snprintf(buffer, sizeof(buffer), "0x%08x", value);
		buffer[sizeof(buffer)-1] = 0;
		return buffer;
	default:
		break;
	}
	_snprintf(buffer, sizeof(buffer), "%u", value);
	buffer[sizeof(buffer)-1] = 0;
	return buffer;
}

//	The state a draw happens under, in the order the shadow pass sets it.
static std::string RS2ShadowAuditStateKey(){
	const RS2ShadowAuditState &s = RS2ShadowAuditGetState();
	static const RS2ShadowAuditField keyed[] = {
		RS2_SA_STENCIL_TEST, RS2_SA_STENCIL_FUNC, RS2_SA_STENCIL_REF,
		RS2_SA_STENCIL_READ_MASK, RS2_SA_STENCIL_WRITE_MASK, RS2_SA_STENCIL_FAIL,
		RS2_SA_STENCIL_DEPTH_FAIL, RS2_SA_STENCIL_PASS, RS2_SA_DEPTH_TEST,
		RS2_SA_DEPTH_WRITE, RS2_SA_DEPTH_FUNC, RS2_SA_CULL, RS2_SA_BLEND, RS2_SA_SHADE
	};
	std::string key;

	for(size_t i = 0; i<sizeof(keyed)/sizeof(keyed[0]); i++){
		if(i) key += " ";
		key += s_FieldNames[keyed[i]];
		key += "=";
		key += s.known[keyed[i]] ? RS2ShadowAuditValue(keyed[i], s.current[keyed[i]]) : "unset";
	}
	return key;
}

/*
 *	Write one audit line.  Debug() formats into a 256-byte buffer, so a long
 *	line is split into numbered pieces rather than allowed to overrun it.
 */
static void RS2ShadowAuditLine(const std::string &head, const std::string &body){
	const size_t piece = 160;
	size_t at = 0;
	unsigned int part = 0;

	do{
		std::string chunk = body.substr(at, piece);

		for(size_t i = 0; i<chunk.size(); i++) if(chunk[i]=='%') chunk[i] = '?';
		Debug("RS2SHADOWAUDIT|%s|%s%s\n", head.c_str(), part ? "+" : "", chunk.c_str());
		at += piece;
		part++;
	}while(at<body.size());
}

static void RS2ShadowAuditFlushDraw(RS2ShadowAuditState &s, std::vector<std::string> &out){
	if(s.lastDraw.empty()) return;

	char buffer[32];

	_snprintf(buffer, sizeof(buffer), " x%u", s.lastDrawRepeat);
	buffer[sizeof(buffer)-1] = 0;
	if(out.size()<RS2_SA_SEQUENCE_MAX) out.push_back(s.lastDraw+buffer);
	s.lastDraw.clear();
	s.lastDrawRepeat = 0;
}

static void RS2ShadowAuditEvent(const std::string &text, bool draw){
	RS2ShadowAuditState &s = RS2ShadowAuditGetState();

	if(s.capturing && s.captureSlot>=0){
		std::vector<std::string> &out = s.captured[s.captureSlot];

		if(draw){
			if(text==s.lastDraw){
				s.lastDrawRepeat++;
				return;
			}
			RS2ShadowAuditFlushDraw(s, out);
			s.lastDraw = text;
			s.lastDrawRepeat = 1;
			return;
		}
		RS2ShadowAuditFlushDraw(s, out);
		if(out.size()<RS2_SA_SEQUENCE_MAX) out.push_back(text);
		if(s.disablesInSequence>=2 && s.tail++>=RS2_SA_TAIL) s.capturing = false;
		return;
	}
	if(s.sequences==0 && !draw){
		s.recent.push_back(text);
		if(s.recent.size()>10) s.recent.erase(s.recent.begin());
	}
}

void RS2ShadowAuditSet(RS2ShadowAuditField field, unsigned int value){
	if(!RS2ShadowAuditEnabled()) return;

	RS2ShadowAuditState &s = RS2ShadowAuditGetState();
	const unsigned int v = value;
	const std::string text = std::string(s_FieldNames[field])+"="+RS2ShadowAuditValue(field, v);

	s.sets[text]++;
	if(field==RS2_SA_STENCIL_REF && (!s.known[field] || s.current[field]!=v)) s.refChanges++;

	if(field==RS2_SA_STENCIL_TEST && value){
		//	CShadowVolume::Render() and Draw() enable the stencil once each,
		//	so the odd-numbered enables open a shadow sequence.  The set
		//	counts in the dump show whether anything else enables it.
		s.enables++;
		if(s.enables%2==1){
			s.sequences++;
			s.sequencesThisPass++;
			s.disablesInSequence = 0;
			if(s.sequences==RS2_SA_SEQUENCE_A || s.sequences==RS2_SA_SEQUENCE_B){
				s.captureSlot = s.sequences==RS2_SA_SEQUENCE_A ? 0 : 1;
				s.capturing = true;
				s.tail = 0;
				if(s.captureSlot==0) s.captured[0] = s.recent;
				s.captured[s.captureSlot].push_back("---- shadow sequence begins");
			}
		}
	}
	if(field==RS2_SA_STENCIL_TEST && !value) s.disablesInSequence++;

	if(field<RS2_SA_FOG_DISABLE){
		s.current[field] = v;
		s.known[field] = true;
	}
	RS2ShadowAuditEvent(text, false);
}

bool RS2ShadowAuditTakeFirstEnable(){
	if(!RS2ShadowAuditEnabled()) return false;

	RS2ShadowAuditState &s = RS2ShadowAuditGetState();

	if(!s.firstEnable) return false;
	s.firstEnable = false;
	return true;
}

static void RS2ShadowAuditDraw(const std::string &what, unsigned int count, const char *colour){
	RS2ShadowAuditState &s = RS2ShadowAuditGetState();
	const bool stencil = s.known[RS2_SA_STENCIL_TEST] && s.current[RS2_SA_STENCIL_TEST]==1;
	const bool preserve = s.known[RS2_SA_BLEND] && s.current[RS2_SA_BLEND]==RS2_BLEND_COLOR_PRESERVE;
	const bool flat = s.known[RS2_SA_SHADE] && s.current[RS2_SA_SHADE]==RS2_SHADE_FLAT;

	s.draws++;
	if(stencil) s.stencilDraws++;
	if(preserve) s.preserveDraws++;
	if(flat) s.flatDraws++;
	if(stencil || preserve || flat){
		const std::string key = what+" | "+RS2ShadowAuditStateKey();

		s.drawKeys[key]++;
		s.drawVertices[key] += count;
		if(stencil && colour) s.overlayColours[colour]++;
		RS2ShadowAuditEvent("draw "+what, true);
	}
}

void RS2ShadowAuditDrawImmediate(const RS2MeshVertexLayout &layout, const void *vertices,
	unsigned int vertexCount){
	if(!RS2ShadowAuditEnabled()) return;

	char what[96], colour[16];
	const char *c = 0;

	//	The overlay's colour, as its first vertex carries it (0xAARRGGBB).
	if(vertices && vertexCount && layout.HasDiffuse()){
		_snprintf(colour, sizeof(colour), "0x%08x",
			*(const unsigned int *)((const unsigned char *)vertices+layout.diffuseOffset));
		colour[sizeof(colour)-1] = 0;
		c = colour;
	}

	_snprintf(what, sizeof(what), "immediate %s%s%s tex=%u stride=%u",
		layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED ? "screen" : "pipeline",
		layout.HasNormal() ? " normal" : "", layout.HasDiffuse() ? " diffuse" : "",
		layout.texCoordCount, layout.stride);
	what[sizeof(what)-1] = 0;
	RS2ShadowAuditDraw(what, vertexCount, c);
}

void RS2ShadowAuditDrawGeometry(const CRS2GeometryResource *, unsigned int count, bool indexed){
	if(!RS2ShadowAuditEnabled()) return;
	RS2ShadowAuditDraw(indexed ? "indexed geometry" : "buffered geometry", count, 0);
}

void RS2ShadowAuditRenderPass(){
	if(!RS2ShadowAuditEnabled()) return;

	RS2ShadowAuditState &s = RS2ShadowAuditGetState();

	if(s.passes) s.sequencesPerPass[s.sequencesThisPass]++;
	s.sequencesThisPass = 0;
	s.passes++;
	s.passesThisPresent++;
}

void RS2ShadowAuditPresent(){
	if(!RS2ShadowAuditEnabled()) return;

	RS2ShadowAuditState &s = RS2ShadowAuditGetState();

	s.passesPerPresent[s.passesThisPresent]++;
	s.passesThisPresent = 0;
	s.presents++;
}

void RS2ShadowAuditDump(){
	if(!RS2ShadowAuditEnabled()) return;

	RS2ShadowAuditState &s = RS2ShadowAuditGetState();

	if(s.dumped) return;
	s.dumped = true;
	if(s.passes) s.sequencesPerPass[s.sequencesThisPass]++;

	Debug("RS2SHADOWAUDIT|draws=%u|stencilDraws=%u|preserveDraws=%u|flatDraws=%u"
		"|sequences=%u|refChanges=%u|passes=%u|presents=%u\n",
		s.draws, s.stencilDraws, s.preserveDraws, s.flatDraws, s.sequences,
		s.refChanges, s.passes, s.presents);

	std::map<unsigned int, unsigned int>::const_iterator h;

	for(h = s.sequencesPerPass.begin(); h!=s.sequencesPerPass.end(); ++h)
		Debug("RS2SHADOWAUDIT|sequencesPerPass|%u|passes=%u\n", h->first, h->second);
	for(h = s.passesPerPresent.begin(); h!=s.passesPerPresent.end(); ++h)
		Debug("RS2SHADOWAUDIT|passesPerPresent|%u|presents=%u\n", h->first, h->second);

	std::map<std::string, unsigned int>::const_iterator it;

	for(it = s.sets.begin(); it!=s.sets.end(); ++it)
		Debug("RS2SHADOWAUDIT|set|%s|%u\n", it->first.c_str(), it->second);
	for(it = s.overlayColours.begin(); it!=s.overlayColours.end(); ++it)
		Debug("RS2SHADOWAUDIT|stencilDrawColour|%s|%u\n", it->first.c_str(), it->second);
	for(it = s.drawKeys.begin(); it!=s.drawKeys.end(); ++it)
	{
		char head[64];

		_snprintf(head, sizeof(head), "drawkey|%u|vertices=%u", it->second,
			s.drawVertices[it->first]);
		head[sizeof(head)-1] = 0;
		RS2ShadowAuditLine(head, it->first);
	}
	for(int slot = 0; slot<2; slot++){
		for(size_t i = 0; i<s.captured[slot].size(); i++)
		{
			char head[48];

			_snprintf(head, sizeof(head), "sequence%u|%03u",
				slot ? RS2_SA_SEQUENCE_B : RS2_SA_SEQUENCE_A, (unsigned int)i);
			head[sizeof(head)-1] = 0;
			RS2ShadowAuditLine(head, s.captured[slot][i]);
		}
	}
	Debug("RS2SHADOWAUDIT|end\n");
}
