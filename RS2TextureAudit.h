//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	Developer-only texture contract instrumentation.
//
//	The public functions in this file contain no renderer-native type.  The
//	Direct3D 8 implementation supplies plain measurements while -textureaudit
//	is present; ordinary runs pay only the cached switch check.

#ifndef RS2TEXTUREAUDIT_H_INCLUDED
#define RS2TEXTUREAUDIT_H_INCLUDED

bool RS2TextureAuditEnabled();

void RS2TextureAuditRecordCreate(
	const char *kind,
	const char *source,
	bool success,
	unsigned int requestedWidth,
	unsigned int requestedHeight,
	unsigned int sourceWidth,
	unsigned int sourceHeight,
	unsigned int actualWidth,
	unsigned int actualHeight,
	unsigned long colourKey,
	int mipArgument,
	unsigned int actualMipCount,
	unsigned int sourceFormat,
	unsigned int actualFormat,
	unsigned int imageFileFormat,
	unsigned int liveTextures);

void RS2TextureAuditRecordRelease(unsigned int liveTextures);
void RS2TextureAuditRecordBind(unsigned int stage, bool hasTexture);
void RS2TextureAuditRecordFilter(unsigned int stage, bool linear);
void RS2TextureAuditRecordAlphaTest(bool enable);
void RS2TextureAuditRecordAlphaRef(unsigned int ref);
void RS2TextureAuditRecordAlphaFunc(unsigned int func);
void RS2TextureAuditRecordLock(bool success);
void RS2TextureAuditRecordUnlock();

//	Called before renderer shutdown, while Debug() and the backend still exist.
//	It is idempotent because a failed initialization can also call Shutdown().
void RS2TextureAuditDump();

#endif	//	RS2TEXTUREAUDIT_H_INCLUDED
