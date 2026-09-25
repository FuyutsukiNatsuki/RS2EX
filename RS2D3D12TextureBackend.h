// RS2EX - D3D12 texture entry points visible to renderer-neutral dispatch.
// Modified for RS2EX on 2026-09-25.
// Native D3D12 types deliberately stay out of this header.
#ifndef RS2D3D12TEXTUREBACKEND_H_INCLUDED
#define RS2D3D12TEXTUREBACKEND_H_INCLUDED

#include "RS2RenderState.h"

class RS2TextureRef;
struct RS2TexturePayloadOps;

bool RS2D3D12_CreateTexturePayloadFromFile(
	void **payload, int *width, int *height,
	const char *path, unsigned long colourKey, int mipArgument);
bool RS2D3D12_CreateTexturePayloadFromResource(
	void **payload, int *width, int *height,
	const char *name, unsigned long colourKey, int mipArgument);
const RS2TexturePayloadOps *RS2D3D12_GetTexturePayloadOps();

//	A CPU-writable texture (v0.1.6): sizes round up to powers of two exactly as
//	the Direct3D 8 backend rounds them; Lock hands out an A4R4G4B4 copy.
bool RS2D3D12_CreateMutableTexturePayload(
	void **payload, int *width, int *height, int requestedWidth, int requestedHeight);
const RS2TexturePayloadOps *RS2D3D12_GetMutableTexturePayloadOps();
void RS2D3D12_BindTexture(unsigned int stage, const RS2TextureRef &texture);
void RS2D3D12_SetTextureFilter(unsigned int stage, RS2TextureFilter filter);
void RS2D3D12_ResetTextureBinding();

#endif
