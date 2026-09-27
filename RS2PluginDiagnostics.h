// RS2EX v0.3.1: stable, copy-out plugin diagnostics catalog.
#ifndef RS2_PLUGIN_DIAGNOSTICS_H_INCLUDED
#define RS2_PLUGIN_DIAGNOSTICS_H_INCLUDED

#include <map>
#include <set>
#include <string>
#include <vector>

class CPlugin;
struct RS2TextureDiagnosticsInfo;

enum RS2PluginType {
    RS2_PLUGIN_ENV, RS2_PLUGIN_GIRDER, RS2_PLUGIN_LINE, RS2_PLUGIN_PIER,
    RS2_PLUGIN_POLE, RS2_PLUGIN_RAIL, RS2_PLUGIN_SKIN, RS2_PLUGIN_STATION,
    RS2_PLUGIN_STRUCT, RS2_PLUGIN_SURFACE, RS2_PLUGIN_TIE, RS2_PLUGIN_TRAIN,
    RS2_PLUGIN_UNKNOWN
};

RS2PluginType RS2PluginTypeFromName(const char *name);
const char *RS2PluginTypeName(RS2PluginType type);

struct RS2PluginKey {
    RS2PluginType type;
    std::string id;
    RS2PluginKey(): type(RS2_PLUGIN_UNKNOWN) {}
    RS2PluginKey(RS2PluginType t, const std::string &i): type(t), id(i) {}
};

struct RS2PluginKeyLess {
    bool operator()(const RS2PluginKey &a, const RS2PluginKey &b) const;
};
struct RS2DiagnosticTextLess {
    bool operator()(const std::string &a, const std::string &b) const;
};

enum RS2PluginDiagnosticState {
    RS2_PLUGIN_DIAG_DISCOVERED, RS2_PLUGIN_DIAG_HEADER_READY,
    RS2_PLUGIN_DIAG_LOADING, RS2_PLUGIN_DIAG_READY,
    RS2_PLUGIN_DIAG_FAILED_DISCOVERY, RS2_PLUGIN_DIAG_FAILED_HEADER,
    RS2_PLUGIN_DIAG_FAILED_LOAD,
    RS2_PLUGIN_DIAG_DIRTY, RS2_PLUGIN_DIAG_RELOADING, RS2_PLUGIN_DIAG_DISABLED
};

enum RS2PluginDiagnosticStage {
    RS2_DIAG_STAGE_DISCOVERY = 1 << 0, RS2_DIAG_STAGE_HEADER = 1 << 1,
    RS2_DIAG_STAGE_FILESYSTEM = 1 << 2, RS2_DIAG_STAGE_CONTENT = 1 << 3,
    RS2_DIAG_STAGE_ASSETS = 1 << 4, RS2_DIAG_STAGE_GPU = 1 << 5,
    RS2_DIAG_STAGE_AUDIO = 1 << 6
};

enum RS2DiagnosticSeverity { RS2_DIAG_INFO, RS2_DIAG_WARNING, RS2_DIAG_ERROR };
enum RS2DiagnosticCode {
    RS2_DIAG_PLUGIN_HEADER_PARSE, RS2_DIAG_PLUGIN_BODY_PARSE,
    RS2_DIAG_PLUGIN_VERSION, RS2_DIAG_PLUGIN_TYPE, RS2_DIAG_FILE_MISSING,
    RS2_DIAG_MESH_IMPORT, RS2_DIAG_TEXTURE_DECODE,
    RS2_DIAG_TEXTURE_UPLOAD, RS2_DIAG_TEXTURE_DESCRIPTOR,
    RS2_DIAG_TEXTURE_DESCRIPTOR_EXHAUSTED,
    RS2_DIAG_AUDIO_LOAD, RS2_DIAG_RESOURCE_PATH, RS2_DIAG_INTERNAL
};

enum RS2PluginTimingKind {
    RS2_DIAG_TIME_HEADER, RS2_DIAG_TIME_FULL_PARSE, RS2_DIAG_TIME_MESH_IMPORT,
    RS2_DIAG_TIME_TEXTURE_DECODE, RS2_DIAG_TIME_TEXTURE_UPLOAD,
    RS2_DIAG_TIME_AUDIO_LOAD
};

struct RS2PluginDiagnostic {
    RS2DiagnosticSeverity severity;
    RS2DiagnosticCode code;
    std::string message;
    std::string sourcePath;
    int line;
    int column;
    RS2PluginDiagnostic(): severity(RS2_DIAG_INFO), code(RS2_DIAG_INTERNAL), line(-1), column(-1) {}
};

// An unmeasured value differs from a measured zero. Overflow invalidates a
// value rather than wrapping it into a plausible-looking smaller number.
struct RS2DiagnosticCount {
    unsigned long long value;
    bool available;
    bool overflow;
    RS2DiagnosticCount(): value(0), available(false), overflow(false) {}
    void Set(unsigned long long n) { value=n; available=true; overflow=false; }
    void Add(unsigned long long n);
};

struct RS2PluginFileStats {
    RS2DiagnosticCount directoryFileCount, directoryBytes;
    RS2DiagnosticCount referencedFileCount, referencedUniqueFileCount;
    RS2DiagnosticCount referencedDiskBytes, missingFileCount;
};
struct RS2PluginGeometryStats {
    RS2DiagnosticCount meshReferenceCount, meshCacheHitCount, meshCacheMissCount;
    RS2DiagnosticCount meshLoadSuccessCount, meshLoadFailureCount;
    RS2DiagnosticCount uniqueMeshCount, vertexCount;
    RS2DiagnosticCount triangleCount, indexCount, materialSlotsFromMeshes;
    RS2DiagnosticCount cpuGeometryBytes, gpuGeometryLogicalBytes;
    RS2DiagnosticCount gpuGeometryAllocationBytes;
};
struct RS2PluginMaterialStats {
    RS2DiagnosticCount materialSlotCount, meshSubsetCount;
};
struct RS2PluginTextureStats {
    RS2DiagnosticCount textureReferenceCount, uniqueTextureSourceCount;
    RS2DiagnosticCount textureCacheHitCount, textureCacheMissCount;
    RS2DiagnosticCount uniqueTextureVariantCount, residentTextureResourceCount;
    RS2DiagnosticCount textureLoadSuccessCount, textureLoadFailureCount;
    RS2DiagnosticCount textureDecodeFailureCount, textureUploadFailureCount;
    RS2DiagnosticCount textureDescriptorFailureCount;
    RS2DiagnosticCount sharedTextureCount, exclusiveTextureCount;
    RS2DiagnosticCount pngCount, bmpCount, ddsCount, otherCount;
    RS2DiagnosticCount maxWidth, maxHeight, maxTexelCount, mipLevelTotal;
    RS2DiagnosticCount mipmappedTextureCount, bc1Count, bc3Count, rgba8Count;
    RS2DiagnosticCount mutableTextureCount, sourceDiskBytes;
    RS2DiagnosticCount decodedLogicalBytes, gpuLogicalBytes, gpuAllocationBytes;
};
struct RS2PluginAudioStats {
    RS2DiagnosticCount referenceCount, uniqueFileCount, loadSuccessCount;
    RS2DiagnosticCount loadFailureCount, diskBytes, decodedPcmBytes, durationMs;
};
struct RS2PluginMemoryStats {
    RS2DiagnosticCount referencedResidentBytes, exclusiveResidentBytes;
    RS2DiagnosticCount sharedResidentBytes;
};
struct RS2PluginTimingStats {
    RS2DiagnosticCount headerParseTimeUs, fullPluginParseTimeUs;
    RS2DiagnosticCount meshImportTimeUs, textureDecodeTimeUs;
    RS2DiagnosticCount textureUploadTimeUs, audioLoadTimeUs;
    RS2DiagnosticCount totalObservedLoadTimeUs;
    RS2DiagnosticCount headerSamples, fullParseSamples, meshImportSamples;
    RS2DiagnosticCount textureDecodeSamples, textureUploadSamples, audioLoadSamples;
};

struct RS2PluginIdentity {
    RS2PluginKey key;
    std::string name, author, rootPath, definitionPath;
    float version;
    bool oldForm;
    unsigned long long discoveryGeneration;
    RS2PluginIdentity(): version(0), oldForm(false), discoveryGeneration(0) {}
};
struct RS2PluginDiagnosticsSnapshot {
    RS2PluginIdentity identity;
    RS2PluginDiagnosticState state;
    unsigned int completedStages;
    RS2PluginFileStats files;
    RS2PluginGeometryStats geometry;
    RS2PluginMaterialStats materials;
    RS2PluginTextureStats textures;
    RS2PluginAudioStats audio;
    RS2PluginMemoryStats memory;
    RS2PluginTimingStats timing;
    std::vector<RS2PluginDiagnostic> diagnostics;
    unsigned long long generation;
    RS2PluginDiagnosticsSnapshot(): state(RS2_PLUGIN_DIAG_DISCOVERED), completedStages(0), generation(0) {}
};
struct RS2PluginSummary {
    RS2PluginKey key;
    std::string name;
    RS2PluginDiagnosticState state;
    unsigned int completedStages;
    unsigned long long generation;
};

struct RS2RuntimeDiagnosticsSnapshot {
    RS2DiagnosticCount pluginCount, failedPluginCount;
    RS2PluginGeometryStats geometry;
    RS2PluginTextureStats textures;
    RS2PluginAudioStats audio;
    RS2DiagnosticCount srvCapacity, srvLive, srvPeak;
    RS2DiagnosticCount srvAllocations, srvReleases, srvAllocationFailures;
    RS2DiagnosticCount srvStaleReleaseFailures;
    RS2DiagnosticCount processWorkingSetBytes, processPrivateBytes;
    unsigned long long generation;
    RS2RuntimeDiagnosticsSnapshot(): generation(0) {}
};

class RS2PluginDiagnosticsRegistry {
    struct Entry {
        RS2PluginDiagnosticsSnapshot snapshot;
        CPlugin *runtime; // internal association only; never appears in snapshots
        std::wstring wideFilesystemRoot;
        std::set<std::string, RS2DiagnosticTextLess> files;
        std::set<std::string, RS2DiagnosticTextLess> meshVariants;
        std::set<std::string, RS2DiagnosticTextLess> textureVariants;
        std::set<std::string, RS2DiagnosticTextLess> textureSources;
        std::set<std::string, RS2DiagnosticTextLess> audioSources;
        bool textureLogicalComplete, textureAllocationComplete;
        Entry(): runtime(0), textureLogicalComplete(true),
            textureAllocationComplete(true) {}
    };
    struct MeshResource {
        const void *pointer;
        bool resident;
        unsigned long long vertices, triangles, indices, materials, subsets, cpuBytes;
        std::set<RS2PluginKey, RS2PluginKeyLess> referrers;
        MeshResource(): pointer(0), resident(false), vertices(0), triangles(0),
            indices(0), materials(0), subsets(0), cpuBytes(0) {}
    };
    struct TextureResource {
        const void *pointer;
        bool resident;
        std::string source;
        unsigned int width, height;
        unsigned long long diskBytes;
        int format;
        unsigned int mips;
        unsigned long long decodedBytes, gpuLogicalBytes, gpuAllocationBytes;
        bool logicalAvailable, allocationAvailable;
        std::set<RS2PluginKey, RS2PluginKeyLess> referrers;
        TextureResource(): pointer(0), resident(false), width(0), height(0),
            diskBytes(0), format(0), mips(0), decodedBytes(0),
            gpuLogicalBytes(0), gpuAllocationBytes(0), logicalAvailable(false),
            allocationAvailable(false) {}
    };
    struct SourceFileSize {
        bool exists;
        unsigned long long bytes;
        SourceFileSize(): exists(false), bytes(0) {}
    };
    typedef std::map<RS2PluginKey, Entry, RS2PluginKeyLess> EntryMap;
    EntryMap m_entries;
    std::map<CPlugin *, RS2PluginKey> m_byRuntime;
    std::map<std::string, MeshResource, RS2DiagnosticTextLess> m_meshes;
    std::map<std::string, TextureResource, RS2DiagnosticTextLess> m_textures;
    RS2PluginTextureStats m_textureGlobal;
    RS2PluginGeometryStats m_meshGlobal;
    std::set<std::string, RS2DiagnosticTextLess> m_globalTextureSources;
    RS2PluginAudioStats m_audioGlobal;
    std::set<std::string, RS2DiagnosticTextLess> m_globalAudioSources;
    std::map<std::string, SourceFileSize, RS2DiagnosticTextLess> m_sourceSizes;
    unsigned long long m_generation;
    void Touch(Entry &entry);
    void RecordFileReference(Entry &, const std::string &);
    bool LookupSourceSize(const std::string &, unsigned long long *);
public:
    RS2PluginDiagnosticsRegistry(): m_generation(0) {}
    void RegisterDiscovered(const RS2PluginKey &, const std::string &root);
    void SetDefinition(const RS2PluginKey &, const std::string &path, bool oldForm);
    void SetWideFilesystemRoot(const RS2PluginKey &, const std::wstring &);
    void MarkHeaderReady(const RS2PluginKey &, CPlugin *, const std::string &name,
        const std::string &author, float version);
    void MarkFailure(const RS2PluginKey &, RS2PluginDiagnosticState,
        RS2DiagnosticCode, const std::string &message, const std::string &path);
    void MarkLoading(const RS2PluginKey &);
    void MarkReady(const RS2PluginKey &);
    void Disassociate(CPlugin *);
    bool HasRuntime(const RS2PluginKey &) const;
    bool LoadExplicit(const RS2PluginKey &);
    bool Get(const RS2PluginKey &, RS2PluginDiagnosticsSnapshot *out) const;
    void List(std::vector<RS2PluginSummary> *out) const;
    bool GetRuntime(RS2RuntimeDiagnosticsSnapshot *out) const;
    bool RefreshFilesystem(const RS2PluginKey &);
    void RecordTime(const RS2PluginKey &, RS2PluginTimingKind, unsigned long long us);
    void RecordMesh(const std::string &path, unsigned int colourKey, int mip,
        const void *resource, bool cacheHit, unsigned long long vertices,
        unsigned long long triangles, unsigned long long indices,
        unsigned long long materials, unsigned long long subsets,
        unsigned long long cpuBytes);
    void ForgetMesh(const void *resource);
    void RecordTexture(const std::string &path, unsigned int colourKey, int mip,
        const void *resource, bool cacheHit, unsigned int width, unsigned int height,
        bool fileSource, const RS2TextureDiagnosticsInfo *info);
    void ForgetTexture(const void *resource);
    void RecordAudio(const std::string &path, bool success,
        unsigned long long pcmBytes, unsigned int bytesPerSecond);
    void RecordDescriptorExhaustion();
    unsigned long long Generation() const { return m_generation; }
};

// Thread-local attribution only. It neither owns nor pins a plugin or resource.
class RS2PluginDiagnosticsScope {
    bool m_wasActive;
    RS2PluginKey m_previous;
public:
    explicit RS2PluginDiagnosticsScope(const RS2PluginKey &);
    ~RS2PluginDiagnosticsScope();
    static bool Current(RS2PluginKey *out);
};

class RS2PluginDiagnosticsTimer {
    RS2PluginKey m_key;
    RS2PluginTimingKind m_kind;
    unsigned long long m_started;
    bool m_active;
public:
    RS2PluginDiagnosticsTimer(const RS2PluginKey &, RS2PluginTimingKind);
    explicit RS2PluginDiagnosticsTimer(RS2PluginTimingKind);
    ~RS2PluginDiagnosticsTimer();
};

RS2PluginDiagnosticsRegistry &RS2PluginDiagnostics();
bool RS2GetPluginDiagnostics(const RS2PluginKey &, RS2PluginDiagnosticsSnapshot *);
void RS2ListPluginDiagnostics(std::vector<RS2PluginSummary> *);
bool RS2GetRuntimeDiagnostics(RS2RuntimeDiagnosticsSnapshot *);
bool RS2RefreshPluginFilesystemStats(const RS2PluginKey &);
bool RS2LoadPluginForDiagnostics(const RS2PluginKey &);
bool RS2WritePluginDiagnosticsDump(const char *filename, bool scanDirectories);
void RS2ReconcileUnrepresentablePluginDirectories();
bool RS2RunPluginDiagnosticsSmoke();
void RS2NoteMeshRequest(const char *, unsigned int colourKey, int mip,
    const void *resource, bool cacheHit, unsigned long long vertices,
    unsigned long long triangles, unsigned long long indices,
    unsigned long long materials, unsigned long long subsets,
    unsigned long long cpuBytes);
void RS2NoteMeshDestroyed(const void *resource);
void RS2NoteTextureRequest(const char *, unsigned int colourKey, int mip,
    const void *resource, bool cacheHit, unsigned int width, unsigned int height,
    bool fileSource, const RS2TextureDiagnosticsInfo *info);
void RS2NoteTextureDestroyed(const void *resource);
void RS2NoteAudioLoad(const char *, bool success, unsigned long long pcmBytes,
    unsigned int bytesPerSecond);
void RS2NoteDescriptorExhaustion();
bool RS2RunPluginDiagnosticsRegistrySmoke();

#endif
