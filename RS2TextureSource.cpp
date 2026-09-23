//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	See RS2TextureSource.h.

#include "stdafx.h"
#include "RS2TextureSource.h"
#include "RS2DecodedImage.h"

const char *RS2TextureSourceFormatName(RS2TextureSourceFormat format){
	switch(format){
	case RS2_TEXTURE_SOURCE_RGBA8: return "RGBA8";
	case RS2_TEXTURE_SOURCE_BC1: return "BC1";
	case RS2_TEXTURE_SOURCE_BC3: return "BC3";
	default: return "none";
	}
}

unsigned int RS2TextureSourceUnitBytes(RS2TextureSourceFormat format){
	switch(format){
	case RS2_TEXTURE_SOURCE_RGBA8: return 4;
	case RS2_TEXTURE_SOURCE_BC1: return 8;
	case RS2_TEXTURE_SOURCE_BC3: return 16;
	default: return 0;
	}
}

bool RS2TextureSourceIsBlockCompressed(RS2TextureSourceFormat format){
	return format==RS2_TEXTURE_SOURCE_BC1 || format==RS2_TEXTURE_SOURCE_BC3;
}

/*
 *	Stored row size and row count of one level.
 *
 *	returns	: false if the level is empty or its size overflows 32 bits
 */
static bool RS2TextureSourceLevelShape(
	RS2TextureSourceFormat format, unsigned int width, unsigned int height,
	unsigned int *rowBytes, unsigned int *rows){
	const unsigned int unit = RS2TextureSourceUnitBytes(format);

	if(!unit || !width || !height) return false;

	unsigned long long across, down;

	if(RS2TextureSourceIsBlockCompressed(format)){
		across = ((unsigned long long)width+3)/4;
		down = ((unsigned long long)height+3)/4;
	}else{
		across = width;
		down = height;
	}

	const unsigned long long bytes = across*unit;

	if(bytes>0xffffffffULL || down>0xffffffffULL) return false;
	*rowBytes = (unsigned int)bytes;
	*rows = (unsigned int)down;
	return true;
}

unsigned long long RS2TextureSourceLevelBytes(
	RS2TextureSourceFormat format, unsigned int width, unsigned int height){
	unsigned int rowBytes = 0, rows = 0;

	if(!RS2TextureSourceLevelShape(format, width, height, &rowBytes, &rows)) return 0;
	return (unsigned long long)rowBytes*rows;
}

void CRS2TextureSource::Clear(){
	m_Format = RS2_TEXTURE_SOURCE_NONE;
	m_Mips.clear();
	m_Storage.clear();
}

bool CRS2TextureSource::ViewDecoded(const CRS2DecodedImage &image){
	Clear();
	if(!image.IsValid()) return false;

	unsigned int i;

	for(i = 0; i<image.GetMipCount(); i++){
		const RS2DecodedMip *mip = image.GetMip(i);
		RS2TextureSourceMip level;

		if(!mip || !mip->width || !mip->height || !mip->Data()){
			Clear();
			return false;
		}
		level.width = mip->width;
		level.height = mip->height;
		level.rowBytes = mip->width*4;
		level.rowPitch = mip->rowPitch;
		level.rows = mip->height;
		level.data = mip->Data();
		if(level.rowPitch<level.rowBytes){
			Clear();
			return false;
		}
		m_Mips.push_back(level);
	}
	m_Format = RS2_TEXTURE_SOURCE_RGBA8;
	return true;
}

bool CRS2TextureSource::AdoptNative(
	RS2TextureSourceFormat format,
	unsigned int width,
	unsigned int height,
	unsigned int mipCount,
	std::vector<unsigned char> &storage){
	Clear();
	if(!RS2TextureSourceUnitBytes(format) || !width || !height || !mipCount) return false;

	std::vector<RS2TextureSourceMip> levels;
	unsigned long long offset = 0;
	unsigned int i;

	for(i = 0; i<mipCount; i++){
		RS2TextureSourceMip level;
		const unsigned int w = width>>i ? width>>i : 1;
		const unsigned int h = height>>i ? height>>i : 1;

		if(!RS2TextureSourceLevelShape(format, w, h, &level.rowBytes, &level.rows)) return false;

		const unsigned long long bytes = (unsigned long long)level.rowBytes*level.rows;

		if(offset+bytes>storage.size()) return false;
		level.width = w;
		level.height = h;
		level.rowPitch = level.rowBytes;
		level.data = 0;		//	fixed up once the storage has moved
		levels.push_back(level);
		offset += bytes;
	}

	m_Storage.swap(storage);
	offset = 0;
	for(i = 0; i<levels.size(); i++){
		levels[i].data = &m_Storage[0]+(size_t)offset;
		offset += (unsigned long long)levels[i].rowBytes*levels[i].rows;
	}
	m_Mips.swap(levels);
	m_Format = format;
	return true;
}

static void RS2TextureSourceSmokeStep(const char *name, bool passed, bool *all){
	Debug("RS2TEXSOURCESMOKE|%-32s|%s\n", name, passed ? "pass" : "FAIL");
	if(!passed) *all = false;
}

bool RS2TextureSourceSmoke(){
	bool all = true;

	//	A decoded image is viewed, not copied.
	{
		CRS2DecodedImage image;
		std::vector<unsigned char> pixels(4*2*4, 0x7f);
		CRS2TextureSource source;
		bool ok = image.AppendMip(4, 2, pixels) && source.ViewDecoded(image);
		const RS2TextureSourceMip *mip = source.GetMip(0);

		ok = ok && source.GetFormat()==RS2_TEXTURE_SOURCE_RGBA8 && mip
			&& mip->rows==2 && mip->rowBytes==16 && mip->rowPitch==16
			&& mip->data==image.GetMip(0)->Data();
		RS2TextureSourceSmokeStep("decoded image is a view", ok, &all);
	}

	//	Two BC1 levels of an 8 x 8 image: 2 x 2 blocks, then one block.
	{
		std::vector<unsigned char> storage(32+8);
		CRS2TextureSource source;
		const unsigned char *before;

		storage[32] = 0xab;
		before = &storage[0];
		bool ok = source.AdoptNative(RS2_TEXTURE_SOURCE_BC1, 8, 8, 2, storage);
		const RS2TextureSourceMip *top = source.GetMip(0);
		const RS2TextureSourceMip *next = source.GetMip(1);

		ok = ok && storage.empty() && top && next
			&& top->rows==2 && top->rowBytes==16 && top->width==8
			&& next->rows==1 && next->rowBytes==8 && next->width==4
			&& next->data==top->data+32 && next->data[0]==0xab
			&& top->data==before && source.GetDataBytes()==40;
		RS2TextureSourceSmokeStep("BC1 levels at their offsets", ok, &all);
	}

	//	A level smaller than a block still takes a whole block.
	{
		std::vector<unsigned char> storage(16*3);
		CRS2TextureSource source;
		bool ok = source.AdoptNative(RS2_TEXTURE_SOURCE_BC3, 4, 4, 3, storage);
		const RS2TextureSourceMip *last = source.GetMip(2);

		ok = ok && last && last->width==1 && last->height==1
			&& last->rows==1 && last->rowBytes==16;
		RS2TextureSourceSmokeStep("sub-block level is one block", ok, &all);
	}

	//	Short storage is refused and left where it was.
	{
		std::vector<unsigned char> storage(15);
		CRS2TextureSource source;
		const bool refused = !source.AdoptNative(RS2_TEXTURE_SOURCE_BC3, 4, 4, 1, storage);

		RS2TextureSourceSmokeStep("short storage refused",
			refused && storage.size()==15 && !source.IsValid(), &all);
	}

	//	Sizes that do not fit in 32 bits are refused, not wrapped.
	RS2TextureSourceSmokeStep("level size overflow refused",
		RS2TextureSourceLevelBytes(RS2_TEXTURE_SOURCE_RGBA8, 0xffffffffu, 2)==0, &all);

	{
		std::vector<unsigned char> storage(64);
		CRS2TextureSource source;

		RS2TextureSourceSmokeStep("unknown format refused",
			!source.AdoptNative(RS2_TEXTURE_SOURCE_NONE, 4, 4, 1, storage)
			&& storage.size()==64, &all);
	}
	return all;
}

unsigned long long CRS2TextureSource::GetDataBytes() const{
	unsigned long long total = 0;
	unsigned int i;

	for(i = 0; i<m_Mips.size(); i++)
		total += (unsigned long long)m_Mips[i].rowBytes*m_Mips[i].rows;
	return total;
}
