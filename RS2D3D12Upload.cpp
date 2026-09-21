//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-22.
//
//	See RS2D3D12Upload.h.

#include "stdafx.h"
#include "RS2D3D12Upload.h"

CRS2D3D12Upload::CRS2D3D12Upload()
	: m_Buffer(0),
	  m_Cpu(0),
	  m_Gpu(0),
	  m_Size(0),
	  m_Used(0),
	  m_Peak(0),
	  m_Overflowed(false)
{
}

CRS2D3D12Upload::~CRS2D3D12Upload(){
	Destroy();
}

bool CRS2D3D12Upload::Create(
	ID3D12Device *device,	//	device to allocate from
	unsigned int bytes	//	size of the block
){
	Destroy();

	D3D12_HEAP_PROPERTIES props;

	ZeroMemory(&props, sizeof(props));
	props.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC desc;

	ZeroMemory(&desc, sizeof(desc));
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = bytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
		&props, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&m_Buffer));

	if(FAILED(hr)){
		Debug("[RS2EX D3D12] upload buffer of %u bytes failed (0x%08lx)\n",
			bytes, (unsigned long)hr);
		return false;
	}

	//	Mapped once and left mapped for its whole life.  An upload heap is
	//	written by the CPU and read by the GPU; unmapping between frames would
	//	buy nothing and cost a call per frame.
	D3D12_RANGE none;

	none.Begin = 0;
	none.End = 0;

	if(FAILED(m_Buffer->Map(0, &none, (void **)&m_Cpu))){
		Debug("[RS2EX D3D12] upload buffer would not map\n");
		Destroy();
		return false;
	}

	m_Gpu = m_Buffer->GetGPUVirtualAddress();
	m_Size = bytes;
	m_Used = 0;
	return true;
}

void CRS2D3D12Upload::Destroy(){
	if(m_Buffer && m_Cpu){
		//	Written entirely by the CPU, so the range written back is empty.
		D3D12_RANGE none;

		none.Begin = 0;
		none.End = 0;
		m_Buffer->Unmap(0, &none);
	}

	RELEASE(m_Buffer);
	m_Cpu = 0;
	m_Gpu = 0;
	m_Size = 0;
	m_Used = 0;
	m_Overflowed = false;
}

void CRS2D3D12Upload::Reset(){
	if(m_Used>m_Peak) m_Peak = m_Used;

	m_Used = 0;
	m_Overflowed = false;
}

bool CRS2D3D12Upload::Allocate(
	unsigned int bytes,	//	how much
	unsigned int alignment,	//	what the use needs
	void **cpu,		//	somewhere to put the data
	D3D12_GPU_VIRTUAL_ADDRESS *gpu	//	address for Direct3D 12
){
	if(!m_Cpu || !bytes) return false;
	if(alignment<1) alignment = 1;

	const unsigned int offset = (m_Used+alignment-1)/alignment*alignment;

	if(offset+bytes>m_Size){
		//	Once per frame.  A renderer that has run out will run out again on
		//	the next draw and the one after that.
		if(!m_Overflowed){
			m_Overflowed = true;
			Debug("[RS2EX D3D12] frame scratch exhausted: wanted %u more than %u\n",
				bytes, m_Size-offset);
		}
		return false;
	}

	if(cpu) *cpu = m_Cpu+offset;
	if(gpu) *gpu = m_Gpu+offset;

	m_Used = offset+bytes;
	return true;
}

bool CRS2D3D12Upload::Write(
	const void *data,	//	what to copy
	unsigned int bytes,	//	how much
	unsigned int alignment,	//	what the use needs
	D3D12_GPU_VIRTUAL_ADDRESS *gpu	//	address for Direct3D 12
){
	void *cpu = 0;

	if(!data || !Allocate(bytes, alignment, &cpu, gpu)) return false;

	memcpy(cpu, data, bytes);
	return true;
}
