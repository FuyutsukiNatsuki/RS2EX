#include "stdafx.h"
#include "RS2PluginDiagnosticsUI.h"
#include "RS2PluginDiagnostics.h"
#include "CWindowCtrl.h"
#include "CListView.h"
#include "CMultiStatic.h"
#include "CPushButton.h"
#include <sstream>

namespace {
const char *StateName(RS2PluginDiagnosticState state) {
    switch (state) {
    case RS2_PLUGIN_DIAG_DISCOVERED: return "discovered";
    case RS2_PLUGIN_DIAG_HEADER_READY: return "header-ready";
    case RS2_PLUGIN_DIAG_LOADING: return "loading";
    case RS2_PLUGIN_DIAG_READY: return "ready";
    case RS2_PLUGIN_DIAG_FAILED_DISCOVERY: return "failed-discovery";
    case RS2_PLUGIN_DIAG_FAILED_HEADER: return "failed-header";
    case RS2_PLUGIN_DIAG_FAILED_LOAD: return "failed-load";
    case RS2_PLUGIN_DIAG_DIRTY: return "dirty";
    case RS2_PLUGIN_DIAG_RELOADING: return "reloading";
    case RS2_PLUGIN_DIAG_DISABLED: return "disabled";
    }
    return "unknown";
}

std::string Count(const RS2DiagnosticCount &value) {
    if (value.overflow) return "overflow";
    if (!value.available) return "unavailable";
    std::ostringstream output;
    output << value.value;
    return output.str();
}

class Inspector {
    bool m_initialized;
    bool m_renderLogged;
    bool m_global;
    int m_selected;
    unsigned int m_frames;
    CInterface m_root;
    CWindowCtrl m_window;
    CListView m_list;
    CMultiStatic m_properties;
    CPushButton m_refresh, m_scan, m_load, m_globalButton;
    std::vector<RS2PluginSummary> m_summaries;
    unsigned long long m_lastGeneration;

    void SetProperties(const std::string &value) {
        m_properties.SetText((char *)value.c_str());
        m_properties.SetScroll();
    }
    void Populate() {
        RS2ListPluginDiagnostics(&m_summaries);
        m_list.DeleteAllItems();
        for (int i=(int)m_summaries.size()-1; i>=0; --i) {
            const RS2PluginSummary &item=m_summaries[(size_t)i];
            CListElement *row=m_list.InsertItem(0,(char *)RS2PluginTypeName(item.key.type));
            row->SetString(1,(char *)item.key.id.c_str());
            row->SetString(2,(char *)StateName(item.state));
        }
        if (m_selected>=(int)m_summaries.size()) m_selected=-1;
        if (m_selected>=0) m_list.SetSelectionMark(m_selected,0);
    }
    void ShowGlobal() {
        RS2RuntimeDiagnosticsSnapshot value;
        RS2GetRuntimeDiagnostics(&value);
        std::ostringstream out;
        out << "GLOBAL  generation " << value.generation << "\n"
            << "Plugins " << Count(value.pluginCount) << ", failed " << Count(value.failedPluginCount) << "\n"
            << "Texture references " << Count(value.textures.textureReferenceCount)
            << ", unique sources " << Count(value.textures.uniqueTextureSourceCount)
            << ", variants " << Count(value.textures.uniqueTextureVariantCount)
            << ", resident " << Count(value.textures.residentTextureResourceCount) << "\n"
            << "Texture failures " << Count(value.textures.textureLoadFailureCount) << "\n"
            << "Triangles " << Count(value.geometry.triangleCount)
            << ", material slots " << Count(value.geometry.materialSlotsFromMeshes) << "\n"
            << "Texture GPU logical bytes " << Count(value.textures.gpuLogicalBytes)
            << ", allocation bytes " << Count(value.textures.gpuAllocationBytes) << "\n"
            << "SRV live/peak/capacity " << Count(value.srvLive) << "/"
            << Count(value.srvPeak) << "/" << Count(value.srvCapacity)
            << ", failures " << Count(value.srvAllocationFailures) << "\n"
            << "Process working/private bytes " << Count(value.processWorkingSetBytes)
            << "/" << Count(value.processPrivateBytes) << "\n";
        SetProperties(out.str());
    }
    void ShowSelected() {
        if (m_selected<0 || m_selected>=(int)m_summaries.size()) {
            SetProperties("Select a plugin. Selection never loads resources.\n");
            return;
        }
        RS2PluginDiagnosticsSnapshot value;
        if (!RS2GetPluginDiagnostics(m_summaries[(size_t)m_selected].key,&value)) return;
        std::ostringstream out;
        out << RS2PluginTypeName(value.identity.key.type) << "/" << value.identity.key.id
            << "  " << StateName(value.state) << "\n"
            << "Name " << value.identity.name << "\n"
            << "Root " << value.identity.rootPath << "\n"
            << "Definition " << value.identity.definitionPath << "\n"
            << "Stages " << value.completedStages << ", generation " << value.generation << "\n"
            << "Directory files/bytes " << Count(value.files.directoryFileCount) << "/"
            << Count(value.files.directoryBytes) << "\n"
            << "Referenced files/bytes/missing " << Count(value.files.referencedUniqueFileCount)
            << "/" << Count(value.files.referencedDiskBytes) << "/"
            << Count(value.files.missingFileCount) << "\n"
            << "Meshes/vertices/triangles/material slots "
            << Count(value.geometry.uniqueMeshCount) << "/" << Count(value.geometry.vertexCount)
            << "/" << Count(value.geometry.triangleCount) << "/"
            << Count(value.materials.materialSlotCount) << "\n"
            << "Texture refs/sources/variants/resident "
            << Count(value.textures.textureReferenceCount) << "/"
            << Count(value.textures.uniqueTextureSourceCount) << "/"
            << Count(value.textures.uniqueTextureVariantCount) << "/"
            << Count(value.textures.residentTextureResourceCount) << "\n"
            << "Texture logical/allocation bytes " << Count(value.textures.gpuLogicalBytes)
            << "/" << Count(value.textures.gpuAllocationBytes) << "\n"
            << "Observed resident ref/exclusive/shared bytes "
            << Count(value.memory.referencedResidentBytes) << "/"
            << Count(value.memory.exclusiveResidentBytes) << "/"
            << Count(value.memory.sharedResidentBytes) << "\n"
            << "Audio files/PCM bytes/duration ms " << Count(value.audio.uniqueFileCount)
            << "/" << Count(value.audio.decodedPcmBytes) << "/"
            << Count(value.audio.durationMs) << "\n"
            << "Header/full load us " << Count(value.timing.headerParseTimeUs)
            << "/" << Count(value.timing.fullPluginParseTimeUs) << "\n";
        unsigned int warnings=0, errors=0;
        for (size_t i=0; i<value.diagnostics.size(); ++i) {
            if (value.diagnostics[i].severity==RS2_DIAG_WARNING) ++warnings;
            if (value.diagnostics[i].severity==RS2_DIAG_ERROR) ++errors;
        }
        out << "Diagnostics: " << errors << " error(s), " << warnings
            << " warning(s)\n";
        for (size_t i=0; i<value.diagnostics.size(); ++i) {
            const RS2PluginDiagnostic &diagnostic=value.diagnostics[i];
            out << (diagnostic.severity==RS2_DIAG_ERROR ? "ERROR" :
                diagnostic.severity==RS2_DIAG_WARNING ? "WARN" : "INFO")
                << " code=" << diagnostic.code << " " << diagnostic.sourcePath;
            if (diagnostic.line>=0) out << ":" << diagnostic.line;
            out << " " << diagnostic.message << "\n";
        }
        SetProperties(out.str());
    }
public:
    Inspector(): m_initialized(false), m_renderLogged(false), m_global(false),
        m_selected(-1), m_frames(0), m_lastGeneration(0) {}
    void Init() {
        if (m_initialized) return;
        m_initialized=true;
        const int width=min(g_DispWidth-24,900);
        const int height=min(g_DispHeight-24,600);
        m_root.Init(0,0,g_DispWidth,g_DispHeight,(char *)"",0);
        m_window.Init(12,12,width,height,(char *)"Plugin Diagnostics (F10)",&m_root,true);
        char *columns[3]={(char *)"Type",(char *)"Plugin ID",(char *)"State"};
        const int listWidth=width*43/100;
        m_list.Init(8,24,listWidth-12,height-78,&m_window,3,columns,DRAG_NONE,0);
        m_properties.Init(listWidth,24,width-listWidth-8,height-78,&m_window,FONT_HEIGHT);
        const int buttonWidth=(width-16)/4;
        m_refresh.Init(8,height-42,buttonWidth-4,28,(char *)"Refresh Snapshot",&m_window);
        m_scan.Init(8+buttonWidth,height-42,buttonWidth-4,28,(char *)"Scan Directory",&m_window);
        m_load.Init(8+buttonWidth*2,height-42,buttonWidth-4,28,(char *)"Load & Measure",&m_window);
        m_globalButton.Init(8+buttonWidth*3,height-42,buttonWidth-4,28,(char *)"Global",&m_window);
        Populate();
        ShowGlobal();
        Debug("RS2PLUGINDIAGUI|opened|records=%u\n",
            (unsigned int)m_summaries.size());
    }
    bool Scan() {
        Init();
        if (GetKey(DIK_F10)==S_PUSH) {
            m_window.Show(!m_window.IsVisible());
            return true;
        }
        if (!m_window.IsVisible()) return false;
        const bool handled=m_window.ScanInput();
        if (m_window.CheckClose()) { m_window.Show(false); return true; }
        if (m_refresh.IsPushed()) {
            Populate();
            if (m_global) ShowGlobal(); else ShowSelected();
            return true;
        }
        if (m_globalButton.IsPushed()) {
            m_global=true; ShowGlobal(); return true;
        }
        const int selected=m_list.GetSelectionMark();
        if (selected!=m_selected && selected>=0) {
            m_selected=selected; m_global=false; ShowSelected();
        }
        if (m_selected>=0 && m_selected<(int)m_summaries.size()) {
            const RS2PluginKey key=m_summaries[(size_t)m_selected].key;
            if (m_scan.IsPushed()) {
                RS2RefreshPluginFilesystemStats(key);
                m_global=false; ShowSelected(); return true;
            }
            if (m_load.IsPushed()) {
                RS2LoadPluginForDiagnostics(key);
                Populate(); m_global=false; ShowSelected(); return true;
            }
        }
        return handled;
    }
    void Render() {
        Init();
        if (!m_window.IsVisible()) return;
        if (m_global && (m_lastGeneration!=RS2PluginDiagnostics().Generation() ||
            (++m_frames%30)==0))
            ShowGlobal();
        m_lastGeneration=RS2PluginDiagnostics().Generation();
        m_window.Render();
        if (!m_renderLogged) {
            Debug("RS2PLUGINDIAGUI|rendered\n");
            m_renderLogged=true;
        }
    }
};

Inspector &GetInspector() {
    static Inspector *inspector=new Inspector;
    return *inspector;
}
}

bool RS2PluginDiagnosticsUIScanInput() {
    static const bool requested=CheckArguments("-plugindiag")!=FALSE;
    return requested && GetInspector().Scan();
}
void RS2PluginDiagnosticsUIRender() {
    static const bool requested=CheckArguments("-plugindiag")!=FALSE;
    if (requested) GetInspector().Render();
}
