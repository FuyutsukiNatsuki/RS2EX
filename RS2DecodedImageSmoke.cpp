//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	CPU-only deterministic tests for the v0.1.2 decoded-image boundary.

#include "stdafx.h"
#include "RS2DecodedImage.h"

static const unsigned char s_PngFixture[] = {
	0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
	0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,
	0x08,0x06,0x00,0x00,0x00,0x7f,0xa8,0x7d,0x63,0x00,0x00,0x00,
	0x25,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xc0,0xf0,
	0x9f,0xe1,0x3f,0x43,0x03,0x98,0xfa,0xff,0xff,0x3f,0x03,0x03,
	0x03,0xc3,0x7f,0x2e,0x11,0x39,0x8d,0x06,0x07,0x85,0xff,0x20,
	0x21,0x00,0xef,0xb1,0x0e,0xb8,0xc0,0xa3,0xb5,0xa4,0x00,0x00,
	0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};

static const unsigned char s_BmpFixture[] = {
	0x42,0x4d,0x4e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,
	0x00,0x00,0x28,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x00,
	0x00,0x00,0x01,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x18,0x00,
	0x00,0x00,0xc4,0x0e,0x00,0x00,0xc4,0x0e,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1e,0x14,0x0a,
	0x20,0x40,0x80,0xff,0x00,0xff,0x00,0x00,0xff,0x00,0xff,0x00,
	0xff,0x00,0x00,0xff,0xff,0xff
};

static const unsigned char s_IndexedPngFixture[] = {
	0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
	0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,
	0x02,0x03,0x00,0x00,0x00,0x0f,0xd8,0xe5,0xb7,0x00,0x00,0x00,
	0x0c,0x50,0x4c,0x54,0x45,0xff,0x00,0x00,0x00,0xff,0x00,0x00,
	0x00,0xff,0x00,0x00,0x00,0xfb,0xbe,0x46,0xe4,0x00,0x00,0x00,
	0x04,0x74,0x52,0x4e,0x53,0xff,0x80,0x00,0xff,0xa1,0xa1,0x94,
	0x66,0x00,0x00,0x00,0x0c,0x49,0x44,0x41,0x54,0x78,0xda,0x63,
	0x10,0x60,0xd8,0x00,0x00,0x00,0xe4,0x00,0xc1,0x19,0x55,0x3b,
	0xfb,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,
	0x82
};

static bool RS2WriteDecodedFixture(
	const char *path,
	const unsigned char *bytes,
	unsigned int count
){
	HANDLE file = CreateFileA(
		path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY, NULL);
	if(file==INVALID_HANDLE_VALUE) return false;

	DWORD written = 0;
	const bool ok = WriteFile(file, bytes, count, &written, NULL)!=FALSE &&
		written==count;
	CloseHandle(file);
	return ok;
}

bool RS2WriteKnownAlphaPngFixture(const char *path){
	return path && RS2WriteDecodedFixture(
		path, s_IndexedPngFixture, sizeof(s_IndexedPngFixture));
}

static bool RS2DecodedPixelEquals(
	const CRS2DecodedImage &image,
	unsigned int mipIndex,
	unsigned int x,
	unsigned int y,
	unsigned char r,
	unsigned char g,
	unsigned char b,
	unsigned char a
){
	const RS2DecodedMip *mip = image.GetMip(mipIndex);
	if(!mip || x>=mip->width || y>=mip->height) return false;
	const unsigned char *pixel = mip->Data()+y*mip->rowPitch+x*4;
	return pixel[0]==r && pixel[1]==g && pixel[2]==b && pixel[3]==a;
}

static void RS2DecodedSmokeStep(const char *name, bool passed, bool *allPassed){
	Debug("RS2DECODEDSMOKE|%-28s|%s\n", name, passed ? "pass" : "FAIL");
	if(!passed) *allPassed = false;
}

bool RS2DecodedImageSmoke(){
	char directory[MAX_PATH];
	char pngPath[MAX_PATH];
	char bmpPath[MAX_PATH];
	char indexedPath[MAX_PATH];
	if(!GetTempPathA(MAX_PATH, directory)) return false;

	_snprintf(pngPath, MAX_PATH-1, "%sRS2EX-decoded-%lu.png",
		directory, (unsigned long)GetCurrentProcessId());
	_snprintf(bmpPath, MAX_PATH-1, "%sRS2EX-decoded-%lu.bmp",
		directory, (unsigned long)GetCurrentProcessId());
	_snprintf(indexedPath, MAX_PATH-1, "%sRS2EX-decoded-indexed-%lu.png",
		directory, (unsigned long)GetCurrentProcessId());
	pngPath[MAX_PATH-1] = 0;
	bmpPath[MAX_PATH-1] = 0;
	indexedPath[MAX_PATH-1] = 0;

	DeleteFileA(pngPath);
	DeleteFileA(bmpPath);
	DeleteFileA(indexedPath);
	bool allPassed = true;
	bool passed = RS2WriteDecodedFixture(
		pngPath, s_PngFixture, sizeof(s_PngFixture));
	passed = RS2WriteDecodedFixture(
		bmpPath, s_BmpFixture, sizeof(s_BmpFixture)) && passed;
	passed = RS2WriteKnownAlphaPngFixture(indexedPath) && passed;
	RS2DecodedSmokeStep("write tiny fixtures", passed, &allPassed);

	CRS2DecodedImage image;
	std::string error;

	//	4 x 2 RGBA PNG, opaque-black keyed out, complete 4x2 -> 2x1 -> 1x1 chain.
	bool decoded = RS2DecodeImageFile(
		pngPath, 0xff000000, 0, &image, &error);
	passed = decoded && error.empty() &&
		image.GetWidth()==4 && image.GetHeight()==2 && image.GetMipCount()==3;
	RS2DecodedSmokeStep("PNG dimensions/mip count", passed, &allPassed);
	passed = decoded &&
		RS2DecodedPixelEquals(image, 0, 0, 0, 255, 0, 0, 255) &&
		RS2DecodedPixelEquals(image, 0, 1, 0, 0, 255, 0, 128) &&
		RS2DecodedPixelEquals(image, 0, 2, 0, 0, 0, 255, 0);
	RS2DecodedSmokeStep("PNG RGBA/alpha", passed, &allPassed);
	passed = decoded &&
		RS2DecodedPixelEquals(image, 0, 0, 1, 0, 0, 0, 0);
	RS2DecodedSmokeStep("ARGB colour key", passed, &allPassed);
	passed = decoded &&
		RS2DecodedPixelEquals(image, 1, 0, 0, 66, 69, 8, 106) &&
		RS2DecodedPixelEquals(image, 1, 1, 0, 160, 80, 199, 191) &&
		RS2DecodedPixelEquals(image, 2, 0, 0, 113, 75, 104, 149);
	RS2DecodedSmokeStep("box mip pixels", passed, &allPassed);

	//	24-bit BMP is normalised to top-down RGBA with opaque alpha.
	decoded = RS2DecodeImageFile(bmpPath, 0, 1, &image, &error);
	passed = decoded && error.empty() && image.GetMipCount()==1 &&
		RS2DecodedPixelEquals(image, 0, 0, 0, 255, 0, 0, 255) &&
		RS2DecodedPixelEquals(image, 0, 3, 0, 255, 255, 255, 255) &&
		RS2DecodedPixelEquals(image, 0, 1, 1, 10, 20, 30, 255);
	RS2DecodedSmokeStep("BMP top-down RGBA", passed, &allPassed);

	//	The accepted content contains one indexed PNG; preserve palette alpha.
	decoded = RS2DecodeImageFile(indexedPath, 0, 1, &image, &error);
	passed = decoded && error.empty() &&
		image.GetWidth()==2 && image.GetHeight()==2 && image.GetMipCount()==1 &&
		RS2DecodedPixelEquals(image, 0, 0, 0, 255, 0, 0, 255) &&
		RS2DecodedPixelEquals(image, 0, 1, 0, 0, 255, 0, 128) &&
		RS2DecodedPixelEquals(image, 0, 0, 1, 0, 0, 255, 0) &&
		RS2DecodedPixelEquals(image, 0, 1, 1, 0, 0, 0, 255);
	RS2DecodedSmokeStep("indexed PNG palette/alpha", passed, &allPassed);

	//	The installed OPENING RT_BITMAP proves DIB orientation and exact pixels.
	decoded = RS2DecodeImageResource("OPENING", 0, 1, &image, &error);
	passed = decoded && error.empty() &&
		image.GetWidth()==128 && image.GetHeight()==128 && image.GetMipCount()==1 &&
		RS2DecodedPixelEquals(image, 0, 0, 0, 0, 0, 0, 255) &&
		RS2DecodedPixelEquals(image, 0, 20, 2, 48, 48, 48, 255);
	RS2DecodedSmokeStep("RT_BITMAP DIB pixels", passed, &allPassed);

	//	Both invalid entry points must clear stale output and explain the failure.
	decoded = RS2DecodeImageFile(
		"RS2EX-this-file-does-not-exist.png", 0, 1, &image, &error);
	passed = !decoded && !image.IsValid() && !error.empty();
	RS2DecodedSmokeStep("invalid file failure", passed, &allPassed);
	decoded = RS2DecodeImageResource(
		"RS2EX_MISSING_BITMAP", 0, 1, &image, &error);
	passed = !decoded && !image.IsValid() && !error.empty();
	RS2DecodedSmokeStep("invalid resource failure", passed, &allPassed);
	decoded = RS2DecodeImageFile(
		"RS2EX-unsupported.dds", 0, 1, &image, &error);
	passed = !decoded && !image.IsValid() && !error.empty();
	RS2DecodedSmokeStep("unsupported format failure", passed, &allPassed);

	DeleteFileA(pngPath);
	DeleteFileA(bmpPath);
	DeleteFileA(indexedPath);
	return allPassed;
}
