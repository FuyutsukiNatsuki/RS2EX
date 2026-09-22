//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	Renderer-neutral decoded image data.
//
//	Every mip is tightly-packed, top-down RGBA8.  There is deliberately no
//	Direct3D resource, descriptor or format enum here: decoding can be tested
//	without constructing either renderer backend.

#ifndef RS2DECODEDIMAGE_H_INCLUDED
#define RS2DECODEDIMAGE_H_INCLUDED

#include <string>
#include <vector>

struct RS2DecodedMip
{
	unsigned int width;
	unsigned int height;
	unsigned int rowPitch;
	std::vector<unsigned char> pixels;

	RS2DecodedMip() : width(0), height(0), rowPitch(0){}

	const unsigned char *Data() const{
		return pixels.empty() ? 0 : &pixels[0];
	}
	unsigned char *Data(){
		return pixels.empty() ? 0 : &pixels[0];
	}
};

class CRS2DecodedImage
{
private:
	std::vector<RS2DecodedMip> m_Mips;

public:
	void Clear(){ m_Mips.clear(); }
	bool IsValid() const{ return !m_Mips.empty(); }

	unsigned int GetWidth() const{
		return m_Mips.empty() ? 0 : m_Mips[0].width;
	}
	unsigned int GetHeight() const{
		return m_Mips.empty() ? 0 : m_Mips[0].height;
	}
	unsigned int GetMipCount() const{ return (unsigned int)m_Mips.size(); }

	const RS2DecodedMip *GetMip(unsigned int index) const{
		return index<m_Mips.size() ? &m_Mips[index] : 0;
	}

	//	Decoder construction surface.  Copies one complete tightly-packed mip.
	bool AppendMip(
		unsigned int width,
		unsigned int height,
		const std::vector<unsigned char> &pixels);
};

/*
 *	Decode an installed-content file.  v0.1.2 supports PNG and BMP explicitly.
 *
 *	colourKey	: 32-bit ARGB; zero disables keying
 *	mipArgument	: zero builds a complete chain, positive values request that
 *			  many levels
 *	error		: optional human-readable reason, cleared on success
 */
bool RS2DecodeImageFile(
	const char *path,
	unsigned long colourKey,
	int mipArgument,
	CRS2DecodedImage *out,
	std::string *error);

/*
 *	Decode a Win32 RT_BITMAP/DIB from the main executable.
 */
bool RS2DecodeImageResource(
	const char *resourceName,
	unsigned long colourKey,
	int mipArgument,
	CRS2DecodedImage *out,
	std::string *error);

//	CPU-only deterministic validation used by -dx12smoke.
bool RS2DecodedImageSmoke();

#endif	//	RS2DECODEDIMAGE_H_INCLUDED
