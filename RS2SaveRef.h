//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Serialized object references: the identity of an object inside one saved
//	layout (v0.3.0, save schema 2).
//
//	RailSim II wrote every object's own address ("Address = %p;") and the
//	addresses of the objects it referred to, then on loading mapped each old
//	address to the new object and replaced the stored addresses in a second
//	pass.  The addresses were only ever used as identities - nothing
//	dereferenced them after loading - but they were process addresses, eight
//	hex digits on x86 and sixteen on x64.  A save is data, not memory.
//
//	RS2SaveRef is that identity, made explicit:
//
//	    0 is null; anything else names one object of one saved layout;
//	    it is 64 bits wide whatever the process is;
//	    it is never a pointer and is never dereferenced.
//
//	Saving (schema 2) numbers objects 1, 2, 3, ... in the order the save first
//	meets them - as a definition or as a reference, whichever comes first -
//	so the same scene always saves to the same numbers.  Written in decimal.
//
//	Loading reads the numbers into a table of reference -> new object.  Files
//	without the schema-2 identity (RailSim II, RS2EX v0.1.x, and RS2EX v0.2.0
//	= schema 1) wrote hexadecimal addresses; those are read as opaque 64-bit
//	numbers into the same table, and never treated as addresses.
//
//	The inherited readers keep a reference in the referring object's pointer
//	field until the second pass replaces it (RestoreAddress / ReplaceAdr).  The
//	value parked there is a pending RS2SaveRef, not an address: only
//	RS2SaveRefToSlot / RS2SaveRefFromSlot convert, and nothing may use the
//	field before the second pass has run.  On x64 the 64-bit reference fits
//	the pointer; a 32-bit build refuses references that would not.

#ifndef RS2SAVEREF_H_INCLUDED
#define RS2SAVEREF_H_INCLUDED

#include <stdio.h>
#include <vector>

typedef unsigned long long RS2SaveRef;

//	printf format for writing a reference (schema 2, decimal)
#define RS2_SAVEREF_FMT	"%llu"

enum RS2SaveRefSyntax{
	RS2_SAVEREF_HEX,		//	RailSim II / RS2EX v0.1.x / v0.2.0 (schema 1): %p spelling
	RS2_SAVEREF_DECIMAL,	//	RS2EX v0.3.0 (schema 2)
};

//	---- loading ----------------------------------------------------------------

//	Start a load: empty table, hexadecimal until the identity says otherwise.
void RS2SaveRefBeginLoad();
void RS2SaveRefSetSyntax(RS2SaveRefSyntax syntax);
RS2SaveRefSyntax RS2SaveRefGetSyntax();

//	One reference.  NULL on anything that is not a well-formed number in the
//	current syntax, or does not fit 64 bits (the caller throws CSynErr).
char *RS2SaveRefValue(char *str, RS2SaveRef *out);
char *RS2AsgnSaveRef(char *str, char *name, RS2SaveRef *out, int n = 1, bool fill = false);

//	The same, parked as a pending reference in a pointer field.
char *RS2SaveRefSlotValue(char *str, void **slot);
char *RS2AsgnSaveRefSlot(char *str, char *name, void **slot);
void *RS2SaveRefToSlot(RS2SaveRef ref);
RS2SaveRef RS2SaveRefFromSlot(const void *slot);

//	Register an object under its reference.  false for 0 or for a reference
//	already registered (a duplicate makes the graph ambiguous: the caller
//	refuses the file).
bool RS2SaveRefRegister(RS2SaveRef ref, void *object);

//	The object for a reference; NULL for 0, and NULL (counted) for a
//	reference nothing registered.
void *RS2SaveRefResolve(RS2SaveRef ref);
unsigned int RS2SaveRefRegisteredCount();
unsigned int RS2SaveRefUnresolvedCount();

//	DepartureTime: schema 2 writes the double itself; the old files wrote its
//	two 32-bit halves (low, high) as hexadecimal "addresses".
char *RS2AsgnSaveDouble(char *str, char *name, double *out);
bool RS2SaveDoubleFromHalves(RS2SaveRef low, RS2SaveRef high, double *out);

//	---- saving -----------------------------------------------------------------

//	A save runs twice (CSaveFile::Save): first a numbering pass, whose output
//	is thrown away, in which only definitions ("Address = ") are numbered, in
//	the order they are written; then the real pass.  So a reference written
//	before its target's definition - or while iterating a container ordered by
//	pointer value - never decides a number, and the same scene always saves to
//	the same numbers.
void RS2SaveRefBeginSave();
void RS2SaveRefSetNumbering(bool numbering);
RS2SaveRef RS2SaveRefDefine(const void *object);	//	the object's own "Address = "
RS2SaveRef RS2SaveRefOf(const void *object);		//	a reference; NULL -> 0
void RS2SaveRefEndSave();

//	Write a reference list ordered by reference, for containers whose own
//	order is their elements' addresses (set<T *>, map<T *, ...>).
void RS2SaveRefWriteSorted(FILE *df, const std::vector<const void *> &objects);

#endif	//	RS2SAVEREF_H_INCLUDED
