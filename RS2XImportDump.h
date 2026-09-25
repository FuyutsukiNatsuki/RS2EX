//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	A deterministic dump of what the .x importer hands the runtime
//	(-ximportdump).
//
//	Every .x file under the working directory is imported through the same
//	call CMesh::Load makes, and the result - vertex layout, vertex bytes,
//	indices, per-face material ids, subsets, materials, texture names and the
//	bounds carried out of the importer - is written, byte for byte, to
//	ximport-dump.bin.  tools/ximport_compare.py reads two dumps and compares
//	them, ordered and order-independently, so an importer can be checked
//	against another without rendering anything.
//
//	v0.2.0 uses it to freeze the D3DX8 importer's output (the oracle) before
//	that importer is replaced.

#ifndef RS2XIMPORTDUMP_H_INCLUDED
#define RS2XIMPORTDUMP_H_INCLUDED

bool RS2XImportDumpRequested();
bool RS2XImportDumpRun();

#endif	//	RS2XIMPORTDUMP_H_INCLUDED
