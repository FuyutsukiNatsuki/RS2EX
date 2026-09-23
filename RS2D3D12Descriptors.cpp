// RS2EX - RailSim II development fork
// Created for RS2EX on 2026-09-23.

#include "stdafx.h"
#include "RS2D3D12Descriptors.h"

CRS2D3D12Descriptors::CRS2D3D12Descriptors()
	: m_SrvHeap(0), m_SamplerHeap(0), m_SrvStride(0), m_SamplerStride(0),
	  m_Next(0), m_FreeCount(0), m_NextSerial(0), m_Live(0), m_Peak(0)
{
	ZeroMemory(m_Free, sizeof(m_Free));
	ZeroMemory(m_Serial, sizeof(m_Serial));
	ZeroMemory(m_Used, sizeof(m_Used));
}

CRS2D3D12Descriptors::~CRS2D3D12Descriptors(){ Destroy(); }

bool CRS2D3D12Descriptors::Create(ID3D12Device *device){
	Destroy();
	if(!device) return false;

	D3D12_DESCRIPTOR_HEAP_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = RS2D3D12_SRV_CAPACITY;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_SrvHeap));
	if(FAILED(hr)){
		Debug("[RS2EX D3D12 Descriptor] SRV heap failed (0x%08lx)\n", (unsigned long)hr);
		return false;
	}

	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	desc.NumDescriptors = 2;
	hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_SamplerHeap));
	if(FAILED(hr)){
		Debug("[RS2EX D3D12 Descriptor] sampler heap failed (0x%08lx)\n", (unsigned long)hr);
		Destroy();
		return false;
	}

	m_SrvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_SamplerStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	if(!m_SrvStride || !m_SamplerStride){
		Debug("[RS2EX D3D12 Descriptor] zero descriptor stride\n");
		Destroy();
		return false;
	}

	D3D12_SAMPLER_DESC sampler;
	ZeroMemory(&sampler, sizeof(sampler));
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MipLODBias = 0.0f;
	sampler.MaxAnisotropy = 1;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_SamplerHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateSampler(&sampler, cpu);
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	cpu.ptr += m_SamplerStride;
	device->CreateSampler(&sampler, cpu);
	Debug("[RS2EX D3D12 Descriptor] SRV capacity %u, sampler point/linear\n",
		RS2D3D12_SRV_CAPACITY);
	return true;
}

void CRS2D3D12Descriptors::Destroy(){
	if(m_SrvHeap && m_Live)
		Debug("[RS2EX D3D12 Descriptor] destroying heap with %u live SRVs\n", m_Live);
	RELEASE(m_SamplerHeap);
	RELEASE(m_SrvHeap);
	m_SrvStride = m_SamplerStride = 0;
	m_Next = m_FreeCount = m_Live = m_Peak = 0;
	ZeroMemory(m_Used, sizeof(m_Used));
}

bool CRS2D3D12Descriptors::Allocate(RS2D3D12SrvSlot *out){
	if(out) *out = RS2D3D12SrvSlot();
	if(!out || !m_SrvHeap) return false;
	if(m_NextSerial==0xffffffffu){
		Debug("[RS2EX D3D12 Descriptor] SRV allocation serial exhausted\n");
		return false;
	}
	unsigned int index;
	if(m_FreeCount) index = m_Free[--m_FreeCount];
	else if(m_Next<RS2D3D12_SRV_CAPACITY) index = m_Next++;
	else{
		Debug("[RS2EX D3D12 Descriptor] SRV heap exhausted: live %u capacity %u\n",
			m_Live, RS2D3D12_SRV_CAPACITY);
		return false;
	}
	m_Used[index] = true;
	if(++m_NextSerial==0) ++m_NextSerial;
	m_Serial[index] = m_NextSerial;
	out->index = index;
	out->serial = m_NextSerial;
	if(++m_Live>m_Peak) m_Peak = m_Live;
	return true;
}

bool CRS2D3D12Descriptors::IsLive(const RS2D3D12SrvSlot &slot) const{
	return m_SrvHeap && slot.index<RS2D3D12_SRV_CAPACITY
		&& slot.serial && m_Used[slot.index] && m_Serial[slot.index]==slot.serial;
}

bool CRS2D3D12Descriptors::Free(const RS2D3D12SrvSlot &slot){
	if(!IsLive(slot)){
		Debug("[RS2EX D3D12 Descriptor] stale or invalid SRV release\n");
		return false;
	}
	m_Used[slot.index] = false;
	m_Free[m_FreeCount++] = slot.index;
	m_Live--;
	return true;
}

bool CRS2D3D12Descriptors::GetSrvHandles(
	const RS2D3D12SrvSlot &slot,
	D3D12_CPU_DESCRIPTOR_HANDLE *cpu,
	D3D12_GPU_DESCRIPTOR_HANDLE *gpu
) const{
	if(!IsLive(slot)) return false;
	if(cpu){
		*cpu = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
		cpu->ptr += slot.index*m_SrvStride;
	}
	if(gpu){
		*gpu = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
		gpu->ptr += slot.index*m_SrvStride;
	}
	return true;
}

bool CRS2D3D12Descriptors::WriteTexture(
	ID3D12Device *device,
	const RS2D3D12SrvSlot &slot,
	ID3D12Resource *texture,
	unsigned int mipCount
){
	if(!device || !texture || !mipCount || mipCount>0xffff) return false;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu;
	if(!GetSrvHandles(slot, &cpu, 0)) return false;
	const D3D12_RESOURCE_DESC resource = texture->GetDesc();

	//	The view reads the resource as what it is.  Only the formats the
	//	uploader creates are accepted, so a resource made some other way
	//	cannot be given a view that reinterprets it.
	const bool format = resource.Format==DXGI_FORMAT_R8G8B8A8_UNORM
		|| resource.Format==DXGI_FORMAT_BC1_UNORM
		|| resource.Format==DXGI_FORMAT_BC3_UNORM;

	if(resource.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D
			|| !format || resource.DepthOrArraySize!=1
			|| mipCount>resource.MipLevels) return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC view;
	ZeroMemory(&view, sizeof(view));
	view.Format = resource.Format;
	view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	view.Texture2D.MipLevels = mipCount;
	device->CreateShaderResourceView(texture, &view, cpu);
	return true;
}

bool CRS2D3D12Descriptors::GetSamplerHandle(
	RS2TextureFilter filter,
	D3D12_GPU_DESCRIPTOR_HANDLE *gpu
) const{
	if(!gpu || !m_SamplerHeap || (filter!=RS2_FILTER_POINT && filter!=RS2_FILTER_LINEAR))
		return false;
	*gpu = m_SamplerHeap->GetGPUDescriptorHandleForHeapStart();
	if(filter==RS2_FILTER_LINEAR) gpu->ptr += m_SamplerStride;
	return true;
}

void CRS2D3D12Descriptors::BindHeaps(ID3D12GraphicsCommandList *list) const{
	if(!list || !m_SrvHeap || !m_SamplerHeap) return;
	ID3D12DescriptorHeap *heaps[2] = { m_SrvHeap, m_SamplerHeap };
	list->SetDescriptorHeaps(2, heaps);
}

bool RS2D3D12DescriptorsSmoke(CRS2D3D12Descriptors *descriptors, ID3D12Device *device){
	if(!descriptors || !device || descriptors->GetLive()!=0
			|| descriptors->GetCapacity()!=RS2D3D12_SRV_CAPACITY) return false;
	D3D12_GPU_DESCRIPTOR_HANDLE point, linear;
	if(!descriptors->GetSamplerHandle(RS2_FILTER_POINT, &point)
			|| !descriptors->GetSamplerHandle(RS2_FILTER_LINEAR, &linear)
			|| point.ptr==linear.ptr) return false;
	D3D12_HEAP_PROPERTIES heap;
	ZeroMemory(&heap, sizeof(heap));
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = 4;
	desc.Height = 4;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	ID3D12Resource *texture = 0;
	if(FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
			&desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0,
			IID_PPV_ARGS(&texture)))) return false;

	RS2D3D12SrvSlot slots[RS2D3D12_SRV_CAPACITY];
	bool ok = true;
	unsigned int i;
	for(i = 0; i<RS2D3D12_SRV_CAPACITY; i++){
		if(!descriptors->Allocate(&slots[i])
				|| !descriptors->WriteTexture(device, slots[i], texture, 1)){
			ok = false;
			break;
		}
		D3D12_GPU_DESCRIPTOR_HANDLE handle;
		if(!descriptors->GetSrvHandles(slots[i], 0, &handle)){
			ok = false;
			break;
		}
		for(unsigned int j = 0; j<i; j++){
			D3D12_GPU_DESCRIPTOR_HANDLE other;
			if(!descriptors->GetSrvHandles(slots[j], 0, &other)
					|| other.ptr==handle.ptr) ok = false;
		}
	}
	if(ok){
		RS2D3D12SrvSlot overflow;
		ok = !descriptors->Allocate(&overflow) && !descriptors->IsLive(overflow)
			&& descriptors->GetLive()==RS2D3D12_SRV_CAPACITY
			&& descriptors->GetPeak()==RS2D3D12_SRV_CAPACITY;
		for(unsigned int j = 0; j<RS2D3D12_SRV_CAPACITY; j++){
			D3D12_GPU_DESCRIPTOR_HANDLE handle;
			if(!descriptors->IsLive(slots[j])
					|| !descriptors->GetSrvHandles(slots[j], 0, &handle)) ok = false;
		}
	}
	if(ok){
		const RS2D3D12SrvSlot stale = slots[37];
		ok = descriptors->Free(stale) && !descriptors->Free(stale);
		RS2D3D12SrvSlot reused;
		ok = ok && descriptors->Allocate(&reused)
			&& reused.index==stale.index && reused.serial!=stale.serial
			&& !descriptors->IsLive(stale)
			&& !descriptors->GetSrvHandles(stale, 0, 0)
			&& !descriptors->WriteTexture(device, stale, texture, 1)
			&& descriptors->WriteTexture(device, reused, texture, 1);
		slots[37] = reused;
	}
	for(i = 0; i<RS2D3D12_SRV_CAPACITY; i++)
		if(descriptors->IsLive(slots[i]) && !descriptors->Free(slots[i])) ok = false;
	if(descriptors->GetLive()!=0) ok = false;
	texture->Release();
	Debug("RS2D3D12DESCRIPTOR|capacity=%u peak=%u live=%u sampler=point,linear|%s\n",
		descriptors->GetCapacity(), descriptors->GetPeak(), descriptors->GetLive(),
		ok ? "pass" : "FAIL");
	return ok;
}
