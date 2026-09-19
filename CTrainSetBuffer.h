//	Modified for RS2EX on 2026-09-20.
#ifndef CTRAINSETBUFFER_H_INCLUDED
#define CTRAINSETBUFFER_H_INCLUDED

class CRailWay;
class CAxleObject;

/*
 *	車軸姿勢バッファ
 */
class CAxlePosture{
	friend class CTrain;
	friend class CTrainGroup;
private:
	float m_Rotation;		//	回転量
	float m_Distance;		//	走行距離
	bool m_Terminate;		//	端フラグ
	VEC3 m_Pos;				//	座標
	VEC3 m_Right;			//	right
	VEC3 m_Up;				//	up
	VEC3 m_Dir;				//	dir
	CRailWay *m_Rail;		//	レール
	CAxleObject *m_Axle;	//	車軸
	CTrain *m_Train;		//	車輌

	//	[RS2EX] Render-only interpolation state.  The members above stay the
	//	authoritative simulation posture and are never written from the render
	//	path; these mirror them for presentation between two 30 Hz states.
	VEC3 m_RenderPrevPos;	//	posture at the previous outer fixed tick
	VEC3 m_RenderPrevDir;
	VEC3 m_RenderPrevUp;
	VEC3 m_RenderPos;		//	posture handed to the renderer this frame
	VEC3 m_RenderRight;
	VEC3 m_RenderUp;
	VEC3 m_RenderDir;
	//	Explicit, not inferred from coordinates: the origin is a valid position.
	bool m_RenderStateValid;
public:
	CAxlePosture(CAxleObject *, CTrain *, bool);
	void SetPosture(VEC3, VEC3, VEC3, CRailWay *);
	float GetZPos();
	void Apply();
	void Rotate(float, bool);

	//	[RS2EX] render-only interpolation
	void CaptureRenderState();
	void InvalidateRenderState();
	void PrepareRenderState(float alpha, bool interpolate);
	void ApplyRender();
	VEC3 GetRenderPos() const{ return m_RenderPos; }
	VEC3 GetRenderDir() const{ return m_RenderDir; }
	VEC3 GetRenderUp() const{ return m_RenderUp; }
	VEC3 GetRenderRight() const{ return m_RenderRight; }
};

//	反復子
typedef list<CAxlePosture>::iterator IAxlePosture;

/*
 *	車輌配置バッファ
 */
class CTrainSetBuffer{
	friend class CRailWay;
	friend class CTrainSetCurve;
	friend class CTrainGroup;
private:
	float m_SumLen;				//	積算距離
	bool m_Reverse;				//	反転フラグ
	CAxlePosture *m_Posture;	//	姿勢バッファ
public:
	CTrainSetBuffer(float, bool, CAxlePosture *);
	bool operator<(const CTrainSetBuffer &rhs) const{ return m_SumLen<rhs.m_SumLen; }
	void SetPosture(VEC3 pos, VEC3 dir, VEC3 up, CRailWay *way){
		if(m_Posture) m_Posture->SetPosture(pos, m_Reverse ? -dir : dir, up, way);
	}
	void Rotate(float dist){
		if(m_Posture) m_Posture->Rotate(dist, m_Reverse);
	}
};

//	反復子
typedef list<CTrainSetBuffer>::iterator ITrainSetBuffer;

/*
 *	編成終端情報
 */
class CGroupEndLocator{
public:
	int m_Side;				//	サイド
	int m_Type;				//	タイプ (0: front, 1: tail, 2: seeker)
	float m_Offset;			//	オフセット
	CRailWay *m_SetRail;	//	設置レール
	CTrainGroup *m_Group;	//	編成
public:
	CGroupEndLocator();
	CGroupEndLocator(int, float, CRailWay *, CTrainGroup *);
	void Attach(int);
	void Detach();
	int GetDirection();
	void RestoreAddress();
	char *Read(char *, char *);
	void Save(FILE *, char *, char *);
};

//	反復子
typedef list<CGroupEndLocator *>::iterator IPGroupEndLocator;

#endif
