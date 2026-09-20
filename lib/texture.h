//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21.

using namespace std;

#include "..\RS2TextureResource.h"
#include "..\RS2RenderState.h"

#define UDX_TEXTURE_MEASURE (0)

#if UDX_TEXTURE_MEASURE
#include <vector>
#include <string>
#include "..\CJobTimer.h"
#endif

/*
 *	テクスチャーの読込み
 *
 *	ppTex		: 読込み先
 *	strFile	: ファイル名
 *	cTrans	: 透過色
 *	nMipLv	: ミップマップ・レベル
 *
 *	※サイズが２の乗数でない場合は自動的に透明な領域が追加される。
 */
//	[RS2EX] LOAD_TEXTURE() and LOAD_TEXTURE_RES() moved to RS2D3D8Resources.
//	They put D3DX texture creation in a header included by almost every
//	translation unit, which is the opposite of having one owner for it.
//	システムメモリへ
//
//	[RS2EX] Left here deliberately.  Its only caller is lib/height_field.cpp,
//	which is not in the build, so routing it through the resource module would
//	add a function nothing calls - but deleting it would break that file if it
//	is ever revived.  An unowned creation path that is unreachable; recorded in
//	docs/v0.0.5-resource-inventory.md rather than touched.
inline HRESULT LOAD_TEXTURE_SYS(
	LPTEX8 *ppTex, LPCSTR strFile, D3DCOLOR cTrans = 0, int nMipLv = 1){
	HRESULT hr;

	hr = D3DXCreateTextureFromFileExA(
		sv3.pDev, strFile, 0, 0, nMipLv, 0,
		D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
		D3DX_DEFAULT, D3DX_DEFAULT,
		cTrans, NULL, NULL, ppTex);
	return hr;
}

/*
 *	テクスチャ・クラス
 */
class CTexture{
	BOOL m_fCreate;			//	自作フラグ
	RS2TextureRef m_Texture;	//	テクスチャ・オブジェクト
	int m_Width, m_Height;		//	サイズ等の情報

	//	コピーコンストラクタ封印
	CTexture& operator = (const CTexture&){return *this;}
public:
	CTexture();
	~CTexture();

	BOOL Load(LPCSTR strFile, D3DCOLOR cTrans = 0, int nMipLv = 1);
	BOOL LoadResource(LPCSTR strRes, D3DCOLOR cTrans = 0, int nMipLv = 1);
	void Free();

	BOOL Create(int w, int h);
	BOOL DrawInText(int x, int y, LPCSTR str, HFONT hFont,
		D3DCOLOR col = 0xffffffff, D3DCOLOR sdw = 0, int w = -1, int h = -1);
	void Render(int x, int y);

	/*
	 *	サイズの取得（２の乗数サイズに揃えられる）
	 *
	 *	pSize	: サイズの格納先
	 */
	void GetSize(int *pW, int *pH){*pW = m_Width, *pH = m_Height;}
	/*
	 *	テクスチャ・オブジェクトの取得
	 *
	 *	[RS2EX] Replaces GetObject(), which handed out the Direct3D texture.
	 *	A reference is not ownership: the caller may bind it, and must not
	 *	release it.
	 *
	 *	[RS2EX] GetSurface() removed with it.  It had no caller in the
	 *	compiled tree, and keeping it would have meant a native escape hatch
	 *	for nobody.  Capture does its own surface work - see section 6 of
	 *	docs/v0.0.7-material-texture-inventory.md.
	 */
	RS2TextureRef GetRef(){ return m_Texture; }
};

//	テクスチャリストの要素
struct TEXINFO{
	CRS2TextureResource *pTex;	//	[RS2EX] owned by this entry
	string strName;
	int nRef;
	D3DCOLOR cTrans;
	int nMipLv;
	TEXINFO *pNext;
};

/*
 *	テクスチャリスト・クラス (CMeshクラスで使用)
 *	・同じテクスチャーが多重にロードされないようにする。
 *	・参照数を監視し、使用されていないテクスチャーを解放する。
 */
class CTexList{
	TEXINFO *m_pList;	//	リストの先頭
public:
	CTexList();
	~CTexList();
	RS2TextureRef Get(BOOL fRes, LPCSTR strName, D3DCOLOR cTrans = 0, int nMipLv = 1);
	void Release(RS2TextureRef tex);
};

//	関数宣言
D3DCOLOR CheckTexTrans(LPCSTR str);
void CalcTextRect(int *w, int *h, LPCSTR str, HFONT hFont);

//	[RS2EX] The texture-stage wrappers that used to live here moved to
//	RS2RenderState.h in v0.0.8.  devSetTexAddress() and its five macros
//	went with them rather than becoming RS2 API: nothing ever called them,
//	and texture addressing runs on the Direct3D default throughout.
