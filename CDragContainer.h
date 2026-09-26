//	Modified for RS2EX on 2026-09-26.
#ifndef CDRAGCONTAINER_H_INCLUDED
#define CDRAGCONTAINER_H_INCLUDED

const int DRAG_THD = FONT_HEIGHT/2;

//	反復子
//	[RS2EX] v0.3.0: the drag data is pointer-sized (RS2OpaqueData, RS2Width.h).
typedef list<RS2OpaqueData>::iterator IOPAQUEDATA;

//	ドラッグタイプ
typedef enum{
	DRAG_NONE = 0,	//	不明
	DRAG_PLUGIN,	//	プラグイン・ディレクトリ
	DRAG_INSERT,	//	同一リスト内挿入のみ
} DRAGTYPE;

/*
 *	ドラッグコンテナ
 */
class CDragContainer{
	friend class CDragInterface;
private:
	static CDragContainer *ms_Drag;	//	ドラッグ物
	DRAGTYPE m_Type;			//	タイプ
	list<RS2OpaqueData> m_Data;			//	データ
	CDragInterface *m_Owner;	//	所有者
public:
	static void BeginDrag(DRAGTYPE, CDragInterface *);
	static void EndDrag();
	static bool IsDragging(){ return !!ms_Drag; }
	static void Insert(RS2OpaqueData);
	static void Render();
	static list<RS2OpaqueData> &GetData(){ return ms_Drag->m_Data; }
	static DRAGTYPE GetType(){ return ms_Drag ? ms_Drag->m_Type : DRAG_NONE; }
	static CDragInterface *GetOwner(){ return ms_Drag ? ms_Drag->m_Owner : NULL; }
	CDragContainer(DRAGTYPE, CDragInterface *);
	void InsertData(RS2OpaqueData);
	void RenderDragItem();
};

/*
 *	ドラッグ可能インターフェイス
 */
class CDragInterface{
protected:
public:
	virtual void RenderDragItem() = 0;
};

#endif CDRAGCONTAINER_H_INCLUDED
