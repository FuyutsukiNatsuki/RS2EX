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
	RS2BindTexture(0, RS2TextureRef());
	RS2SetLighting(false);
	RS2SetDepthTest(true);
	RS2SetDepthWrite(false);
	RS2SetDepthFunc(RS2_COMPARE_ALWAYS);
	::DrawBox(&box, ScaleColor(0x40ff0000, g_BlinkAlpha));
	RS2SetDepthFunc(RS2_COMPARE_LESS_EQUAL);
	::DrawBox(&box, ScaleColor(0xffff0000, g_BlinkAlpha));
	RS2SetDepthWrite(true);
}
