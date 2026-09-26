//	Modified for RS2EX on 2026-09-26.
#ifndef CSCENEEDITMODE_H_INCLUDED
#define CSCENEEDITMODE_H_INCLUDED

#include "C3DPluginMode.h"

class CSurfacePlugin;
class CScene;

/*
 *	シーンリストビュー
 */
class CSceneListView: public CListView{
private:
public:
	bool IsRenamable(CListElement *){ return !g_NetworkInitialized; }
	void EndRename(CListElement *);
	void DoubleClick();
};

/*
 *	シーン編集モード
 */
class CSceneEditMode: public CModelPluginMode{
private:
	CPushButton m_AddButton;		//	車輌追加ボタン
	CScene *m_SelectScene;			//	選択したシーン
	CCamera m_MyCamera;				//	カメラ
	CWindowCtrl m_SceneWindow;		//	シーン窓
	CSceneListView m_SceneListView;	//	シーンリスト
	CPopMenu *m_SceneMenu;			//	シーンメニュー
public:
	CSceneEditMode();
	~CSceneEditMode();
	void WindowResized(int, int, CWindowCtrl *);
	CPopMenu *Dispatch(CMDTYPE, RS2OpaqueData);
	void DoubleClick(CMDTYPE, RS2OpaqueData);
	CModelPlugin *GetModelPlugin();
	void AddScene(CSurfacePlugin *);
	void DeleteScene(CScene *);
	char *PluginDirName(){ return "Surface"; }
	CPluginList *GetPluginList();
	void EnterModelPlugin();
	CModelInst *ScanInputModelPlugin();
	void RenderModelPlugin();
};

//	外部グローバル
extern CSceneEditMode *g_SceneEditMode;

#endif
