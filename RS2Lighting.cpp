//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//	Modified for RS2EX on 2026-09-22, 2026-09-23.
//
//	The engine half of the scene light.  Owns the values; the backend owns the
//	submission.  Provenance: the normalisation, the diffuse-equals-specular
//	rule and the unconditional slot-0 enable are inherited from
//	SetDirLight() in lib/light.cpp (Copyright (c) 2002 Midikyou).

#include "stdafx.h"
#include "RS2Renderer.h"
#include "RS2D3D12Unsupported.h"
#include "RS2Lighting.h"
#include "RS2D3D8Lighting.h"
#include "RS2LightingAudit.h"
#include "RS2D3D12Draw.h"

#include <math.h>

static RS2DirectionalLight s_Light;
static bool s_Enabled = false;

void RS2SetDirectionalLight(const RS2Direction &direction, const RS2Color4 &color){
	const float len = (float)sqrt(
		direction.x*direction.x + direction.y*direction.y + direction.z*direction.z);

	//	D3DXVec3Normalize leaves a zero vector alone rather than producing NaN.
	//	Nothing passes one today, but matching that is free.
	if(len>0.0f){
		s_Light.direction = RS2MakeDirection(
			direction.x/len, direction.y/len, direction.z/len);
	}else{
		s_Light.direction = direction;
	}

	s_Light.color = color;
	s_Enabled = true;
	RS2LightingAuditDirectionalLight(s_Light);

	//	[RS2EX] The value above is engine state and is kept whatever backend
	//	is running - the shadow code and the sun read it.  Only the
	//	submission is a backend matter.
	if(GetRS2Renderer().GetBackendType()==RS2_RENDERER_D3D8)
		RS2D3D8_SubmitDirectionalLight(s_Light);
	else RS2D3D12_SubmitDirectionalLight(s_Light);
}

const RS2DirectionalLight &RS2GetDirectionalLight(){
	return s_Light;
}

bool RS2IsDirectionalLightEnabled(){
	return s_Enabled;
}
