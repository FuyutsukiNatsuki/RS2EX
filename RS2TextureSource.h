//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-23.
//
//	What a texture upload is made from, independent of any renderer.
//
//	v0.1.2 uploaded one thing: a decoded, tightly-packed RGBA8 image.  DDS
//	files carry block-compressed data that the GPU samples directly, and
//	pretending those blocks are RGBA8 rows would be wrong in every row, so the
//	upload now takes a source that says what it holds.
//
//	A source is either a view of a CRS2DecodedImage (PNG, BMP, DIB - nothing
//	is copied, and the accepted decode path is untouched) or native storage
//	(DDS - the file's own blocks).  Either way it describes each mip in the
//	units the data is stored in: texel rows for RGBA8, block rows for BC.
//
//	No renderer-native type appears here.  The backend maps the format.

#ifndef RS2TEXTURESOURCE_H_INCLUDED
#define RS2TEXTURESOURCE_H_INCLUDED

#include <vector>

class CRS2DecodedImage;

enum RS2TextureSourceFormat
{
	RS2_TEXTURE_SOURCE_NONE,
	RS2_TEXTURE_SOURCE_RGBA8,	//	8-bit R, G, B, A, top-down
	RS2_TEXTURE_SOURCE_BC1,		//	DXT1: 4x4 blocks of 8 bytes, 1-bit alpha
	RS2_TEXTURE_SOURCE_BC3		//	DXT5: 4x4 blocks of 16 bytes, interpolated alpha
};

const char *RS2TextureSourceFormatName(RS2TextureSourceFormat format);

//	Bytes per block for BC formats, bytes per texel for RGBA8.
unsigned int RS2TextureSourceUnitBytes(RS2TextureSourceFormat format);
bool RS2TextureSourceIsBlockCompressed(RS2TextureSourceFormat format);

struct RS2TextureSourceMip
{
	unsigned int width;		//	texels
	unsigned int height;	//	texels
	unsigned int rowBytes;	//	bytes in one stored row (texel row or block row)
	unsigned int rowPitch;	//	distance between stored rows, >= rowBytes
	unsigned int rows;		//	stored rows: height for RGBA8, block rows for BC
	const unsigned char *data;
};

class CRS2TextureSource
{
private:
	RS2TextureSourceFormat m_Format;
	std::vector<RS2TextureSourceMip> m_Mips;

	//	Owned bytes for native sources.  Views of a decoded image leave this
	//	empty and point into the image, which must outlive the source.
	std::vector<unsigned char> m_Storage;

	CRS2TextureSource(const CRS2TextureSource &);
	CRS2TextureSource &operator=(const CRS2TextureSource &);

public:
	CRS2TextureSource() : m_Format(RS2_TEXTURE_SOURCE_NONE){}

	void Clear();

	RS2TextureSourceFormat GetFormat() const{ return m_Format; }
	unsigned int GetMipCount() const{ return (unsigned int)m_Mips.size(); }
	unsigned int GetWidth() const{ return m_Mips.empty() ? 0 : m_Mips[0].width; }
	unsigned int GetHeight() const{ return m_Mips.empty() ? 0 : m_Mips[0].height; }
	const RS2TextureSourceMip *GetMip(unsigned int index) const{
		return index<m_Mips.size() ? &m_Mips[index] : 0;
	}
	bool IsValid() const{ return m_Format!=RS2_TEXTURE_SOURCE_NONE && !m_Mips.empty(); }

	//	Point at a decoded RGBA8 image without copying it.
	bool ViewDecoded(const CRS2DecodedImage &image);

	//	Take ownership of native bytes and describe `mipCount` tightly-packed
	//	levels starting at offset 0.  Every level is checked against the
	//	storage size before anything is kept.
	bool AdoptNative(
		RS2TextureSourceFormat format,
		unsigned int width,
		unsigned int height,
		unsigned int mipCount,
		std::vector<unsigned char> &storage);

	//	Bytes the stored levels occupy, tightly packed.
	unsigned long long GetDataBytes() const;
};

/*
 *	The size of one level, tightly packed, or 0 if it cannot be expressed
 *	without overflow.  width and height are the level's texel size.
 */
unsigned long long RS2TextureSourceLevelBytes(
	RS2TextureSourceFormat format, unsigned int width, unsigned int height);

//	CPU-only validation used by -dx12smoke: a decoded view is a view, native
//	levels land at the offsets their sizes imply, and short storage, overflow
//	and unknown formats are refused without touching the caller's bytes.
bool RS2TextureSourceSmoke();

#endif	//	RS2TEXTURESOURCE_H_INCLUDED
