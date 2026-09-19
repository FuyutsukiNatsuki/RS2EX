//	Modified for RS2EX on 2026-09-20.
//	Copyright (c) 2002 Midikyou

//	※このファイルを書き換えた場合はmain.cppをリビルドして下さい。

//	[RS2EX] MAXFPS used to mean both "the rate we render at" and "the rate the
//	RailSim II 2.15 UI and camera speeds were tuned against".  Those stop being
//	the same number once the render loop runs faster than 30 FPS, so they are
//	now separate constants and MAXFPS is gone.  Simulation timing is neither of
//	these: see RS2EXTiming::SIMULATION_HZ.

//	What the render loop aims for right now.
const int RENDER_TARGET_FPS = 30;

//	The frame rate RailSim II 2.15 was written against.  Per-frame movement
//	amounts that were tuned at 30 FPS are scaled relative to this, never to
//	RENDER_TARGET_FPS - dividing the target by itself would just give 1 and
//	double the real-time speed.
const int LEGACY_RENDER_FPS = 30;

class CFrame{
	DWORD frame;		//	フレームをカウント
	DWORD frameWait;	//	フレーム毎のウエイト
	DWORD fineWait;		//	RENDER_TARGET_FPS/10毎のウエイト
	DWORD cnt;			//	RENDER_TARGET_FPS/10までフレームをカウント
	DWORD start;		//	開始時間
	DWORD old;			//	RENDER_TARGET_FPS/2フレーム前の時間

public:
	float fps;		//	FPSの実測値
	DWORD framecnt;	//	起動後のフレームカウント

	void Init();
	void Sync();
};

extern CFrame g_frame;

/*
 *	FPS の取得
 */
inline float GetFPS(){ return g_frame.fps; }

/*
 *	フレームカウントの取得
 */
inline DWORD GetFrameCount(){ return g_frame.framecnt; }

/*
 *	フレーム同期
 */
inline void SyncFrame(){ g_frame.Sync(); }

/*
 *	乱数初期化
 */
inline void Randomize(){ srand((unsigned int)time(NULL)); }

/*
 *	0～x-1の乱数
 */
inline int Rand(int x){ return rand()%x; }
inline float FRand(float x){ return rand()*x/RAND_MAX; }

/*
 *	範囲指定の乱数
 */
inline int Rand2(int a, int b){ return Rand(b-a)+a; }
inline float FRand2(float a, float b){ return FRand(b-a)+a; }

inline VEC3 V3Rand(VEC3 v){ return VEC3(FRand(v.x), FRand(v.y), FRand(v.z)); }
inline VEC3 V3Rand2(VEC3 a, VEC3 b){ return V3Rand(b-a)+a; }
inline VEC3 V3RandS(float s){ return V3Rand2(VEC3(-s, -s, -s), VEC3(s, s, s)); }
