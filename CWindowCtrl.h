//	Modified for RS2EX on 2026-09-26.
#ifndef CWINDOWCTRL_H_INCLUDED
#define CWINDOWCTRL_H_INCLUDED

#include "CInterface.h"
#include "CMiniButton.h"

class CWindowCtrl;

/*
 *	ウィンドウリサイザ
 */
class CWindowResizer{
public:
	virtual void WindowResized(int, int, CWindowCtrl *) = 0;
};

/*
 *	ウィンドウコントロール
 */
class CWindowCtrl: public CInterface{
protected:
	int m_State;					//	状態
	int m_OldX, m_OldY;				//	移動前座標
	int m_DragX, m_DragY;			//	タイトルバードラッグ座標
	int m_MinWidth, m_MinHeight;	//	最小サイズ
	int m_MaxWidth, m_MaxHeight;	//	最大サイズ
	RS2PackedColor m_Color;				//	ウィンドウ色
	CMiniButton *m_CloseButton;		//	クローズボタン
	CWindowResizer *m_Resizer;		//	リサイザ
	unsigned int m_DisplayGeneration;	//	[RS2EX] 位置を確かめた表示サイズ
	void KeepReachable();
public:
	CWindowCtrl();
	virtual ~CWindowCtrl();
	void Init(int, int, int, int, char *, CInterface *p, bool);
	virtual void SetSize(int, int);
	void SetResize(int, int, int, int, CWindowResizer *);
	bool CheckClose(){ return m_Visible && m_CloseButton->IsPushed(); }
	void SetColor(RS2PackedColor c){ m_Color = c; }
	void SetAutoTransparent();
	bool IsInsideTitleBar(int, int);
	bool IsInsideLeftGrab(int, int);
	bool IsInsideTopGrab(int, int);
	bool IsInsideRightGrab(int, int);
	bool IsInsideBottomGrab(int, int);
	void BeginDrag(int, int, int);
	virtual int GetWindowState(){ return 0; }
	bool ScanInput();
	virtual bool ScanInputWindow(){ return false; }
	void Render();
	virtual void RenderWindow(){}
};

//	外部グローバル
extern CWindowCtrl *g_ModalDialog;

#endif
