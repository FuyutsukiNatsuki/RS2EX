//	Modified for RS2EX on 2026-09-26.
//	Copyright (c) 2002 Midikyou

void Draw2DLine(int x1, int y1, int x2, int y2, RS2PackedColor c1 = 0xffffffff, RS2PackedColor c2 = 0);
void Draw3DLine(VEC3 p1, VEC3 p2, RS2PackedColor c1 = 0xffffffff, RS2PackedColor c2 = 0);
void Draw2DRect(int x1, int y1, int x2, int y2, RS2PackedColor c = 0xffffffff);
void Fill2DRect(int x1, int y1, int x2, int y2, RS2PackedColor c = 0xffffffff);
void Grad2DRect(int x1, int y1, int x2, int y2, RS2PackedColor *c);
void TexMap2DRect(int x1, int y1, int x2, int y2, RS2PackedColor c = 0xffffffff);
void TexMap2DRect90(int x1, int y1, int x2, int y2, RS2PackedColor c = 0xffffffff);
void Draw3DRect(VEC3 p1, VEC3 p2, VEC3 p3, VEC3 p4, RS2PackedColor c = 0xffffffff);
void Fill3DRect(VEC3 p1, VEC3 p2, VEC3 p3, VEC3 p4, RS2PackedColor c = 0xffffffff);
void TexMap3DRect(VEC3 p1, VEC3 p2, VEC3 p3, VEC3 p4, RS2PackedColor c = 0xffffffff);
void Tex2Map3DRect(VEC3 p1, VEC3 p2, VEC3 p3, VEC3 p4, RS2PackedColor c = 0xffffffff);
void Fill3DHex(VEC3 pos, float r, RS2PackedColor cc, RS2PackedColor ca = 0xffffffff);
void Draw3DCircle(VEC3 pos, float r, RS2PackedColor c = 0xffffffff);
void Fill3DCircle(VEC3 pos, float r, RS2PackedColor cc = 0xffffffff, RS2PackedColor ca = 0xffffffff);
void DrawBox(BOX8 *pB, RS2PackedColor c = 0xffffffff);

/*
 *	UV座標の指定
 *
 *	u1, v1	: 左端の値
 *	u2, v2	: 右端の値
 */
inline void SetUVMap(float u1, float v1, float u2, float v2){
	sv3.u[0] = u1, sv3.u[1] = u2;
	sv3.v[0] = v1, sv3.v[1] = v2;
}

/*
 *	座標軸を描画
 *
 *	s		: 軸の長さ
 */
inline void DrawAxis(float s = 5){
	Draw3DLine(VEC3(0, 0, 0), VEC3(s, 0, 0), 0xffff0000);
	Draw3DLine(VEC3(0, 0, 0), VEC3(0, s, 0), 0xff00ff00);
	Draw3DLine(VEC3(0, 0, 0), VEC3(0, 0, s), 0xff0000ff);
}
