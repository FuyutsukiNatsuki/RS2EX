//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	See RS2SaveRef.h.

#include "stdafx.h"
#include "RS2SaveRef.h"
#include <map>
#include <algorithm>

static RS2SaveRefSyntax s_Syntax = RS2_SAVEREF_HEX;
static std::map<RS2SaveRef, void *> s_Loaded;
static unsigned int s_Unresolved = 0;

static std::map<const void *, RS2SaveRef> s_Saving;
static RS2SaveRef s_NextRef = 1;
static bool s_Numbering = false;

void RS2SaveRefBeginLoad(){
	s_Syntax = RS2_SAVEREF_HEX;
	s_Loaded.clear();
	s_Unresolved = 0;
}

void RS2SaveRefSetSyntax(RS2SaveRefSyntax syntax){ s_Syntax = syntax; }
RS2SaveRefSyntax RS2SaveRefGetSyntax(){ return s_Syntax; }

static int RS2SaveRefDigit(char c, int base){
	int d;
	if('0'<=c && c<='9') d = c-'0';
	else if(base==16 && 'a'<=c && c<='f') d = c-'a'+10;
	else if(base==16 && 'A'<=c && c<='F') d = c-'A'+10;
	else return -1;
	return d<base ? d : -1;
}

char *RS2SaveRefValue(char *str, RS2SaveRef *out){
	const int base = s_Syntax==RS2_SAVEREF_DECIMAL ? 10 : 16;
	RS2SaveRef v = 0;
	char *p = str;
	int d;

	if(RS2SaveRefDigit(*p, base)<0) return NULL;
	while((d = RS2SaveRefDigit(*p, base))>=0){
		//	Checked, not wrapped: a number that does not fit 64 bits is not a
		//	reference this program ever wrote.
		if(v>(~0ULL-(RS2SaveRef)d)/(RS2SaveRef)base) return NULL;
		v = v*base+d;
		p++;
	}
	//	A reference ends where the value ends: "12x" is not 12.
	if(*p && !(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n' || *p==',' || *p==';' || *p=='/')) return NULL;
	*out = v;
	return Space(p);
}

char *RS2AsgnSaveRef(char *str, char *name, RS2SaveRef *out, int n, bool fill){
	if(!(str = Assignment(str, name))) return NULL;
	if(!(str = RS2SaveRefValue(str, out++))) return NULL;
	while(--n>0){
		char *tmp;
		if(tmp = Character2(str, ';')){
			if(!fill) return NULL;
			while(n-->0){ *out = *(out-1); out++; }
			return tmp;
		}
		if(!(str = Character2(str, ','))) return NULL;
		if(!(str = RS2SaveRefValue(str, out++))) return NULL;
	}
	return Character2(str, ';');
}

void *RS2SaveRefToSlot(RS2SaveRef ref){
	return (void *)(uintptr_t)ref;
}

RS2SaveRef RS2SaveRefFromSlot(const void *slot){
	return (RS2SaveRef)(uintptr_t)slot;
}

char *RS2SaveRefSlotValue(char *str, void **slot){
	RS2SaveRef ref;
	if(!(str = RS2SaveRefValue(str, &ref))) return NULL;
	//	A 32-bit build cannot park a reference above 32 bits; refuse rather
	//	than truncate it into a different reference.
	if(ref>(RS2SaveRef)UINTPTR_MAX) return NULL;
	*slot = RS2SaveRefToSlot(ref);
	return str;
}

char *RS2AsgnSaveRefSlot(char *str, char *name, void **slot){
	if(!(str = Assignment(str, name))) return NULL;
	if(!(str = RS2SaveRefSlotValue(str, slot))) return NULL;
	return Character2(str, ';');
}

bool RS2SaveRefRegister(RS2SaveRef ref, void *object){
	if(!ref) return false;
	return s_Loaded.insert(std::make_pair(ref, object)).second;
}

void *RS2SaveRefResolve(RS2SaveRef ref){
	if(!ref) return NULL;
	std::map<RS2SaveRef, void *>::const_iterator it = s_Loaded.find(ref);
	if(it==s_Loaded.end()){
		s_Unresolved++;
		return NULL;
	}
	return it->second;
}

unsigned int RS2SaveRefRegisteredCount(){ return (unsigned int)s_Loaded.size(); }
unsigned int RS2SaveRefUnresolvedCount(){ return s_Unresolved; }

char *RS2AsgnSaveDouble(char *str, char *name, double *out){
	if(!(str = Assignment(str, name))) return NULL;
	char *end = NULL;
	const double v = strtod(str, &end);
	if(end==str) return NULL;
	*out = v;
	return Character2(Space(end), ';');
}

bool RS2SaveDoubleFromHalves(RS2SaveRef low, RS2SaveRef high, double *out){
	if(low>0xffffffffULL || high>0xffffffffULL) return false;
	const unsigned long long bits = (high<<32)|low;
	memcpy(out, &bits, sizeof(*out));
	return true;
}

void RS2SaveRefBeginSave(){
	s_Saving.clear();
	s_NextRef = 1;
	s_Numbering = false;
}

void RS2SaveRefSetNumbering(bool numbering){ s_Numbering = numbering; }

static RS2SaveRef RS2SaveRefAssign(const void *object){
	std::map<const void *, RS2SaveRef>::const_iterator it = s_Saving.find(object);
	if(it!=s_Saving.end()) return it->second;
	const RS2SaveRef ref = s_NextRef++;
	s_Saving[object] = ref;
	return ref;
}

RS2SaveRef RS2SaveRefDefine(const void *object){
	return object ? RS2SaveRefAssign(object) : 0;
}

RS2SaveRef RS2SaveRefOf(const void *object){
	if(!object) return 0;
	if(s_Numbering){
		//	References do not number anything in the numbering pass.
		std::map<const void *, RS2SaveRef>::const_iterator it = s_Saving.find(object);
		return it!=s_Saving.end() ? it->second : 0;
	}
	//	A reference to an object that was never defined (a dangling pointer the
	//	inherited code left behind) still gets a number of its own, after all
	//	the definitions; loading it finds nothing, as before.
	return RS2SaveRefAssign(object);
}

void RS2SaveRefEndSave(){
	s_Saving.clear();
	s_Numbering = false;
}

void RS2SaveRefWriteSorted(FILE *df, const std::vector<const void *> &objects){
	std::vector<RS2SaveRef> refs;
	size_t i;
	for(i = 0; i<objects.size(); i++) refs.push_back(RS2SaveRefOf(objects[i]));
	std::sort(refs.begin(), refs.end());
	for(i = 0; i<refs.size(); i++) fprintf(df, i ? ", " RS2_SAVEREF_FMT : RS2_SAVEREF_FMT, refs[i]);
}
