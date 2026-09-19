//	Modified for RS2EX on 2026-09-20.
//	Copyright (c) 2002 Midikyou

//	※このファイルを書き換えた場合はmain.cppをリビルドして下さい。

//	[RS2EX] MAXFPS used to mean both "the rate we render at" and "the rate the
//	RailSim II 2.15 UI and camera speeds were tuned against".  Those stop being
//	the same number once the render loop runs faster than 30 FPS, so they are
//	now separate constants and MAXFPS is gone.  Simulation timing is neither of
//	these: see RS2EXTiming::SIMULATION_HZ.

//	What the render loop aims for right now.
const int RENDER_TARGET_FPS = 60;

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
 *	[RS2EX] How much of a legacy 30 FPS frame the current frame represents.
 *
 *	RailSim II 2.15 applied fixed per-frame amounts - camera nudges, UI lerps,
 *	decay factors - and got its real-time speeds from running at 30 FPS.  At a
 *	higher render rate those same amounts are applied more often, so anything
 *	expressed per frame has to be scaled by this to keep its speed in seconds.
 *
 *	Does NOT apply to event quantities such as mouse deltas or a wheel notch:
 *	those already arrive once per event, not once per frame.
 *
 *	returns	: 1.0 at 30 FPS, 0.5 at 60 FPS, clamped to [0.25, 4.0]
 */
inline float GetLegacyFrameScale(){
	const float fps = GetFPS();
	if(fps<1.0f) return 1.0f;
	float scale = (float)LEGACY_RENDER_FPS/fps;
	//	A debugger stall or a momentary FPS spike must not teleport the camera.
	if(scale<0.25f) scale = 0.25f;
	if(scale>4.0f) scale = 4.0f;
	return scale;
}

/*
 *	[RS2EX] Convert a legacy per-frame lerp factor to the current frame rate.
 *
 *	For x = a*target+(1-a)*x, halving a is not the same as halving the frame
 *	time.  The residual after one legacy frame is (1-a), so the equivalent
 *	residual after a shorter frame is (1-a)^scale.
 *
 *	alpha	: the factor the original code was tuned with
 */
inline float AdjustLegacyLerp(float alpha){
	if(alpha<=0.0f) return 0.0f;
	if(alpha>=1.0f) return 1.0f;
	return 1.0f-powf(1.0f-alpha, GetLegacyFrameScale());
}

/*
 *	[RS2EX] Convert a legacy per-frame multiplier to the current frame rate.
 *
 *	For v *= m applied once per frame, m^scale keeps the same time constant.
 *	Works for growth (m>1) as well as decay: 1.1 becomes 1.1^0.5 at 60 FPS, and
 *	1.1^0.5 applied 60 times equals 1.1 applied 30 times.
 *
 *	multiplier	: the factor the original code was tuned with
 */
inline float AdjustLegacyMultiplier(float multiplier){
	return powf(multiplier, GetLegacyFrameScale());
}

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
