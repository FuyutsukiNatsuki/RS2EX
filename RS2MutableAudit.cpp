//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-25.
//
//	See RS2MutableAudit.h.  Records observations; never changes a value.

#include "stdafx.h"
#include "RS2MutableAudit.h"
#include "RS2TextureResource.h"

#include <map>
#include <string>
#include <vector>
#include <stdio.h>

struct RS2MARect
{
	int x0, y0, x1, y1;		//	x1 / y1 exclusive; empty when x0 >= x1
	bool Empty() const{ return x0>=x1 || y0>=y1; }
	bool Overlaps(const RS2MARect &o) const{
		return !Empty() && !o.Empty() && x0<o.x1 && o.x0<x1 && y0<o.y1 && o.y0<y1;
	}
};

struct RS2MATexture
{
	int requestedW, requestedH, w, h;
	unsigned int locks, unlocks, lockFailures, doubleLocks, unlockWithoutLock;
	unsigned int noChangeUnlocks, fullUpdates;
	unsigned long long dirtyBytes;		//	changed rectangle area x 2 bytes
	bool locked;
	const unsigned char *bits;
	int pitch;
	std::vector<unsigned char> before;	//	the surface as it was at Lock
	std::map<int, unsigned int> pitches;
	RS2MARect largest;
	unsigned int draws;
};

struct RS2MAEvent
{
	bool update;
	const CRS2TextureResource *texture;
	RS2MARect rect;
};

struct RS2MutableAuditState
{
	std::map<const CRS2TextureResource *, RS2MATexture> textures;
	const CRS2TextureResource *bound0;
	int filter0, blend, alphaTest, lighting;

	std::vector<RS2MAEvent> frame;		//	this frame's updates and draws
	std::map<unsigned int, unsigned int> updatesPerFrame;
	std::map<std::string, unsigned int> drawKeys;
	std::map<std::string, unsigned int> areaBuckets;
	std::vector<std::string> sequence;	//	a few frames with updates, in order
	unsigned int framesWithSequence;

	unsigned int frames, maxUpdatesInFrame;
	unsigned int drawReadsUpdateSameFrame;	//	draw samples a region updated earlier this frame
	unsigned int updateAfterDrawSameFrame;	//	update overwrites a region drawn earlier this frame
	unsigned int updatesWithoutDrawBetween;	//	back-to-back updates of one texture
	unsigned int destroyedLocked;
	int lastEventWasUpdateOf;

	std::map<int, unsigned int> fontSizes;
	unsigned int textDraws, textChars, heightQueries;
	std::map<int, unsigned int> heights;
	bool dumped;

	RS2MutableAuditState()
		: bound0(0), filter0(-1), blend(-1), alphaTest(-1), lighting(-1),
		  framesWithSequence(0), frames(0), maxUpdatesInFrame(0),
		  drawReadsUpdateSameFrame(0), updateAfterDrawSameFrame(0),
		  updatesWithoutDrawBetween(0), destroyedLocked(0), lastEventWasUpdateOf(0),
		  textDraws(0), textChars(0), heightQueries(0), dumped(false){}
};

static RS2MutableAuditState &RS2MAGet(){
	static RS2MutableAuditState *state = new RS2MutableAuditState;
	return *state;
}

bool RS2MutableAuditEnabled(){
	static int enabled = -1;

	if(enabled<0){
		enabled = CheckArguments("-mutableaudit") ? 1 : 0;
		if(enabled) Debug("RS2MUTABLEAUDIT|begin\n");
	}
	return enabled!=0;
}

static RS2MATexture *RS2MAFind(const CRS2TextureResource *texture){
	RS2MutableAuditState &s = RS2MAGet();
	std::map<const CRS2TextureResource *, RS2MATexture>::iterator it = s.textures.find(texture);

	return it==s.textures.end() ? 0 : &it->second;
}

bool RS2MutableAuditIsMutable(const CRS2TextureResource *texture){
	return RS2MutableAuditEnabled() && texture && RS2MAFind(texture)!=0;
}

void RS2MutableAuditCreated(const CRS2TextureResource *texture, int requestedW, int requestedH){
	if(!RS2MutableAuditEnabled()) return;
	if(!texture){
		Debug("RS2MUTABLEAUDIT|create|failed|%dx%d\n", requestedW, requestedH);
		return;
	}

	RS2MATexture t;

	ZeroMemory(&t.largest, sizeof(t.largest));
	t.requestedW = requestedW;
	t.requestedH = requestedH;
	t.w = texture->GetWidth();
	t.h = texture->GetHeight();
	t.locks = t.unlocks = t.lockFailures = t.doubleLocks = t.unlockWithoutLock = 0;
	t.noChangeUnlocks = t.fullUpdates = 0;
	t.dirtyBytes = 0;
	t.locked = false;
	t.bits = 0;
	t.pitch = 0;
	t.draws = 0;
	RS2MAGet().textures[texture] = t;
	Debug("RS2MUTABLEAUDIT|create|%p|requested=%dx%d|actual=%dx%d\n", (const void *)texture,
		requestedW, requestedH, t.w, t.h);
}

static void RS2MAReport(const CRS2TextureResource *texture, const RS2MATexture &t){
	Debug("RS2MUTABLEAUDIT|texture|%p|%dx%d|locks=%u|unlocks=%u|lockFailed=%u|doubleLock=%u"
		"|unlockWithoutLock=%u\n", (const void *)texture, t.w, t.h, t.locks, t.unlocks,
		t.lockFailures, t.doubleLocks, t.unlockWithoutLock);
	Debug("RS2MUTABLEAUDIT|texture|%p|unchanged=%u|full=%u|dirtyBytes=%llu|largest=%d,%d-%d,%d"
		"|draws=%u\n", (const void *)texture, t.noChangeUnlocks, t.fullUpdates, t.dirtyBytes,
		t.largest.x0, t.largest.y0, t.largest.x1, t.largest.y1, t.draws);

	std::map<int, unsigned int>::const_iterator p;

	for(p = t.pitches.begin(); p!=t.pitches.end(); ++p)
		Debug("RS2MUTABLEAUDIT|texture|%p|pitch=%d|locks=%u|tight=%d\n", (const void *)texture,
			p->first, p->second, t.w*2);
}

void RS2MutableAuditDestroyed(const CRS2TextureResource *texture){
	if(!RS2MutableAuditEnabled()) return;

	RS2MATexture *t = RS2MAFind(texture);

	if(!t) return;
	if(t->locked) RS2MAGet().destroyedLocked++;
	RS2MAReport(texture, *t);
	Debug("RS2MUTABLEAUDIT|destroy|%p\n", (const void *)texture);
	RS2MAGet().textures.erase(texture);
}

static void RS2MASequence(const std::string &text){
	RS2MutableAuditState &s = RS2MAGet();

	if(s.framesWithSequence<3 && s.sequence.size()<160) s.sequence.push_back(text);
}

void RS2MutableAuditLocked(const CRS2TextureResource *texture, bool ok, const void *bits, int pitch){
	if(!RS2MutableAuditEnabled()) return;

	RS2MATexture *t = RS2MAFind(texture);

	if(!t) return;
	if(!ok){
		t->lockFailures++;
		if(t->locked) t->doubleLocks++;
		return;
	}
	if(t->locked) t->doubleLocks++;
	t->locks++;
	t->locked = true;
	t->bits = (const unsigned char *)bits;
	t->pitch = pitch;
	t->pitches[pitch]++;
	t->before.assign(t->bits, t->bits+(size_t)pitch*t->h);
}

static RS2MARect RS2MADiff(const RS2MATexture &t){
	RS2MARect r = { t.w, t.h, 0, 0 };
	int x, y;

	for(y = 0; y<t.h; y++){
		const unsigned short *now = (const unsigned short *)(t.bits+(size_t)t.pitch*y);
		const unsigned short *was = (const unsigned short *)(&t.before[0]+(size_t)t.pitch*y);

		if(memcmp(now, was, (size_t)t.w*2)==0) continue;
		for(x = 0; x<t.w; x++){
			if(now[x]!=was[x]){
				if(x<r.x0) r.x0 = x;
				if(x+1>r.x1) r.x1 = x+1;
			}
		}
		if(y<r.y0) r.y0 = y;
		if(y+1>r.y1) r.y1 = y+1;
	}
	return r;
}

void RS2MutableAuditUnlocking(const CRS2TextureResource *texture){
	if(!RS2MutableAuditEnabled()) return;

	RS2MutableAuditState &s = RS2MAGet();
	RS2MATexture *t = RS2MAFind(texture);

	if(!t) return;
	if(!t->locked){
		t->unlockWithoutLock++;
		return;
	}
	t->unlocks++;
	t->locked = false;

	const RS2MARect r = RS2MADiff(*t);
	char text[128];

	if(r.Empty()){
		t->noChangeUnlocks++;
		RS2MASequence("update (no change)");
		return;
	}

	const int area = (r.x1-r.x0)*(r.y1-r.y0);

	t->dirtyBytes += (unsigned long long)area*2;
	if(r.x0==0 && r.y0==0 && r.x1==t->w && r.y1==t->h) t->fullUpdates++;
	if(area>(t->largest.x1-t->largest.x0)*(t->largest.y1-t->largest.y0)) t->largest = r;
	s.areaBuckets[area<=64*16 ? "<=64x16" : area<=128*16 ? "<=128x16" : area<=256*16
		? "<=256x16" : area<=512*32 ? "<=512x32" : ">512x32"]++;

	size_t i;
	bool drawnSince = false;

	for(i = 0; i<s.frame.size(); i++){
		const RS2MAEvent &e = s.frame[i];

		if(!e.update && e.texture==texture && e.rect.Overlaps(r)) drawnSince = true;
	}
	if(drawnSince) s.updateAfterDrawSameFrame++;
	if(s.lastEventWasUpdateOf && !s.frame.empty() && s.frame.back().update
			&& s.frame.back().texture==texture) s.updatesWithoutDrawBetween++;

	RS2MAEvent e;

	e.update = true;
	e.texture = texture;
	e.rect = r;
	s.frame.push_back(e);
	s.lastEventWasUpdateOf = 1;
	_snprintf(text, sizeof(text), "update %d,%d-%d,%d (%dx%d)%s", r.x0, r.y0, r.x1, r.y1,
		r.x1-r.x0, r.y1-r.y0, drawnSince ? " over a region drawn this frame" : "");
	text[sizeof(text)-1] = 0;
	RS2MASequence(text);
}

void RS2MutableAuditBind(unsigned int stage, const CRS2TextureResource *texture){
	if(!RS2MutableAuditEnabled() || stage!=0) return;
	RS2MAGet().bound0 = texture;
}

void RS2MutableAuditFilter(unsigned int stage, RS2TextureFilter filter){
	if(!RS2MutableAuditEnabled() || stage!=0) return;
	RS2MAGet().filter0 = (int)filter;
}

void RS2MutableAuditBlend(RS2BlendMode mode){
	if(RS2MutableAuditEnabled()) RS2MAGet().blend = (int)mode;
}

void RS2MutableAuditAlphaTest(bool enable){
	if(RS2MutableAuditEnabled()) RS2MAGet().alphaTest = enable ? 1 : 0;
}

void RS2MutableAuditLighting(bool enable){
	if(RS2MutableAuditEnabled()) RS2MAGet().lighting = enable ? 1 : 0;
}

static void RS2MADraw(const char *what, const RS2MARect &r, bool known){
	RS2MutableAuditState &s = RS2MAGet();
	RS2MATexture *t = RS2MAFind(s.bound0);

	if(!t) return;
	t->draws++;

	static const char *const blends[] = { "DISABLED", "ALPHA", "ALPHA_ADD", "PRESERVE" };
	char key[160];

	_snprintf(key, sizeof(key), "%s filter=%s blend=%s alphaTest=%d lighting=%d", what,
		s.filter0<0 ? "unset" : s.filter0==RS2_FILTER_LINEAR ? "LINEAR" : "POINT",
		s.blend<0 || s.blend>3 ? "unset" : blends[s.blend], s.alphaTest, s.lighting);
	key[sizeof(key)-1] = 0;
	s.drawKeys[key]++;

	bool readsUpdate = false;
	size_t i;

	if(known){
		for(i = 0; i<s.frame.size(); i++){
			const RS2MAEvent &e = s.frame[i];

			if(e.update && e.texture==s.bound0 && e.rect.Overlaps(r)) readsUpdate = true;
		}
	}
	if(readsUpdate) s.drawReadsUpdateSameFrame++;

	RS2MAEvent e;

	e.update = false;
	e.texture = s.bound0;
	e.rect = r;
	s.frame.push_back(e);
	s.lastEventWasUpdateOf = 0;

	char text[128];

	if(known)
		_snprintf(text, sizeof(text), "draw %s %d,%d-%d,%d%s", what, r.x0, r.y0, r.x1, r.y1,
			readsUpdate ? " reads a region updated this frame" : "");
	else _snprintf(text, sizeof(text), "draw %s (region unknown)", what);
	text[sizeof(text)-1] = 0;
	RS2MASequence(text);
}

void RS2MutableAuditDrawImmediate(const RS2MeshVertexLayout &layout, const void *vertices,
	unsigned int vertexCount){
	if(!RS2MutableAuditEnabled() || !RS2MAFind(RS2MAGet().bound0)) return;

	const RS2MATexture &t = *RS2MAFind(RS2MAGet().bound0);
	RS2MARect r = { 0, 0, 0, 0 };
	bool known = false;
	const char *what = layout.positionSemantic==RS2_POSITION_ALREADY_TRANSFORMED
		? "screen" : "pipeline";

	if(vertices && vertexCount && layout.texCoordCount>0){
		float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
		unsigned int i;

		for(i = 0; i<vertexCount; i++){
			const float *uv = (const float *)((const unsigned char *)vertices
				+i*layout.stride+layout.texCoord[0].offset);

			if(uv[0]<u0) u0 = uv[0];
			if(uv[0]>u1) u1 = uv[0];
			if(uv[1]<v0) v0 = uv[1];
			if(uv[1]>v1) v1 = uv[1];
		}
		r.x0 = (int)(u0*t.w+0.5f);
		r.x1 = (int)(u1*t.w+0.5f);
		r.y0 = (int)(v0*t.h+0.5f);
		r.y1 = (int)(v1*t.h+0.5f);
		known = true;
	}
	RS2MADraw(what, r, known);
}

void RS2MutableAuditDrawGeometry(const CRS2GeometryResource *){
	if(!RS2MutableAuditEnabled() || !RS2MAFind(RS2MAGet().bound0)) return;

	RS2MARect r = { 0, 0, 0, 0 };

	RS2MADraw("geometry", r, false);
}

void RS2MutableAuditPresent(){
	if(!RS2MutableAuditEnabled()) return;

	RS2MutableAuditState &s = RS2MAGet();
	unsigned int updates = 0;
	size_t i;

	for(i = 0; i<s.frame.size(); i++) if(s.frame[i].update) updates++;
	s.updatesPerFrame[updates]++;
	if(updates>s.maxUpdatesInFrame) s.maxUpdatesInFrame = updates;
	if(updates && s.framesWithSequence<3){
		RS2MASequence("---- end of frame");
		s.framesWithSequence++;
	}else if(!updates && s.framesWithSequence<3){
		//	Keep the capture to frames that updated something.
		while(!s.sequence.empty() && s.sequence.back().compare(0, 4, "----")!=0)
			s.sequence.pop_back();
	}
	s.frame.clear();
	s.lastEventWasUpdateOf = 0;
	s.frames++;
}

void RS2MutableAuditFont(int size, bool bold){
	if(!RS2MutableAuditEnabled()) return;
	RS2MAGet().fontSizes[size*2+(bold ? 1 : 0)]++;
	Debug("RS2MUTABLEAUDIT|font|size=%d|bold=%d\n", size, bold ? 1 : 0);
}

void RS2MutableAuditText(int, int, const char *text){
	if(!RS2MutableAuditEnabled()) return;
	RS2MAGet().textDraws++;
	if(text) RS2MAGet().textChars += (unsigned int)strlen(text);
}

void RS2MutableAuditTextHeight(int height){
	if(!RS2MutableAuditEnabled()) return;
	RS2MAGet().heightQueries++;
	RS2MAGet().heights[height]++;
}

void RS2MutableAuditDump(){
	if(!RS2MutableAuditEnabled()) return;

	RS2MutableAuditState &s = RS2MAGet();

	if(s.dumped) return;
	s.dumped = true;

	std::map<const CRS2TextureResource *, RS2MATexture>::const_iterator t;

	for(t = s.textures.begin(); t!=s.textures.end(); ++t) RS2MAReport(t->first, t->second);
	Debug("RS2MUTABLEAUDIT|frames=%u|maxUpdatesInFrame=%u|drawReadsUpdateSameFrame=%u"
		"|updateOverDrawnSameFrame=%u\n", s.frames, s.maxUpdatesInFrame,
		s.drawReadsUpdateSameFrame, s.updateAfterDrawSameFrame);
	Debug("RS2MUTABLEAUDIT|updatesBackToBack=%u|destroyedLocked=%u\n",
		s.updatesWithoutDrawBetween, s.destroyedLocked);

	std::map<unsigned int, unsigned int>::const_iterator h;

	for(h = s.updatesPerFrame.begin(); h!=s.updatesPerFrame.end(); ++h)
		Debug("RS2MUTABLEAUDIT|updatesPerFrame|%u|frames=%u\n", h->first, h->second);

	std::map<std::string, unsigned int>::const_iterator k;

	for(k = s.areaBuckets.begin(); k!=s.areaBuckets.end(); ++k)
		Debug("RS2MUTABLEAUDIT|updateArea|%s|%u\n", k->first.c_str(), k->second);
	for(k = s.drawKeys.begin(); k!=s.drawKeys.end(); ++k)
		Debug("RS2MUTABLEAUDIT|drawkey|%u|%s\n", k->second, k->first.c_str());

	std::map<int, unsigned int>::const_iterator f;

	for(f = s.fontSizes.begin(); f!=s.fontSizes.end(); ++f)
		Debug("RS2MUTABLEAUDIT|fontCreated|size=%d|bold=%d|%u\n", f->first/2, f->first&1, f->second);
	Debug("RS2MUTABLEAUDIT|text|draws=%u|chars=%u|heightQueries=%u\n",
		s.textDraws, s.textChars, s.heightQueries);
	for(f = s.heights.begin(); f!=s.heights.end(); ++f)
		Debug("RS2MUTABLEAUDIT|textHeight|%d|%u\n", f->first, f->second);

	size_t i;

	for(i = 0; i<s.sequence.size(); i++)
		Debug("RS2MUTABLEAUDIT|sequence|%03u|%s\n", (unsigned int)i, s.sequence[i].c_str());
	Debug("RS2MUTABLEAUDIT|end\n");
}
