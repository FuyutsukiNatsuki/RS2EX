//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	See RS2TextureAudit.h.  This deliberately records observations rather than
//	changing loader, cache or render-state policy.

#include "stdafx.h"
#include "RS2TextureAudit.h"

#include <map>
#include <string>

struct RS2TextureAuditState
{
	unsigned int attempts;
	unsigned int successes;
	unsigned int failures;
	unsigned int fileAttempts;
	unsigned int fileSuccesses;
	unsigned int resourceAttempts;
	unsigned int resourceSuccesses;
	unsigned int mutableAttempts;
	unsigned int mutableSuccesses;
	unsigned int releases;
	unsigned int live;
	unsigned int peak;
	unsigned int binds[2][2];	//	stage 0/1, empty/texture
	unsigned int otherBinds[2];
	unsigned int filters[2][2];	//	stage 0/1, point/linear
	unsigned int otherFilters[2];
	unsigned int alphaTest[2];
	unsigned int lockAttempts;
	unsigned int lockSuccesses;
	unsigned int unlocks;
	bool dumped;
	std::map<std::string, unsigned int> sources;
	std::map<unsigned long, unsigned int> colourKeys;
	std::map<int, unsigned int> mipArguments;
	std::map<unsigned int, unsigned int> actualMipCounts;
	std::map<unsigned int, unsigned int> alphaRefs;
	std::map<unsigned int, unsigned int> alphaFuncs;
	std::map<unsigned int, unsigned int> sourceFormats;
	std::map<unsigned int, unsigned int> actualFormats;
	std::map<unsigned int, unsigned int> imageFileFormats;

	RS2TextureAuditState()
		: attempts(0), successes(0), failures(0),
		  fileAttempts(0), fileSuccesses(0),
		  resourceAttempts(0), resourceSuccesses(0),
		  mutableAttempts(0), mutableSuccesses(0),
		  releases(0), live(0), peak(0),
		  lockAttempts(0), lockSuccesses(0), unlocks(0), dumped(false)
	{
		ZeroMemory(binds, sizeof(binds));
		ZeroMemory(otherBinds, sizeof(otherBinds));
		ZeroMemory(filters, sizeof(filters));
		ZeroMemory(otherFilters, sizeof(otherFilters));
		ZeroMemory(alphaTest, sizeof(alphaTest));
	}
};

static RS2TextureAuditState &RS2TextureAuditGetState(){
	//	The renderer explicitly dumps this before shutdown.  Keeping the state
	//	alive avoids making static-destruction order part of the audit contract.
	static RS2TextureAuditState *state = new RS2TextureAuditState;
	return *state;
}

bool RS2TextureAuditEnabled(){
	static int enabled = -1;

	if(enabled<0){
		enabled = CheckArguments("-textureaudit") ? 1 : 0;
		if(enabled) Debug("RS2TEXAUDIT|begin|backend=d3d8\n");
	}
	return enabled!=0;
}

template<class K>
static void RS2TextureAuditCount(std::map<K, unsigned int> &counts, const K &key){
	typename std::map<K, unsigned int>::iterator found = counts.find(key);
	if(found==counts.end()) counts.insert(std::make_pair(key, 1u));
	else found->second++;
}

static std::string RS2TextureAuditSourceLabel(const char *kind, const char *source){
	if(!strcmp(kind, "mutable")) return "mutable";

	if(!source) return "null";
	if(IS_INTRESOURCE(source)){
		char number[24];
		_snprintf(number, sizeof(number)-1, "resource-%u",
			(unsigned int)(ULONG_PTR)source);
		number[sizeof(number)-1] = 0;
		return number;
	}

	if(!strcmp(kind, "file")){
		const char *slash = strrchr(source, '\\');
		const char *forward = strrchr(source, '/');
		if(forward && (!slash || forward>slash)) slash = forward;
		const char *dot = strrchr(slash ? slash+1 : source, '.');
		if(!dot) return "(none)";

		char extension[20];
		unsigned int i;
		for(i = 0; dot[i] && i<sizeof(extension)-1; i++)
			extension[i] = (char)tolower((unsigned char)dot[i]);
		extension[i] = 0;
		return extension;
	}

	char label[40];
	unsigned int i;
	for(i = 0; source[i] && i<sizeof(label)-1; i++){
		const unsigned char c = (unsigned char)source[i];
		label[i] = (c>=' ' && c<='~' && c!='|') ? (char)c : '_';
	}
	label[i] = 0;
	return label;
}

void RS2TextureAuditRecordCreate(
	const char *kind,
	const char *source,
	bool success,
	unsigned int requestedWidth,
	unsigned int requestedHeight,
	unsigned int sourceWidth,
	unsigned int sourceHeight,
	unsigned int actualWidth,
	unsigned int actualHeight,
	unsigned long colourKey,
	int mipArgument,
	unsigned int actualMipCount,
	unsigned int sourceFormat,
	unsigned int actualFormat,
	unsigned int imageFileFormat,
	unsigned int liveTextures
){
	if(!RS2TextureAuditEnabled()) return;

	RS2TextureAuditState &s = RS2TextureAuditGetState();
	const std::string label = RS2TextureAuditSourceLabel(kind, source);

	s.attempts++;
	if(success) s.successes++;
	else s.failures++;

	if(!strcmp(kind, "file")){
		s.fileAttempts++;
		if(success) s.fileSuccesses++;
	}else if(!strcmp(kind, "resource")){
		s.resourceAttempts++;
		if(success) s.resourceSuccesses++;
	}else{
		s.mutableAttempts++;
		if(success) s.mutableSuccesses++;
	}

	if(success){
		RS2TextureAuditCount(s.sources, label);
		RS2TextureAuditCount(s.actualMipCounts, actualMipCount);
		RS2TextureAuditCount(s.sourceFormats, sourceFormat);
		RS2TextureAuditCount(s.actualFormats, actualFormat);
		RS2TextureAuditCount(s.imageFileFormats, imageFileFormat);
	}
	RS2TextureAuditCount(s.colourKeys, colourKey);
	RS2TextureAuditCount(s.mipArguments, mipArgument);

	s.live = liveTextures;
	if(s.live>s.peak) s.peak = s.live;

	Debug("RS2TEXAUDIT|create|kind=%s|src=%s|ok=%u|req=%ux%u|source=%ux%u|out=%ux%u|key=%08lx|miparg=%d|mips=%u|sfmt=%u|afmt=%u|ifmt=%u|live=%u\n",
		kind, label.c_str(), success ? 1u : 0u,
		requestedWidth, requestedHeight, sourceWidth, sourceHeight,
		actualWidth, actualHeight, colourKey, mipArgument, actualMipCount,
		sourceFormat, actualFormat, imageFileFormat, liveTextures);
}

void RS2TextureAuditRecordRelease(unsigned int liveTextures){
	if(!RS2TextureAuditEnabled()) return;
	RS2TextureAuditState &s = RS2TextureAuditGetState();
	s.releases++;
	s.live = liveTextures;
}

void RS2TextureAuditRecordBind(unsigned int stage, bool hasTexture){
	if(!RS2TextureAuditEnabled()) return;
	RS2TextureAuditState &s = RS2TextureAuditGetState();
	const unsigned int texture = hasTexture ? 1u : 0u;
	if(stage<2) s.binds[stage][texture]++;
	else s.otherBinds[texture]++;
}

void RS2TextureAuditRecordFilter(unsigned int stage, bool linear){
	if(!RS2TextureAuditEnabled()) return;
	RS2TextureAuditState &s = RS2TextureAuditGetState();
	const unsigned int value = linear ? 1u : 0u;
	if(stage<2) s.filters[stage][value]++;
	else s.otherFilters[value]++;
}

void RS2TextureAuditRecordAlphaTest(bool enable){
	if(RS2TextureAuditEnabled())
		RS2TextureAuditGetState().alphaTest[enable ? 1 : 0]++;
}

void RS2TextureAuditRecordAlphaRef(unsigned int ref){
	if(RS2TextureAuditEnabled())
		RS2TextureAuditCount(RS2TextureAuditGetState().alphaRefs, ref);
}

void RS2TextureAuditRecordAlphaFunc(unsigned int func){
	if(RS2TextureAuditEnabled())
		RS2TextureAuditCount(RS2TextureAuditGetState().alphaFuncs, func);
}

void RS2TextureAuditRecordLock(bool success){
	if(!RS2TextureAuditEnabled()) return;
	RS2TextureAuditState &s = RS2TextureAuditGetState();
	s.lockAttempts++;
	if(success) s.lockSuccesses++;
}

void RS2TextureAuditRecordUnlock(){
	if(RS2TextureAuditEnabled()) RS2TextureAuditGetState().unlocks++;
}

void RS2TextureAuditDump(){
	if(!RS2TextureAuditEnabled()) return;

	RS2TextureAuditState &s = RS2TextureAuditGetState();
	if(s.dumped) return;
	s.dumped = true;

	Debug("RS2TEXAUDIT|summary|attempts=%u|success=%u|failed=%u|file=%u/%u|resource=%u/%u|mutable=%u/%u|release=%u|live=%u|peak=%u\n",
		s.attempts, s.successes, s.failures,
		s.fileSuccesses, s.fileAttempts,
		s.resourceSuccesses, s.resourceAttempts,
		s.mutableSuccesses, s.mutableAttempts,
		s.releases, s.live, s.peak);
	Debug("RS2TEXAUDIT|bind|s0_texture=%u|s0_empty=%u|s1_texture=%u|s1_empty=%u|other_texture=%u|other_empty=%u\n",
		s.binds[0][1], s.binds[0][0], s.binds[1][1], s.binds[1][0],
		s.otherBinds[1], s.otherBinds[0]);
	Debug("RS2TEXAUDIT|filter|s0_point=%u|s0_linear=%u|s1_point=%u|s1_linear=%u|other_point=%u|other_linear=%u\n",
		s.filters[0][0], s.filters[0][1], s.filters[1][0], s.filters[1][1],
		s.otherFilters[0], s.otherFilters[1]);
	Debug("RS2TEXAUDIT|alpha|disabled=%u|enabled=%u|lock=%u/%u|unlock=%u\n",
		s.alphaTest[0], s.alphaTest[1], s.lockSuccesses, s.lockAttempts, s.unlocks);

	std::map<std::string, unsigned int>::const_iterator text;
	for(text = s.sources.begin(); text!=s.sources.end(); ++text)
		Debug("RS2TEXAUDIT|source|value=%s|success=%u\n",
			text->first.c_str(), text->second);

	std::map<unsigned long, unsigned int>::const_iterator colour;
	for(colour = s.colourKeys.begin(); colour!=s.colourKeys.end(); ++colour)
		Debug("RS2TEXAUDIT|colourkey|value=%08lx|attempts=%u\n",
			colour->first, colour->second);

	std::map<int, unsigned int>::const_iterator signedValue;
	for(signedValue = s.mipArguments.begin(); signedValue!=s.mipArguments.end(); ++signedValue)
		Debug("RS2TEXAUDIT|miparg|value=%d|attempts=%u\n",
			signedValue->first, signedValue->second);

	std::map<unsigned int, unsigned int>::const_iterator value;
	for(value = s.actualMipCounts.begin(); value!=s.actualMipCounts.end(); ++value)
		Debug("RS2TEXAUDIT|mips|value=%u|success=%u\n", value->first, value->second);
	for(value = s.alphaRefs.begin(); value!=s.alphaRefs.end(); ++value)
		Debug("RS2TEXAUDIT|alpharef|value=%u|calls=%u\n", value->first, value->second);
	for(value = s.alphaFuncs.begin(); value!=s.alphaFuncs.end(); ++value)
		Debug("RS2TEXAUDIT|alphafunc|value=%u|calls=%u\n", value->first, value->second);
	for(value = s.sourceFormats.begin(); value!=s.sourceFormats.end(); ++value)
		Debug("RS2TEXAUDIT|sourceformat|value=%u|success=%u\n", value->first, value->second);
	for(value = s.actualFormats.begin(); value!=s.actualFormats.end(); ++value)
		Debug("RS2TEXAUDIT|actualformat|value=%u|success=%u\n", value->first, value->second);
	for(value = s.imageFileFormats.begin(); value!=s.imageFileFormats.end(); ++value)
		Debug("RS2TEXAUDIT|imageformat|value=%u|success=%u\n", value->first, value->second);

	Debug("RS2TEXAUDIT|end\n");
}
