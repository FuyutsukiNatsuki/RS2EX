//	Copyright (c) 2002 Midikyou
//	Modified for RS2EX on 2026-09-20, 2026-09-21.

#include "..\RS2RenderState.h"

/*
 *	変換行列リセット
 */
inline void devResetMatrix(){
	devTransform(&sv3.mtxFront);
}

//	[RS2EX] devSetMaterial() removed in v0.0.7.  Every caller now goes
//	through RS2SetMaterial(); keeping a native material binder that nothing
//	calls would only invite new ones.

void devResetMaterial();
void devSetLineMaterial();

//	[RS2EX] The render-state wrappers that used to live here moved to
//	RS2RenderState.h in v0.0.8.  devResetMatrix() and the two material
//	declarations above are not render state and stay.
