//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	DDS as a Stage 0 texture container.
//
//	Only what the installed RailSim content actually uses is accepted (see
//	docs v0.1.3-dds-compatibility-audit): the legacy header, DXT1 and DXT5,
//	2D, no colour key.  Everything else is refused with a reason, never
//	reinterpreted.  The blocks are kept exactly as stored - no decompression,
//	no conversion - because the GPU samples them as they are.
//
//	A DDS is a file format, not a material semantic.  Nothing here knows or
//	cares whether a texture is a normal map or an ORM map; v0.1.3 uses every
//	DDS as an ordinary base colour, which is what RailSim asks for.
//
//	No renderer type appears here.  The output is a CRS2TextureSource.

#ifndef RS2DDS_H_INCLUDED
#define RS2DDS_H_INCLUDED

#include "RS2TextureSource.h"

#include <string>

struct RS2DDSInfo
{
	RS2TextureSourceFormat format;
	unsigned int width, height;
	unsigned int storedMips;	//	levels in the file
	unsigned int requestedMips;	//	what the caller asked for, resolved
	unsigned int usedMips;		//	what the source describes
	unsigned long long fileBytes;

	RS2DDSInfo()
		: format(RS2_TEXTURE_SOURCE_NONE), width(0), height(0),
		  storedMips(0), requestedMips(0), usedMips(0), fileBytes(0){}
};

//	Whether a file should go to the DDS parser: a .dds name, or DDS bytes
//	under another name (Direct3D 8's loader looked at the content too).
bool RS2IsDDSFile(const char *path);

/*
 *	Parse DDS bytes into a native source.
 *
 *	colourKey	: must be zero; a keyed DDS is a compatibility blocker
 *	mipArgument	: RailSim's nMipLv.  Zero or less asks for a complete chain,
 *			  n asks for n levels.  The file's stored levels are used as
 *			  they are; when fewer are stored than asked for, the source
 *			  has the stored ones and info->usedMips says so.  No level is
 *			  ever generated.
 *	returns		: false with *error set, and *out cleared
 */
bool RS2ParseDDS(
	const unsigned char *data,
	size_t bytes,
	unsigned long colourKey,
	int mipArgument,
	CRS2TextureSource *out,
	RS2DDSInfo *info,
	std::string *error);

//	Read a file and parse it.  Files beyond RS2_DDS_MAX_FILE_BYTES are refused
//	before being read.
bool RS2LoadDDSFile(
	const char *path,
	unsigned long colourKey,
	int mipArgument,
	CRS2TextureSource *out,
	RS2DDSInfo *info,
	std::string *error);

//	The largest file read.  The largest installed DDS is 5.6 MB; a 16384 x
//	16384 BC3 with a full chain is about 358 MB.
#define RS2_DDS_MAX_FILE_BYTES (512u*1024u*1024u)

//	CPU-only validation for -dx12smoke: good files parse to the right levels,
//	every malformed or unsupported one is refused for the right reason.
bool RS2DDSSmoke();

#endif	//	RS2DDS_H_INCLUDED
