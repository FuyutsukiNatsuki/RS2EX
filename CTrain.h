//	Modified for RS2EX on 2026-09-20.
#ifndef CTRAIN_H_INCLUDED
#define CTRAIN_H_INCLUDED

#include "CTrainSetBuffer.h"

class CScene;
class CTrainGroup;
class CTrainPlugin;
class CListElement;

#include "CModelInst.h"

/*
 *	車輌インスタンス
 */
class CTrain: public CModelInst{
	friend class CTrainGroup;
private:
	bool m_Reverse;					//	反転
	bool m_Setting;					//	配置作業中
	bool m_Warping;					//	ワープ中
	bool m_WarpingOld;				//	旧ワープフラグ
	VEC3 m_OldPos[2];				//	旧座標
	VEC3 m_TiltDir;					//	振り子ベクトル
	CTrainGroup *m_Group;			//	編成
	CTrainPlugin *m_TrainPlugin;	//	車輌プラグイン
	CListElement *m_ListElement;	//	リスト要素
	list<CAxlePosture> m_AxleList;	//	車軸リスト
	CTrain *m_Next;					//	次

	//	[RS2EX] Render-only vehicle posture, rebuilt from interpolated axles.
	//	CModelInst::m_Pos / m_Right / m_Up / m_Dir stay authoritative.
	VEC3 m_RenderPos;
	VEC3 m_RenderRight;
	VEC3 m_RenderUp;
	VEC3 m_RenderDir;
public:
	CTrain(CTrainGroup *);
	CTrain(CTrainPlugin *, CTrainGroup *, bool);
	~CTrain();
	CTrain *Next(){ return m_Next; }
	CTrain **NextAdr(){ return &m_Next; }
	CTrainGroup *GetTrainGroup(){ return m_Group; }
	bool IsReverse(){ return m_Reverse; }
	void Reverse();
	void ToggleSetting(){ m_Setting = !m_Setting; }
	void NotifyWarp(){ if(m_Setting) m_Warping = true; }
	bool IsWarping(){ return m_Warping; }
	void SetListElement(CListElement *le){ m_ListElement = le; }
	float GetLength();
	float SetSetBuffer(float, list<CTrainSetBuffer> *);
	CScene *GetScene();
	bool CheckScene();
	void PushAxle(CAxlePosture &a){ m_AxleList.push_back(a); }
	void ApplyAxle(bool);

	//	[RS2EX] render-only interpolation
	void CaptureRenderState();
	void InvalidateRenderState();
	void PrepareRenderState(float alpha, bool interpolate);
	VEC3 GetRenderPos() const{ return m_RenderPos; }
	VEC3 GetRenderRight() const{ return m_RenderRight; }
	VEC3 GetRenderUp() const{ return m_RenderUp; }
	VEC3 GetRenderDir() const{ return m_RenderDir; }
	//	Render-space counterpart of CModelInst::SetLocalAxis().
	void SetLocalAxisRender();
	void ResetTilt();
	void PrintInfo();	
	CModelInst *Control();
	void SetCabinView(bool, VEC3, VEC3);
	void ScanInput(int, VEC3 &, VEC3 &);
	void Render();
	void SimulateModelInst();
	char *Read(char *, CTrain ***);
	void Save(FILE *);
};

#endif
