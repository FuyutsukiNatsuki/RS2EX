//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	See RS2DecodedImage.h.

#include "stdafx.h"
#include "RS2DecodedImage.h"

#include <wincodec.h>

enum RS2DecodedMipFilter
{
	RS2_DECODED_MIP_BOX,
	RS2_DECODED_MIP_POINT
};

bool CRS2DecodedImage::AppendMip(
	unsigned int width,
	unsigned int height,
	const std::vector<unsigned char> &pixels
){
	if(!width || !height || width>0x3fffffffu) return false;

	const unsigned int rowPitch = width*4;
	const ULONGLONG required = (ULONGLONG)rowPitch*height;
	if(required!=(ULONGLONG)pixels.size()) return false;

	RS2DecodedMip mip;
	mip.width = width;
	mip.height = height;
	mip.rowPitch = rowPitch;
	mip.pixels = pixels;
	m_Mips.push_back(mip);
	return true;
}

static bool RS2DecodeFail(
	CRS2DecodedImage *out,
	std::string *error,
	const char *reason,
	HRESULT hr
){
	if(out) out->Clear();

	char message[512];
	if(FAILED(hr))
		_snprintf(message, sizeof(message)-1, "%s (HRESULT 0x%08lx)",
			reason, (unsigned long)hr);
	else
		_snprintf(message, sizeof(message)-1, "%s", reason);
	message[sizeof(message)-1] = 0;

	if(error) *error = message;
	Debug("[RS2EX Texture Decode] %s\n", message);
	return false;
}

static void RS2ApplyColourKey(
	std::vector<unsigned char> &pixels,
	unsigned long colourKey
){
	if(!colourKey) return;

	unsigned int i;
	for(i = 0; i+3<pixels.size(); i += 4){
		const unsigned long argb =
			((unsigned long)pixels[i+3]<<24) |
			((unsigned long)pixels[i+0]<<16) |
			((unsigned long)pixels[i+1]<<8) |
			(unsigned long)pixels[i+2];

		if(argb==colourKey){
			pixels[i+0] = 0;
			pixels[i+1] = 0;
			pixels[i+2] = 0;
			pixels[i+3] = 0;
		}
	}
}

static unsigned char RS2AverageChannel(
	const RS2DecodedMip &source,
	unsigned int x,
	unsigned int y,
	unsigned int channel
){
	unsigned int sum = 0;
	unsigned int count = 0;
	unsigned int yy;

	for(yy = y; yy<y+2 && yy<source.height; yy++){
		unsigned int xx;
		for(xx = x; xx<x+2 && xx<source.width; xx++){
			sum += source.pixels[yy*source.rowPitch + xx*4 + channel];
			count++;
		}
	}
	return (unsigned char)((sum+count/2)/count);
}

static bool RS2AppendNextMip(
	CRS2DecodedImage *image,
	RS2DecodedMipFilter filter
){
	const RS2DecodedMip *source = image->GetMip(image->GetMipCount()-1);
	if(!source) return false;

	const unsigned int width = source->width>1 ? source->width/2 : 1;
	const unsigned int height = source->height>1 ? source->height/2 : 1;
	std::vector<unsigned char> pixels(width*height*4);
	unsigned int y;

	for(y = 0; y<height; y++){
		unsigned int x;
		for(x = 0; x<width; x++){
			unsigned int channel;
			for(channel = 0; channel<4; channel++){
				pixels[(y*width+x)*4+channel] =
					filter==RS2_DECODED_MIP_POINT
					? source->pixels[
						(y*2)*source->rowPitch + (x*2)*4 + channel]
					: RS2AverageChannel(*source, x*2, y*2, channel);
			}
		}
	}

	return image->AppendMip(width, height, pixels);
}

static unsigned int RS2MaximumMipCount(unsigned int width, unsigned int height){
	unsigned int count = 1;
	while(width>1 || height>1){
		if(width>1) width /= 2;
		if(height>1) height /= 2;
		count++;
	}
	return count;
}

static bool RS2BuildDecodedImage(
	unsigned int width,
	unsigned int height,
	std::vector<unsigned char> &basePixels,
	unsigned long colourKey,
	int mipArgument,
	RS2DecodedMipFilter filter,
	CRS2DecodedImage *out,
	std::string *error
){
	if(!out) return RS2DecodeFail(0, error, "decoded image output is null", S_OK);
	out->Clear();
	if(error) error->clear();

	if(!width || !height)
		return RS2DecodeFail(out, error, "decoded image has zero size", S_OK);
	if(mipArgument<0)
		return RS2DecodeFail(out, error, "negative mip count is unsupported", S_OK);

	const unsigned int maximum = RS2MaximumMipCount(width, height);
	const unsigned int wanted = mipArgument==0 ? maximum : (unsigned int)mipArgument;
	if(!wanted || wanted>maximum)
		return RS2DecodeFail(out, error, "requested mip count exceeds the image chain", S_OK);

	RS2ApplyColourKey(basePixels, colourKey);
	if(!out->AppendMip(width, height, basePixels))
		return RS2DecodeFail(out, error, "decoded base image size overflow", S_OK);

	while(out->GetMipCount()<wanted){
		if(!RS2AppendNextMip(out, filter))
			return RS2DecodeFail(out, error, "decoded mip size overflow", S_OK);
	}
	return true;
}

static const char *RS2FileExtension(const char *path){
	if(!path) return 0;
	const char *slash = strrchr(path, '\\');
	const char *forward = strrchr(path, '/');
	if(forward && (!slash || forward>slash)) slash = forward;
	const char *dot = strrchr(slash ? slash+1 : path, '.');
	return dot;
}

bool RS2DecodeImageFile(
	const char *path,
	unsigned long colourKey,
	int mipArgument,
	CRS2DecodedImage *out,
	std::string *error
){
	if(out) out->Clear();
	if(error) error->clear();
	if(!out) return RS2DecodeFail(0, error, "decoded image output is null", S_OK);
	if(!path || !*path)
		return RS2DecodeFail(out, error, "file texture path is empty", S_OK);

	const char *extension = RS2FileExtension(path);
	if(!extension || (_stricmp(extension, ".png") && _stricmp(extension, ".bmp"))){
		char reason[320];
		_snprintf(reason, sizeof(reason)-1,
			"unsupported file texture format: %s", extension ? extension : "(none)");
		reason[sizeof(reason)-1] = 0;
		return RS2DecodeFail(out, error, reason, S_OK);
	}

	const int wideCount = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
	if(wideCount<=0)
		return RS2DecodeFail(out, error, "file texture path conversion failed", HRESULT_FROM_WIN32(GetLastError()));
	std::vector<wchar_t> widePath(wideCount);
	if(!MultiByteToWideChar(CP_ACP, 0, path, -1, &widePath[0], wideCount))
		return RS2DecodeFail(out, error, "file texture path conversion failed", HRESULT_FROM_WIN32(GetLastError()));

	IWICImagingFactory *factory = 0;
	IWICBitmapDecoder *decoder = 0;
	IWICBitmapFrameDecode *frame = 0;
	IWICFormatConverter *converter = 0;
	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory, (void **)&factory);
	const char *operation = "WIC factory creation failed";
	UINT width = 0, height = 0;
	std::vector<unsigned char> pixels;

	if(SUCCEEDED(hr)){
		operation = "WIC file decoder creation failed";
		hr = factory->CreateDecoderFromFilename(
			&widePath[0], NULL, GENERIC_READ,
			WICDecodeMetadataCacheOnDemand, &decoder);
	}
	if(SUCCEEDED(hr)){
		operation = "WIC frame decode failed";
		hr = decoder->GetFrame(0, &frame);
	}
	if(SUCCEEDED(hr)){
		operation = "WIC image size query failed";
		hr = frame->GetSize(&width, &height);
		if(SUCCEEDED(hr) && (!width || !height || width>0x3fffffffu)){
			hr = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
			operation = "WIC image dimensions are invalid";
		}
	}
	if(SUCCEEDED(hr)){
		operation = "WIC RGBA converter creation failed";
		hr = factory->CreateFormatConverter(&converter);
	}
	if(SUCCEEDED(hr)){
		operation = "WIC RGBA conversion failed";
		hr = converter->Initialize(
			frame, GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone, NULL, 0.0,
			WICBitmapPaletteTypeCustom);
	}
	if(SUCCEEDED(hr)){
		const ULONGLONG bytes = (ULONGLONG)width*4*height;
		if(bytes>0xffffffffu){
			hr = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
			operation = "WIC decoded image is too large";
		}else{
			pixels.resize((size_t)bytes);
			operation = "WIC pixel copy failed";
			hr = converter->CopyPixels(
				NULL, width*4, (UINT)bytes, &pixels[0]);
		}
	}

	if(converter) converter->Release();
	if(frame) frame->Release();
	if(decoder) decoder->Release();
	if(factory) factory->Release();

	if(FAILED(hr)) return RS2DecodeFail(out, error, operation, hr);
	return RS2BuildDecodedImage(
		width, height, pixels, colourKey, mipArgument,
		RS2_DECODED_MIP_BOX, out, error);
}

bool RS2DecodeImageResource(
	const char *resourceName,
	unsigned long colourKey,
	int mipArgument,
	CRS2DecodedImage *out,
	std::string *error
){
	if(out) out->Clear();
	if(error) error->clear();
	if(!out) return RS2DecodeFail(0, error, "decoded image output is null", S_OK);
	if(!resourceName)
		return RS2DecodeFail(out, error, "bitmap resource name is null", S_OK);

	HINSTANCE module = GetModuleHandle(NULL);
	HRSRC found = FindResourceA(module, resourceName, RT_BITMAP);
	if(!found)
		return RS2DecodeFail(out, error, "RT_BITMAP resource was not found", HRESULT_FROM_WIN32(GetLastError()));

	const DWORD resourceBytes = SizeofResource(module, found);
	HGLOBAL loaded = LoadResource(module, found);
	const unsigned char *data = loaded
		? (const unsigned char *)LockResource(loaded) : 0;
	if(!data || resourceBytes<sizeof(BITMAPINFOHEADER))
		return RS2DecodeFail(out, error, "RT_BITMAP resource data is invalid", HRESULT_FROM_WIN32(GetLastError()));

	BITMAPINFOHEADER header;
	memcpy(&header, data, sizeof(header));
	if(header.biSize<sizeof(BITMAPINFOHEADER) || header.biSize>resourceBytes)
		return RS2DecodeFail(out, error, "unsupported DIB header", S_OK);
	if(header.biWidth<=0 || !header.biHeight || header.biHeight==LONG_MIN)
		return RS2DecodeFail(out, error, "DIB dimensions are invalid", S_OK);
	if(header.biPlanes!=1 || (header.biBitCount!=24 && header.biBitCount!=32) ||
		header.biCompression!=BI_RGB)
		return RS2DecodeFail(out, error, "DIB must be uncompressed 24-bit or 32-bit RGB", S_OK);

	const unsigned int width = (unsigned int)header.biWidth;
	const unsigned int height = (unsigned int)(header.biHeight<0
		? -header.biHeight : header.biHeight);
	const bool topDown = header.biHeight<0;
	const unsigned int bytesPerPixel = header.biBitCount/8;
	const ULONGLONG sourcePitch64 =
		(((ULONGLONG)width*header.biBitCount+31)/32)*4;
	const ULONGLONG paletteBytes = (ULONGLONG)header.biClrUsed*sizeof(RGBQUAD);
	const ULONGLONG pixelOffset = (ULONGLONG)header.biSize+paletteBytes;
	const ULONGLONG pixelBytes = sourcePitch64*height;
	const ULONGLONG outputBytes = (ULONGLONG)width*height*4;
	if(width>0x3fffffffu || outputBytes>0xffffffffu ||
		pixelOffset+pixelBytes>resourceBytes)
		return RS2DecodeFail(out, error, "DIB pixel data is truncated or too large", S_OK);

	std::vector<unsigned char> pixels((size_t)outputBytes);
	const unsigned int sourcePitch = (unsigned int)sourcePitch64;
	unsigned int y;
	for(y = 0; y<height; y++){
		const unsigned int sourceY = topDown ? y : height-1-y;
		const unsigned char *source = data+(size_t)pixelOffset+sourceY*sourcePitch;
		unsigned char *destination = &pixels[y*width*4];
		unsigned int x;
		for(x = 0; x<width; x++){
			destination[x*4+0] = source[x*bytesPerPixel+2];
			destination[x*4+1] = source[x*bytesPerPixel+1];
			destination[x*4+2] = source[x*bytesPerPixel+0];
			destination[x*4+3] = bytesPerPixel==4
				? source[x*bytesPerPixel+3] : 255;
		}
	}

	return RS2BuildDecodedImage(
		width, height, pixels, colourKey, mipArgument,
		RS2_DECODED_MIP_POINT, out, error);
}
