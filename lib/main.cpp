//	Modified for RS2EX on 2026-09-20, 2026-09-21.
//	Copyright (c) 2002 Midikyou

#include "udx.h"
#include "libraries.h"
#include "sysvalue.h"
#include "..\RS2D3D12Smoke.h"
#include "..\RS2D3D12DrawSmoke.h"

/*
 *	コンパイル・オプション
 *
 *	※機能を使用しているのに「使用しない」としてコンパイルすると危険です。
 */
//	#define NO_SOUNDS	//	サウンド関係を使用しない
#define NO_MOVIE	//	ムービー関係を使用しない
#define NO_MSGLOOP	//	独自のメッセージループ記述する場合
//	#define SCREENSAVER	//	スクリーンセーバーとして実行

/*
 *	実行時引数（複数指定可能）
 *
 *	-win	:ウインドウモードで実行する。
 *	-2nd	:2枚目のビデオカードを使用する（Voodoo等）。
 *	-dbf	:デバッグ出力先をファイルにする。
 *	-fixture:RS2EX: run a fixed number of simulation ticks, then stop the
 *		 world so captures are reproducible.
 *	-dx12	:RS2EX: select the Direct3D 12 backend.  No fallback.
 *	-dx12smoke:RS2EX: run the Direct3D 12 bootstrap test and exit.
 *	-dx12drawsmoke:RS2EX: draw through the RS2 boundary and exit.
 *	/3ds	:3Dサウンドを使用しない。
 *	/fx		:サウンドにエフェクトを使用しない。
 */
CApp theApp;

/*
 *	プログラム開始位置
 */
INT WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, INT){

	WakeUp();	//	初期化の初期化

	if(!theApp.Init(hInst)) return 0;	//	もろもろの初期化

#ifdef NO_MSGLOOP
	Main();
#else
	StartUp();	//	プログラマ定義の初期化処理

	//	メッセージループ
	while(PeekAllMessage()){
		//	ウインドウがアクティブなら
		if(IsActive()){
			theApp.Run();	//	処理続行
		}else{
		#ifdef SCREENSAVER
			SendWM_CLOSE();	//	終了要求
		#endif
			WaitMessage();	//	メッセージ待ち
		}
	}
	CleanUp();	//	プログラマ定義の解放処理
#endif
	return 0;
}

/*
 *	初期化処理
 */
BOOL CApp::Init(HINSTANCE hInst){
#ifdef SCREENSAVER
	if(CheckArguments("-s") || CheckArguments("/s")){
		//	スクリーンセーバーとして実行されたか？
		;
	}else{
		//	それ以外なら即終了
		MsgBox("プレビュー、設定項目はありません.");
		return FALSE;
	}
#endif

	//	多重起動禁止
	if(!m_mutex.BeginSingleBoot()){
		MsgBox("このプログラムは既に起動しています.\n");
		return FALSE;
	}

	//	浮動小数点例外を無効にする。
#ifdef __BORLANDC__
	_control87(MCW_EM, MCW_EM);
#endif

	CoInitialize(NULL);	//	COMの初期化

	if(!InitDebugStream()) return FALSE;		//	デバッグ出力の初期化
	if(!CreateMainWindow(hInst)) return FALSE;	//	ウインドウの作成

	//	[RS2EX] -dx12smoke exercises the Direct3D 12 backend and exits.  It
	//	runs here because it needs the window and nothing else: no Direct3D 8
	//	device, no input, no sound, no layout.  Returning FALSE leaves through
	//	the same path as any other start-up refusal, so nothing is half
	//	initialised behind it.
	if(RS2D3D12SmokeRequested()){
		RS2D3D12SmokeRun(svw.hWnd);
		return FALSE;
	}
	//	DirectX関連の初期化
	if(!InitDirect3D()) return FALSE;

	//	[RS2EX] -dx12drawsmoke draws through the public boundary and exits.
	//	It runs here because it needs the renderer up with the Direct3D 12
	//	backend installed - drawing through RS2Draw is the whole point, and
	//	that dispatches on whichever backend the renderer is running.
	if(RS2D3D12DrawSmokeRequested()){
		RS2D3D12DrawSmokeRun();
		return FALSE;
	}
	if(!InitDirectInput()) return FALSE;

#ifndef NO_SOUNDS
	if(!InitDirectSound()) return FALSE;
//	if(!InitDirectMusic()) return FALSE;
#endif

#ifndef NO_MOVIE
	if(!InitDirectShow()) return FALSE;
#endif

#ifndef NO_COMM
	if(!InitDirectPlay()) return FALSE;
#endif

	g_frame.Init();	//	FPS管理機構の初期化

#ifndef NO_MSGLOOP
	m_func = MainLoop;	//	ループ関数のセット
#endif
	return TRUE;
}

/*
 *	解放処理
 */
CApp::~CApp(){
	//	DirectX関連の解放
#ifndef NO_COMM
	FreeDirectPlay();
#endif

#ifndef NO_MOVIE
	FreeDirectShow();
#endif

#ifndef NO_SOUNDS
//	FreeDirectMusic();
	FreeDirectSound();
#endif
	FreeDirectInput();
	FreeDirect3D();
	DestroyWindow(svw.hWnd);	//	ウインドウの解放（Direct3Dの解放よりも後）

	CoUninitialize();			//	COMの解放
	m_mutex.EndSingleBoot();	//	多重起動禁止の解除
}

/*
 *	メインループ内処理
 */
void CApp::Run(){
	ScanInputDevice();	//	入力処理
	m_func();			//	ループ関数呼び出し
	SyncFrame();		//	FPS調節とCPU休憩
}

//	フレーム管理

/*
 *	FPS調節のため初期化
 */
void CFrame::Init(){
	frameWait = 1000/RENDER_TARGET_FPS;
	fineWait = (1000%RENDER_TARGET_FPS)/10;

	frame = 0;
	cnt = 0;
	start = timeGetTime();
	old = start;
	fps = RENDER_TARGET_FPS;
	framecnt = RENDER_TARGET_FPS;

	srand((unsigned)time(NULL));
}

/*
 *	FPSの調節
 */
void CFrame::Sync(){
	DWORD now;	//	現在時間
	DWORD diff;	//	経過時間

	now = timeGetTime();
	diff = now-start;
	frame++;

	framecnt++;

	//	約0.5secごとにFPSを計算
	if(frame==RENDER_TARGET_FPS/2){
		fps = (RENDER_TARGET_FPS*500.f)/(now-old);
		old = now;
		frame = 0;
	}
#if 1
	//	ウエイト処理
	DWORD wait;

	if(++cnt==RENDER_TARGET_FPS/10) wait = frameWait+fineWait, cnt = 0;
	else wait = frameWait;

	if(diff<wait){
		//	経過時間が周期より短い
		Sleep(wait-diff);
		start += wait;
	}else if(diff<wait*4){
		//	周期を超えたが許容範囲内
		start += wait;
	}else{
		//	許容範囲ではない
		start = now-wait;
	}
#else
	Sleep(1);
	start = now;
#endif
}
