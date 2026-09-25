//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//	Modified for RS2EX on 2026-09-21, 2026-09-22, 2026-09-26.
//
//	Renderer facade.
//
//	RailSim II 2.15 had no renderer boundary at all.  Direct3D 8 device creation,
//	device-lost handling, scene begin/end, presentation and viewport submission
//	were spread through lib/graphic.cpp, lib/view.cpp, the game modes and the
//	window-division code, and every one of them reached sv3.pDev directly.
//	Replacing Direct3D 8 therefore meant untangling startup, reset, resize and
//	frame lifecycle from the whole game first.
//
//	This class is that seam and nothing more.  It owns the device/frame
//	lifecycle; it knows nothing about trains, rails, cameras or simulation, and
//	it deliberately does not mirror the Direct3D 8 API.  Fixed-function state,
//	textures, meshes, vertex buffers and D3DX math stay where they are - see
//	docs/v0.0.4-d3d8-inventory.md for what was intentionally left behind.
//
//	Direct3D 8 remains the only backend.  The point of v0.0.4 is not to replace
//	it but to make it replaceable.
//
//	This header must stay includable without d3d8.h / d3dx8.h: no Direct3D type
//	may appear in the interface below.

#ifndef RS2RENDERER_H_INCLUDED
#define RS2RENDERER_H_INCLUDED

class IRS2RendererBackend;

/*
 *	Which backend is running.
 *
 *	[RS2EX] Added in v0.1.0.  Code that has to behave differently per
 *	backend asks this; it must never ask whether sv3.pDev is null, which
 *	would rebuild the dependency v0.0.9 spent the release removing.
 */
enum RS2RendererBackendType
{
	RS2_RENDERER_NONE,
	RS2_RENDERER_D3D12
};

/*
 *	Renderer backend interface
 *
 *	Implemented once, by CRS2D3D12Backend (v0.2.0; the Direct3D 8 backend was
 *	removed).  Static source-level backends only: there is no plugin
 *	mechanism and no external ABI promise.
 */
class IRS2RendererBackend
{
public:
	virtual ~IRS2RendererBackend(){}

	virtual bool Initialize(int width, int height) = 0;
	virtual void Shutdown() = 0;

	virtual RS2RendererBackendType GetType() const = 0;

	virtual bool BeginRenderPass(unsigned int clearColor, bool clearColorBuffer) = 0;
	virtual void EndRenderPass() = 0;
	virtual void Present() = 0;

	virtual bool Reset() = 0;

	virtual void ClearTarget(unsigned int color) = 0;

	//	Whether this backend can read rendered pixels back.  See CRS2Renderer.
	virtual bool SupportsReadback() const = 0;

	virtual void SetViewport(
		unsigned int x,
		unsigned int y,
		unsigned int width,
		unsigned int height,
		float minZ,
		float maxZ) = 0;

	virtual void GetViewportSize(
		unsigned int *width,
		unsigned int *height) const = 0;

	virtual const char *GetName() const = 0;
};

/*
 *	Renderer facade
 */
class CRS2Renderer
{
private:
	IRS2RendererBackend *m_Backend;

	//	Resolved once at initialisation and never reassigned.
	RS2RendererBackendType m_BackendType;

	//	Pass-state diagnostics.  These observe; they never alter control flow.
	//	The 2.15 code ends a frame even when BeginScene() failed on a lost
	//	device, so an unbalanced pass is a legitimate state to be in.
	bool m_InRenderPass;

public:
	CRS2Renderer();
	~CRS2Renderer();

	bool Initialize(int width, int height);
	void Shutdown();

	bool BeginRenderPass(unsigned int clearColor, bool clearColorBuffer);
	void EndRenderPass();
	void Present();

	bool Reset();

	//	Submits the GPU viewport only.  The engine-side viewport matrix used by
	//	WorldToScreen/ScreenToWorld stays in lib/view.cpp.
	void SetViewport(
		unsigned int x,
		unsigned int y,
		unsigned int width,
		unsigned int height,
		float minZ = 0.0f,
		float maxZ = 1.0f);

	//	Last submitted viewport size, owned by the backend rather than read back
	//	from the device.  A read-back has no natural equivalent in an explicit
	//	API, and the caller only ever wants the aspect ratio.
	void GetViewportSize(unsigned int *width, unsigned int *height) const;

	/*
	 *	Whether a backend is initialised and can be drawn to.
	 *
	 *	[RS2EX] Replaces the "if(!sv3.pDev) return" checks high-level code used
	 *	to make.  Asking whether a Direct3D 8 device exists is the wrong
	 *	question for code that should not know which backend is running.
	 */
	bool IsReady() const;

	/*
	 *	Which backend is running, or the one that was asked for before
	 *	initialisation.
	 *
	 *	[RS2EX] The selection is made once, from the command line, and does
	 *	not change for the life of the process.
	 */
	RS2RendererBackendType GetBackendType() const;

	/*
	 *	Clear the whole target, including depth and stencil.
	 *
	 *	[RS2EX] The one-time startup clear.  Per-frame clearing belongs to
	 *	BeginRenderPass; this is the separate one InitRenderState issued before
	 *	the first frame.
	 */
	void ClearTarget(unsigned int color);

	/*
	 *	Whether the backend can read rendered pixels back.
	 *
	 *	[RS2EX] Capture, the offscreen target and GetPixelColor all depend on
	 *	readback.  They were Direct3D 8 only, and v0.2.0 removed that backend,
	 *	so the Direct3D 12 backend answers false and those paths are inert.
	 */
	bool SupportsReadback() const;

	const char *GetBackendName() const;
};

/*
 *	The renderer instance.
 *
 *	A function-local static rather than a file-scope object: graphics start-up
 *	must not depend on static initialisation order, and construction has to stay
 *	inert until Initialize() is called after the main window exists.
 */
CRS2Renderer &GetRS2Renderer();

#endif	//	RS2RENDERER_H_INCLUDED
