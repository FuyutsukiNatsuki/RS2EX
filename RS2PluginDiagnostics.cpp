// RS2EX v0.3.1: plugin diagnostics registry. Querying never loads a plugin.
#include "stdafx.h"
#include "RS2PluginDiagnostics.h"
#include <climits>
#include <cstring>

static const char *const kRS2PluginTypes[] = {
    "Env", "Girder", "Line", "Pier", "Pole", "Rail", "Skin", "Station",
    "Struct", "Surface", "Tie", "Train"
};

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
    entry.snapshot=RS2PluginDiagnosticsSnapshot();
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

bool RS2PluginDiagnosticsRegistry::Get(const RS2PluginKey &key,
    RS2PluginDiagnosticsSnapshot *out) const {
    if (!out) return false;
    EntryMap::const_iterator it=m_entries.find(key);
    if (it==m_entries.end()) return false;
    *out=it->second.snapshot;
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
    for (EntryMap::const_iterator it=m_entries.begin(); it!=m_entries.end(); ++it) {
        RS2PluginDiagnosticState state=it->second.snapshot.state;
        if (state==RS2_PLUGIN_DIAG_FAILED_DISCOVERY ||
            state==RS2_PLUGIN_DIAG_FAILED_HEADER ||
            state==RS2_PLUGIN_DIAG_FAILED_LOAD)
            out->failedPluginCount.Add(1);
    }
    out->generation=m_generation;
    return true;
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
