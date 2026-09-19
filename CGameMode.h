//	Modified for RS2EX on 2026-09-19, 2026-09-20.
#ifndef CGAMEMODE_H_INCLUDED
#define CGAMEMODE_H_INCLUDED

#include "CCursor.h"
#include "CCamera.h"
#include "CInterface.h"
#include "CFixedSimulationClock.h"

class CToggleIcon;

/*
 *	ゲームモード
 */
class CGameMode{
protected:
	static int ms_TopPanelTime;					//	上パネル表示時間
	static int ms_RightPanelTime;				//	右パネル表示時間
	static float ms_TopPanelShow;				//	上パネル表示度
	static float ms_RightPanelShow;				//	右パネル表示度
	static float ms_WindDirTemp;				//	風力計変数
	static string ms_ModeLabel;					//	モードラベル
	static CGameMode *ms_ActiveMode;			//	現在のモード
	static CToggleIcon *ms_MenuIcon[MODE_NUM];	//	モードアイコン

	//	[RS2EX] Real-time source for simulation ticks, shared by every mode so
	//	that switching modes does not restart or double-count elapsed time.
	static CFixedSimulationClock ms_SimulationClock;

	//	How many base simulation ticks to run now.  Passing enabled=false clears
	//	the accumulator instead of banking it, so a pause or a modal dialog does
	//	not replay its whole duration once it ends.
	static int ConsumeSimulationTicks(bool enabled);
	static void ResetSimulationClock(bool prime = false);

	//	[RS2EX] One outer fixed tick: snapshot the train posture the renderer
	//	will interpolate from, then advance the world exactly once.  Routing
	//	every tick through here keeps the snapshot on the presentation boundary
	//	rather than inside Simulate()'s speed-multiplier loop.
	static void RunSimulationTick();
	//	Whether train posture may be blended this frame, and by how much.
	static bool IsTrainInterpolationEnabled();
	static float GetTrainInterpolationAlpha();
	CInterface m_Interface;	//	統括インターフェイス
public:
	static void WakeUp();
	static void InitMenu();
	static void MainLoop();
	static void LoadModeSettings();
	static void SaveModeSettings(FILE *);
	static void SetNeutral();
	static void Exit(){ ms_ActiveMode = NULL; }
	bool ScanInputFrame(int);
	void RenderFrame(int);
	void RenderCompass();
	bool RenderDialog();
	CGameMode();
	virtual ~CGameMode(){}
	bool IsModeActive(){ return ms_ActiveMode==this; }
	virtual char *LoadSetting(char *str){ return str; }
	virtual void SaveSetting(FILE *){}
	void Enter();
	virtual void EnterGame() = 0;
	void Spin();
	void SpinSound();
	virtual void SpinGame() = 0;
	virtual bool IsPaused(){ return false; }
	int GetEffectSpeed();
};

#endif
