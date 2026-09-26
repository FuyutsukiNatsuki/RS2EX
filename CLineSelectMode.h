//	Modified for RS2EX on 2026-09-26.
#ifndef CLINESELECTMODE_H_INCLUDED
#define CLINESELECTMODE_H_INCLUDED

#include "C3DPluginMode.h"
#include "CRailwayMode.h"

/*
 *	架線選択モード
 */
class CLineSelectMode: public C3DPluginMode, public CRailwayMode{
private:
public:
	CLineSelectMode();
	~CLineSelectMode(){}
	CPopMenu *Dispatch(CMDTYPE, RS2OpaqueData);
	char *PluginDirName(){ return "Line"; }
	CPluginList *GetPluginList();
	void Enter3DPlugin();
	void ModalFunc3DPlugin();
	void ScanInput3DPlugin();
	void Render3DPlugin();
};

//	外部グローバル
extern CLineSelectMode *g_LineSelectMode;

#endif
