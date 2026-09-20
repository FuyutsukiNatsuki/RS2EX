//	Modified for RS2EX on 2026-09-21.
#include "stdafx.h"
#include "CPartsInst.h"

/*
 *	[static]
 *	フォーカスボックス描画
 */
void CPartsInst::DrawBox(){
	BOX8 box = m_Object.GetBox();
	devResetMatrix();
	devSetTexture(0, NULL);
	RS2SetLighting(false);
	RS2SetDepthTest(true);
	RS2SetDepthWrite(false);
	devSetState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	::DrawBox(&box, ScaleColor(0x40ff0000, g_BlinkAlpha));
	devSetState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	::DrawBox(&box, ScaleColor(0xffff0000, g_BlinkAlpha));
	RS2SetDepthWrite(true);
}
