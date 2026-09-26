//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	-x64stress: address space, resource lifetimes and memory counters (v0.3.0
//	WP6, plan sections 24.8 - 24.10).
//
//	Runs once, after the start-up layout has loaded, and then lets the program
//	carry on: the scene keeps rendering, so a capture taken afterwards shows
//	that nothing the stress did disturbed the live resources.
//
//	    address space  (x64 only) reserve 8 GiB - twice the whole 32-bit
//	                   address space - without committing it; commit and touch
//	                   one 64 KiB page every 256 MiB; check every page reads
//	                   back its own 64-bit address, that the pages span more
//	                   than 4 GiB, and that only the sampled pages cost private
//	                   memory; release it all and check the range is free.
//	    geometry       4096 create / destroy cycles through the public
//	                   renderer API (3 to 30000 vertices, indexed and not),
//	                   then 512 resources alive at once; the live counters
//	                   return to their baseline.
//	    textures       create / destroy through the public API, more times than
//	                   the descriptor heap has slots (a skin PNG, then mutable
//	                   textures), so descriptors are reused; live textures and
//	                   descriptors return to their baseline.
//	    memory         working set, private bytes and used address space before,
//	                   during and after.
//
//	This is lifetime and pointer validation, not a performance test, and it
//	raises no renderer budget.

#include "stdafx.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12Texture.h"
#include "RS2Draw.h"
#include "RS2MeshData.h"
#include "RS2TextureResource.h"
#include "RS2X64Stress.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

bool RS2X64StressRequested(){
	return CheckArguments("-x64stress")!=FALSE;
}

static void RS2XSStep(const char *name, bool ok, bool *all){
	Debug("RS2X64STRESS|%-52s|%s\n", name, ok ? "pass" : "FAIL");
	if(!ok) *all = false;
}

struct RS2XSMemory{
	unsigned long long workingSet, peakWorkingSet, privateBytes, virtualUsed, virtualTotal;
};

static RS2XSMemory RS2XSSample(const char *when){
	RS2XSMemory m;
	ZeroMemory(&m, sizeof(m));
	PROCESS_MEMORY_COUNTERS_EX pmc;
	ZeroMemory(&pmc, sizeof(pmc));
	pmc.cb = sizeof(pmc);
	if(GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc))){
		m.workingSet = pmc.WorkingSetSize;
		m.peakWorkingSet = pmc.PeakWorkingSetSize;
		m.privateBytes = pmc.PrivateUsage;
	}
	MEMORYSTATUSEX ms;
	ZeroMemory(&ms, sizeof(ms));
	ms.dwLength = sizeof(ms);
	if(GlobalMemoryStatusEx(&ms)){
		m.virtualTotal = ms.ullTotalVirtual;
		m.virtualUsed = ms.ullTotalVirtual-ms.ullAvailVirtual;
	}
	Debug("RS2X64STRESS|memory %-18s working set %6llu MiB (peak %6llu), private %6llu MiB, address space used %8llu MiB of %llu GiB\n",
		when, m.workingSet>>20, m.peakWorkingSet>>20, m.privateBytes>>20, m.virtualUsed>>20, m.virtualTotal>>30);
	return m;
}

//	---- address space --------------------------------------------------------------

#ifdef _WIN64
static void RS2XSAddressSpace(bool *all){
	const SIZE_T reserveBytes = (SIZE_T)8<<30;
	const SIZE_T step = (SIZE_T)256<<20;
	const SIZE_T page = 0x10000;
	const int samples = (int)(reserveBytes/step);
	int i;

	RS2XSMemory before = RS2XSSample("before reserve");
	BYTE *base = (BYTE *)VirtualAlloc(NULL, reserveBytes, MEM_RESERVE, PAGE_NOACCESS);
	RS2XSStep("reserve 8 GiB (not committed)", base!=NULL, all);
	if(!base) return;
	MEMORY_BASIC_INFORMATION mbi;
	const bool reserved = VirtualQuery(base, &mbi, sizeof(mbi))==sizeof(mbi)
		&& mbi.State==MEM_RESERVE && mbi.RegionSize>=reserveBytes;
	RS2XSStep("reserved region is 8 GiB, MEM_RESERVE", reserved, all);
	RS2XSMemory held = RS2XSSample("reserved");

	bool committed = true;
	for(i = 0; i<samples; i++){
		BYTE *p = base+(SIZE_T)i*step;
		if(VirtualAlloc(p, page, MEM_COMMIT, PAGE_READWRITE)!=p){
			committed = false;
			break;
		}
		unsigned long long *q = (unsigned long long *)p;
		q[0] = (unsigned long long)(uintptr_t)p;
		q[page/sizeof(*q)-1] = ~(unsigned long long)(uintptr_t)p;
	}
	RS2XSStep("commit and touch 32 pages, one per 256 MiB", committed, all);
	bool readBack = committed;
	for(i = 0; readBack && i<samples; i++){
		const BYTE *p = base+(SIZE_T)i*step;
		const unsigned long long *q = (const unsigned long long *)p;
		readBack = q[0]==(unsigned long long)(uintptr_t)p
			&& q[page/sizeof(*q)-1]==~(unsigned long long)(uintptr_t)p;
	}
	RS2XSStep("every page reads back its own address", readBack, all);
	const BYTE *last = base+(SIZE_T)(samples-1)*step;
	Debug("RS2X64STRESS|range 0x%016llx - 0x%016llx, first page 0x%016llx, last page 0x%016llx\n",
		(unsigned long long)(uintptr_t)base, (unsigned long long)(uintptr_t)(base+reserveBytes),
		(unsigned long long)(uintptr_t)base, (unsigned long long)(uintptr_t)last);
	RS2XSStep("touched pages span more than 4 GiB",
		(unsigned long long)(last-base)>=(4ULL<<30), all);
	RS2XSStep("touched pages above the 32-bit range",
		((unsigned long long)(uintptr_t)last>>32)!=0, all);
	RS2XSMemory touched = RS2XSSample("32 pages touched");
	//	2 MiB were committed; the rest of the 8 GiB costs nothing but address
	//	space.  The margin absorbs whatever else the process allocated.
	RS2XSStep("private bytes grew by the samples, not by 8 GiB",
		touched.privateBytes<before.privateBytes+(64ULL<<20), all);
	RS2XSStep("address space in use grew by 8 GiB",
		held.virtualUsed>=before.virtualUsed+reserveBytes-(64ULL<<20), all);

	const BOOL freed = VirtualFree(base, 0, MEM_RELEASE);
	const bool released = freed && VirtualQuery(base, &mbi, sizeof(mbi))==sizeof(mbi)
		&& mbi.State==MEM_FREE && mbi.RegionSize>=reserveBytes;
	RS2XSStep("whole range released (VirtualQuery: MEM_FREE)", released, all);
	RS2XSMemory after = RS2XSSample("released");
	RS2XSStep("private bytes back to the start",
		after.privateBytes<before.privateBytes+(16ULL<<20), all);
}
#endif

//	---- geometry -------------------------------------------------------------------

struct RS2XSVertex{
	float x, y, z, rhw;
	unsigned int diffuse;
};

static void RS2XSGeometry(bool *all){
	static const unsigned int sizes[] = {3, 300, 3000, 30000};
	const unsigned int maxVertices = 30000;
	const int cycles = 4096, held = 512;
	RS2MeshVertexLayout layout;
	layout.Clear();
	layout.stride = sizeof(RS2XSVertex);
	layout.positionOffset = 0;
	layout.positionSemantic = RS2_POSITION_ALREADY_TRANSFORMED;
	layout.diffuseOffset = (int)(sizeof(float)*4);

	vector<RS2XSVertex> vertices(maxVertices);
	vector<unsigned int> indices(maxVertices);
	unsigned int i;
	for(i = 0; i<maxVertices; i++){
		RS2XSVertex &v = vertices[i];
		v.x = (float)(i%640);
		v.y = (float)(i/640);
		v.z = 0.5f;
		v.rhw = 1.0f;
		v.diffuse = 0xff000000u|i;
		indices[i] = maxVertices-1-i;
	}

	const unsigned int liveBaseline = RS2GetLiveGeometryCount();
	const unsigned int vertexBaseline = RS2GetGeometryVertexBytes();
	const unsigned int indexBaseline = RS2GetGeometryIndexBytes();
	Debug("RS2X64STRESS|geometry baseline: live %u, vertex bytes %u, index bytes %u\n",
		liveBaseline, vertexBaseline, indexBaseline);

	int c, done = 0;
	bool ok = true;
	for(c = 0; c<cycles && ok; c++){
		const unsigned int n = sizes[c%4];
		for(i = 0; i<n; i++) indices[i] = n-1-i;
		CRS2GeometryResource *g = (c&4)
			? RS2CreateIndexedGeometry(layout, &vertices[0], n, &indices[0], n)
			: RS2CreateGeometry(layout, &vertices[0], n);
		ok = g && RS2GetGeometryVertexCount(g)==n && RS2GetLiveGeometryCount()==liveBaseline+1;
		RS2DestroyGeometry(g);
		ok = ok && RS2GetLiveGeometryCount()==liveBaseline
			&& RS2GetGeometryVertexBytes()==vertexBaseline
			&& RS2GetGeometryIndexBytes()==indexBaseline;
		if(ok) done++;
	}
	Debug("RS2X64STRESS|geometry cycles %d of %d\n", done, cycles);
	RS2XSStep("geometry: 4096 create / destroy cycles, counters exact", done==cycles, all);

	vector<CRS2GeometryResource *> live;
	for(c = 0; c<held; c++){
		CRS2GeometryResource *g = RS2CreateGeometry(layout, &vertices[0], 300);
		if(!g) break;
		live.push_back(g);
	}
	const bool heldOk = live.size()==(size_t)held
		&& RS2GetLiveGeometryCount()==liveBaseline+(unsigned int)held
		&& RS2GetGeometryVertexBytes()==vertexBaseline+(unsigned int)held*300*sizeof(RS2XSVertex);
	RS2XSSample("512 geometries held");
	for(i = 0; i<live.size(); i++) RS2DestroyGeometry(live[i]);
	RS2XSStep("geometry: 512 alive at once, then freed", heldOk
		&& RS2GetLiveGeometryCount()==liveBaseline
		&& RS2GetGeometryVertexBytes()==vertexBaseline
		&& RS2GetGeometryIndexBytes()==indexBaseline, all);
}

//	---- textures -------------------------------------------------------------------

static void RS2XSTextures(bool *all){
	CRS2D3D12Backend *backend = RS2D3D12GetActiveBackend();
	RS2XSStep("Direct3D 12 backend active", backend!=NULL, all);
	if(!backend) return;

	CRS2D3D12Descriptors *descriptors = backend->GetDescriptors();
	const unsigned int capacity = descriptors->GetCapacity();
	backend->WaitForGpu();
	backend->CollectRetiredTextures();
	const unsigned int textureBaseline = RS2D3D12_GetLiveTextureCount();
	const unsigned int descriptorBaseline = descriptors->GetLive();
	Debug("RS2X64STRESS|texture baseline: live %u, descriptors %u of %u\n",
		textureBaseline, descriptorBaseline, capacity);

	chdir(g_BaseDir);
	const unsigned int fileCycles = capacity*2+2, mutableCycles = capacity+2;
	unsigned int c, fileDone = 0, mutableDone = 0, peak = descriptorBaseline;
	for(c = 0; c<fileCycles; c++){
		CRS2TextureResource *t = RS2CreateTextureFromFile("Skin\\Default_Blue\\Frame.png", 0, 1);
		const bool valid = t && t->IsValid();
		if(descriptors->GetLive()>peak) peak = descriptors->GetLive();
		RS2DestroyTexture(t);
		if(!valid) break;
		fileDone++;
	}
	for(c = 0; c<mutableCycles; c++){
		CRS2TextureResource *t = RS2CreateMutableTexture(64, 64);
		const bool valid = t && t->IsValid();
		if(descriptors->GetLive()>peak) peak = descriptors->GetLive();
		RS2DestroyTexture(t);
		if(!valid) break;
		mutableDone++;
	}
	backend->WaitForGpu();
	backend->CollectRetiredTextures();
	Debug("RS2X64STRESS|texture cycles: file %u of %u, mutable %u of %u, descriptors peak %u\n",
		fileDone, fileCycles, mutableDone, mutableCycles, peak);
	RS2XSStep("textures: skin PNG, 2 x heap capacity + 2 cycles", fileDone==fileCycles, all);
	RS2XSStep("textures: mutable 64 x 64, heap capacity + 2 cycles", mutableDone==mutableCycles, all);
	RS2XSStep("descriptors reused (peak below capacity)", peak<capacity, all);
	RS2XSStep("live textures and descriptors back to baseline",
		RS2D3D12_GetLiveTextureCount()==textureBaseline && descriptors->GetLive()==descriptorBaseline, all);
}

//	---- run ---------------------------------------------------------------------------

bool RS2X64StressRun(){
	bool all = true;
	Debug("RS2X64STRESS|process: %d-bit\n", (int)(sizeof(void *)*8));
	RS2XSSample("start");
#ifdef _WIN64
	RS2XSAddressSpace(&all);
#else
	Debug("RS2X64STRESS|%-52s|skip (32-bit process)\n", "address space beyond 4 GiB");
#endif
	RS2XSGeometry(&all);
	RS2XSTextures(&all);
	RS2XSSample("end");
	Debug("RS2X64STRESS|result|%s\n", all ? "pass" : "FAIL");
	return all;
}
