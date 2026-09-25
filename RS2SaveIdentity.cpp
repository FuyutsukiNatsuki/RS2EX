//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Save-file identity (v0.2.0): see RS2SaveIdentity.h.

#include "stdafx.h"
#include "RS2SaveIdentity.h"
#include "CSaveFile.h"

#include <direct.h>
#include <io.h>
#include <sys/stat.h>

const int RS2_SAVE_SCHEMA_CURRENT = 1;
const char *const RS2_SAVE_PRODUCT = "RS2EX";
const char *const RS2_SAVE_PRODUCER = "0.2.0";

extern const char *LAYOUT_DIRNAME;

void RS2SaveIdentity::Clear(){
	saveClass = RS2_SAVE_EMPTY;
	railSimVersion = 0.0f;
	schema = -1;
	product.clear();
	producer.clear();
	reason.clear();
}

char *RS2ReadSaveIdentityKeys(char *str, RS2SaveIdentity *out){
	char *tmp;
	string product, producer;
	int schema = -1;

	if(!str) return NULL;
	if(!(tmp = AsgnString(str, "RS2EXProduct", &product))) return str;	//	none: a legacy file
	if(!(tmp = AsgnInteger(tmp, "RS2EXSaveSchema", &schema))) return NULL;
	if(!(tmp = AsgnString(tmp, "RS2EXProducer", &producer))) return NULL;
	if(out){
		out->product = product;
		out->schema = schema;
		out->producer = producer;
	}
	return tmp;
}

void RS2WriteSaveIdentityKeys(FILE *file){
	fprintf(file, "\tRS2EXProduct = \"%s\";\n", RS2_SAVE_PRODUCT);
	fprintf(file, "\tRS2EXSaveSchema = %d;\n", RS2_SAVE_SCHEMA_CURRENT);
	fprintf(file, "\tRS2EXProducer = \"%s\";\n", RS2_SAVE_PRODUCER);
}

static void RS2SaveSet(RS2SaveIdentity *out, RS2SaveClass c, const char *reason){
	out->saveClass = c;
	if(reason) out->reason = reason;
}

void RS2ClassifySaveText(const char *text, RS2SaveIdentity *out){
	if(!out) return;
	out->Clear();
	if(!text){
		RS2SaveSet(out, RS2_SAVE_EMPTY, "no data");
		return;
	}

	//	The parser takes char *; work on a copy of at most the part a header
	//	can occupy, so a huge layout is not duplicated to read five lines.
	const size_t limit = 4096;
	size_t n = strlen(text);
	const bool clipped = n>limit;

	if(clipped) n = limit;

	std::vector<char> copy(text, text+n);
	copy.push_back(0);

	char *str = &copy[0], *tmp;

	try{
		str = Space(str);
		if(!str || !*str){
			RS2SaveSet(out, RS2_SAVE_EMPTY, "only white space");
			return;
		}
		if(!(tmp = BeginBlock(str, "DatafileHeader"))){
			RS2SaveSet(out, RS2_SAVE_UNKNOWN, "no DatafileHeader at the start");
			return;
		}
		str = tmp;
		if(!(tmp = AsgnFloat(str, "RailSimVersion", &out->railSimVersion))){
			RS2SaveSet(out, RS2_SAVE_CORRUPT, *Space(str) ? "RailSimVersion missing or invalid" : "the header ends early");
			return;
		}
		str = tmp;
		if(out->railSimVersion<2.00f || RAILSIM_VERSION<out->railSimVersion){
			RS2SaveSet(out, RS2_SAVE_UNKNOWN, FlashIn("RailSimVersion %.2f is outside 2.00-%.2f",
				out->railSimVersion, RAILSIM_VERSION));
			return;
		}
		string type;
		if(!(tmp = AsgnIdentifier(str, "DatafileType", &type))){
			RS2SaveSet(out, RS2_SAVE_CORRUPT, *Space(str) ? "DatafileType missing or invalid" : "the header ends early");
			return;
		}
		str = tmp;
		if(type!=LAYOUT_DIRNAME){
			RS2SaveSet(out, RS2_SAVE_UNKNOWN, FlashIn("DatafileType is %s, not %s", type.c_str(), LAYOUT_DIRNAME));
			return;
		}
		if(!(tmp = RS2ReadSaveIdentityKeys(str, out))){
			RS2SaveSet(out, RS2_SAVE_CORRUPT, "the RS2EX identity is incomplete");
			return;
		}
		str = tmp;
		if(!(tmp = EndBlock(str))){
			RS2SaveSet(out, RS2_SAVE_CORRUPT, *Space(str) ? "unexpected data in DatafileHeader" : "the header ends early");
			return;
		}
	}
	catch(CSynErr err){
		RS2SaveSet(out, RS2_SAVE_CORRUPT, err.Get());
		return;
	}

	if(out->product.empty()){
		RS2SaveSet(out, RS2_SAVE_LEGACY, NULL);
	}else if(out->product!=RS2_SAVE_PRODUCT){
		RS2SaveSet(out, RS2_SAVE_UNKNOWN, FlashIn("product \"%s\"", out->product.c_str()));
	}else if(out->schema<RS2_SAVE_SCHEMA_CURRENT){
		RS2SaveSet(out, RS2_SAVE_OLDER_RS2EX, NULL);
	}else if(out->schema>RS2_SAVE_SCHEMA_CURRENT){
		RS2SaveSet(out, RS2_SAVE_NEWER_RS2EX, NULL);
	}else{
		RS2SaveSet(out, RS2_SAVE_CURRENT, NULL);
	}
}

void RS2ClassifySaveFile(const char *dirname, const char *fname, RS2SaveIdentity *out){
	if(!out) return;
	out->Clear();

	FILE *file = 0;

	if(chdir(g_BaseDir) || (dirname && chdir(dirname)) || !(file = fopen(fname, "rb"))){
		chdir(g_BaseDir);
		RS2SaveSet(out, RS2_SAVE_CORRUPT, "the file cannot be opened");
		return;
	}
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if(size<=0){
		fclose(file);
		chdir(g_BaseDir);
		RS2SaveSet(out, RS2_SAVE_EMPTY, "0 bytes");
		return;
	}

	std::vector<char> buf(size<4096 ? size : 4096);
	const size_t got = fread(&buf[0], 1, buf.size(), file);

	fclose(file);
	chdir(g_BaseDir);
	buf.resize(got);
	buf.push_back(0);
	RS2ClassifySaveText(&buf[0], out);
	if(out->saveClass==RS2_SAVE_EMPTY && got==0) out->reason = "0 bytes";
}

bool RS2SaveLoadsNormally(RS2SaveClass c){
	return c==RS2_SAVE_CURRENT;
}

bool RS2SaveNeedsConsent(RS2SaveClass c){
	return c==RS2_SAVE_LEGACY || c==RS2_SAVE_OLDER_RS2EX;
}

const char *RS2SaveClassName(RS2SaveClass c){
	switch(c){
	case RS2_SAVE_CURRENT: return "current";
	case RS2_SAVE_OLDER_RS2EX: return "older-rs2ex";
	case RS2_SAVE_NEWER_RS2EX: return "newer-rs2ex";
	case RS2_SAVE_LEGACY: return "legacy";
	case RS2_SAVE_UNKNOWN: return "unknown";
	case RS2_SAVE_CORRUPT: return "corrupt";
	case RS2_SAVE_EMPTY: return "empty";
	}
	return "?";
}

std::string RS2DescribeSaveIdentity(const RS2SaveIdentity &id){
	char detected[256];

	switch(id.saveClass){
	case RS2_SAVE_CURRENT:
	case RS2_SAVE_OLDER_RS2EX:
	case RS2_SAVE_NEWER_RS2EX:
		_snprintf(detected, sizeof(detected), "RS2EX schema %d (%s)", id.schema, id.producer.c_str());
		break;
	case RS2_SAVE_LEGACY:
		_snprintf(detected, sizeof(detected), "RailSimVersion %.2f, no RS2EX identity", id.railSimVersion);
		break;
	default:
		_snprintf(detected, sizeof(detected), "%s", id.reason.c_str());
		break;
	}
	detected[sizeof(detected)-1] = 0;

	char line[384];

	_snprintf(line, sizeof(line), "%s / RS2EX %s: schema %d", detected, RS2_SAVE_PRODUCER, RS2_SAVE_SCHEMA_CURRENT);
	line[sizeof(line)-1] = 0;
	return line;
}

char *RS2SaveClassMessage(RS2SaveClass c){
	switch(c){
	case RS2_SAVE_LEGACY: return lang(SaveFormatLegacyCfm);
	case RS2_SAVE_OLDER_RS2EX: return lang(SaveFormatOlderCfm);
	case RS2_SAVE_NEWER_RS2EX: return lang(SaveFormatNewer);
	case RS2_SAVE_UNKNOWN: return lang(SaveFormatUnknown);
	case RS2_SAVE_CORRUPT: return lang(SaveFormatCorrupt);
	case RS2_SAVE_EMPTY: return lang(SaveFormatEmpty);
	default: return "";
	}
}

bool RS2SaveAutoAcceptLegacy(){
	return CheckArguments("-acceptlegacysave") || CheckArguments("-fixture");
}

////////////////////////////////////////////////////////////////////////////////
//	-savefixturecheck
////////////////////////////////////////////////////////////////////////////////

bool RS2SaveFixtureCheckRequested(){
	return CheckArguments("-savefixturecheck")!=FALSE;
}

static const char *RS2_SAVE_FIXTURE_DIR = "RS2EX_SaveFixture";

static bool RS2SaveFixtureWrite(const char *name, const char *text){
	if(chdir(g_BaseDir) || chdir(RS2_SAVE_FIXTURE_DIR)) return false;

	FILE *f = fopen(name, "wb");

	if(!f) return false;
	fwrite(text, 1, strlen(text), f);
	fclose(f);
	chdir(g_BaseDir);
	return true;
}

static bool RS2SaveFixtureExpect(const char *name, RS2SaveClass want, bool *all){
	RS2SaveIdentity id;

	RS2ClassifySaveFile(RS2_SAVE_FIXTURE_DIR, name, &id);

	const bool ok = id.saveClass==want;

	Debug("RS2SAVEFIXTURE|%s|expected=%s|got=%s|%s|%s\n", name, RS2SaveClassName(want),
		RS2SaveClassName(id.saveClass), RS2DescribeSaveIdentity(id).c_str(), ok ? "pass" : "FAIL");
	if(!ok) *all = false;
	return ok;
}

bool RS2SaveFixtureCheckRun(){
	bool all = true;

	chdir(g_BaseDir);
	_mkdir(RS2_SAVE_FIXTURE_DIR);

	const char *body = "LayoutInfo{\n\tDate = \"\";\n\tNote = \"\";\n}\n";
	char text[1024];

	//	One file per class, written the way each would be found.
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 2.15;\n\tDatafileType = Layout;\n}\n\n%s", body);
	RS2SaveFixtureWrite("legacy.rs2", text);
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 2.00;\n\tDatafileType = Layout;\n}\n\n%s", body);
	RS2SaveFixtureWrite("legacy200.rs2", text);
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 2.15;\n\tDatafileType = Layout;\n"
		"\tRS2EXProduct = \"RS2EX\";\n\tRS2EXSaveSchema = 0;\n\tRS2EXProducer = \"0.1.9\";\n}\n\n%s", body);
	RS2SaveFixtureWrite("older.rs2", text);
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 2.15;\n\tDatafileType = Layout;\n"
		"\tRS2EXProduct = \"RS2EX\";\n\tRS2EXSaveSchema = 2;\n\tRS2EXProducer = \"0.3.0\";\n}\n\n%s", body);
	RS2SaveFixtureWrite("newer.rs2", text);
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 2.15;\n\tDatafileType = Layout;\n"
		"\tRS2EXProduct = \"SomethingElse\";\n\tRS2EXSaveSchema = 1;\n\tRS2EXProducer = \"1.0\";\n}\n\n%s", body);
	RS2SaveFixtureWrite("unknown-product.rs2", text);
	RS2SaveFixtureWrite("unknown-noheader.rs2", "Layout{\n}\n");
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 2.15;\n\tDatafileType = Config;\n}\n\n%s", body);
	RS2SaveFixtureWrite("unknown-type.rs2", text);
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 9.99;\n\tDatafileType = Layout;\n}\n\n%s", body);
	RS2SaveFixtureWrite("unknown-version.rs2", text);
	RS2SaveFixtureWrite("truncated.rs2", "DatafileHeader{\n\tRailSimVersion = 2.15;\n");
	RS2SaveFixtureWrite("truncated-identity.rs2",
		"DatafileHeader{\n\tRailSimVersion = 2.15;\n\tDatafileType = Layout;\n\tRS2EXProduct = \"RS2EX\";\n");
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = abc;\n\tDatafileType = Layout;\n}\n\n%s", body);
	RS2SaveFixtureWrite("corrupt-version.rs2", text);
	_snprintf(text, sizeof(text),
		"DatafileHeader{\n\tRailSimVersion = 2.15;\n\tDatafileType = Layout;\n"
		"\tRS2EXProduct = \"RS2EX\";\n\tRS2EXSaveSchema = one;\n\tRS2EXProducer = \"0.2.0\";\n}\n\n%s", body);
	RS2SaveFixtureWrite("corrupt-schema.rs2", text);
	RS2SaveFixtureWrite("corrupt-comment.rs2", "/* never closed\nDatafileHeader{\n");
	RS2SaveFixtureWrite("empty.rs2", "");
	RS2SaveFixtureWrite("blank.rs2", "  \r\n\t\r\n");

	RS2SaveFixtureExpect("legacy.rs2", RS2_SAVE_LEGACY, &all);
	RS2SaveFixtureExpect("legacy200.rs2", RS2_SAVE_LEGACY, &all);
	RS2SaveFixtureExpect("older.rs2", RS2_SAVE_OLDER_RS2EX, &all);
	RS2SaveFixtureExpect("newer.rs2", RS2_SAVE_NEWER_RS2EX, &all);
	RS2SaveFixtureExpect("unknown-product.rs2", RS2_SAVE_UNKNOWN, &all);
	RS2SaveFixtureExpect("unknown-noheader.rs2", RS2_SAVE_UNKNOWN, &all);
	RS2SaveFixtureExpect("unknown-type.rs2", RS2_SAVE_UNKNOWN, &all);
	RS2SaveFixtureExpect("unknown-version.rs2", RS2_SAVE_UNKNOWN, &all);
	RS2SaveFixtureExpect("truncated.rs2", RS2_SAVE_CORRUPT, &all);
	RS2SaveFixtureExpect("truncated-identity.rs2", RS2_SAVE_CORRUPT, &all);
	RS2SaveFixtureExpect("corrupt-version.rs2", RS2_SAVE_CORRUPT, &all);
	RS2SaveFixtureExpect("corrupt-schema.rs2", RS2_SAVE_CORRUPT, &all);
	RS2SaveFixtureExpect("corrupt-comment.rs2", RS2_SAVE_CORRUPT, &all);
	RS2SaveFixtureExpect("empty.rs2", RS2_SAVE_EMPTY, &all);
	RS2SaveFixtureExpect("blank.rs2", RS2_SAVE_EMPTY, &all);
	RS2SaveFixtureExpect("missing.rs2", RS2_SAVE_CORRUPT, &all);

	//	A current layout, round trip: what this build writes classifies as
	//	current and loads back.
	{
		const int saved = g_SaveFile->Save("current.rs2", RS2_SAVE_FIXTURE_DIR, true, false);
		bool loaded = false;

		chdir(g_BaseDir);
		Debug("RS2SAVEFIXTURE|save current.rs2|result=%d\n", saved);
		if(saved==0 && RS2SaveFixtureExpect("current.rs2", RS2_SAVE_CURRENT, &all)){
			//	Replaced the way opening a file replaces it; the check exits
			//	right after, so the layout that was loaded before is not wanted.
			DELETE_V(g_SaveFile);
			g_SaveFile = new CSaveFile(false);
			loaded = g_SaveFile->Load("current.rs2", RS2_SAVE_FIXTURE_DIR, false, false, NULL, NULL, false, NULL);
			if(loaded) loaded = g_SaveFile->GetIdentity().saveClass==RS2_SAVE_CURRENT;
		}
		chdir(g_BaseDir);
		Debug("RS2SAVEFIXTURE|round trip load|%s\n", loaded ? "pass" : "FAIL");
		if(!loaded) all = false;
	}

	//	Tidy up.
	{
		static const char *const names[] = {
			"legacy.rs2", "legacy200.rs2", "older.rs2", "newer.rs2", "unknown-product.rs2",
			"unknown-noheader.rs2", "unknown-type.rs2", "unknown-version.rs2", "truncated.rs2",
			"truncated-identity.rs2", "corrupt-version.rs2", "corrupt-schema.rs2",
			"corrupt-comment.rs2", "empty.rs2", "blank.rs2", "current.rs2"
		};
		size_t i;

		if(!chdir(g_BaseDir) && !chdir(RS2_SAVE_FIXTURE_DIR)){
			for(i = 0; i<sizeof(names)/sizeof(names[0]); i++) remove(names[i]);
		}
		chdir(g_BaseDir);
		_rmdir(RS2_SAVE_FIXTURE_DIR);
	}

	Debug("RS2SAVEFIXTURE|%s\n", all ? "pass" : "FAIL");
	return all;
}
