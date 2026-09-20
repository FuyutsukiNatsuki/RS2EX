//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	RS2-owned sampled textures.
//
//	RailSim II 2.15 passed IDirect3DTexture8* around as LPTEX8: the mesh material
//	table, customizers, the cursor, the skin, particles, plugin icons and the text
//	rasteriser all stored one.  None of them ever needed a Direct3D object - they
//	needed to say "sample this image".
//
//	CRS2TextureResource owns a texture.  RS2TextureRef names one.  Copying a ref is
//	free and does not touch CTexList's reference count: acquiring and releasing
//	stay explicit, exactly as they were, because that is the lifetime contract the
//	whole texture cache is built on.
//
//	This header deliberately does not include d3d8.h, and exposes no native handle.

#ifndef RS2TEXTURERESOURCE_H_INCLUDED
#define RS2TEXTURERESOURCE_H_INCLUDED

class CRS2TextureResource;

/*
 *	Where a locked texture's pixels are.
 *
 *	Rows are pitch bytes apart, which is not necessarily width * bytes-per-pixel.
 */
struct RS2TextureLock
{
	void *bits;
	int pitch;

	RS2TextureLock() : bits(0), pitch(0){}
	bool IsValid() const{ return bits!=0; }
};

/*
 *	A lightweight, nullable reference to a sampled texture.
 *
 *	Copying is free and changes no ownership.  An empty ref is the normal way to
 *	say "no texture", and binding one unbinds the stage.
 */
class RS2TextureRef
{
private:
	CRS2TextureResource *m_Resource;

public:
	RS2TextureRef() : m_Resource(0){}
	explicit RS2TextureRef(CRS2TextureResource *resource) : m_Resource(resource){}

	bool IsEmpty() const{ return m_Resource==0; }
	void Clear(){ m_Resource = 0; }

	//	Identity, not value: two refs are the same when they name the same
	//	resource.  CTexList::Release() depends on this.
	bool operator==(const RS2TextureRef &rhs) const{ return m_Resource==rhs.m_Resource; }
	bool operator!=(const RS2TextureRef &rhs) const{ return m_Resource!=rhs.m_Resource; }

	//	For the cache and the backend binder.  Not a native handle.
	CRS2TextureResource *GetResource() const{ return m_Resource; }

	bool GetSize(int *width, int *height) const;
};

/*
 *	A texture owned by RailSim.
 *
 *	Created through the functions below, which keep the pool and format choices
 *	where v0.0.5 put them.  The backend object behind it is never exposed here.
 */
class CRS2TextureResource
{
private:
	void *m_Native;		//	IDirect3DTexture8*, opaque above the backend
	int m_Width;
	int m_Height;

	CRS2TextureResource(const CRS2TextureResource &);
	CRS2TextureResource &operator=(const CRS2TextureResource &);

public:
	CRS2TextureResource();
	~CRS2TextureResource();

	void Free();
	bool IsValid() const{ return m_Native!=0; }

	RS2TextureRef GetRef(){ return RS2TextureRef(this); }

	int GetWidth() const{ return m_Width; }
	int GetHeight() const{ return m_Height; }

	/*
	 *	Lock the whole texture for writing.
	 *
	 *	Used by the GDI text rasteriser, which writes 16-bit pixels by hand.
	 *	Unlock() must follow a successful Lock().
	 */
	bool Lock(RS2TextureLock *out);
	void Unlock();

	//	--- backend use only ---
	//	Resolving this outside the backend would recreate the problem v0.0.7
	//	exists to remove.  Declared here because C++ has no better place.
	void *GetNativeForBackend() const{ return m_Native; }
	void AdoptNativeFromBackend(void *native, int width, int height);
};

/*
 *	Creation.  Each returns 0 on failure, which callers must tolerate: a missing
 *	plugin texture has always produced an untextured draw, not a crash.
 */
CRS2TextureResource *RS2CreateTextureFromFile(
	const char *strFile, unsigned long cTrans, int nMipLv);
CRS2TextureResource *RS2CreateTextureFromResource(
	const char *strRes, unsigned long cTrans, int nMipLv);
CRS2TextureResource *RS2CreateMutableTexture(int w, int h);

void RS2DestroyTexture(CRS2TextureResource *resource);

#endif	//	RS2TEXTURERESOURCE_H_INCLUDED
