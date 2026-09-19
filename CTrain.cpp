//	Modified for RS2EX on 2026-09-20.
#include "stdafx.h"
#include "RailMap.h"
#include "CSimpleDialog.h"
#include "CRailWay.h"
#include "CScene.h"
#include "CTrain.h"
#include "CTrainGroup.h"
#include "CTrainPlugin.h"
#include "CSkinPlugin.h"
#include "CListView.h"
#include "CSimulationMode.h"

/*
 *	コンストラクタ (読込用)
 */
CTrain::CTrain(
	CTrainGroup *group	//	編成
){
	m_Reverse = false;
	m_Warping = m_WarpingOld = false;
	m_OldPos[0] = m_OldPos[1] = m_TiltDir = V3ZERO;
	m_Group = group;
	m_TrainPlugin = NULL;
	m_ListElement = NULL;
	m_Next = NULL;
}

/*
 *	コンストラクタ
 */
CTrain::CTrain(
	CTrainPlugin *tpi,	//	車輌プラグイン
	CTrainGroup *group,	//	編成
	bool rev			//	反転フラグ
):
	CModelInst(tpi)	//	基本クラス
{
	m_Reverse = rev;
	m_Warping = m_WarpingOld = false;
	m_OldPos[0] = m_OldPos[1] = m_TiltDir = V3ZERO;
	m_Group = group;
	m_TrainPlugin = tpi;
	m_TrainPlugin->SetAxleList(this);
	m_ListElement = NULL;
	m_Next = NULL;
}

/*
 *	デストラクタ
 */
CTrain::~CTrain(){
	DELETE_V(m_Next);
}

/*
 *	車輌反転
 */
void CTrain::Reverse(){
	if(m_Group->IsSet()){
		EnqueueCommonDialog(new CSimpleDialog(lang(CannotChangeWhileTrainSet), lang(Error)));
		g_Skin->Error();
		return;
	}
	m_Reverse = !m_Reverse;
	if(m_ListElement) m_ListElement->SetString(1, m_Reverse ? lang(Yes) : lang(No));
}

/*
 *	車輌長を求める
 */
float CTrain::GetLength(){
	return m_TrainPlugin->m_Length;
}

/*
 *	車輌配置準備
 */
float CTrain::SetSetBuffer(
	float ofs,						//	オフセット
	list<CTrainSetBuffer> *buflist	//	リスト
){
	IAxlePosture ia = m_AxleList.begin();
	for(; ia!=m_AxleList.end(); ia++){
		float sumlen = m_Reverse
			? ofs-m_TrainPlugin->m_TailLimit+ia->GetZPos()
			: ofs+m_TrainPlugin->m_FrontLimit-ia->GetZPos();
		ia->m_Rail = NULL;
		ia->m_Distance = -sumlen;
		buflist->push_back(CTrainSetBuffer(sumlen, m_Reverse, &*ia));
	}
	ofs += GetLength();
	m_WarpingOld = false;
	if(m_Next) return m_Next->SetSetBuffer(ofs, buflist);
	else return ofs;
}

/*
 *	シーン取得
 */
CScene *CTrain::GetScene(){
	if(m_Warping || !m_AxleList.size()) return NULL;
	CRailWay *rail = m_AxleList.begin()->m_Rail;
	return rail ? rail->GetScene() : NULL;
}

/*
 *	シーンチェック
 */
bool CTrain::CheckScene(){
	CScene *scene = GetScene();
	if(scene){
		scene->SetSeason();
		if(scene!=g_Scene) return false;
	}
	return true;
}

/*
 *	車軸適用
 */
void CTrain::ApplyAxle(
	bool sim	//	シミュレーションフラグ
){
	IAxlePosture ia = m_AxleList.begin();
	bool oldwarp = m_WarpingOld;
	if(sim){
		m_OldPos[1] = m_OldPos[0];
		m_OldPos[0] = m_Pos;
		m_WarpingOld = m_Warping;
	}
	if(m_AxleList.size()){
		IAxlePosture ia2 = --m_AxleList.end();
		m_Pos = 0.5f*(ia->m_Pos+ia2->m_Pos);
		m_Dir = ia->m_Dir+ia2->m_Dir;
		m_Up = ia->m_Up+ia2->m_Up;
		V3NormAxis(&m_Right, &m_Up, &m_Dir);
		//	[RS2EX] Same reason as CAxlePosture::SetPosture(): the render
		//	getters must stay valid when no interpolation pass runs.
		m_RenderPos = m_Pos;
		m_RenderDir = m_Dir;
		m_RenderUp = m_Up;
		m_RenderRight = m_Right;
		if(sim){
			if(oldwarp){
				ResetTilt();
				//	[RS2EX] Coming out of a warp the vehicle is somewhere else
				//	entirely; a straight line from where it was is meaningless.
				InvalidateRenderState();
			}else{
				float tiltspeed = m_TrainPlugin->m_TiltSpeed;
				m_TiltDir = (1.0f-tiltspeed)*m_TiltDir
					+tiltspeed*((m_OldPos[0]-m_OldPos[1])-(m_OldPos[1]-m_Pos));
			}
		}
		CBodyObject::SetTiltDir(m_TiltDir);
	}
	for(; ia!=m_AxleList.end(); ia++) ia->Apply();
}

/*
 *	[RS2EX] Publish the render posture as SYS_OBJ_LOCAL.
 *
 *	Separate from CModelInst::SetLocalAxis() so simulation and plugin logic keep
 *	seeing authoritative coordinates; only render-time evaluation gets these.
 */
void CTrain::SetLocalAxisRender(){
	g_SystemObject[SYS_OBJ_LOCAL].SetPreviewPosture(
		m_RenderPos, m_RenderDir, m_RenderUp);
}

/*
 *	[RS2EX] Record the interpolation origin for every axle of this vehicle.
 */
void CTrain::CaptureRenderState(){
	IAxlePosture ia = m_AxleList.begin();
	for(; ia!=m_AxleList.end(); ia++) ia->CaptureRenderState();
}

/*
 *	[RS2EX] Drop interpolation history for every axle of this vehicle.
 */
void CTrain::InvalidateRenderState(){
	IAxlePosture ia = m_AxleList.begin();
	for(; ia!=m_AxleList.end(); ia++) ia->InvalidateRenderState();
}

/*
 *	[RS2EX] Build the posture this vehicle will be drawn with.
 *
 *	alpha		: 0..1 between the previous and current simulation states
 *	interpolate	: false to present the authoritative state
 *
 *	Mirrors the centre/orientation half of ApplyAxle() against the render
 *	posture, then hands the interpolated axles to the existing body system so
 *	bodies, joints and free objects are rebuilt by the code that already knows
 *	how.  Deliberately does not touch m_Pos / m_Dir / m_Up / m_Right, m_OldPos
 *	or m_TiltDir: those are simulation state, and tilt stays at 30 Hz in this
 *	version.
 */
void CTrain::PrepareRenderState(
	float alpha,		//	interpolation factor
	bool interpolate	//	interpolation allowed
){
	IAxlePosture ia = m_AxleList.begin();
	for(; ia!=m_AxleList.end(); ia++) ia->PrepareRenderState(alpha, interpolate);
	if(!m_AxleList.size()) return;
	ia = m_AxleList.begin();
	IAxlePosture ia2 = --m_AxleList.end();
	m_RenderPos = 0.5f*(ia->GetRenderPos()+ia2->GetRenderPos());
	m_RenderDir = ia->GetRenderDir()+ia2->GetRenderDir();
	m_RenderUp = ia->GetRenderUp()+ia2->GetRenderUp();
	V3NormAxis(&m_RenderRight, &m_RenderUp, &m_RenderDir);
	if(!CheckScene() || m_Warping) return;
	//	Rebuild the drawable geometry from the render posture.  SetPartsInst()
	//	re-points the shared plugin object tree at this vehicle's own instance
	//	objects, and the axle/body/free traversals consume that iterator in
	//	order, so each vehicle writes only its own posture.
	m_TrainPlugin->SetSwitch(this);
	m_TrainPlugin->SetPartsInst(this);
	CBodyObject::SetTiltDir(m_TiltDir);
	for(ia = m_AxleList.begin(); ia!=m_AxleList.end(); ia++) ia->ApplyRender();
	m_TrainPlugin->SetPostureRender(this);
}

/*
 *	振り子リセット
 */
void CTrain::ResetTilt(){
	m_OldPos[0] = m_OldPos[1] = m_Pos;
	m_TiltDir = V3ZERO;
}

/*
 *	速度情報表示
 */
void CTrain::PrintInfo(){
	m_Group->PrintInfo();
}

/*
 *	速度操作
 */
CModelInst *CTrain::Control(){
	return m_Group->Control(this);
}

/*
 *	運転席視点設定
 */
void CTrain::SetCabinView(
	bool rev,	//	逆方向フラグ
	VEC3 pos,	//	座標オフセット
	VEC3 dir	//	方向オフセット
){
	if(m_Warping) return;
	if(GetScene()!=g_Scene){
		CCamera cam = *CCamera::GetCurrentCamera();
		g_Scene = GetScene();
		*g_Scene->GetCamera() = cam;
		g_Scene->Enter(true);
		*g_Scene->GetCamera() = cam;
	}
	if(m_Reverse) rev = !rev;
	VEC3 vpos, vright, vup, vdir;
	CObjectJoint3D *joint = rev ? &m_TrainPlugin->m_TailCabin : &m_TrainPlugin->m_FrontCabin;
	if(joint->IsLinked()){
	//	m_TrainPlugin->SetSwitch(this);
	//	m_TrainPlugin->SetPartsInst(this);
	//	ApplyAxle(false);
		m_TrainPlugin->SetPartsInst(this);
		m_TrainPlugin->AttachPartsObject();
		vpos = joint->GetPos();
		vup = joint->GetUp();
		vdir = joint->GetDir();
	}else{
		//	[RS2EX] Render posture: this runs during camera setup, and the
		//	linked-joint branch above already uses interpolated body geometry.
		vpos = GetRenderPos()
			+(rev ? -0.5f : 0.5f)*GetRenderDir()*m_TrainPlugin->m_Length;
		vup = GetRenderUp();
		vdir = rev ? -GetRenderDir() : GetRenderDir();
	}
	V3NormAxis(&vright, &vup, &vdir);
	vpos += V3LocalToWorld(&pos, &vright, &vup, &vdir);
	SetView(vpos, vpos+V3LocalToWorld(&dir, &vright, &vup, &vdir), vup);
}

/*
 *	入力チェック
 */
void CTrain::ScanInput(
	int mode,		//	モード (0: link)
	VEC3 &rect1,	//	領域始点
	VEC3 &rect2		//	領域終点
){
	if(!CheckScene()) return;
	ms_CurrentInst = this;
	m_TrainPlugin->SetSwitch(this);
	m_TrainPlugin->SetPartsInst(this);
	ApplyAxle(false);
	m_TrainPlugin->SetPartsInst(this);	//	リセットが要る！
	m_TrainPlugin->ScanInput(this);
}

/*
 *	レンダリング
 */
void CTrain::Render(){
	if(!CheckScene() || m_Warping) return;
	m_TrainPlugin->SetSwitch(this);
	m_TrainPlugin->SetPartsInst(this);
//	ApplyAxle(false);					//	ApplyAxle 不要？
//	m_TrainPlugin->SetPartsInst(this);	//	リセットが要る！
	m_TrainPlugin->Render(this);
	if(g_MapDrawNeeded){
		float front, tail;
		if(m_Reverse){
			front = m_TrainPlugin->m_TailLimit;
			tail = m_TrainPlugin->m_FrontLimit;
		}else{
			front = m_TrainPlugin->m_FrontLimit;
			tail = m_TrainPlugin->m_TailLimit;
		}
		RailMapLine(m_Pos+front*m_Dir, 0xffff0000,
			m_Pos+tail*m_Dir, 0xffff0000, true, true);
	}
}

/*
 *	シミュレート進行
 */
void CTrain::SimulateModelInst(){
	CheckScene();
	if(m_Warping) StopSound();
	m_TrainPlugin->SetSwitch(this);
	m_TrainPlugin->SetPartsInst(this);
	ApplyAxle(true);
//	m_TrainPlugin->SetPartsInst(this);	//	CAxlePosture は処理済なのでリセットは不要
	m_TrainPlugin->Simulate(this);
}

/*
 *	読込
 */
char *CTrain::Read(
	char *str,		//	対象文字列
	CTrain ***root	//	格納先
){
	char *eee;
	if(!(str = BeginBlock(str, "Train"))){
		delete this;
		return NULL;
	}
	string pid;
	if(!(str = AsgnString(eee = str, "TrainPlugin", &pid))) throw CSynErr(eee);
	m_ModelPlugin = m_TrainPlugin = g_TrainPluginList->FindPlugin(pid.c_str(), true);
	if(m_TrainPlugin) m_TrainPlugin->SetAxleList(this);
	if(!(str = ReadModelInst(eee = str, true))) throw CSynErr(eee);
	if(!(str = AsgnYesNo(eee = str, "Reverse", &m_Reverse))) throw CSynErr(eee);
	if(!(str = Assignment(eee = str, "Warping"))) throw CSynErr(eee);
	if(!(str = BoolYesNo(eee = str, &m_Warping))) throw CSynErr(eee);
	if(!(str = Character2(eee = str, ','))) throw CSynErr(eee);
	if(!(str = BoolYesNo(eee = str, &m_WarpingOld))) throw CSynErr(eee);
	if(!(str = Character2(eee = str, ';'))) throw CSynErr(eee);
	if(!(str = AsgnVector3D(eee = str, "OldPos", m_OldPos, 2, false))) throw CSynErr(eee);
	if(!(str = AsgnVector3D(eee = str, "TiltDir", &m_TiltDir))) throw CSynErr(eee);
	if(!(str = EndBlock(eee = str))) throw CSynErr(eee, ERR_ENDBLOCK);
	**root = this;
	*root = &m_Next;
	return str;
}

/*
 *	保存
 */
void CTrain::Save(
	FILE *df	//	ファイル
){
	fprintf(df, "\t\tTrain{\n");
	fprintf(df, "\t\t\tTrainPlugin = \"%s\";\n", CheckPluginID(m_TrainPlugin));
	SaveModelInst(df, "\t\t\t", true);
	fprintf(df, "\t\t\tReverse = %s;\n", YESNO[m_Reverse]);
	fprintf(df, "\t\t\tWarping = %s, %s;\n", YESNO[m_Warping], YESNO[m_WarpingOld]);
	fprintf(df, "\t\t\tOldPos = ");
	V3Save(df, m_OldPos[0], ", "); V3Save(df, m_OldPos[1], ";\n");
	fprintf(df, "\t\t\tTiltDir = ");
	V3Save(df, m_TiltDir, ";\n");
	fprintf(df, "\t\t}\n");
}
