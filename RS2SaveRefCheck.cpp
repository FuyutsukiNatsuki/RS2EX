//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	-saverefcheck: the save-reference fixtures of v0.3.0 (plan section 20.7).
//
//	Runs after the start-up layout has loaded, then closes the program.  Two
//	halves:
//
//	    unit      the RS2SaveRef primitives on fixed inputs: decimal and
//	              hexadecimal references, overflow, malformed tokens, null,
//	              registration and duplicates, unresolved references, the
//	              pending-slot round trip (with a value above 32 bits on x64)
//	              and the DepartureTime halves;
//	    layout    the real graph: the loaded layout is saved as schema 2, the
//	              file is checked (identity, decimal IDs, every definition
//	              unique and small), loaded back with no unresolved reference,
//	              saved again and compared line by line - the same text apart
//	              from the save date and two inherited non-reference values,
//	              which proves the IDs are deterministic and every reference
//	              (lists, forward references, cycles) came back to the same
//	              object.  Then deliberately broken copies: a duplicate ID and
//	              an overflowing ID must be refused, a reference to nothing must
//	              load with that reference dropped.
//
//	Files live in RS2EX_SaveRefCheck under the run directory and are removed.

#include "stdafx.h"
#include "CSaveFile.h"
#include "RS2SaveRef.h"
#include "RS2SaveRefCheck.h"
#include <direct.h>

static const char *const RS2_SAVEREF_CHECK_DIR = "RS2EX_SaveRefCheck";

bool RS2SaveRefCheckRequested(){
	return CheckArguments("-saverefcheck")!=FALSE;
}

static void RS2SRStep(const char *name, bool ok, bool *all){
	Debug("RS2SAVEREF|%-48s|%s\n", name, ok ? "pass" : "FAIL");
	if(!ok) *all = false;
}

static bool RS2SRParse(RS2SaveRefSyntax syntax, const char *text, RS2SaveRef *out){
	char buf[64];
	strncpy(buf, text, sizeof(buf)-1);
	buf[sizeof(buf)-1] = 0;
	RS2SaveRefSetSyntax(syntax);
	return RS2SaveRefValue(buf, out)!=NULL;
}

static void RS2SRUnit(bool *all){
	RS2SaveRef v = 0;

	RS2SRStep("decimal 12", RS2SRParse(RS2_SAVEREF_DECIMAL, "12;", &v) && v==12, all);
	RS2SRStep("decimal 0 is null", RS2SRParse(RS2_SAVEREF_DECIMAL, "0;", &v) && v==0, all);
	RS2SRStep("decimal 2^64-1", RS2SRParse(RS2_SAVEREF_DECIMAL, "18446744073709551615;", &v) && v==~0ULL, all);
	RS2SRStep("decimal 2^64 overflows", !RS2SRParse(RS2_SAVEREF_DECIMAL, "18446744073709551616;", &v), all);
	RS2SRStep("decimal rejects hex digits", !RS2SRParse(RS2_SAVEREF_DECIMAL, "0A1B;", &v), all);
	RS2SRStep("decimal rejects trailing garbage", !RS2SRParse(RS2_SAVEREF_DECIMAL, "12x;", &v), all);
	RS2SRStep("decimal rejects empty", !RS2SRParse(RS2_SAVEREF_DECIMAL, ";", &v), all);
	RS2SRStep("hex 0A1B2C3D (an x86 address spelling)", RS2SRParse(RS2_SAVEREF_HEX, "0A1B2C3D,", &v) && v==0x0A1B2C3DULL, all);
	RS2SRStep("hex 000001A2B3C4D5E6 (an x64 address spelling)", RS2SRParse(RS2_SAVEREF_HEX, "000001A2B3C4D5E6;", &v) && v==0x000001A2B3C4D5E6ULL, all);
	RS2SRStep("hex 17 digits overflow", !RS2SRParse(RS2_SAVEREF_HEX, "10000000000000000;", &v), all);

	RS2SaveRefBeginLoad();
	int a = 1, b = 2;
	RS2SRStep("register 1", RS2SaveRefRegister(1, &a), all);
	RS2SRStep("register 2", RS2SaveRefRegister(2, &b), all);
	RS2SRStep("duplicate 1 refused", !RS2SaveRefRegister(1, &b), all);
	RS2SRStep("null refused", !RS2SaveRefRegister(0, &a), all);
	RS2SRStep("resolve 1, 2", RS2SaveRefResolve(1)==&a && RS2SaveRefResolve(2)==&b, all);
	RS2SRStep("resolve 0 is NULL, not counted", RS2SaveRefResolve(0)==NULL && RS2SaveRefUnresolvedCount()==0, all);
	RS2SRStep("resolve 3 is NULL, counted", RS2SaveRefResolve(3)==NULL && RS2SaveRefUnresolvedCount()==1, all);

	const RS2SaveRef slots[] = {1ULL, 0xffffffffULL, 0x100000005ULL, 0x00007ffffffff000ULL};
	bool slotok = true;
	size_t i;
	for(i = 0; i<sizeof(slots)/sizeof(slots[0]); i++){
		if(slots[i]>(RS2SaveRef)UINTPTR_MAX) continue;	//	32-bit build: refused by RS2SaveRefSlotValue
		if(RS2SaveRefFromSlot(RS2SaveRefToSlot(slots[i]))!=slots[i]) slotok = false;
	}
	RS2SRStep("pending slot round trip", slotok, all);
	RS2SRStep("pending slot holds 64 bits (x64)", sizeof(void *)<8 || RS2SaveRefFromSlot(RS2SaveRefToSlot(0x100000005ULL))==0x100000005ULL, all);

	const double t = 12345.678901234567;
	unsigned long long bits;
	memcpy(&bits, &t, sizeof(bits));
	double back = 0.0;
	RS2SRStep("DepartureTime halves", RS2SaveDoubleFromHalves(bits&0xffffffffULL, bits>>32, &back) && memcmp(&back, &t, sizeof(t))==0, all);
	RS2SRStep("DepartureTime half above 32 bits refused", !RS2SaveDoubleFromHalves(0x100000000ULL, 0, &back), all);
}

static bool RS2SRReadFile(const char *name, string *out){
	chdir(g_BaseDir);
	if(chdir(RS2_SAVEREF_CHECK_DIR)) return false;
	FILE *f = fopen(name, "rb");
	chdir(g_BaseDir);
	if(!f) return false;
	char buf[4096];
	size_t n;
	out->clear();
	while((n = fread(buf, 1, sizeof(buf), f))>0) out->append(buf, n);
	fclose(f);
	return true;
}

static bool RS2SRWriteFile(const char *name, const string &text){
	chdir(g_BaseDir);
	if(chdir(RS2_SAVEREF_CHECK_DIR)) return false;
	FILE *f = fopen(name, "wb");
	chdir(g_BaseDir);
	if(!f) return false;
	const bool ok = fwrite(text.data(), 1, text.size(), f)==text.size();
	fclose(f);
	return ok;
}

//	The save date is the only thing two saves of the same scene must differ in.
static string RS2SRWithoutDate(const string &text){
	string out;
	size_t pos = 0;
	while(pos<text.size()){
		size_t eol = text.find('\n', pos);
		if(eol==string::npos) eol = text.size()-1;
		const string line = text.substr(pos, eol-pos+1);
		if(line.find("\tDate = \"")!=0) out += line;
		pos = eol+1;
	}
	return out;
}

static bool RS2SRLoad(const char *name){
	DELETE_V(g_SaveFile);
	g_SaveFile = new CSaveFile(false);
	const bool ok = g_SaveFile->Load(name, RS2_SAVEREF_CHECK_DIR, false, false, NULL, NULL, false, NULL);
	chdir(g_BaseDir);
	return ok;
}

//	Every "Address = n;" value, in file order.
static vector<string> RS2SRAddresses(const string &text){
	vector<string> out;
	size_t pos = 0;
	while((pos = text.find("Address = ", pos))!=string::npos){
		pos += 10;
		const size_t end = text.find(';', pos);
		if(end==string::npos) break;
		out.push_back(text.substr(pos, end-pos));
	}
	return out;
}

//	"Up = (x, y, z);" / "Dir = (x, y, z);" lines that agree to within the
//	printing precision: vectors recomputed (normalised) on loading, which can
//	print one unit differently in the last of their six decimals.
static bool RS2SRSameVector(const string &la, const string &lb){
	float a[3], b[3];
	char ka[32], kb[32];
	if(sscanf(la.c_str(), " %31[A-Za-z] = (%f, %f, %f);", ka, &a[0], &a[1], &a[2])!=4) return false;
	if(sscanf(lb.c_str(), " %31[A-Za-z] = (%f, %f, %f);", kb, &b[0], &b[1], &b[2])!=4) return false;
	if(strcmp(ka, kb) || (strcmp(ka, "Up") && strcmp(ka, "Dir"))) return false;
	int i;
	for(i = 0; i<3; i++) if(fabsf(a[i]-b[i])>2e-6f) return false;
	return true;
}

//	Line by line.  Apart from the save date, a save of what was loaded must be
//	the same text, with two inherited exceptions that are not references: the
//	wind directions (Dir1 / Dir2) are drawn at random on every load, and Up /
//	Dir vectors are recomputed and can print one unit differently in the last
//	decimal (RS2SRSameVector).  Anything else is a failure.
static void RS2SRCompare(const string &a, const string &b, bool *all){
	const string da = RS2SRWithoutDate(a), db = RS2SRWithoutDate(b);
	size_t pa = 0, pb = 0, n = 0, noise = 0, other = 0;

	while(pa<da.size() && pb<db.size()){
		size_t ea = da.find('\n', pa), eb = db.find('\n', pb);
		if(ea==string::npos) ea = da.size();
		if(eb==string::npos) eb = db.size();
		const string la = da.substr(pa, ea-pa), lb = db.substr(pb, eb-pb);
		n++;
		if(la!=lb){
			const size_t k = la.find_first_not_of('\t');
			const size_t eq = k==string::npos ? string::npos : la.find(" = ", k);
			const string key = (k==string::npos || eq==string::npos) ? "" : la.substr(k, eq-k);
			if(key=="Dir1" || key=="Dir2" || RS2SRSameVector(la, lb)){
				noise++;
			}else if(++other<=20){
				Debug("RS2SAVEREF|difference at line %u:\n  a: %s\n  b: %s\n", (unsigned int)n, la.c_str(), lb.c_str());
			}
		}
		pa = ea+1;
		pb = eb+1;
	}
	const bool sameShape = pa>=da.size() && pb>=db.size();
	Debug("RS2SAVEREF|round trip: %u lines, %u known-noise differences (wind Dir1 / Dir2, last-digit Up / Dir), %u other\n",
		(unsigned int)n, (unsigned int)noise, (unsigned int)other);
	RS2SRStep("round trip: same lines", !b.empty() && sameShape, all);
	RS2SRStep("round trip: identical except date / wind / last digit", !b.empty() && other==0, all);
}

static void RS2SRLayout(bool *all){
	if(!g_SaveFile){
		RS2SRStep("a layout is loaded", false, all);
		return;
	}
	chdir(g_BaseDir);
	_mkdir(RS2_SAVEREF_CHECK_DIR);

	//	1. save as schema 2
	const int saved = g_SaveFile->Save("a.rs2", RS2_SAVEREF_CHECK_DIR, true, false);
	chdir(g_BaseDir);
	string a;
	RS2SRStep("save schema 2 (a.rs2)", saved==0 && RS2SRReadFile("a.rs2", &a), all);
	if(a.empty()) return;
	RS2SRStep("identity is schema 2 / 0.3.0",
		a.find("RS2EXSaveSchema = 2;")!=string::npos && a.find("RS2EXProducer = \"0.3.0\";")!=string::npos, all);

	const vector<string> addr = RS2SRAddresses(a);
	set<string> unique;
	bool decimal = !addr.empty();
	size_t i;
	for(i = 0; i<addr.size(); i++){
		unique.insert(addr[i]);
		if(addr[i].empty() || addr[i].find_first_not_of("0123456789")!=string::npos || addr[i]=="0") decimal = false;
	}
	Debug("RS2SAVEREF|layout: %u definitions, lists: PointList %s, GroupEnd %s, RailList %s, LineList %s\n",
		(unsigned int)addr.size(),
		a.find("PointList = ")!=string::npos ? "yes" : "no", a.find("GroupEnd = ")!=string::npos ? "yes" : "no",
		a.find("RailList = ")!=string::npos ? "yes" : "no", a.find("LineList = ")!=string::npos ? "yes" : "no");
	RS2SRStep("definitions are nonzero decimal IDs", decimal, all);
	RS2SRStep("definitions are unique", unique.size()==addr.size(), all);
	//	Deterministic, small, encounter-ordered: never a process address.
	bool logicalIds = true;
	for(i = 0; i<addr.size(); i++){
		if(_strtoui64(addr[i].c_str(), NULL, 10)>(unsigned long long)addr.size()*8+64) logicalIds = false;
	}
	RS2SRStep("IDs are logical (small), not addresses", logicalIds, all);

	//	2. load back, save again, compare.  Loading ends with one simulation
	//	step (inherited), which moves the clock, the trains and the texture
	//	animations; without it, what was loaded must save to the same text.
	g_RS2LoadWithoutSimulation = true;
	const bool loaded = RS2SRLoad("a.rs2");
	RS2SRStep("load schema 2", loaded, all);
	RS2SRStep("every reference resolved", loaded && RS2SaveRefUnresolvedCount()==0, all);
	RS2SRStep("every definition registered", loaded && RS2SaveRefRegisteredCount()==addr.size(), all);
	string b;
	const int saved2 = loaded ? g_SaveFile->Save("b.rs2", RS2_SAVEREF_CHECK_DIR, true, false) : -1;
	chdir(g_BaseDir);
	RS2SRStep("save again (b.rs2)", saved2==0 && RS2SRReadFile("b.rs2", &b), all);
	RS2SRCompare(a, b, all);
	g_RS2LoadWithoutSimulation = false;

	//	3. broken copies (errors to debug.txt, not message boxes)
	g_RS2QuietSyntaxErrors = true;
	if(addr.size()>=2){
		string dup = a;
		const string first = "Address = "+addr[0]+";";
		const size_t second = dup.find("Address = "+addr[1]+";");
		dup.replace(second, 10+addr[1].size()+1, first);
		RS2SRWriteFile("dup.rs2", dup);
		RS2SRStep("duplicate ID refused", !RS2SRLoad("dup.rs2"), all);

		string ovf = a;
		const size_t p = ovf.find("Address = "+addr[0]+";");
		ovf.replace(p, 10+addr[0].size()+1, "Address = 18446744073709551616;");
		RS2SRWriteFile("overflow.rs2", ovf);
		RS2SRStep("overflowing ID refused", !RS2SRLoad("overflow.rs2"), all);
	}
	{
		//	A nullable reference pointed at nothing: dropped, and counted.
		static const char *const keys[] = {"\tUser = ", "\tPlatform = "};
		string miss = a;
		size_t at = string::npos, len = 0;
		size_t k;
		for(k = 0; k<2 && at==string::npos; k++){
			size_t pos = 0;
			while((pos = miss.find(keys[k], pos))!=string::npos){
				const size_t v = pos+strlen(keys[k]);
				if(miss[v]>='1' && miss[v]<='9'){
					at = v;
					len = miss.find(';', v)-v;
					break;
				}
				pos = v;
			}
		}
		if(at==string::npos){
			Debug("RS2SAVEREF|%-48s|skipped (no nonzero User / Platform in this layout)\n", "reference to a missing object");
		}else{
			miss.replace(at, len, "999999999");
			RS2SRWriteFile("missing.rs2", miss);
			const bool ok = RS2SRLoad("missing.rs2");
			RS2SRStep("reference to a missing object is dropped", ok && RS2SaveRefUnresolvedCount()>=1, all);
		}
	}
	g_RS2QuietSyntaxErrors = false;

	//	Leave the scene loaded, and tidy up.
	RS2SRLoad("a.rs2");
	static const char *const names[] = {"a.rs2", "b.rs2", "dup.rs2", "overflow.rs2", "missing.rs2"};
	if(!chdir(g_BaseDir) && !chdir(RS2_SAVEREF_CHECK_DIR)){
		for(i = 0; i<sizeof(names)/sizeof(names[0]); i++) remove(names[i]);
	}
	chdir(g_BaseDir);
	_rmdir(RS2_SAVEREF_CHECK_DIR);
}

bool RS2SaveRefCheckRun(){
	bool all = true;
	RS2SRUnit(&all);
	RS2SRLayout(&all);
	Debug("RS2SAVEREF|%s\n", all ? "pass" : "FAIL");
	return all;
}
