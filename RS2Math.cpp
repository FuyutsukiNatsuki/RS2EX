//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Vector / matrix math (v0.2.0): see RS2Math.h.  The formulas are the ones
//	the DirectX 8 documentation gives for the D3DX functions of the same
//	names; the code is RS2's own.

#include "RS2Math.h"

#include <string.h>

RS2Vector2 *RS2Vec2Normalize(RS2Vector2 *out, const RS2Vector2 *v){
	const float l = RS2Vec2Length(v);

	if(l==0.0f){
		out->x = out->y = 0.0f;
	}else{
		const float inv = 1.0f/l;

		out->x = v->x*inv;
		out->y = v->y*inv;
	}
	return out;
}

RS2Vector3 *RS2Vec3Normalize(RS2Vector3 *out, const RS2Vector3 *v){
	const float l = RS2Vec3Length(v);

	if(l==0.0f){
		out->x = out->y = out->z = 0.0f;
	}else{
		const float inv = 1.0f/l;

		out->x = v->x*inv;
		out->y = v->y*inv;
		out->z = v->z*inv;
	}
	return out;
}

RS2Vector3 *RS2Vec3TransformCoord(RS2Vector3 *out, const RS2Vector3 *v, const RS2Matrix *m){
	const float x = v->x*m->_11+v->y*m->_21+v->z*m->_31+m->_41;
	const float y = v->x*m->_12+v->y*m->_22+v->z*m->_32+m->_42;
	const float z = v->x*m->_13+v->y*m->_23+v->z*m->_33+m->_43;
	const float w = v->x*m->_14+v->y*m->_24+v->z*m->_34+m->_44;
	//	No guard: D3DX8 divided by a zero w too, and callers have always
	//	received NaN for it (measured), which fails every comparison.
	if(w==0.0f){
		const float nan = sqrtf(-1.0f);

		out->x = out->y = out->z = nan;
		return out;
	}
	const float inv = 1.0f/w;

	out->x = x*inv;
	out->y = y*inv;
	out->z = z*inv;
	return out;
}

RS2Vector3 *RS2Vec3TransformNormal(RS2Vector3 *out, const RS2Vector3 *v, const RS2Matrix *m){
	const float x = v->x*m->_11+v->y*m->_21+v->z*m->_31;
	const float y = v->x*m->_12+v->y*m->_22+v->z*m->_32;
	const float z = v->x*m->_13+v->y*m->_23+v->z*m->_33;

	out->x = x;
	out->y = y;
	out->z = z;
	return out;
}

RS2Matrix *RS2MatrixIdentity(RS2Matrix *out){
	*out = RS2Matrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	return out;
}

RS2Matrix *RS2MatrixTranslation(RS2Matrix *out, float x, float y, float z){
	RS2MatrixIdentity(out);
	out->_41 = x;
	out->_42 = y;
	out->_43 = z;
	return out;
}

RS2Matrix *RS2MatrixScaling(RS2Matrix *out, float x, float y, float z){
	RS2MatrixIdentity(out);
	out->_11 = x;
	out->_22 = y;
	out->_33 = z;
	return out;
}

RS2Matrix *RS2MatrixRotationX(RS2Matrix *out, float angle){
	const float c = cosf(angle), s = sinf(angle);

	RS2MatrixIdentity(out);
	out->_22 = c;
	out->_23 = s;
	out->_32 = -s;
	out->_33 = c;
	return out;
}

RS2Matrix *RS2MatrixRotationY(RS2Matrix *out, float angle){
	const float c = cosf(angle), s = sinf(angle);

	RS2MatrixIdentity(out);
	out->_11 = c;
	out->_13 = -s;
	out->_31 = s;
	out->_33 = c;
	return out;
}

RS2Matrix *RS2MatrixRotationZ(RS2Matrix *out, float angle){
	const float c = cosf(angle), s = sinf(angle);

	RS2MatrixIdentity(out);
	out->_11 = c;
	out->_12 = s;
	out->_21 = -s;
	out->_22 = c;
	return out;
}

RS2Matrix *RS2MatrixRotationAxis(RS2Matrix *out, const RS2Vector3 *axis, float angle){
	RS2Vector3 n;

	RS2Vec3Normalize(&n, axis);

	const float c = cosf(angle), s = sinf(angle), t = 1.0f-c;

	RS2MatrixIdentity(out);
	out->_11 = t*n.x*n.x+c;
	out->_12 = t*n.x*n.y+s*n.z;
	out->_13 = t*n.x*n.z-s*n.y;
	out->_21 = t*n.x*n.y-s*n.z;
	out->_22 = t*n.y*n.y+c;
	out->_23 = t*n.y*n.z+s*n.x;
	out->_31 = t*n.x*n.z+s*n.y;
	out->_32 = t*n.y*n.z-s*n.x;
	out->_33 = t*n.z*n.z+c;
	return out;
}

RS2Matrix *RS2MatrixRotationYawPitchRoll(RS2Matrix *out, float yaw, float pitch, float roll){
	RS2Matrix z, x, y;

	RS2MatrixRotationZ(&z, roll);
	RS2MatrixRotationX(&x, pitch);
	RS2MatrixRotationY(&y, yaw);
	*out = z*x*y;
	return out;
}

RS2Matrix *RS2MatrixRotationQuaternion(RS2Matrix *out, const RS2Quaternion *q){
	const float x = q->x, y = q->y, z = q->z, w = q->w;

	RS2MatrixIdentity(out);
	out->_11 = 1.0f-2.0f*(y*y+z*z);
	out->_12 = 2.0f*(x*y+z*w);
	out->_13 = 2.0f*(x*z-y*w);
	out->_21 = 2.0f*(x*y-z*w);
	out->_22 = 1.0f-2.0f*(x*x+z*z);
	out->_23 = 2.0f*(y*z+x*w);
	out->_31 = 2.0f*(x*z+y*w);
	out->_32 = 2.0f*(y*z-x*w);
	out->_33 = 1.0f-2.0f*(x*x+y*y);
	return out;
}

RS2Matrix *RS2MatrixLookAtLH(RS2Matrix *out, const RS2Vector3 *eye, const RS2Vector3 *at, const RS2Vector3 *up){
	RS2Vector3 zaxis = *at-*eye, xaxis, yaxis;

	RS2Vec3Normalize(&zaxis, &zaxis);
	RS2Vec3Cross(&xaxis, up, &zaxis);
	RS2Vec3Normalize(&xaxis, &xaxis);
	RS2Vec3Cross(&yaxis, &zaxis, &xaxis);

	*out = RS2Matrix(
		xaxis.x, yaxis.x, zaxis.x, 0.0f,
		xaxis.y, yaxis.y, zaxis.y, 0.0f,
		xaxis.z, yaxis.z, zaxis.z, 0.0f,
		-RS2Vec3Dot(&xaxis, eye), -RS2Vec3Dot(&yaxis, eye), -RS2Vec3Dot(&zaxis, eye), 1.0f);
	return out;
}

RS2Matrix *RS2MatrixPerspectiveFovLH(RS2Matrix *out, float fovy, float aspect, float zn, float zf){
	const float ys = 1.0f/tanf(fovy*0.5f);
	const float xs = ys/aspect;
	const float q = zf/(zf-zn);

	*out = RS2Matrix(
		xs, 0.0f, 0.0f, 0.0f,
		0.0f, ys, 0.0f, 0.0f,
		0.0f, 0.0f, q, 1.0f,
		0.0f, 0.0f, -zn*q, 0.0f);
	return out;
}

RS2Matrix *RS2MatrixPerspectiveOffCenterLH(RS2Matrix *out, float l, float r, float b, float t, float zn, float zf){
	*out = RS2Matrix(
		2.0f*zn/(r-l), 0.0f, 0.0f, 0.0f,
		0.0f, 2.0f*zn/(t-b), 0.0f, 0.0f,
		(l+r)/(l-r), (t+b)/(b-t), zf/(zf-zn), 1.0f,
		0.0f, 0.0f, zn*zf/(zn-zf), 0.0f);
	return out;
}

RS2Matrix *RS2MatrixInverse(RS2Matrix *out, float *determinant, const RS2Matrix *src){
	//	Cofactors of the 4x4, in float; D3DX8 did the same arithmetic in its own order.
	const float *a = &src->_11;
	float inv[16];

	inv[0] = a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
	inv[4] = -a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
	inv[8] = a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
	inv[12] = -a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
	inv[1] = -a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
	inv[5] = a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
	inv[9] = -a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
	inv[13] = a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
	inv[2] = a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6];
	inv[6] = -a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
	inv[10] = a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5];
	inv[14] = -a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
	inv[3] = -a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6];
	inv[7] = a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
	inv[11] = -a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5];
	inv[15] = a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];

	const float det = a[0]*inv[0]+a[1]*inv[4]+a[2]*inv[8]+a[3]*inv[12];

	if(determinant) *determinant = det;
	if(det==0.0f) return 0;

	const float r = 1.0f/det;
	int i;

	for(i = 0; i<16; i++) (&out->_11)[i] = inv[i]*r;
	return out;
}

RS2Plane *RS2PlaneFromPoints(RS2Plane *out, const RS2Vector3 *a, const RS2Vector3 *b, const RS2Vector3 *c){
	const RS2Vector3 e1 = *b-*a, e2 = *c-*a;
	RS2Vector3 n;

	RS2Vec3Cross(&n, &e1, &e2);
	//	Divided by the length unguarded, as D3DX8 did: three points on a line
	//	give a NaN plane, which every comparison against it then rejects.
	const float l = RS2Vec3Length(&n);

	out->a = n.x/l;
	out->b = n.y/l;
	out->c = n.z/l;
	out->d = -(out->a*a->x+out->b*a->y+out->c*a->z);
	return out;
}

RS2Quaternion *RS2QuaternionSlerp(RS2Quaternion *out, const RS2Quaternion *a, const RS2Quaternion *b, float t){
	float cosine = a->x*b->x+a->y*b->y+a->z*b->z+a->w*b->w;
	float sign = 1.0f;

	//	The shorter way round.
	if(cosine<0.0f){
		cosine = -cosine;
		sign = -1.0f;
	}

	float wa = 1.0f-t, wb = t;

	if(1.0f-cosine>0.001f){
		const float theta = acosf(cosine), s = sinf(theta);

		wa = sinf((1.0f-t)*theta)/s;
		wb = sinf(t*theta)/s;
	}
	wb *= sign;
	*out = RS2Quaternion(
		wa*a->x+wb*b->x, wa*a->y+wb*b->y, wa*a->z+wb*b->z, wa*a->w+wb*b->w);
	return out;
}

bool RS2SphereBoundProbe(const RS2Vector3 *center, float radius, const RS2Vector3 *origin, const RS2Vector3 *direction){
	//	|origin + t * direction - center| = radius for some t >= 0.
	const RS2Vector3 d = *origin-*center;
	const float a = RS2Vec3LengthSq(direction);
	const float b = RS2Vec3Dot(&d, direction);
	const float c = RS2Vec3LengthSq(&d)-radius*radius;
	const float disc = b*b-a*c;

	//	A hit (or a graze) in front of the origin: the larger root
	//	-b + sqrt(disc) is not negative.  D3DX8 counted a tangent ray as a hit.
	return disc>=0.0f && sqrtf(disc)>=b;
}

bool RS2BoxBoundProbe(const RS2Vector3 *boxMin, const RS2Vector3 *boxMax, const RS2Vector3 *origin, const RS2Vector3 *direction){
	//	Slabs: the ray's parameter interval inside each pair of planes.
	const float *lo = &boxMin->x, *hi = &boxMax->x, *o = &origin->x, *d = &direction->x;
	float tmin = -1e30f, tmax = 1e30f;
	int i;

	for(i = 0; i<3; i++){
		if(d[i]==0.0f){
			if(o[i]<lo[i] || o[i]>hi[i]) return false;
			continue;
		}

		const float inv = 1.0f/d[i];
		float t1 = (lo[i]-o[i])*inv, t2 = (hi[i]-o[i])*inv;

		if(t1>t2){ const float s = t1; t1 = t2; t2 = s; }
		if(t1>tmin) tmin = t1;
		if(t2<tmax) tmax = t2;
		if(tmin>tmax) return false;
	}
	return tmax>=0.0f;
}
