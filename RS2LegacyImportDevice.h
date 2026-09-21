//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	A Direct3D 8 device that belongs to the mesh importer.
//
//	D3DXLoadMeshFromX and the sphere, box and teapot generators all need a
//	device.  Until v0.0.9 they borrowed the renderer's, which meant a renderer
//	without a Direct3D 8 device could not load a model at all - the importer
//	produced API-neutral data but could not be reached to produce it.
//
//	So the importer gets its own, and the renderer's is none of its business.
//	This device never draws, never presents, is never reset with the renderer
//	and never appears in a frame.  It exists to let D3DX parse a file.
//
//	Keeping D3DX is deliberate.  The content contract includes the result of
//	Optimize(ATTRSORT|COMPACT|VERTEXCACHE) and bounds measured before it, and
//	v0.0.6 measured COMPACT changing a real mesh's bounds.  Reimplementing
//	those algorithms would move every model slightly.  Replacing the parser is
//	left as later work, and nothing outside this module depends on it.
//
//	Only RS2LegacyXMeshImporter includes this.

#ifndef RS2LEGACYIMPORTDEVICE_H_INCLUDED
#define RS2LEGACYIMPORTDEVICE_H_INCLUDED

/*
 *	The importer's device, created on first use.
 *
 *	Returns 0 if it cannot be created, and the caller must treat that as an
 *	import failure.  There is deliberately no fallback to the renderer's
 *	device: a fallback would work today and quietly reintroduce exactly the
 *	dependency this module removes.
 */
IDirect3DDevice8 *RS2GetLegacyImportDevice();

/*
 *	Release it.  Called at shutdown; safe to call when nothing was created.
 */
void RS2ReleaseLegacyImportDevice();

#endif	//	RS2LEGACYIMPORTDEVICE_H_INCLUDED
