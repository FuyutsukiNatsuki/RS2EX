//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	See RS2DDS.h.  The header layout is the documented DDS_HEADER /
//	DDS_PIXELFORMAT; it is read field by field from bytes rather than cast,
//	so an unaligned or short buffer can never be read past its end.

#include "stdafx.h"
#include "RS2DDS.h"

#include <string.h>
#include <vector>

static const unsigned int RS2_DDS_MAGIC = 0x20534444;	//	"DDS "
static const unsigned int RS2_DDS_HEADER_BYTES = 124;
static const unsigned int RS2_DDS_PIXELFORMAT_BYTES = 32;
static const unsigned int RS2_DDS_DATA_OFFSET = 4+124;

static const unsigned int RS2_DDSD_MIPMAPCOUNT = 0x00020000;
static const unsigned int RS2_DDSD_DEPTH = 0x00800000;
static const unsigned int RS2_DDPF_FOURCC = 0x00000004;
static const unsigned int RS2_DDSCAPS2_CUBEMAP = 0x00000200;
static const unsigned int RS2_DDSCAPS2_VOLUME = 0x00200000;

//	Direct3D 12's Texture2D limit.  Stated here rather than taken from the
//	Direct3D headers so the parser stays renderer-neutral.
static const unsigned int RS2_DDS_MAX_DIMENSION = 16384;

static unsigned int RS2DDSFourCC(char a, char b, char c, char d){
	return (unsigned int)(unsigned char)a | ((unsigned int)(unsigned char)b<<8)
		| ((unsigned int)(unsigned char)c<<16) | ((unsigned int)(unsigned char)d<<24);
}

static unsigned int RS2DDSRead32(const unsigned char *data, size_t offset){
	unsigned int value;

	memcpy(&value, data+offset, 4);
	return value;
}

static bool RS2DDSFail(std::string *error, CRS2TextureSource *out, const char *why){
	if(error) *error = why;
	if(out) out->Clear();
	return false;
}

static unsigned int RS2DDSFullChain(unsigned int width, unsigned int height){
	unsigned int size = width>height ? width : height;
	unsigned int levels = 1;

	while(size>1){
		size >>= 1;
		levels++;
	}
	return levels;
}

bool RS2IsDDSFile(const char *path){
	if(!path) return false;

	const size_t length = strlen(path);

	if(length>=4 && _stricmp(path+length-4, ".dds")==0) return true;

	FILE *file = fopen(path, "rb");

	if(!file) return false;

	unsigned char magic[4];
	const bool dds = fread(magic, 1, 4, file)==4 && RS2DDSRead32(magic, 0)==RS2_DDS_MAGIC;

	fclose(file);
	return dds;
}

bool RS2ParseDDS(
	const unsigned char *data,
	size_t bytes,
	unsigned long colourKey,
	int mipArgument,
	CRS2TextureSource *out,
	RS2DDSInfo *info,
	std::string *error){
	RS2DDSInfo local;

	if(!info) info = &local;
	*info = RS2DDSInfo();
	info->fileBytes = bytes;
	if(error) error->clear();
	if(!out) return RS2DDSFail(error, out, "no output");
	out->Clear();

	if(!data || bytes<RS2_DDS_DATA_OFFSET)
		return RS2DDSFail(error, out, "truncated header");
	if(RS2DDSRead32(data, 0)!=RS2_DDS_MAGIC)
		return RS2DDSFail(error, out, "not a DDS file (magic)");
	if(RS2DDSRead32(data, 4)!=RS2_DDS_HEADER_BYTES)
		return RS2DDSFail(error, out, "unexpected DDS header size");
	if(RS2DDSRead32(data, 4+72)!=RS2_DDS_PIXELFORMAT_BYTES)
		return RS2DDSFail(error, out, "unexpected DDS pixel format size");

	const unsigned int flags = RS2DDSRead32(data, 8);
	const unsigned int height = RS2DDSRead32(data, 12);
	const unsigned int width = RS2DDSRead32(data, 16);
	const unsigned int depth = RS2DDSRead32(data, 24);
	const unsigned int mipCount = RS2DDSRead32(data, 28);
	const unsigned int pixelFlags = RS2DDSRead32(data, 4+76);
	const unsigned int fourCC = RS2DDSRead32(data, 4+80);
	const unsigned int caps2 = RS2DDSRead32(data, 4+108);

	//	What the file is, before whether it is well-formed: an unsupported
	//	class is the more useful thing to say.
	if(caps2&RS2_DDSCAPS2_CUBEMAP) return RS2DDSFail(error, out, "cubemap DDS is not supported");
	if((caps2&RS2_DDSCAPS2_VOLUME) || ((flags&RS2_DDSD_DEPTH) && depth>1))
		return RS2DDSFail(error, out, "volume DDS is not supported");
	if(!(pixelFlags&RS2_DDPF_FOURCC))
		return RS2DDSFail(error, out, "uncompressed DDS is not supported");
	if(fourCC==RS2DDSFourCC('D', 'X', '1', '0'))
		return RS2DDSFail(error, out, "DX10-header DDS is not supported");

	RS2TextureSourceFormat format;

	if(fourCC==RS2DDSFourCC('D', 'X', 'T', '1')) format = RS2_TEXTURE_SOURCE_BC1;
	else if(fourCC==RS2DDSFourCC('D', 'X', 'T', '5')) format = RS2_TEXTURE_SOURCE_BC3;
	else return RS2DDSFail(error, out, "unsupported DDS format (FourCC)");

	if(!width || !height) return RS2DDSFail(error, out, "DDS has a zero dimension");
	if(width>RS2_DDS_MAX_DIMENSION || height>RS2_DDS_MAX_DIMENSION)
		return RS2DDSFail(error, out, "DDS is larger than a Direct3D 12 texture");
	if(width%4 || height%4)
		return RS2DDSFail(error, out, "block-compressed DDS size is not a multiple of 4");

	const unsigned int fullChain = RS2DDSFullChain(width, height);
	const unsigned int stored = ((flags&RS2_DDSD_MIPMAPCOUNT) && mipCount) ? mipCount : 1;

	if(stored>fullChain) return RS2DDSFail(error, out, "DDS stores more mips than its size allows");

	//	A keyed DDS would need decompressing to compare colours.  Refused, not
	//	ignored: dropping the key would draw what the author wanted hidden.
	if(colourKey) return RS2DDSFail(error, out, "colour key requested for a DDS (not supported)");

	//	Every stored level must be in the file, even the ones not used: a
	//	file whose tail is missing is damaged, and damage is not uploaded.
	unsigned long long payload = 0;
	unsigned int i;

	for(i = 0; i<stored; i++){
		const unsigned int w = width>>i ? width>>i : 1;
		const unsigned int h = height>>i ? height>>i : 1;
		const unsigned long long level = RS2TextureSourceLevelBytes(format, w, h);

		if(!level) return RS2DDSFail(error, out, "DDS level size overflows");
		payload += level;
	}
	if(payload>(unsigned long long)(bytes-RS2_DDS_DATA_OFFSET))
		return RS2DDSFail(error, out, "truncated DDS payload");

	const unsigned int requested = mipArgument<=0
		? fullChain : ((unsigned int)mipArgument<fullChain ? (unsigned int)mipArgument : fullChain);
	const unsigned int used = requested<stored ? requested : stored;
	unsigned long long usedBytes = 0;

	for(i = 0; i<used; i++){
		const unsigned int w = width>>i ? width>>i : 1;
		const unsigned int h = height>>i ? height>>i : 1;

		usedBytes += RS2TextureSourceLevelBytes(format, w, h);
	}

	std::vector<unsigned char> storage(data+RS2_DDS_DATA_OFFSET,
		data+RS2_DDS_DATA_OFFSET+(size_t)usedBytes);

	if(!out->AdoptNative(format, width, height, used, storage))
		return RS2DDSFail(error, out, "DDS levels do not fit their storage");

	info->format = format;
	info->width = width;
	info->height = height;
	info->storedMips = stored;
	info->requestedMips = requested;
	info->usedMips = used;
	return true;
}

bool RS2LoadDDSFile(
	const char *path,
	unsigned long colourKey,
	int mipArgument,
	CRS2TextureSource *out,
	RS2DDSInfo *info,
	std::string *error){
	if(out) out->Clear();
	if(info) *info = RS2DDSInfo();
	if(!path) return RS2DDSFail(error, out, "no path");

	HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if(file==INVALID_HANDLE_VALUE) return RS2DDSFail(error, out, "DDS file cannot be opened");

	LARGE_INTEGER size;

	if(!GetFileSizeEx(file, &size) || size.QuadPart<0
			|| (unsigned long long)size.QuadPart>RS2_DDS_MAX_FILE_BYTES){
		CloseHandle(file);
		return RS2DDSFail(error, out, "DDS file is too large");
	}

	std::vector<unsigned char> bytes((size_t)size.QuadPart);
	DWORD read = 0;
	const bool ok = bytes.empty()
		|| (ReadFile(file, &bytes[0], (DWORD)bytes.size(), &read, NULL) && read==bytes.size());

	CloseHandle(file);
	if(!ok) return RS2DDSFail(error, out, "DDS file cannot be read");

	return RS2ParseDDS(bytes.empty() ? 0 : &bytes[0], bytes.size(),
		colourKey, mipArgument, out, info, error);
}

////////////////////////////////////////////////////////////////////////////////
//	CPU-only validation
////////////////////////////////////////////////////////////////////////////////

/*
 *	A DDS in memory: header, then `payload` bytes of recognisable data.
 */
static std::vector<unsigned char> RS2DDSMake(
	unsigned int width, unsigned int height, unsigned int fourCC,
	unsigned int mipCount, unsigned int payload,
	unsigned int extraFlags = 0, unsigned int pixelFlags = RS2_DDPF_FOURCC,
	unsigned int caps2 = 0){
	std::vector<unsigned char> file(RS2_DDS_DATA_OFFSET+payload, 0);
	unsigned int values[32];

	memset(values, 0, sizeof(values));
	values[0] = RS2_DDS_MAGIC;
	values[1] = RS2_DDS_HEADER_BYTES;
	values[2] = 0x1007|extraFlags|(mipCount ? RS2_DDSD_MIPMAPCOUNT : 0);
	values[3] = height;
	values[4] = width;
	values[7] = mipCount;
	values[19] = RS2_DDS_PIXELFORMAT_BYTES;
	values[20] = pixelFlags;
	values[21] = fourCC;
	values[27] = 0x1000;
	values[28] = caps2;
	memcpy(&file[0], values, RS2_DDS_DATA_OFFSET);

	unsigned int i;

	for(i = 0; i<payload; i++) file[RS2_DDS_DATA_OFFSET+i] = (unsigned char)(i*7+1);
	return file;
}

static bool RS2DDSRefused(const std::vector<unsigned char> &file, unsigned long key,
	int mips, const char *expect){
	CRS2TextureSource source;
	std::string error;
	const bool parsed = RS2ParseDDS(file.empty() ? 0 : &file[0], file.size(),
		key, mips, &source, 0, &error);

	return !parsed && !source.IsValid() && error.find(expect)!=std::string::npos;
}

static void RS2DDSSmokeStep(const char *name, bool passed, bool *all){
	Debug("RS2DDSSMOKE|%-36s|%s\n", name, passed ? "pass" : "FAIL");
	if(!passed) *all = false;
}

bool RS2DDSSmoke(){
	const unsigned int dxt1 = RS2DDSFourCC('D', 'X', 'T', '1');
	const unsigned int dxt5 = RS2DDSFourCC('D', 'X', 'T', '5');
	bool all = true;

	//	8 x 8 BC1, no stored mips, one level asked for: 2 x 2 blocks.
	{
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt1, 0, 32);
		CRS2TextureSource source;
		RS2DDSInfo info;
		std::string error;
		bool ok = RS2ParseDDS(&file[0], file.size(), 0, 1, &source, &info, &error);
		const RS2TextureSourceMip *top = source.GetMip(0);

		ok = ok && info.format==RS2_TEXTURE_SOURCE_BC1 && info.storedMips==1
			&& info.usedMips==1 && top && top->rows==2 && top->rowBytes==16
			&& top->data[0]==file[RS2_DDS_DATA_OFFSET] && top->data[31]==file[RS2_DDS_DATA_OFFSET+31];
		RS2DDSSmokeStep("BC1 single level", ok, &all);
	}

	//	8 x 8 BC3 with its whole chain stored and asked for: 4 levels.
	{
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt5, 4, 64+16+16+16);
		CRS2TextureSource source;
		RS2DDSInfo info;
		std::string error;
		bool ok = RS2ParseDDS(&file[0], file.size(), 0, 0, &source, &info, &error);

		ok = ok && info.format==RS2_TEXTURE_SOURCE_BC3 && info.storedMips==4
			&& info.requestedMips==4 && info.usedMips==4 && source.GetMipCount()==4
			&& source.GetMip(3)->data==source.GetMip(0)->data+64+16+16;
		RS2DDSSmokeStep("BC3 stored chain used", ok, &all);
	}

	//	A chain asked for, one level stored: the stored level, and a record
	//	that the request was not met.  Nothing is generated.
	{
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt5, 0, 64);
		CRS2TextureSource source;
		RS2DDSInfo info;
		std::string error;
		bool ok = RS2ParseDDS(&file[0], file.size(), 0, 0, &source, &info, &error);

		ok = ok && info.requestedMips==4 && info.usedMips==1 && source.GetMipCount()==1;
		RS2DDSSmokeStep("missing mips are not generated", ok, &all);
	}

	//	Fewer asked for than stored: only those.
	{
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt5, 4, 112);
		CRS2TextureSource source;
		RS2DDSInfo info;
		std::string error;
		bool ok = RS2ParseDDS(&file[0], file.size(), 0, 2, &source, &info, &error);

		ok = ok && info.usedMips==2 && source.GetMipCount()==2 && source.GetDataBytes()==64+16;
		RS2DDSSmokeStep("request below stored chain", ok, &all);
	}

	{
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt1, 0, 32);

		file[0] = 'X';
		RS2DDSSmokeStep("invalid magic refused", RS2DDSRefused(file, 0, 1, "magic"), &all);
	}
	{
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt1, 0, 32);

		file.resize(100);
		RS2DDSSmokeStep("truncated header refused", RS2DDSRefused(file, 0, 1, "truncated header"), &all);
	}
	{
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt1, 0, 31);

		RS2DDSSmokeStep("truncated payload refused", RS2DDSRefused(file, 0, 1, "truncated DDS payload"), &all);
	}
	{
		//	A tail level missing, though only the top level is asked for.
		std::vector<unsigned char> file = RS2DDSMake(8, 8, dxt5, 4, 64+16+16);

		RS2DDSSmokeStep("damaged unused tail refused", RS2DDSRefused(file, 0, 1, "truncated DDS payload"), &all);
	}
	RS2DDSSmokeStep("DXT3 refused",
		RS2DDSRefused(RS2DDSMake(8, 8, RS2DDSFourCC('D', 'X', 'T', '3'), 0, 64), 0, 1, "FourCC"), &all);
	RS2DDSSmokeStep("DX10 header refused",
		RS2DDSRefused(RS2DDSMake(8, 8, RS2DDSFourCC('D', 'X', '1', '0'), 0, 64+20), 0, 1, "DX10"), &all);
	RS2DDSSmokeStep("uncompressed refused",
		RS2DDSRefused(RS2DDSMake(8, 8, 0, 0, 256, 0, 0x41), 0, 1, "uncompressed"), &all);
	RS2DDSSmokeStep("cubemap refused",
		RS2DDSRefused(RS2DDSMake(8, 8, dxt1, 0, 32*6, 0, RS2_DDPF_FOURCC, 0xfe00), 0, 1, "cubemap"), &all);
	RS2DDSSmokeStep("volume refused",
		RS2DDSRefused(RS2DDSMake(8, 8, dxt1, 0, 64, 0, RS2_DDPF_FOURCC, RS2_DDSCAPS2_VOLUME), 0, 1, "volume"), &all);
	RS2DDSSmokeStep("non-multiple-of-4 refused",
		RS2DDSRefused(RS2DDSMake(6, 8, dxt1, 0, 32), 0, 1, "multiple of 4"), &all);
	RS2DDSSmokeStep("zero dimension refused",
		RS2DDSRefused(RS2DDSMake(0, 8, dxt1, 0, 32), 0, 1, "zero dimension"), &all);
	RS2DDSSmokeStep("oversize refused",
		RS2DDSRefused(RS2DDSMake(32768, 8, dxt1, 0, 32), 0, 1, "larger"), &all);
	RS2DDSSmokeStep("too many mips refused",
		RS2DDSRefused(RS2DDSMake(8, 8, dxt1, 5, 64), 0, 1, "more mips"), &all);
	RS2DDSSmokeStep("colour key refused",
		RS2DDSRefused(RS2DDSMake(8, 8, dxt1, 0, 32), 0xff000000, 1, "colour key"), &all);

	//	A header claiming the largest size over a tiny file: the size check
	//	must fail on the arithmetic, not on reading past the buffer.
	RS2DDSSmokeStep("huge header over tiny file refused",
		RS2DDSRefused(RS2DDSMake(16384, 16384, dxt5, 15, 16), 0, 0, "truncated DDS payload"), &all);

	return all;
}
