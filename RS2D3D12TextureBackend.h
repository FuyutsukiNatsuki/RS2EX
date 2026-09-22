// RS2EX - D3D12 texture entry points visible to renderer-neutral dispatch.
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
void RS2D3D12_BindTexture(unsigned int stage, const RS2TextureRef &texture);
void RS2D3D12_SetTextureFilter(unsigned int stage, RS2TextureFilter filter);
void RS2D3D12_ResetTextureBinding();

#endif
