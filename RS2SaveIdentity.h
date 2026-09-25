//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Save-file identity (v0.2.0).
//
//	From v0.2.0 a layout carries who wrote it, in its DatafileHeader:
//
//		RS2EXProduct = "RS2EX";
//		RS2EXSaveSchema = 1;
//		RS2EXProducer = "0.2.0";
//
//	The schema is the save format's own version and changes only when the
//	format does, never with every release.  The producer is informational.
//
//	This module is the one place that knows the current schema and decides
//	what a file is.  Classification reads the header only, before anything is
//	loaded: CSaveFile::Load discards the current scene before it parses a
//	byte, so the decision has to be made first.  Files from RailSim II and from
//	RS2EX v0.1.x carry no identity and cannot be told apart - they share one
//	class.  Nothing here migrates anything: an older file loaded at the user's
//	risk is read by the existing parser, as it always was.

#ifndef RS2SAVEIDENTITY_H_INCLUDED
#define RS2SAVEIDENTITY_H_INCLUDED

#include <stdio.h>
#include <string>

enum RS2SaveClass
{
	RS2_SAVE_CURRENT,		//	this build's schema
	RS2_SAVE_OLDER_RS2EX,	//	an earlier RS2EX schema
	RS2_SAVE_NEWER_RS2EX,	//	a later RS2EX schema: refused
	RS2_SAVE_LEGACY,		//	no RS2EX identity: RailSim II, or RS2EX v0.1.x and earlier
	RS2_SAVE_UNKNOWN,		//	not a RailSim II / RS2EX layout
	RS2_SAVE_CORRUPT,		//	the header is broken or truncated
	RS2_SAVE_EMPTY			//	nothing in the file
};

struct RS2SaveIdentity
{
	RS2SaveClass saveClass;
	float railSimVersion;	//	0 when not read
	int schema;				//	-1 when there is none
	std::string product;
	std::string producer;
	std::string reason;		//	for CORRUPT / UNKNOWN: what was wrong

	RS2SaveIdentity(){ Clear(); }
	void Clear();
};

//	The save format this build writes and loads normally.
extern const int RS2_SAVE_SCHEMA_CURRENT;
extern const char *const RS2_SAVE_PRODUCT;
extern const char *const RS2_SAVE_PRODUCER;

/*
 *	Classify a layout from its text.  Only the DatafileHeader is read.
 */
void RS2ClassifySaveText(const char *text, RS2SaveIdentity *out);

/*
 *	Classify a layout file (dirname relative to the program's base directory).
 *	A file that cannot be opened is reported as CORRUPT with the reason.
 */
void RS2ClassifySaveFile(const char *dirname, const char *fname, RS2SaveIdentity *out);

/*
 *	Parse the identity keys at str, right after DatafileType, as CSaveFile::Load
 *	does.  Returns the position after them, or str unchanged when the file has
 *	none (a legacy file).  Returns NULL when they are present but malformed.
 */
char *RS2ReadSaveIdentityKeys(char *str, RS2SaveIdentity *out);

/*
 *	Write this build's identity keys (inside DatafileHeader, after DatafileType).
 */
void RS2WriteSaveIdentityKeys(FILE *file);

//	What the class allows.
bool RS2SaveLoadsNormally(RS2SaveClass c);		//	CURRENT
bool RS2SaveNeedsConsent(RS2SaveClass c);		//	LEGACY, OLDER: only by the user's explicit choice
const char *RS2SaveClassName(RS2SaveClass c);	//	for logs

/*
 *	One line saying what was detected and what this build expects, for the
 *	dialog title and the log.
 */
std::string RS2DescribeSaveIdentity(const RS2SaveIdentity &id);

/*
 *	The dialog message for a class (a lang() string).
 */
char *RS2SaveClassMessage(RS2SaveClass c);

/*
 *	Automated runs (-fixture, -acceptlegacysave) take the "load it anyway"
 *	choice for LEGACY / OLDER files instead of asking.  Logged when used.
 */
bool RS2SaveAutoAcceptLegacy();

/*
 *	-savefixturecheck: write one small file per class, classify each, load and
 *	save a current layout round trip, log the results, and exit.
 */
bool RS2SaveFixtureCheckRequested();
bool RS2SaveFixtureCheckRun();

#endif	//	RS2SAVEIDENTITY_H_INCLUDED
