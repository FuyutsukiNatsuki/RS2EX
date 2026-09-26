//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-26.

#include "headers.h"
#include "debug.h"
#include "graphic.h"
#include "vertex.h"
#include "draw.h"
#include "texture.h"
#include "..\RS2MaterialBinding.h"

//	内部グローバル
CTexList g_TexList;		//	テクスチャリスト

/*
 *	コンストラクタ
 */
CTexture::CTexture(){
	m_fCreate = FALSE;
	m_Width = m_Height = 0;
}

/*
 *	デストラクタ
 */
CTexture::~CTexture(){
	Free();
}

/*
 *	読込み
 *
 *	strFile	: ファイル名（BMP, PNGなど）
 *	cTrans	: 透過色
 *	nMipLv	: ミップマップＬＶ
 */
BOOL CTexture::Load(LPCSTR strFile, RS2PackedColor cTrans, int nMipLv){
	Free();	//	既存なら解放
	m_Texture = g_TexList.Get(FALSE, strFile, cTrans, nMipLv);
	if(m_Texture.IsEmpty()) return FALSE;
	m_Texture.GetSize(&m_Width, &m_Height);
	return TRUE;
}
//	リソース（ビットマップのみ）
BOOL CTexture::LoadResource(LPCSTR strRes, RS2PackedColor cTrans, int nMipLv){
	Free();	//	既存なら解放
	m_Texture = g_TexList.Get(TRUE, strRes, cTrans, nMipLv);
	if(m_Texture.IsEmpty()) return FALSE;
	m_Texture.GetSize(&m_Width, &m_Height);
	return TRUE;
}

/*
 *	解放
 */
void CTexture::Free(){
	if(m_Texture.IsEmpty()) return;
	if(m_fCreate){
		//	[RS2EX] m_fCreate is deliberately left set, exactly as before.
		//	Clearing it here would be a behaviour change; it stays a known
		//	issue rather than a silent repair.
		RS2DestroyTexture(m_Texture.GetResource());
		m_Texture.Clear();
	}else{
		m_fCreate = FALSE;
		g_TexList.Release(m_Texture);
		m_Texture.Clear();
	}
}

/*
 *	テクスチャの作成
 */
BOOL CTexture::Create(int w, int h){
	Free();	//	既存なら解放
	m_fCreate = TRUE;

	//	テクスチャの作成
	//	[RS2EX] Power-of-two rounding, A4R4G4B4 and the single mip level moved
	//	to the resource module together - DrawInText() writes 16-bit pixels into
	//	the locked surface by hand, so the three are one decision, not three.
	CRS2TextureResource *resource = RS2CreateMutableTexture(w, h);
	if(!resource) return FALSE;

	m_Texture = resource->GetRef();
	m_Texture.GetSize(&m_Width, &m_Height);
#if 0
	Debug("Allocated Texture %d x %d.\n", m_Width, m_Height);
#endif

	return TRUE;
}

/*
 *	文字列を書き込む
 *
 *	str		: 文字列（TAB無効）
 *	font	: フォントハンドル
 */
BOOL CTexture::DrawInText(int x, int y, LPCSTR str,
	HFONT hFont, RS2PackedColor col, RS2PackedColor sdw, int w, int h){
	if(m_Texture.IsEmpty()) return FALSE;

	HDC hDC = CreateCompatibleDC(NULL);

	HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);

	SetMapMode(hDC, MM_TEXT);
	SetBkMode(hDC, TRANSPARENT);
	SetTextColor(hDC, 0x00ffffff);

	if(w<0){
		//	描画領域の取得
		RECT drawRect = {0, 0, 1, 1};

		DrawText(hDC, str, -1, &drawRect, DT_CALCRECT|DT_LEFT|DT_EXPANDTABS|DT_NOPREFIX);

		w = drawRect.right;
		h = drawRect.bottom;
	}

	//	DIBの作成
	BITMAPINFO bmi;

	ZeroMemory(&bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biBitCount = 32;

	BYTE *pDIB;
	DWORD dibSize = 4*w*h;
	RECT rect = {0, 0, w, h};

	HBITMAP hBM = CreateDIBSection(
		hDC, &bmi, DIB_RGB_COLORS,
		(VOID **)&pDIB, NULL, 0);
	if(!pDIB) return FALSE;

	//	DIBに文字列を描画
	HBITMAP hOldBM = (HBITMAP)SelectObject(hDC, hBM);
	memset(pDIB, 0, dibSize);
	DrawText(hDC, str, -1, &rect, DT_LEFT|DT_EXPANDTABS|DT_NOCLIP|DT_NOPREFIX);

	//	DIB(32bit)をテクスチャ(16bit)へ転送
	RS2TextureLock lockRect;
	CRS2TextureResource *resource = m_Texture.GetResource();

	if(resource->Lock(&lockRect)){
		int i, j;
		BYTE a = (col&0xff000000)>>24;
		BYTE r = (col&0x00ff0000)>>16;
		BYTE g = (col&0x0000ff00)>>8;
		BYTE b = col&0x000000ff;
		WORD color = ((a&0xf0)<<8)|((r&0xf0)<<4)|(g&0xf0)|((b&0xf0)>>4);	//	16bit化

		WORD *pDst, *pSdw, *pSdw2 /* , *pSdw3 */ ;
		DWORD *pSrc = (DWORD *)pDIB;

		int cx = 0, cy = 0;
		if(x<0){ cx = -x; x = 0; }
		if(y<0){ cy = -y; y = 0; }
		int cw = w-cx, ch = h-cy;
		if(x+cw>m_Width) cw = m_Width-x;
		if(y+ch>m_Height) ch = m_Height-y;

		if(sdw){
			a = (sdw&0xff000000)>>24;
			r = (sdw&0x00ff0000)>>16;
			g = (sdw&0x0000ff00)>>8;
			b = sdw&0x000000ff;
			WORD shadow = ((a&0xf0)<<8)|((r&0xf0)<<4)|(g&0xf0)|((b&0xf0)>>4);	//	16bit化
			for(i = 0; i<ch; i++){
				pDst = (WORD *)lockRect.bits+(lockRect.pitch/2)*(y+i)+x;
				pSdw = (WORD *)lockRect.bits+(lockRect.pitch/2)*(y+i+1)+x+1;
				pSdw2 = pSdw-1;
				//pSdw3 = pDst+1;
				pSrc = (DWORD *)pDIB+w*(i+cy)+cx;
				if(i){
					*pDst = 0x00000000;
				}else{
					WORD *tmp = pDst;
					for(j = 0; j<=cw; j++) *tmp++ = 0x00000000;
				}
				for(int j = 0; j<cw; j++){
					if(*pSrc){
						*pDst = color; *pSdw = *pSdw2 = /* *pSdw3 = */ shadow;
					}else{
						*pSdw = /* *pSdw2 = *pSdw3 = */ 0x00000000;
					}
					pDst++; pSdw++; pSdw2++; /* pSdw3++; */ pSrc++;
				}
			}
		}else{
			for(i = 0; i<ch; i++){
				pDst = (WORD *)lockRect.bits+(lockRect.pitch/2)*(y+i)+x;
				pSrc = (DWORD *)pDIB+w*(i+cy)+cx;
				for(j = 0; j<cw; j++){
					*pDst = *pSrc ? color : 0x00000000;
					pDst++; pSrc++;
				}
			}
		}
		resource->Unlock();	//	ロック解除
	}
	//	後始末
	SelectObject(hDC, hOldBM);
	SelectObject(hDC, hOldFont);
	DeleteObject(hBM);
	DeleteDC(hDC);

	return TRUE;
}

/*
 *	2D画像としてレンダリング
 *
 *	x, y	: 描画位置
 *	更新するデバイスパラメータ	: テクスチャ
 *
 *	※事前にライティングOFFにすること。
 */
void CTexture::Render(int x, int y){
	if(!m_Texture.IsEmpty()){
		RS2BindTexture(0, m_Texture);
		TexMap2DRect(x, y, x+m_Width, y+m_Height);
	}
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
 *	コンストラクタ
 *
 *	※CMeshクラスからRelease()が呼ばれるのでデストラクタは不要
 */
CTexList::CTexList(){
	m_pList = NULL;
}
/*
 *	デストラクタ
 */
CTexList::~CTexList(){
	while(m_pList) Release(RS2TextureRef(m_pList->pTex));
}

/*
 *	テクスチャをリストから検索し、なければロード
 */
RS2TextureRef CTexList::Get(
	BOOL fRes, LPCSTR strName, RS2PackedColor cTrans, int nMipLv
){
	//	リストからテクスチャーを検索
	TEXINFO *p = m_pList;

	//	ファイルならフルパスに変換
	char full[_MAX_PATH];
	if(!fRes){
		_fullpath(full, strName, _MAX_PATH);
		strName = full;
	}

	if(!cTrans) cTrans = CheckTexTrans(strName);

	while(p){
		//	見つかれば参照カウンタを増やし、テクスチャを返す
		if(!_mbsicmp((PUCHAR)p->strName.c_str(), (PUCHAR)strName)
			&& p->cTrans==cTrans && p->nMipLv==nMipLv){
			//	Debug("[%s] is in texture-list.\n", strName); /*デバッグ*/
			p->nRef++;
			return RS2TextureRef(p->pTex);
		}
		p = p->pNext;
	}
	//	見つからなければロード
	Debug("load(%s) ... ", strName);

	p = new TEXINFO;

	//	[RS2EX] Loading policy stays here; the texture itself is created and
	//	owned as a CRS2TextureResource.  The list hands out references, and
	//	nRef below - not the reference - is still the lifetime contract.
	p->pTex = fRes
		? RS2CreateTextureFromResource(strName, cTrans, nMipLv)
		: RS2CreateTextureFromFile(strName, cTrans, nMipLv);
	if(!p->pTex){
		Debug("failed.\n");
		return RS2TextureRef();
	}
	Debug("ok.\n");

	//	リストの先頭に追加、参照カウンタを設定する
	TEXINFO *q = m_pList;
	m_pList = p;
	p->pNext = q;
	p->strName = strName;
	p->nRef = 1;
	p->cTrans = cTrans;
	p->nMipLv = nMipLv;

	return RS2TextureRef(p->pTex);
}

/*
 *	可能ならテクスチャを解放する
 */
void CTexList::Release(RS2TextureRef tex){
	//	テクスチャを検索
	const CRS2TextureResource *pTex = tex.GetResource();
	TEXINFO *p = m_pList;
	TEXINFO *q = NULL;

	while(p){
		//	見つかれば参照カウンタを減らす
		if(p->pTex==pTex){
			p->nRef--;

			//	参照がなくなればテクスチャを解放、リストから外す
			if(p->nRef==0){
				Debug("release(%s)\n", p->strName.c_str());
				RS2DestroyTexture(p->pTex);
				p->pTex = 0;

				if(p==m_pList){
					//	リストの先頭
					m_pList = p->pNext;
					delete p;
				}else{
					//	途中
					q->pNext = p->pNext;
					delete p;
				}
			}
			break;
		}
		q = p;
		p = p->pNext;
	}
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*
 *	テクスチャファイル名から透過色を求める
 */
RS2PackedColor CheckTexTrans(LPCSTR str){
	char *ptr = (char *)str, *sharp = NULL, *dot = NULL;
	while(*ptr){
		switch(*ptr){
		case '#':
			sharp = ptr;
			break;
		case '.':
			dot = ptr;
			break;
		case '\\':
			sharp = dot = NULL;
			break;
		}
		ptr = CharNext(ptr);
	}
	if(!sharp) return 0x00000000;
	if(!dot || dot<sharp) dot = ptr;
	int len = RS2DiffToInt(dot-sharp);
	if(len==2){
		switch(sharp[1]){
		case 'B': case 'b': return 0xff000000;
		case 'W': case 'w': return 0xffffffff;
		}
	}else if(len==9){
		RS2PackedColor trans;
		if(sscanf(sharp+1, "%x", &trans)==1) return trans;
	}
	return 0x00000000;
}

/*
 *	文字列描画サイズの計算
 */
void CalcTextRect(int *w, int *h, LPCSTR str, HFONT hFont){
	if(!*str){
		*w = *h = 0;
		return;
	}
	HDC hDC = CreateCompatibleDC(NULL);
	HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);
	SetMapMode(hDC, MM_TEXT);
	RECT drawRect = {0, 0, 1, 1};
	DrawText(hDC, str, -1, &drawRect, DT_CALCRECT|DT_LEFT|DT_EXPANDTABS|DT_NOPREFIX);
	*w = drawRect.right;
	*h = drawRect.bottom;
	SelectObject(hDC, hOldFont);
	DeleteDC(hDC);
}
