//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	Scratch GPU memory that lives exactly one frame.
//
//	Immediate draws hand the renderer vertices that exist only for the duration
//	of the call, constants change per draw, and a triangle fan has to be
//	expanded into a list somewhere.  Direct3D 8 had DrawPrimitiveUP for the
//	first of those and a fixed-function pipeline for the rest; Direct3D 12 has
//	neither, so the data has to be in a buffer the GPU can read.
//
//	One of these belongs to each frame context.  The context is not reused
//	until its fence has completed, so by the time this is reset the GPU has
//	finished with everything in it.  That is the whole lifetime argument: no
//	per-draw synchronisation, no reference counting, no waiting.
//
//	It does not grow.  A frame that asks for more than it has is refused and
//	says so, because a scratch allocator that silently wrapped would overwrite
//	vertices the GPU was still reading, and the result would be geometry that
//	is wrong rather than geometry that is missing - which is much harder to
//	notice.

#ifndef RS2D3D12UPLOAD_H_INCLUDED
#define RS2D3D12UPLOAD_H_INCLUDED

#include "RS2D3D12.h"

class CRS2D3D12Upload
{
private:
	ID3D12Resource *m_Buffer;
	unsigned char *m_Cpu;
	D3D12_GPU_VIRTUAL_ADDRESS m_Gpu;

	unsigned int m_Size;
	unsigned int m_Used;

	//	The most any one frame has needed.  Reported at shutdown, because the
	//	right size for this is a measurement and not a guess.
	unsigned int m_Peak;

	//	So a frame that runs out says so once rather than per draw.
	bool m_Overflowed;

public:
	CRS2D3D12Upload();
	~CRS2D3D12Upload();

	bool Create(ID3D12Device *device, unsigned int bytes);
	void Destroy();

	/*
	 *	Start a frame.  Only safe once the GPU has finished with the last one.
	 */
	void Reset();

	/*
	 *	Take a block.
	 *
	 *	bytes		: how much
	 *	alignment	: what the use needs - 256 for a constant buffer, the
	 *			  vertex stride for vertex data
	 *	cpu		: written with somewhere to put the data
	 *	gpu		: written with the address to give Direct3D 12
	 *
	 *	returns		: false when the frame has run out, having said so
	 */
	bool Allocate(
		unsigned int bytes,
		unsigned int alignment,
		void **cpu,
		D3D12_GPU_VIRTUAL_ADDRESS *gpu);

	/*
	 *	Copy something in, in one call.
	 */
	bool Write(
		const void *data,
		unsigned int bytes,
		unsigned int alignment,
		D3D12_GPU_VIRTUAL_ADDRESS *gpu);

	unsigned int GetPeak() const{ return m_Peak; }
	unsigned int GetSize() const{ return m_Size; }
};

#endif	//	RS2D3D12UPLOAD_H_INCLUDED
