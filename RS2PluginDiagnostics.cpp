// RS2EX v0.3.1: plugin diagnostics registry. Querying never loads a plugin.
#include "stdafx.h"
#include "RS2PluginDiagnostics.h"
#include "RS2D3D12Backend.h"
#include "RS2D3D12TextureBackend.h"
#include "RS2D3D12Texture.h"
#include "RS2TextureResource.h"
#include "CPlugin.h"
#include <climits>
#include <cstring>
#include <psapi.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#pragma comment(lib, "psapi.lib")

static const char *const kRS2PluginTypes[] = {
    "Env", "Girder", "Line", "Pier", "Pole", "Rail", "Skin", "Station",
    "Struct", "Surface", "Tie", "Train"
};

struct RS2PluginScopeState {
    bool active;
    RS2PluginKey key;
    RS2PluginScopeState(): active(false) {}
};
static thread_local RS2PluginScopeState s_pluginScope;

static unsigned long long RS2DiagnosticClock() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (unsigned long long)now.QuadPart;
}

static unsigned long long RS2DiagnosticElapsedUs(unsigned long long started) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const unsigned long long elapsed=RS2DiagnosticClock()-started;
    return (unsigned long long)((long double)elapsed*1000000.0L/frequency.QuadPart);
}

static std::wstring RS2DiagnosticExtendedPath(std::wstring wide) {
    if (wide.size()>=4 && wide[0]==L'\\' && wide[1]==L'\\' &&
        wide[2]==L'?' && wide[3]==L'\\') return wide;
    // The legacy loader builds roots from a base path that may already end
    // in a separator. Extended-length paths do not normalize doubled ones.
    std::wstring normalized;
    normalized.reserve(wide.size());
    for (size_t i=0; i<wide.size(); ++i) {
        if (wide[i]==L'\\' && !normalized.empty() &&
            normalized[normalized.size()-1]==L'\\' &&
            !(i==1 && wide[0]==L'\\')) continue;
        normalized+=wide[i];
    }
    wide.swap(normalized);
    if (wide.size()>=2 && wide[0]==L'\\' && wide[1]==L'\\')
        return L"\\\\?\\UNC\\"+wide.substr(2);
    if (wide.size()>=3 && wide[1]==L':' && wide[2]==L'\\')
        return L"\\\\?\\"+wide;
    return wide;
}

static std::wstring RS2DiagnosticWidePath(const std::string &path) {
    const int length=MultiByteToWideChar(CP_ACP,0,path.c_str(),-1,0,0);
    if (length<=0) return std::wstring();
    std::wstring wide((size_t)length,L'\0');
    if (!MultiByteToWideChar(CP_ACP,0,path.c_str(),-1,&wide[0],length))
        return std::wstring();
    wide.resize((size_t)length-1);
    return RS2DiagnosticExtendedPath(wide);
}

RS2PluginType RS2PluginTypeFromName(const char *name) {
    if (!name) return RS2_PLUGIN_UNKNOWN;
    for (unsigned int i=0; i<sizeof(kRS2PluginTypes)/sizeof(kRS2PluginTypes[0]); ++i)
        if (_mbsicmp((const unsigned char *)name,
                (const unsigned char *)kRS2PluginTypes[i])==0)
            return (RS2PluginType)i;
    return RS2_PLUGIN_UNKNOWN;
}

const char *RS2PluginTypeName(RS2PluginType type) {
    return type>=RS2_PLUGIN_ENV && type<RS2_PLUGIN_UNKNOWN
        ? kRS2PluginTypes[(int)type] : "Unknown";
}

bool RS2PluginKeyLess::operator()(const RS2PluginKey &a,
    const RS2PluginKey &b) const {
    if (a.type!=b.type) return a.type<b.type;
    return _mbsicmp((const unsigned char *)a.id.c_str(),
        (const unsigned char *)b.id.c_str())<0;
}

bool RS2DiagnosticTextLess::operator()(const std::string &a,
    const std::string &b) const {
    return _mbsicmp((const unsigned char *)a.c_str(),
        (const unsigned char *)b.c_str())<0;
}

void RS2DiagnosticCount::Add(unsigned long long n) {
    if (overflow) return;
    if (ULLONG_MAX-value<n) { available=false; overflow=true; return; }
    value+=n;
    available=true;
}

void RS2PluginDiagnosticsRegistry::Touch(Entry &entry) {
    entry.snapshot.generation=++m_generation;
}

void RS2PluginDiagnosticsRegistry::RegisterDiscovered(const RS2PluginKey &key,
    const std::string &root) {
    Entry &entry=m_entries[key];
    if (entry.runtime) m_byRuntime.erase(entry.runtime);
    entry=Entry();
    entry.snapshot.identity.key=key;
    entry.snapshot.identity.rootPath=root;
    entry.snapshot.state=RS2_PLUGIN_DIAG_DISCOVERED;
    entry.snapshot.completedStages=RS2_DIAG_STAGE_DISCOVERY;
    entry.runtime=0;
    Touch(entry);
    entry.snapshot.identity.discoveryGeneration=entry.snapshot.generation;
}

void RS2PluginDiagnosticsRegistry::SetDefinition(const RS2PluginKey &key,
    const std::string &path, bool oldForm) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return;
    it->second.snapshot.identity.definitionPath=path;
    it->second.snapshot.identity.oldForm=oldForm;
    Touch(it->second);
}

void RS2PluginDiagnosticsRegistry::SetWideFilesystemRoot(
    const RS2PluginKey &key, const std::wstring &path) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return;
    it->second.wideFilesystemRoot=RS2DiagnosticExtendedPath(path);
    Touch(it->second);
}

void RS2PluginDiagnosticsRegistry::MarkHeaderReady(const RS2PluginKey &key,
    CPlugin *runtime, const std::string &name, const std::string &author,
    float version) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return;
    Entry &entry=it->second;
    if (entry.runtime) m_byRuntime.erase(entry.runtime);
    entry.runtime=runtime;
    if (runtime) m_byRuntime[runtime]=key;
    entry.snapshot.identity.name=name;
    entry.snapshot.identity.author=author;
    entry.snapshot.identity.version=version;
    entry.snapshot.state=RS2_PLUGIN_DIAG_HEADER_READY;
    entry.snapshot.completedStages|=RS2_DIAG_STAGE_HEADER;
    Touch(entry);
}

void RS2PluginDiagnosticsRegistry::MarkFailure(const RS2PluginKey &key,
    RS2PluginDiagnosticState state, RS2DiagnosticCode code,
    const std::string &message, const std::string &path) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return;
    Entry &entry=it->second;
    entry.snapshot.state=state;
    if (state!=RS2_PLUGIN_DIAG_FAILED_LOAD) {
        if (entry.runtime) m_byRuntime.erase(entry.runtime);
        entry.runtime=0;
    }
    RS2PluginDiagnostic diagnostic;
    diagnostic.severity=RS2_DIAG_ERROR;
    diagnostic.code=code;
    diagnostic.message=message;
    diagnostic.sourcePath=path;
    entry.snapshot.diagnostics.push_back(diagnostic);
    Touch(entry);
}

void RS2PluginDiagnosticsRegistry::MarkLoading(const RS2PluginKey &key) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return;
    it->second.snapshot.state=RS2_PLUGIN_DIAG_LOADING;
    Touch(it->second);
}

void RS2PluginDiagnosticsRegistry::MarkReady(const RS2PluginKey &key) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return;
    it->second.snapshot.state=RS2_PLUGIN_DIAG_READY;
    it->second.snapshot.completedStages|=RS2_DIAG_STAGE_CONTENT;
    Touch(it->second);
}

void RS2PluginDiagnosticsRegistry::Disassociate(CPlugin *plugin) {
    if (!plugin) return;
    std::map<CPlugin *,RS2PluginKey>::iterator found=m_byRuntime.find(plugin);
    if (found==m_byRuntime.end()) return;
    EntryMap::iterator it=m_entries.find(found->second);
    if (it!=m_entries.end()) {
        it->second.runtime=0;
        Touch(it->second);
    }
    m_byRuntime.erase(found);
}

bool RS2PluginDiagnosticsRegistry::HasRuntime(const RS2PluginKey &key) const {
    EntryMap::const_iterator it=m_entries.find(key);
    return it!=m_entries.end() && it->second.runtime!=0;
}

bool RS2PluginDiagnosticsRegistry::LoadExplicit(const RS2PluginKey &key) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end() || !it->second.runtime) return false;
    if (it->second.snapshot.state==RS2_PLUGIN_DIAG_READY) return true;
    // A failed body may have partially mutated the legacy object. Never retry it.
    if (it->second.snapshot.state!=RS2_PLUGIN_DIAG_HEADER_READY) return false;
    return it->second.runtime->LoadAndGet()!=0;
}

bool RS2PluginDiagnosticsRegistry::Get(const RS2PluginKey &key,
    RS2PluginDiagnosticsSnapshot *out) const {
    if (!out) return false;
    EntryMap::const_iterator it=m_entries.find(key);
    if (it==m_entries.end()) return false;
    *out=it->second.snapshot;
    if (!it->second.textureLogicalComplete) {
        out->textures.decodedLogicalBytes.available=false;
        out->textures.gpuLogicalBytes.available=false;
    }
    if (!it->second.textureAllocationComplete)
        out->textures.gpuAllocationBytes.available=false;
    if (out->completedStages&RS2_DIAG_STAGE_ASSETS) {
        out->textures.residentTextureResourceCount.Set(0);
        out->textures.sharedTextureCount.Set(0);
        out->textures.exclusiveTextureCount.Set(0);
    }
    for (std::map<std::string,TextureResource,RS2DiagnosticTextLess>::const_iterator
        texture=m_textures.begin(); texture!=m_textures.end(); ++texture) {
        if (!texture->second.resident ||
            texture->second.referrers.find(key)==texture->second.referrers.end())
            continue;
        out->textures.residentTextureResourceCount.Add(1);
        if (texture->second.referrers.size()==1)
            out->textures.exclusiveTextureCount.Add(1);
        else out->textures.sharedTextureCount.Add(1);
    }
    if (out->completedStages&RS2_DIAG_STAGE_ASSETS) {
    // Observed resident payload: mesh CPU geometry plus exact D3D12 texture
    // allocation. It deliberately excludes unavailable mesh GPU/owner overhead.
    out->memory.referencedResidentBytes.Set(0);
    out->memory.exclusiveResidentBytes.Set(0);
    out->memory.sharedResidentBytes.Set(0);
    for (std::map<std::string,MeshResource,RS2DiagnosticTextLess>::const_iterator
        mesh=m_meshes.begin(); mesh!=m_meshes.end(); ++mesh) {
        if (!mesh->second.resident ||
            mesh->second.referrers.find(key)==mesh->second.referrers.end()) continue;
        out->memory.referencedResidentBytes.Add(mesh->second.cpuBytes);
        if (mesh->second.referrers.size()==1)
            out->memory.exclusiveResidentBytes.Add(mesh->second.cpuBytes);
        else out->memory.sharedResidentBytes.Add(mesh->second.cpuBytes);
    }
    bool allocationComplete=true;
    for (std::map<std::string,TextureResource,RS2DiagnosticTextLess>::const_iterator
        texture=m_textures.begin(); texture!=m_textures.end(); ++texture) {
        if (!texture->second.resident ||
            texture->second.referrers.find(key)==texture->second.referrers.end()) continue;
        if (!texture->second.allocationAvailable) {
            allocationComplete=false;
            continue;
        }
        const unsigned long long bytes=texture->second.gpuAllocationBytes;
        out->memory.referencedResidentBytes.Add(bytes);
        if (texture->second.referrers.size()==1)
            out->memory.exclusiveResidentBytes.Add(bytes);
        else out->memory.sharedResidentBytes.Add(bytes);
    }
    if (!allocationComplete) {
        out->memory.referencedResidentBytes.available=false;
        out->memory.exclusiveResidentBytes.available=false;
        out->memory.sharedResidentBytes.available=false;
    }
    }
    return true;
}

void RS2PluginDiagnosticsRegistry::List(
    std::vector<RS2PluginSummary> *out) const {
    if (!out) return;
    out->clear();
    out->reserve(m_entries.size());
    for (EntryMap::const_iterator it=m_entries.begin(); it!=m_entries.end(); ++it) {
        RS2PluginSummary summary;
        summary.key=it->second.snapshot.identity.key;
        summary.name=it->second.snapshot.identity.name;
        summary.state=it->second.snapshot.state;
        summary.completedStages=it->second.snapshot.completedStages;
        summary.generation=it->second.snapshot.generation;
        out->push_back(summary);
    }
}

bool RS2PluginDiagnosticsRegistry::GetRuntime(
    RS2RuntimeDiagnosticsSnapshot *out) const {
    if (!out) return false;
    *out=RS2RuntimeDiagnosticsSnapshot();
    out->pluginCount.Set((unsigned long long)m_entries.size());
    out->failedPluginCount.Set(0);
    out->textures.textureReferenceCount=m_textureGlobal.textureReferenceCount;
    out->textures.textureLoadSuccessCount=m_textureGlobal.textureLoadSuccessCount;
    out->textures.textureLoadFailureCount=m_textureGlobal.textureLoadFailureCount;
    out->textures.textureDecodeFailureCount=m_textureGlobal.textureDecodeFailureCount;
    out->textures.textureUploadFailureCount=m_textureGlobal.textureUploadFailureCount;
    out->textures.textureDescriptorFailureCount=m_textureGlobal.textureDescriptorFailureCount;
    out->geometry.meshReferenceCount=m_meshGlobal.meshReferenceCount;
    out->geometry.meshCacheHitCount=m_meshGlobal.meshCacheHitCount;
    out->geometry.meshCacheMissCount=m_meshGlobal.meshCacheMissCount;
    out->geometry.meshLoadSuccessCount=m_meshGlobal.meshLoadSuccessCount;
    out->geometry.meshLoadFailureCount=m_meshGlobal.meshLoadFailureCount;
    if (!out->textures.textureReferenceCount.available &&
        !out->textures.textureReferenceCount.overflow)
        out->textures.textureReferenceCount.Set(0);
    if (!out->textures.textureLoadSuccessCount.available &&
        !out->textures.textureLoadSuccessCount.overflow)
        out->textures.textureLoadSuccessCount.Set(0);
    if (!out->textures.textureLoadFailureCount.available &&
        !out->textures.textureLoadFailureCount.overflow)
        out->textures.textureLoadFailureCount.Set(0);
    if (!out->textures.textureDecodeFailureCount.available &&
        !out->textures.textureDecodeFailureCount.overflow)
        out->textures.textureDecodeFailureCount.Set(0);
    if (!out->textures.textureUploadFailureCount.available &&
        !out->textures.textureUploadFailureCount.overflow)
        out->textures.textureUploadFailureCount.Set(0);
    if (!out->textures.textureDescriptorFailureCount.available &&
        !out->textures.textureDescriptorFailureCount.overflow)
        out->textures.textureDescriptorFailureCount.Set(0);
    if (!out->geometry.meshReferenceCount.available &&
        !out->geometry.meshReferenceCount.overflow)
        out->geometry.meshReferenceCount.Set(0);
    if (!out->geometry.meshCacheHitCount.available && !out->geometry.meshCacheHitCount.overflow) out->geometry.meshCacheHitCount.Set(0);
    if (!out->geometry.meshCacheMissCount.available && !out->geometry.meshCacheMissCount.overflow) out->geometry.meshCacheMissCount.Set(0);
    if (!out->geometry.meshLoadSuccessCount.available && !out->geometry.meshLoadSuccessCount.overflow) out->geometry.meshLoadSuccessCount.Set(0);
    if (!out->geometry.meshLoadFailureCount.available && !out->geometry.meshLoadFailureCount.overflow) out->geometry.meshLoadFailureCount.Set(0);
    out->textures.textureCacheHitCount=m_textureGlobal.textureCacheHitCount;
    out->textures.textureCacheMissCount=m_textureGlobal.textureCacheMissCount;
    if (!out->textures.textureCacheHitCount.available && !out->textures.textureCacheHitCount.overflow) out->textures.textureCacheHitCount.Set(0);
    if (!out->textures.textureCacheMissCount.available && !out->textures.textureCacheMissCount.overflow) out->textures.textureCacheMissCount.Set(0);
    for (EntryMap::const_iterator it=m_entries.begin(); it!=m_entries.end(); ++it) {
        RS2PluginDiagnosticState state=it->second.snapshot.state;
        if (state==RS2_PLUGIN_DIAG_FAILED_DISCOVERY ||
            state==RS2_PLUGIN_DIAG_FAILED_HEADER ||
            state==RS2_PLUGIN_DIAG_FAILED_LOAD)
            out->failedPluginCount.Add(1);
    }
    out->geometry.uniqueMeshCount.Set(0);
    out->geometry.vertexCount.Set(0);
    out->geometry.triangleCount.Set(0);
    out->geometry.indexCount.Set(0);
    out->geometry.cpuGeometryBytes.Set(0);
    out->geometry.materialSlotsFromMeshes.Set(0);
    out->geometry.gpuGeometryLogicalBytes.available=false;
    out->geometry.gpuGeometryAllocationBytes.available=false;
    for (std::map<std::string,MeshResource,RS2DiagnosticTextLess>::const_iterator
        mesh=m_meshes.begin(); mesh!=m_meshes.end(); ++mesh) {
        if (!mesh->second.resident) continue;
        out->geometry.uniqueMeshCount.Add(1);
        out->geometry.vertexCount.Add(mesh->second.vertices);
        out->geometry.triangleCount.Add(mesh->second.triangles);
        out->geometry.indexCount.Add(mesh->second.indices);
        out->geometry.cpuGeometryBytes.Add(mesh->second.cpuBytes);
        out->geometry.materialSlotsFromMeshes.Add(mesh->second.materials);
    }
    out->textures.residentTextureResourceCount.Set(0);
    out->textures.uniqueTextureSourceCount.Set(0);
    out->textures.uniqueTextureVariantCount.Set(0);
    out->textures.sourceDiskBytes.Set(0);
    out->textures.decodedLogicalBytes.Set(0);
    out->textures.gpuLogicalBytes.Set(0);
    out->textures.gpuAllocationBytes.Set(0);
    out->textures.mipLevelTotal.Set(0);
    out->textures.bc1Count.Set(0);
    out->textures.bc3Count.Set(0);
    out->textures.rgba8Count.Set(0);
    bool allLogicalKnown=true, allAllocationsKnown=true;
    std::set<std::string,RS2DiagnosticTextLess> textureSources;
    for (std::map<std::string,TextureResource,RS2DiagnosticTextLess>::const_iterator
        texture=m_textures.begin(); texture!=m_textures.end(); ++texture) {
        const TextureResource &value=texture->second;
        if (!value.resident) continue;
        out->textures.residentTextureResourceCount.Add(1);
        out->textures.uniqueTextureVariantCount.Add(1);
        textureSources.insert(value.source);
        out->textures.decodedLogicalBytes.Add(value.decodedBytes);
        out->textures.gpuLogicalBytes.Add(value.gpuLogicalBytes);
        if (!value.logicalAvailable) allLogicalKnown=false;
        out->textures.mipLevelTotal.Add(value.mips);
        if (value.format==RS2_TEXTURE_DIAG_BC1) out->textures.bc1Count.Add(1);
        else if (value.format==RS2_TEXTURE_DIAG_BC3) out->textures.bc3Count.Add(1);
        else if (value.format==RS2_TEXTURE_DIAG_RGBA8) out->textures.rgba8Count.Add(1);
        if (value.allocationAvailable)
            out->textures.gpuAllocationBytes.Add(value.gpuAllocationBytes);
        else allAllocationsKnown=false;
    }
    for (EntryMap::const_iterator it=m_entries.begin(); it!=m_entries.end(); ++it)
        textureSources.insert(it->second.textureSources.begin(),
            it->second.textureSources.end());
    textureSources.insert(m_globalTextureSources.begin(),m_globalTextureSources.end());
    for (std::set<std::string,RS2DiagnosticTextLess>::const_iterator
        source=textureSources.begin(); source!=textureSources.end(); ++source) {
        out->textures.uniqueTextureSourceCount.Add(1);
        std::map<std::string,SourceFileSize,RS2DiagnosticTextLess>::const_iterator
            size=m_sourceSizes.find(*source);
        if (size!=m_sourceSizes.end() && size->second.exists)
            out->textures.sourceDiskBytes.Add(size->second.bytes);
    }
    if (!allLogicalKnown) {
        out->textures.decodedLogicalBytes.available=false;
        out->textures.gpuLogicalBytes.available=false;
    }
    if (!allAllocationsKnown) out->textures.gpuAllocationBytes.available=false;
    out->audio=m_audioGlobal;
    out->textures.mutableTextureCount.Set(RS2D3D12_GetMutableStats().live);
    CRS2D3D12Backend *backend=RS2D3D12GetActiveBackend();
    if (backend) {
        const CRS2D3D12Descriptors *descriptors=backend->GetDescriptors();
        out->srvCapacity.Set(descriptors->GetCapacity());
        out->srvLive.Set(descriptors->GetLive());
        out->srvPeak.Set(descriptors->GetPeak());
        out->srvAllocations.Set(descriptors->GetAllocations());
        out->srvReleases.Set(descriptors->GetReleases());
        out->srvAllocationFailures.Set(descriptors->GetAllocationFailures());
        out->srvStaleReleaseFailures.Set(descriptors->GetStaleReleaseFailures());
    }
    PROCESS_MEMORY_COUNTERS_EX memory;
    ZeroMemory(&memory,sizeof(memory));
    memory.cb=sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
        (PROCESS_MEMORY_COUNTERS *)&memory,sizeof(memory))) {
        out->processWorkingSetBytes.Set(memory.WorkingSetSize);
        out->processPrivateBytes.Set(memory.PrivateUsage);
    }
    out->generation=m_generation;
    return true;
}

bool RS2PluginDiagnosticsRegistry::RefreshFilesystem(const RS2PluginKey &key) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return false;
    Entry &entry=it->second;
    const std::string root=entry.snapshot.identity.rootPath;
    RS2PluginFileStats measured;
    measured.directoryFileCount.Set(0);
    measured.directoryBytes.Set(0);
    bool complete=true;
    const std::wstring wideRoot=entry.wideFilesystemRoot.empty()
        ? RS2DiagnosticWidePath(root) : entry.wideFilesystemRoot;
    const DWORD attributes=wideRoot.empty() ? INVALID_FILE_ATTRIBUTES
        : GetFileAttributesW(wideRoot.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES ||
        !(attributes&FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        complete=false;
    } else {
        std::vector<std::wstring> pending;
        pending.push_back(wideRoot);
        while (!pending.empty()) {
            const std::wstring dir=pending.back();
            pending.pop_back();
            const std::wstring pattern=dir+L"\\*";
            WIN32_FIND_DATAW data;
            HANDLE search=FindFirstFileW(pattern.c_str(),&data);
            if (search==INVALID_HANDLE_VALUE) {
                if (GetLastError()!=ERROR_FILE_NOT_FOUND) complete=false;
                continue;
            }
            do {
                if (!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))
                    continue;
                // Never follow junctions or symlinks out of the plugin root.
                if (data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) continue;
                if (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
                    pending.push_back(dir+L"\\"+data.cFileName);
                } else {
                    measured.directoryFileCount.Add(1);
                    const unsigned long long bytes=
                        ((unsigned long long)data.nFileSizeHigh<<32)|data.nFileSizeLow;
                    measured.directoryBytes.Add(bytes);
                }
            } while (FindNextFileW(search,&data));
            if (GetLastError()!=ERROR_NO_MORE_FILES) complete=false;
            FindClose(search);
        }
    }
    if (measured.directoryFileCount.overflow || measured.directoryBytes.overflow)
        complete=false;
    entry.snapshot.files.directoryFileCount=measured.directoryFileCount;
    entry.snapshot.files.directoryBytes=measured.directoryBytes;
    if (complete) entry.snapshot.completedStages|=RS2_DIAG_STAGE_FILESYSTEM;
    else {
        entry.snapshot.completedStages&=~RS2_DIAG_STAGE_FILESYSTEM;
        RS2PluginDiagnostic diagnostic;
        diagnostic.severity=RS2_DIAG_WARNING;
        diagnostic.code=RS2_DIAG_RESOURCE_PATH;
        diagnostic.message="Directory footprint scan incomplete";
        diagnostic.sourcePath=root;
        entry.snapshot.diagnostics.push_back(diagnostic);
    }
    Touch(entry);
    return complete;
}

void RS2PluginDiagnosticsRegistry::RecordTime(const RS2PluginKey &key,
    RS2PluginTimingKind kind, unsigned long long us) {
    EntryMap::iterator it=m_entries.find(key);
    if (it==m_entries.end()) return;
    RS2PluginTimingStats &stats=it->second.snapshot.timing;
    switch (kind) {
    case RS2_DIAG_TIME_HEADER:
        stats.headerParseTimeUs.Add(us); stats.headerSamples.Add(1);
        stats.totalObservedLoadTimeUs.Add(us); break;
    case RS2_DIAG_TIME_FULL_PARSE:
        stats.fullPluginParseTimeUs.Add(us); stats.fullParseSamples.Add(1);
        stats.totalObservedLoadTimeUs.Add(us); break;
    case RS2_DIAG_TIME_MESH_IMPORT:
        stats.meshImportTimeUs.Add(us); stats.meshImportSamples.Add(1); break;
    case RS2_DIAG_TIME_TEXTURE_DECODE:
        stats.textureDecodeTimeUs.Add(us); stats.textureDecodeSamples.Add(1); break;
    case RS2_DIAG_TIME_TEXTURE_UPLOAD:
        stats.textureUploadTimeUs.Add(us); stats.textureUploadSamples.Add(1); break;
    case RS2_DIAG_TIME_AUDIO_LOAD:
        stats.audioLoadTimeUs.Add(us); stats.audioLoadSamples.Add(1); break;
    }
    Touch(it->second);
}

static std::string RS2VariantName(const std::string &path,
    unsigned int colourKey, int mip) {
    char suffix[64];
    sprintf(suffix,"\x1f%08X\x1f%d",colourKey,mip);
    return path+suffix;
}

bool RS2PluginDiagnosticsRegistry::LookupSourceSize(const std::string &path,
    unsigned long long *bytes) {
    std::map<std::string,SourceFileSize,RS2DiagnosticTextLess>::iterator
        found=m_sourceSizes.find(path);
    if (found==m_sourceSizes.end()) {
        SourceFileSize value;
        WIN32_FILE_ATTRIBUTE_DATA data;
        const std::wstring widePath=RS2DiagnosticWidePath(path);
        if (!widePath.empty() &&
            GetFileAttributesExW(widePath.c_str(),GetFileExInfoStandard,&data) &&
            !(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            value.exists=true;
            value.bytes=((unsigned long long)data.nFileSizeHigh<<32)|data.nFileSizeLow;
        }
        found=m_sourceSizes.insert(std::make_pair(path,value)).first;
    }
    if (bytes) *bytes=found->second.bytes;
    return found->second.exists;
}

void RS2PluginDiagnosticsRegistry::RecordFileReference(Entry &entry,
    const std::string &path) {
    if (!entry.snapshot.files.referencedDiskBytes.available)
        entry.snapshot.files.referencedDiskBytes.Set(0);
    if (!entry.snapshot.files.missingFileCount.available)
        entry.snapshot.files.missingFileCount.Set(0);
    entry.snapshot.files.referencedFileCount.Add(1);
    if (entry.files.insert(path).second) {
        entry.snapshot.files.referencedUniqueFileCount.Add(1);
        unsigned long long bytes=0;
        if (LookupSourceSize(path,&bytes))
            entry.snapshot.files.referencedDiskBytes.Add(bytes);
        else entry.snapshot.files.missingFileCount.Add(1);
    }
}

void RS2PluginDiagnosticsRegistry::RecordMesh(const std::string &path,
    unsigned int colourKey, int mip, const void *resource, bool cacheHit,
    unsigned long long vertices, unsigned long long triangles,
    unsigned long long indices, unsigned long long materials,
    unsigned long long subsets, unsigned long long cpuBytes) {
    const std::string variant=RS2VariantName(path,colourKey,mip);
    m_meshGlobal.meshReferenceCount.Add(1);
    if (cacheHit) m_meshGlobal.meshCacheHitCount.Add(1);
    else {
        m_meshGlobal.meshCacheMissCount.Add(1);
        if (resource) m_meshGlobal.meshLoadSuccessCount.Add(1);
        else m_meshGlobal.meshLoadFailureCount.Add(1);
    }
    RS2PluginKey plugin;
    const bool attributed=RS2PluginDiagnosticsScope::Current(&plugin);
    EntryMap::iterator owner=attributed ? m_entries.find(plugin) : m_entries.end();
    if (owner!=m_entries.end()) {
        Entry &entry=owner->second;
        if (!entry.snapshot.geometry.meshCacheHitCount.available) {
            entry.snapshot.geometry.meshCacheHitCount.Set(0);
            entry.snapshot.geometry.meshCacheMissCount.Set(0);
            entry.snapshot.geometry.meshLoadSuccessCount.Set(0);
            entry.snapshot.geometry.meshLoadFailureCount.Set(0);
        }
        if (!entry.snapshot.geometry.uniqueMeshCount.available) {
            entry.snapshot.geometry.uniqueMeshCount.Set(0);
            entry.snapshot.geometry.vertexCount.Set(0);
            entry.snapshot.geometry.triangleCount.Set(0);
            entry.snapshot.geometry.indexCount.Set(0);
            entry.snapshot.geometry.materialSlotsFromMeshes.Set(0);
            entry.snapshot.geometry.cpuGeometryBytes.Set(0);
            entry.snapshot.materials.materialSlotCount.Set(0);
            entry.snapshot.materials.meshSubsetCount.Set(0);
        }
        entry.snapshot.geometry.meshReferenceCount.Add(1);
        if (cacheHit) entry.snapshot.geometry.meshCacheHitCount.Add(1);
        else {
            entry.snapshot.geometry.meshCacheMissCount.Add(1);
            if (resource) entry.snapshot.geometry.meshLoadSuccessCount.Add(1);
            else entry.snapshot.geometry.meshLoadFailureCount.Add(1);
        }
        entry.snapshot.completedStages|=RS2_DIAG_STAGE_ASSETS;
        RecordFileReference(entry,path);
        if (resource && entry.meshVariants.insert(variant).second) {
            entry.snapshot.geometry.uniqueMeshCount.Add(1);
            entry.snapshot.geometry.vertexCount.Add(vertices);
            entry.snapshot.geometry.triangleCount.Add(triangles);
            entry.snapshot.geometry.indexCount.Add(indices);
            entry.snapshot.geometry.materialSlotsFromMeshes.Add(materials);
            entry.snapshot.geometry.cpuGeometryBytes.Add(cpuBytes);
            entry.snapshot.materials.materialSlotCount.Add(materials);
            entry.snapshot.materials.meshSubsetCount.Add(subsets);
        }
        if (!resource && !cacheHit) {
            RS2PluginDiagnostic diagnostic;
            diagnostic.severity=RS2_DIAG_WARNING;
            diagnostic.code=RS2_DIAG_MESH_IMPORT;
            diagnostic.message="Mesh load failed";
            diagnostic.sourcePath=path;
            entry.snapshot.diagnostics.push_back(diagnostic);
        }
        Touch(entry);
    }
    if (!resource) return;
    MeshResource &observed=m_meshes[variant];
    if (observed.pointer!=resource) {
        observed=MeshResource();
        observed.pointer=resource;
        observed.vertices=vertices; observed.triangles=triangles;
        observed.indices=indices; observed.materials=materials;
        observed.subsets=subsets; observed.cpuBytes=cpuBytes;
    }
    observed.resident=true;
    if (owner!=m_entries.end() && observed.referrers.insert(plugin).second) {
        for (std::set<RS2PluginKey,RS2PluginKeyLess>::const_iterator
            ref=observed.referrers.begin(); ref!=observed.referrers.end(); ++ref) {
            if (ref->type==plugin.type && ref->id==plugin.id) continue;
            EntryMap::iterator affected=m_entries.find(*ref);
            if (affected!=m_entries.end()) Touch(affected->second);
        }
    }
}

void RS2PluginDiagnosticsRegistry::ForgetMesh(const void *resource) {
    if (!resource) return;
    for (std::map<std::string,MeshResource,RS2DiagnosticTextLess>::iterator
        it=m_meshes.begin(); it!=m_meshes.end(); ++it)
        if (it->second.pointer==resource) {
            it->second.resident=false;
            for (std::set<RS2PluginKey,RS2PluginKeyLess>::const_iterator
                ref=it->second.referrers.begin(); ref!=it->second.referrers.end(); ++ref) {
                EntryMap::iterator affected=m_entries.find(*ref);
                if (affected!=m_entries.end()) Touch(affected->second);
            }
            return;
        }
}

void RS2PluginDiagnosticsRegistry::RecordTexture(const std::string &path,
    unsigned int colourKey, int mip, const void *resource, bool cacheHit,
    unsigned int width, unsigned int height, bool fileSource,
    const RS2TextureDiagnosticsInfo *info) {
    const std::string variant=RS2VariantName(path,colourKey,mip);
    const RS2D3D12TextureFailureStage failureStage=(!resource && !cacheHit)
        ? RS2D3D12_GetLastTextureFailureStage()
        : RS2_D3D12_TEXTURE_FAILURE_NONE;
    const std::string failureMessage=(!resource && !cacheHit)
        ? RS2D3D12_GetLastTextureFailureMessage() : "";
    m_textureGlobal.textureReferenceCount.Add(1);
    if (cacheHit) m_textureGlobal.textureCacheHitCount.Add(1);
    else m_textureGlobal.textureCacheMissCount.Add(1);
    m_globalTextureSources.insert(path);
    if (!cacheHit) {
        if (resource) m_textureGlobal.textureLoadSuccessCount.Add(1);
        else {
            m_textureGlobal.textureLoadFailureCount.Add(1);
            if (failureStage==RS2_D3D12_TEXTURE_FAILURE_DECODE)
                m_textureGlobal.textureDecodeFailureCount.Add(1);
            else if (failureStage==RS2_D3D12_TEXTURE_FAILURE_UPLOAD)
                m_textureGlobal.textureUploadFailureCount.Add(1);
            else if (failureStage==RS2_D3D12_TEXTURE_FAILURE_DESCRIPTOR)
                m_textureGlobal.textureDescriptorFailureCount.Add(1);
        }
    }
    unsigned long long diskBytes=0;
    const bool sourceExists=fileSource && LookupSourceSize(path,&diskBytes);
    RS2PluginKey plugin;
    const bool attributed=RS2PluginDiagnosticsScope::Current(&plugin);
    EntryMap::iterator owner=attributed ? m_entries.find(plugin) : m_entries.end();
    if (owner!=m_entries.end()) {
        Entry &entry=owner->second;
        RS2PluginTextureStats &stats=entry.snapshot.textures;
        if (!stats.textureLoadSuccessCount.available) {
            stats.textureCacheHitCount.Set(0);
            stats.textureCacheMissCount.Set(0);
            stats.textureLoadSuccessCount.Set(0);
            stats.textureLoadFailureCount.Set(0);
            stats.textureDecodeFailureCount.Set(0);
            stats.textureUploadFailureCount.Set(0);
            stats.textureDescriptorFailureCount.Set(0);
            stats.sourceDiskBytes.Set(0);
            stats.decodedLogicalBytes.Set(0);
            stats.gpuLogicalBytes.Set(0);
            stats.gpuAllocationBytes.Set(0);
        }
        stats.textureReferenceCount.Add(1);
        if (cacheHit) stats.textureCacheHitCount.Add(1);
        else stats.textureCacheMissCount.Add(1);
        entry.snapshot.completedStages|=RS2_DIAG_STAGE_ASSETS;
        if (fileSource) RecordFileReference(entry,path);
        if (entry.textureSources.insert(path).second) {
            stats.uniqueTextureSourceCount.Add(1);
            if (sourceExists) stats.sourceDiskBytes.Add(diskBytes);
            const char *dot=strrchr(path.c_str(),'.');
            if (dot && !_stricmp(dot,".png")) stats.pngCount.Add(1);
            else if (dot && !_stricmp(dot,".bmp")) stats.bmpCount.Add(1);
            else if (dot && !_stricmp(dot,".dds")) stats.ddsCount.Add(1);
            else stats.otherCount.Add(1);
        }
        if (!cacheHit) {
            if (resource) stats.textureLoadSuccessCount.Add(1);
            else {
                stats.textureLoadFailureCount.Add(1);
                if (failureStage==RS2_D3D12_TEXTURE_FAILURE_DECODE)
                    stats.textureDecodeFailureCount.Add(1);
                else if (failureStage==RS2_D3D12_TEXTURE_FAILURE_UPLOAD)
                    stats.textureUploadFailureCount.Add(1);
                else if (failureStage==RS2_D3D12_TEXTURE_FAILURE_DESCRIPTOR)
                    stats.textureDescriptorFailureCount.Add(1);
            }
        }
        if (resource) {
            entry.snapshot.completedStages|=RS2_DIAG_STAGE_GPU;
            if (entry.textureVariants.insert(variant).second) {
                stats.uniqueTextureVariantCount.Add(1);
                if (info) {
                    stats.mipLevelTotal.Add(info->mipCount);
                    if (info->mipCount>1) stats.mipmappedTextureCount.Add(1);
                    if (info->format==RS2_TEXTURE_DIAG_BC1) stats.bc1Count.Add(1);
                    else if (info->format==RS2_TEXTURE_DIAG_BC3) stats.bc3Count.Add(1);
                    else if (info->format==RS2_TEXTURE_DIAG_RGBA8) stats.rgba8Count.Add(1);
                    stats.decodedLogicalBytes.Add(info->decodedLogicalBytes);
                    stats.gpuLogicalBytes.Add(info->gpuLogicalBytes);
                    if (info->allocationAvailable)
                        stats.gpuAllocationBytes.Add(info->gpuAllocationBytes);
                    else entry.textureAllocationComplete=false;
                } else {
                    entry.textureLogicalComplete=false;
                    entry.textureAllocationComplete=false;
                }
            }
            if (!stats.maxWidth.available || width>stats.maxWidth.value)
                stats.maxWidth.Set(width);
            if (!stats.maxHeight.available || height>stats.maxHeight.value)
                stats.maxHeight.Set(height);
            const unsigned long long texels=(unsigned long long)width*height;
            if (!stats.maxTexelCount.available || texels>stats.maxTexelCount.value)
                stats.maxTexelCount.Set(texels);
        } else if (!cacheHit) {
            RS2PluginDiagnostic diagnostic;
            diagnostic.severity=RS2_DIAG_WARNING;
            diagnostic.code=(!sourceExists && fileSource)
                ? RS2_DIAG_FILE_MISSING :
                failureStage==RS2_D3D12_TEXTURE_FAILURE_UPLOAD
                ? RS2_DIAG_TEXTURE_UPLOAD :
                failureStage==RS2_D3D12_TEXTURE_FAILURE_DESCRIPTOR
                ? RS2_DIAG_TEXTURE_DESCRIPTOR : RS2_DIAG_TEXTURE_DECODE;
            diagnostic.message=(!sourceExists && fileSource)
                ? "Texture source missing" :
                failureMessage.empty() ? "Texture load failed" : failureMessage;
            diagnostic.sourcePath=path;
            entry.snapshot.diagnostics.push_back(diagnostic);
        }
        Touch(entry);
    }
    if (!resource) return;
    TextureResource &observed=m_textures[variant];
    if (observed.pointer!=resource) {
        observed=TextureResource();
        observed.pointer=resource;
        observed.source=path;
        observed.width=width; observed.height=height;
        observed.diskBytes=sourceExists ? diskBytes : 0;
        if (info) {
            observed.logicalAvailable=true;
            observed.format=info->format;
            observed.mips=info->mipCount;
            observed.decodedBytes=info->decodedLogicalBytes;
            observed.gpuLogicalBytes=info->gpuLogicalBytes;
            observed.gpuAllocationBytes=info->gpuAllocationBytes;
            observed.allocationAvailable=info->allocationAvailable;
        }
    }
    observed.resident=true;
    if (owner!=m_entries.end() && observed.referrers.insert(plugin).second) {
        for (std::set<RS2PluginKey,RS2PluginKeyLess>::const_iterator
            ref=observed.referrers.begin(); ref!=observed.referrers.end(); ++ref) {
            if (ref->type==plugin.type && ref->id==plugin.id) continue;
            EntryMap::iterator affected=m_entries.find(*ref);
            if (affected!=m_entries.end()) Touch(affected->second);
        }
    }
}

void RS2PluginDiagnosticsRegistry::ForgetTexture(const void *resource) {
    if (!resource) return;
    for (std::map<std::string,TextureResource,RS2DiagnosticTextLess>::iterator
        it=m_textures.begin(); it!=m_textures.end(); ++it)
        if (it->second.pointer==resource) {
            it->second.resident=false;
            for (std::set<RS2PluginKey,RS2PluginKeyLess>::const_iterator
                ref=it->second.referrers.begin(); ref!=it->second.referrers.end(); ++ref) {
                EntryMap::iterator affected=m_entries.find(*ref);
                if (affected!=m_entries.end()) Touch(affected->second);
            }
            return;
        }
}

void RS2PluginDiagnosticsRegistry::RecordAudio(const std::string &path,
    bool success, unsigned long long pcmBytes, unsigned int bytesPerSecond) {
    unsigned long long diskBytes=0;
    const bool sourceExists=LookupSourceSize(path,&diskBytes);
    m_audioGlobal.referenceCount.Add(1);
    if (m_globalAudioSources.insert(path).second) {
        m_audioGlobal.uniqueFileCount.Add(1);
        if (sourceExists) m_audioGlobal.diskBytes.Add(diskBytes);
    }
    if (success) {
        m_audioGlobal.loadSuccessCount.Add(1);
        m_audioGlobal.decodedPcmBytes.Add(pcmBytes);
        if (bytesPerSecond)
            m_audioGlobal.durationMs.Add(pcmBytes*1000/bytesPerSecond);
    } else m_audioGlobal.loadFailureCount.Add(1);
    RS2PluginKey plugin;
    if (!RS2PluginDiagnosticsScope::Current(&plugin)) return;
    EntryMap::iterator it=m_entries.find(plugin);
    if (it==m_entries.end()) return;
    Entry &entry=it->second;
    RS2PluginAudioStats &stats=entry.snapshot.audio;
    stats.referenceCount.Add(1);
    entry.snapshot.completedStages|=RS2_DIAG_STAGE_AUDIO|RS2_DIAG_STAGE_ASSETS;
    RecordFileReference(entry,path);
    if (entry.audioSources.insert(path).second) {
        stats.uniqueFileCount.Add(1);
        if (sourceExists) stats.diskBytes.Add(diskBytes);
    }
    if (success) {
        stats.loadSuccessCount.Add(1);
        stats.decodedPcmBytes.Add(pcmBytes);
        if (bytesPerSecond) stats.durationMs.Add(pcmBytes*1000/bytesPerSecond);
    } else {
        stats.loadFailureCount.Add(1);
        RS2PluginDiagnostic diagnostic;
        diagnostic.severity=RS2_DIAG_WARNING;
        diagnostic.code=RS2_DIAG_AUDIO_LOAD;
        diagnostic.message="WAVE load failed";
        diagnostic.sourcePath=path;
        entry.snapshot.diagnostics.push_back(diagnostic);
    }
    Touch(entry);
}

void RS2PluginDiagnosticsRegistry::RecordDescriptorExhaustion() {
    RS2PluginKey plugin;
    if (!RS2PluginDiagnosticsScope::Current(&plugin)) return;
    EntryMap::iterator it=m_entries.find(plugin);
    if (it==m_entries.end()) return;
    RS2PluginDiagnostic diagnostic;
    diagnostic.severity=RS2_DIAG_ERROR;
    diagnostic.code=RS2_DIAG_TEXTURE_DESCRIPTOR_EXHAUSTED;
    diagnostic.message="SRV descriptor capacity exhausted";
    it->second.snapshot.diagnostics.push_back(diagnostic);
    Touch(it->second);
}

RS2PluginDiagnosticsScope::RS2PluginDiagnosticsScope(const RS2PluginKey &key):
    m_wasActive(s_pluginScope.active), m_previous(s_pluginScope.key) {
    s_pluginScope.active=true;
    s_pluginScope.key=key;
}
RS2PluginDiagnosticsScope::~RS2PluginDiagnosticsScope() {
    s_pluginScope.active=m_wasActive;
    s_pluginScope.key=m_previous;
}
bool RS2PluginDiagnosticsScope::Current(RS2PluginKey *out) {
    if (!s_pluginScope.active || !out) return false;
    *out=s_pluginScope.key;
    return true;
}

RS2PluginDiagnosticsTimer::RS2PluginDiagnosticsTimer(const RS2PluginKey &key,
    RS2PluginTimingKind kind):
    m_key(key), m_kind(kind), m_started(RS2DiagnosticClock()), m_active(true) {}
RS2PluginDiagnosticsTimer::RS2PluginDiagnosticsTimer(RS2PluginTimingKind kind):
    m_kind(kind), m_started(0), m_active(RS2PluginDiagnosticsScope::Current(&m_key)) {
    if (m_active) m_started=RS2DiagnosticClock();
}
RS2PluginDiagnosticsTimer::~RS2PluginDiagnosticsTimer() {
    if (m_active)
        RS2PluginDiagnostics().RecordTime(m_key,m_kind,RS2DiagnosticElapsedUs(m_started));
}

RS2PluginDiagnosticsRegistry &RS2PluginDiagnostics() {
    static RS2PluginDiagnosticsRegistry *registry=new RS2PluginDiagnosticsRegistry;
    return *registry;
}

bool RS2GetPluginDiagnostics(const RS2PluginKey &key,
    RS2PluginDiagnosticsSnapshot *out) {
    return RS2PluginDiagnostics().Get(key,out);
}
void RS2ListPluginDiagnostics(std::vector<RS2PluginSummary> *out) {
    RS2PluginDiagnostics().List(out);
}
bool RS2GetRuntimeDiagnostics(RS2RuntimeDiagnosticsSnapshot *out) {
    return RS2PluginDiagnostics().GetRuntime(out);
}
bool RS2RefreshPluginFilesystemStats(const RS2PluginKey &key) {
    return RS2PluginDiagnostics().RefreshFilesystem(key);
}
bool RS2LoadPluginForDiagnostics(const RS2PluginKey &key) {
    return RS2PluginDiagnostics().LoadExplicit(key);
}

static std::string RS2DumpEscape(const std::string &value) {
    const char *hex="0123456789ABCDEF";
    std::string out;
    for (size_t i=0; i<value.size(); ++i) {
        unsigned char ch=(unsigned char)value[i];
        if (ch>=32 && ch<127 && ch!='\\' && ch!='=') out+=(char)ch;
        else {
            out+='\\'; out+='x'; out+=hex[ch>>4]; out+=hex[ch&15];
        }
    }
    return out;
}

static std::string RS2DumpCount(const RS2DiagnosticCount &value) {
    if (value.overflow) return "overflow";
    if (!value.available) return "unavailable";
    std::ostringstream out;
    out << value.value;
    return out.str();
}

static bool RS2DiagnosticDumpLess(const RS2PluginDiagnostic &a,
    const RS2PluginDiagnostic &b) {
    if (a.severity!=b.severity) return a.severity<b.severity;
    if (a.code!=b.code) return a.code<b.code;
    if (a.sourcePath!=b.sourcePath) return a.sourcePath<b.sourcePath;
    if (a.line!=b.line) return a.line<b.line;
    return a.message<b.message;
}

bool RS2WritePluginDiagnosticsDump(const char *filename,
    bool scanDirectories) {
    if (!filename || !*filename) return false;
    std::vector<RS2PluginSummary> summaries;
    RS2ListPluginDiagnostics(&summaries);
    if (scanDirectories)
        for (size_t i=0; i<summaries.size(); ++i)
            RS2RefreshPluginFilesystemStats(summaries[i].key);
    const std::string folder=std::string(g_BaseDir)+"\\Diagnostics";
    if (!CreateDirectoryA(folder.c_str(),0) && GetLastError()!=ERROR_ALREADY_EXISTS)
        return false;
    const std::string path=folder+"\\"+filename;
    std::ofstream out(path.c_str(),std::ios::binary|std::ios::trunc);
    if (!out) return false;
    RS2RuntimeDiagnosticsSnapshot global;
    RS2GetRuntimeDiagnostics(&global);
    RS2DiagnosticCount directoryFiles, directoryBytes, referencedBytes;
    directoryFiles.Set(0); directoryBytes.Set(0); referencedBytes.Set(0);
    unsigned long long headerFailures=0, loadFailures=0, loaded=0;
    unsigned long long incompleteDirectoryScans=0;
    for (size_t i=0; i<summaries.size(); ++i) {
        RS2PluginDiagnosticsSnapshot item;
        if (!RS2GetPluginDiagnostics(summaries[i].key,&item)) continue;
        if (item.state==RS2_PLUGIN_DIAG_FAILED_HEADER) ++headerFailures;
        if (item.state==RS2_PLUGIN_DIAG_FAILED_LOAD) ++loadFailures;
        if (item.state==RS2_PLUGIN_DIAG_READY) ++loaded;
        if (!(item.completedStages&RS2_DIAG_STAGE_FILESYSTEM))
            ++incompleteDirectoryScans;
        if (item.files.directoryFileCount.available)
            directoryFiles.Add(item.files.directoryFileCount.value);
        if (item.files.directoryBytes.available)
            directoryBytes.Add(item.files.directoryBytes.value);
        if (item.files.referencedDiskBytes.available)
            referencedBytes.Add(item.files.referencedDiskBytes.value);
    }
    out << "RS2PLUGINDIAG|version=1\n"
        << "global.pluginCount=" << RS2DumpCount(global.pluginCount) << "\n"
        << "global.failedPluginCount=" << RS2DumpCount(global.failedPluginCount) << "\n"
        << "global.headerFailures=" << headerFailures << "\n"
        << "global.loadFailures=" << loadFailures << "\n"
        << "global.loadedPlugins=" << loaded << "\n"
        << "global.incompleteDirectoryScans=" << incompleteDirectoryScans << "\n"
        << "global.directoryFiles=" << RS2DumpCount(directoryFiles) << "\n"
        << "global.directoryBytes=" << RS2DumpCount(directoryBytes) << "\n"
        << "global.referencedBytesAttributed=" << RS2DumpCount(referencedBytes) << "\n"
        << "global.meshReferenceCount=" << RS2DumpCount(global.geometry.meshReferenceCount) << "\n"
        << "global.meshCacheHits=" << RS2DumpCount(global.geometry.meshCacheHitCount) << "\n"
        << "global.meshCacheMisses=" << RS2DumpCount(global.geometry.meshCacheMissCount) << "\n"
        << "global.meshLoadFailures=" << RS2DumpCount(global.geometry.meshLoadFailureCount) << "\n"
        << "global.uniqueMeshCount=" << RS2DumpCount(global.geometry.uniqueMeshCount) << "\n"
        << "global.vertices=" << RS2DumpCount(global.geometry.vertexCount) << "\n"
        << "global.triangles=" << RS2DumpCount(global.geometry.triangleCount) << "\n"
        << "global.materialSlots=" << RS2DumpCount(global.geometry.materialSlotsFromMeshes) << "\n"
        << "global.textureReferences=" << RS2DumpCount(global.textures.textureReferenceCount) << "\n"
        << "global.textureCacheHits=" << RS2DumpCount(global.textures.textureCacheHitCount) << "\n"
        << "global.textureCacheMisses=" << RS2DumpCount(global.textures.textureCacheMissCount) << "\n"
        << "global.textureSources=" << RS2DumpCount(global.textures.uniqueTextureSourceCount) << "\n"
        << "global.textureVariants=" << RS2DumpCount(global.textures.uniqueTextureVariantCount) << "\n"
        << "global.textureResident=" << RS2DumpCount(global.textures.residentTextureResourceCount) << "\n"
        << "global.mutableTextureResident=" << RS2DumpCount(global.textures.mutableTextureCount) << "\n"
        << "global.textureFailures=" << RS2DumpCount(global.textures.textureLoadFailureCount) << "\n"
        << "global.textureDecodeFailures=" << RS2DumpCount(global.textures.textureDecodeFailureCount) << "\n"
        << "global.textureUploadFailures=" << RS2DumpCount(global.textures.textureUploadFailureCount) << "\n"
        << "global.textureDescriptorFailures=" << RS2DumpCount(global.textures.textureDescriptorFailureCount) << "\n"
        << "global.textureSourceBytes=" << RS2DumpCount(global.textures.sourceDiskBytes) << "\n"
        << "global.textureDecodedBytes=" << RS2DumpCount(global.textures.decodedLogicalBytes) << "\n"
        << "global.textureGPULogicalBytes=" << RS2DumpCount(global.textures.gpuLogicalBytes) << "\n"
        << "global.textureGPUAllocationBytes=" << RS2DumpCount(global.textures.gpuAllocationBytes) << "\n"
        << "global.audioFiles=" << RS2DumpCount(global.audio.uniqueFileCount) << "\n"
        << "global.audioDiskBytes=" << RS2DumpCount(global.audio.diskBytes) << "\n"
        << "global.srvLive=" << RS2DumpCount(global.srvLive) << "\n"
        << "global.srvPeak=" << RS2DumpCount(global.srvPeak) << "\n"
        << "global.srvCapacity=" << RS2DumpCount(global.srvCapacity) << "\n"
        << "global.srvFailures=" << RS2DumpCount(global.srvAllocationFailures) << "\n"
        << "global.workingSetBytes=" << RS2DumpCount(global.processWorkingSetBytes) << "\n"
        << "global.privateBytes=" << RS2DumpCount(global.processPrivateBytes) << "\n";
    for (size_t i=0; i<summaries.size(); ++i) {
        RS2PluginDiagnosticsSnapshot value;
        if (!RS2GetPluginDiagnostics(summaries[i].key,&value)) continue;
        const std::string prefix=std::string("plugin.")+RS2PluginTypeName(value.identity.key.type)
            +"."+RS2DumpEscape(value.identity.key.id)+".";
        out << prefix << "state=" << (int)value.state << "\n"
            << prefix << "name=" << RS2DumpEscape(value.identity.name) << "\n"
            << prefix << "root=" << RS2DumpEscape(value.identity.rootPath) << "\n"
            << prefix << "stages=" << value.completedStages << "\n"
            << prefix << "directoryFiles=" << RS2DumpCount(value.files.directoryFileCount) << "\n"
            << prefix << "directoryBytes=" << RS2DumpCount(value.files.directoryBytes) << "\n"
            << prefix << "referencedBytes=" << RS2DumpCount(value.files.referencedDiskBytes) << "\n"
            << prefix << "meshes=" << RS2DumpCount(value.geometry.uniqueMeshCount) << "\n"
            << prefix << "meshCacheHits=" << RS2DumpCount(value.geometry.meshCacheHitCount) << "\n"
            << prefix << "meshLoadFailures=" << RS2DumpCount(value.geometry.meshLoadFailureCount) << "\n"
            << prefix << "vertices=" << RS2DumpCount(value.geometry.vertexCount) << "\n"
            << prefix << "triangles=" << RS2DumpCount(value.geometry.triangleCount) << "\n"
            << prefix << "materialSlots=" << RS2DumpCount(value.materials.materialSlotCount) << "\n"
            << prefix << "textureReferences=" << RS2DumpCount(value.textures.textureReferenceCount) << "\n"
            << prefix << "textureCacheHits=" << RS2DumpCount(value.textures.textureCacheHitCount) << "\n"
            << prefix << "textureCacheMisses=" << RS2DumpCount(value.textures.textureCacheMissCount) << "\n"
            << prefix << "textureSources=" << RS2DumpCount(value.textures.uniqueTextureSourceCount) << "\n"
            << prefix << "textureVariants=" << RS2DumpCount(value.textures.uniqueTextureVariantCount) << "\n"
            << prefix << "textureResident=" << RS2DumpCount(value.textures.residentTextureResourceCount) << "\n"
            << prefix << "textureFailures=" << RS2DumpCount(value.textures.textureLoadFailureCount) << "\n"
            << prefix << "textureDecodeFailures=" << RS2DumpCount(value.textures.textureDecodeFailureCount) << "\n"
            << prefix << "textureUploadFailures=" << RS2DumpCount(value.textures.textureUploadFailureCount) << "\n"
            << prefix << "textureDescriptorFailures=" << RS2DumpCount(value.textures.textureDescriptorFailureCount) << "\n"
            << prefix << "gpuAllocationBytes=" << RS2DumpCount(value.textures.gpuAllocationBytes) << "\n"
            << prefix << "referencedResidentBytes=" << RS2DumpCount(value.memory.referencedResidentBytes) << "\n"
            << prefix << "exclusiveResidentBytes=" << RS2DumpCount(value.memory.exclusiveResidentBytes) << "\n"
            << prefix << "sharedResidentBytes=" << RS2DumpCount(value.memory.sharedResidentBytes) << "\n"
            << prefix << "audioFiles=" << RS2DumpCount(value.audio.uniqueFileCount) << "\n"
            << prefix << "audioPcmBytes=" << RS2DumpCount(value.audio.decodedPcmBytes) << "\n"
            << prefix << "audioDurationMs=" << RS2DumpCount(value.audio.durationMs) << "\n";
        std::vector<RS2PluginDiagnostic> diagnostics=value.diagnostics;
        std::sort(diagnostics.begin(),diagnostics.end(),RS2DiagnosticDumpLess);
        for (size_t j=0; j<diagnostics.size(); ++j)
            out << prefix << "diagnostic." << j << "=" << (int)diagnostics[j].severity
                << "," << (int)diagnostics[j].code << ","
                << RS2DumpEscape(diagnostics[j].sourcePath) << ","
                << diagnostics[j].line << "," << RS2DumpEscape(diagnostics[j].message)
                << "\n";
    }
    out.flush();
    return out.good();
}

void RS2ReconcileUnrepresentablePluginDirectories() {
    // The original ANSI loader silently skips names it cannot chdir into.
    // Keep them visible as failed discovery without changing its load path.
    for (int type=RS2_PLUGIN_ENV; type<RS2_PLUGIN_UNKNOWN; ++type) {
        const char *family=RS2PluginTypeName((RS2PluginType)type);
        const int baseLength=MultiByteToWideChar(CP_ACP,0,g_BaseDir,-1,0,0);
        if (baseLength<=0) continue;
        std::wstring root((size_t)baseLength,L'\0');
        if (!MultiByteToWideChar(CP_ACP,0,g_BaseDir,-1,&root[0],baseLength))
            continue;
        root.resize((size_t)baseLength-1);
        root+=L'\\';
        for (const char *p=family; *p; ++p) root+=(wchar_t)*p;
        const std::wstring pattern=root+L"\\*";
        WIN32_FIND_DATAW data;
        HANDLE search=FindFirstFileW(pattern.c_str(),&data);
        if (search==INVALID_HANDLE_VALUE) continue;
        do {
            if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
                !wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))
                continue;
            BOOL substituted=FALSE;
            const int length=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,
                data.cFileName,-1,0,0,0,&substituted);
            if (length>0 && !substituted) continue;
            std::ostringstream id;
            id << "@UTF16-";
            const char *hex="0123456789ABCDEF";
            for (const wchar_t *p=data.cFileName; *p; ++p) {
                const unsigned int ch=(unsigned int)*p;
                id << hex[(ch>>12)&15] << hex[(ch>>8)&15]
                   << hex[(ch>>4)&15] << hex[ch&15];
            }
            const RS2PluginKey key((RS2PluginType)type,id.str());
            RS2PluginDiagnosticsSnapshot existing;
            if (RS2PluginDiagnostics().Get(key,&existing)) continue;
            const std::string escaped=std::string(g_BaseDir)+"\\"+family+"\\"+id.str();
            RS2PluginDiagnostics().RegisterDiscovered(key,escaped);
            RS2PluginDiagnostics().SetWideFilesystemRoot(key,
                root+L"\\"+data.cFileName);
            RS2PluginDiagnostics().MarkFailure(key,RS2_PLUGIN_DIAG_FAILED_DISCOVERY,
                RS2_DIAG_RESOURCE_PATH,
                "Directory name is not representable by the legacy ANSI loader",
                escaped);
        } while (FindNextFileW(search,&data));
        FindClose(search);
    }
}
void RS2NoteMeshRequest(const char *path, unsigned int colourKey, int mip,
    const void *resource, bool cacheHit, unsigned long long vertices,
    unsigned long long triangles, unsigned long long indices,
    unsigned long long materials, unsigned long long subsets,
    unsigned long long cpuBytes) {
    RS2PluginDiagnostics().RecordMesh(path ? path : "",colourKey,mip,resource,
        cacheHit,vertices,triangles,indices,materials,subsets,cpuBytes);
}
void RS2NoteMeshDestroyed(const void *resource) {
    RS2PluginDiagnostics().ForgetMesh(resource);
}
void RS2NoteTextureRequest(const char *path, unsigned int colourKey, int mip,
    const void *resource, bool cacheHit, unsigned int width,
    unsigned int height, bool fileSource,
    const RS2TextureDiagnosticsInfo *info) {
    RS2PluginDiagnostics().RecordTexture(path ? path : "",colourKey,mip,resource,
        cacheHit,width,height,fileSource,info);
}
void RS2NoteTextureDestroyed(const void *resource) {
    RS2PluginDiagnostics().ForgetTexture(resource);
}
void RS2NoteAudioLoad(const char *path, bool success,
    unsigned long long pcmBytes, unsigned int bytesPerSecond) {
    RS2PluginDiagnostics().RecordAudio(path ? path : "",success,pcmBytes,
        bytesPerSecond);
}
void RS2NoteDescriptorExhaustion() {
    RS2PluginDiagnostics().RecordDescriptorExhaustion();
}

bool RS2RunPluginDiagnosticsRegistrySmoke() {
    RS2PluginDiagnosticsRegistry registry;
    RS2PluginKey good(RS2_PLUGIN_RAIL,"Default_JR_Narrow");
    RS2PluginKey bad(RS2_PLUGIN_TIE,"Broken");
    registry.RegisterDiscovered(good,"Rail\\Default_JR_Narrow");
    registry.RegisterDiscovered(bad,"Tie\\Broken");
    registry.SetDefinition(good,"Rail\\Default_JR_Narrow\\Rail2.txt",false);
    registry.MarkHeaderReady(good,(CPlugin *)0x1,"Default rail","RS2",2.0f);
    RS2PluginDiagnosticsSnapshot first;
    if (!registry.Get(good,&first) || first.identity.name!="Default rail" ||
        first.state!=RS2_PLUGIN_DIAG_HEADER_READY ||
        !(first.completedStages&RS2_DIAG_STAGE_HEADER) ||
        !registry.HasRuntime(good)) return false;
    registry.MarkLoading(good);
    registry.MarkReady(good);
    registry.MarkFailure(bad,RS2_PLUGIN_DIAG_FAILED_HEADER,
        RS2_DIAG_PLUGIN_HEADER_PARSE,"invalid header","Tie\\Broken\\Tie2.txt");
    RS2PluginDiagnosticsSnapshot second;
    if (!registry.Get(good,&second) || second.state!=RS2_PLUGIN_DIAG_READY ||
        second.generation<=first.generation || first.state!=RS2_PLUGIN_DIAG_HEADER_READY)
        return false;
    RS2PluginDiagnosticsSnapshot failed;
    if (!registry.Get(bad,&failed) || failed.diagnostics.size()!=1 ||
        failed.state!=RS2_PLUGIN_DIAG_FAILED_HEADER) return false;
    registry.Disassociate((CPlugin *)0x1);
    if (registry.HasRuntime(good)) return false;
    RS2DiagnosticCount large;
    large.Add(1ULL<<32);
    large.Add(17);
    if (!large.available || large.value!=(1ULL<<32)+17) return false;
    RS2RuntimeDiagnosticsSnapshot global;
    registry.GetRuntime(&global);
    return global.pluginCount.value==2 && global.failedPluginCount.value==1;
}

bool RS2RunPluginDiagnosticsSmoke() {
    RS2PluginDiagnosticsRegistry registry;
    const RS2PluginKey first(RS2_PLUGIN_RAIL,"Fixture_A");
    const RS2PluginKey second(RS2_PLUGIN_TIE,"Fixture_B");
    const RS2PluginKey broken(RS2_PLUGIN_TRAIN,"Fixture_Broken");
    registry.RegisterDiscovered(first,"missing-fixture-a");
    registry.RegisterDiscovered(second,"missing-fixture-b");
    registry.RegisterDiscovered(broken,"missing-fixture-broken");
    registry.MarkHeaderReady(first,0,"A","",2.0f);
    registry.MarkHeaderReady(second,0,"B","",2.0f);
    registry.MarkFailure(broken,RS2_PLUGIN_DIAG_FAILED_HEADER,
        RS2_DIAG_PLUGIN_HEADER_PARSE,"fixture header error","broken.txt");
    RS2TextureDiagnosticsInfo texture;
    texture.format=RS2_TEXTURE_DIAG_RGBA8;
    texture.mipCount=2;
    texture.decodedLogicalBytes=80;
    texture.gpuLogicalBytes=80;
    texture.gpuAllocationBytes=65536;
    texture.allocationAvailable=true;
    {
        RS2PluginDiagnosticsScope scope(first);
        registry.RecordMesh("shared.x",0,0,(void *)0x100,false,12,4,12,2,2,384);
        registry.RecordTexture("shared.png",0,0,(void *)0x200,false,4,4,true,&texture);
        registry.RecordTexture("shared.png",0,0,(void *)0x200,true,4,4,true,&texture);
        registry.RecordAudio("fixture.wav",true,48000,48000);
    }
    RS2PluginDiagnosticsSnapshot copied;
    if (!registry.Get(first,&copied)) return false;
    {
        RS2PluginDiagnosticsScope scope(second);
        registry.RecordTexture("shared.png",0,0,(void *)0x200,true,4,4,true,&texture);
        registry.RecordTexture("unique.png",0,0,(void *)0x300,false,4,4,true,&texture);
    }
    RS2PluginDiagnosticsSnapshot a,b,failed;
    if (!registry.Get(first,&a) || !registry.Get(second,&b) ||
        !registry.Get(broken,&failed)) return false;
    RS2RuntimeDiagnosticsSnapshot global;
    registry.GetRuntime(&global);
    RS2DiagnosticCount large;
    large.Add(1ULL<<32); large.Add(7);
    return a.geometry.vertexCount.available && a.geometry.vertexCount.value==12 &&
        a.geometry.triangleCount.value==4 && a.materials.materialSlotCount.value==2 &&
        a.geometry.meshCacheMissCount.value==1 &&
        a.geometry.meshLoadSuccessCount.value==1 &&
        a.textures.textureReferenceCount.value==2 &&
        a.textures.textureCacheHitCount.value==1 &&
        a.textures.textureCacheMissCount.value==1 &&
        a.textures.uniqueTextureSourceCount.value==1 &&
        a.textures.uniqueTextureVariantCount.value==1 &&
        a.textures.sharedTextureCount.value==1 &&
        a.memory.referencedResidentBytes.value==65920 &&
        a.memory.exclusiveResidentBytes.value==384 &&
        a.memory.sharedResidentBytes.value==65536 &&
        a.audio.decodedPcmBytes.value==48000 && a.audio.durationMs.value==1000 &&
        b.textures.textureReferenceCount.value==2 &&
        b.textures.uniqueTextureSourceCount.value==2 &&
        b.textures.sharedTextureCount.value==1 && b.textures.exclusiveTextureCount.value==1 &&
        b.memory.exclusiveResidentBytes.value==65536 &&
        b.memory.sharedResidentBytes.value==65536 &&
        global.textures.textureReferenceCount.value==4 &&
        global.textures.textureCacheHitCount.value==2 &&
        global.textures.uniqueTextureSourceCount.value==2 &&
        global.textures.uniqueTextureVariantCount.value==2 &&
        global.textures.gpuAllocationBytes.value==131072 &&
        failed.diagnostics.size()==1 &&
        failed.diagnostics[0].code==RS2_DIAG_PLUGIN_HEADER_PARSE &&
        copied.generation<a.generation && copied.textures.sharedTextureCount.value==0 &&
        large.value==(1ULL<<32)+7 && large.available;
}
