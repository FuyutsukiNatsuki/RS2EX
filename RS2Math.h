//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	Vector / matrix / quaternion / plane math (v0.2.0).
//
//	RailSim II used D3DX8's math types as its own - VEC3 was D3DXVECTOR3, MTX4
//	was D3DXMATRIX - and D3DX8's functions for the operations on them.  v0.2.0
//	removes D3DX8, so these are RS2's: the same memory layout (plain floats,
//	x / y / z / w, _11 ... _44 and m[4][4]), the same operators, and functions
//	that compute what the D3DX functions documented, in float, with the same
//	conventions (row vectors, v * M; left-handed; matrices applied left to
//	right).  D3DX8 itself had several CPU-specific paths, so its results were
//	never bit-identical across machines; these are the straightforward
//	formulas.
//
//	The function names are D3DX's with RS2 in place of D3DX, so the game code
//	that calls them reads as it did.

#ifndef RS2MATH_H_INCLUDED
#define RS2MATH_H_INCLUDED

#include <math.h>

#define RS2_PI			3.141592654f
#define RS2ToRadian(degree)	((degree)*(RS2_PI/180.0f))
#define RS2ToDegree(radian)	((radian)*(180.0f/RS2_PI))

////////////////////////////////////////////////////////////////////////////////
//	Types
////////////////////////////////////////////////////////////////////////////////

struct RS2Vector2
{
	float x, y;

	RS2Vector2(){}
	RS2Vector2(const float *f): x(f[0]), y(f[1]){}
	RS2Vector2(float fx, float fy): x(fx), y(fy){}

	operator float *(){ return &x; }
	operator const float *() const{ return &x; }

	RS2Vector2 &operator+=(const RS2Vector2 &v){ x += v.x; y += v.y; return *this; }
	RS2Vector2 &operator-=(const RS2Vector2 &v){ x -= v.x; y -= v.y; return *this; }
	RS2Vector2 &operator*=(float f){ x *= f; y *= f; return *this; }
	RS2Vector2 &operator/=(float f){ const float inv = 1.0f/f; x *= inv; y *= inv; return *this; }

	RS2Vector2 operator+() const{ return *this; }
	RS2Vector2 operator-() const{ return RS2Vector2(-x, -y); }
	RS2Vector2 operator+(const RS2Vector2 &v) const{ return RS2Vector2(x+v.x, y+v.y); }
	RS2Vector2 operator-(const RS2Vector2 &v) const{ return RS2Vector2(x-v.x, y-v.y); }
	RS2Vector2 operator*(float f) const{ return RS2Vector2(x*f, y*f); }
	RS2Vector2 operator/(float f) const{ const float inv = 1.0f/f; return RS2Vector2(x*inv, y*inv); }
	friend RS2Vector2 operator*(float f, const RS2Vector2 &v){ return RS2Vector2(f*v.x, f*v.y); }

	bool operator==(const RS2Vector2 &v) const{ return x==v.x && y==v.y; }
	bool operator!=(const RS2Vector2 &v) const{ return x!=v.x || y!=v.y; }
};

struct RS2Vector3
{
	float x, y, z;

	RS2Vector3(){}
	RS2Vector3(const float *f): x(f[0]), y(f[1]), z(f[2]){}
	RS2Vector3(float fx, float fy, float fz): x(fx), y(fy), z(fz){}

	operator float *(){ return &x; }
	operator const float *() const{ return &x; }

	RS2Vector3 &operator+=(const RS2Vector3 &v){ x += v.x; y += v.y; z += v.z; return *this; }
	RS2Vector3 &operator-=(const RS2Vector3 &v){ x -= v.x; y -= v.y; z -= v.z; return *this; }
	RS2Vector3 &operator*=(float f){ x *= f; y *= f; z *= f; return *this; }
	RS2Vector3 &operator/=(float f){ const float inv = 1.0f/f; x *= inv; y *= inv; z *= inv; return *this; }

	RS2Vector3 operator+() const{ return *this; }
	RS2Vector3 operator-() const{ return RS2Vector3(-x, -y, -z); }
	RS2Vector3 operator+(const RS2Vector3 &v) const{ return RS2Vector3(x+v.x, y+v.y, z+v.z); }
	RS2Vector3 operator-(const RS2Vector3 &v) const{ return RS2Vector3(x-v.x, y-v.y, z-v.z); }
	RS2Vector3 operator*(float f) const{ return RS2Vector3(x*f, y*f, z*f); }
	RS2Vector3 operator/(float f) const{ const float inv = 1.0f/f; return RS2Vector3(x*inv, y*inv, z*inv); }
	friend RS2Vector3 operator*(float f, const RS2Vector3 &v){ return RS2Vector3(f*v.x, f*v.y, f*v.z); }

	bool operator==(const RS2Vector3 &v) const{ return x==v.x && y==v.y && z==v.z; }
	bool operator!=(const RS2Vector3 &v) const{ return x!=v.x || y!=v.y || z!=v.z; }
};

struct RS2Vector4
{
	float x, y, z, w;

	RS2Vector4(){}
	RS2Vector4(const float *f): x(f[0]), y(f[1]), z(f[2]), w(f[3]){}
	RS2Vector4(float fx, float fy, float fz, float fw): x(fx), y(fy), z(fz), w(fw){}

	operator float *(){ return &x; }
	operator const float *() const{ return &x; }

	RS2Vector4 &operator+=(const RS2Vector4 &v){ x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
	RS2Vector4 &operator-=(const RS2Vector4 &v){ x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
	RS2Vector4 &operator*=(float f){ x *= f; y *= f; z *= f; w *= f; return *this; }
	RS2Vector4 &operator/=(float f){ const float inv = 1.0f/f; x *= inv; y *= inv; z *= inv; w *= inv; return *this; }

	RS2Vector4 operator+() const{ return *this; }
	RS2Vector4 operator-() const{ return RS2Vector4(-x, -y, -z, -w); }
	RS2Vector4 operator+(const RS2Vector4 &v) const{ return RS2Vector4(x+v.x, y+v.y, z+v.z, w+v.w); }
	RS2Vector4 operator-(const RS2Vector4 &v) const{ return RS2Vector4(x-v.x, y-v.y, z-v.z, w-v.w); }
	RS2Vector4 operator*(float f) const{ return RS2Vector4(x*f, y*f, z*f, w*f); }
	RS2Vector4 operator/(float f) const{ const float inv = 1.0f/f; return RS2Vector4(x*inv, y*inv, z*inv, w*inv); }
	friend RS2Vector4 operator*(float f, const RS2Vector4 &v){ return RS2Vector4(f*v.x, f*v.y, f*v.z, f*v.w); }

	bool operator==(const RS2Vector4 &v) const{ return x==v.x && y==v.y && z==v.z && w==v.w; }
	bool operator!=(const RS2Vector4 &v) const{ return !(*this==v); }
};

struct RS2Matrix
{
	union{
		struct{
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float m[4][4];
	};

	RS2Matrix(){}
	RS2Matrix(const float *f){ int i; for(i = 0; i<16; i++) (&_11)[i] = f[i]; }
	RS2Matrix(
		float f11, float f12, float f13, float f14,
		float f21, float f22, float f23, float f24,
		float f31, float f32, float f33, float f34,
		float f41, float f42, float f43, float f44
	){
		_11 = f11; _12 = f12; _13 = f13; _14 = f14;
		_21 = f21; _22 = f22; _23 = f23; _24 = f24;
		_31 = f31; _32 = f32; _33 = f33; _34 = f34;
		_41 = f41; _42 = f42; _43 = f43; _44 = f44;
	}

	float &operator()(unsigned int row, unsigned int col){ return m[row][col]; }
	float operator()(unsigned int row, unsigned int col) const{ return m[row][col]; }

	operator float *(){ return &_11; }
	operator const float *() const{ return &_11; }

	RS2Matrix operator*(const RS2Matrix &b) const{
		RS2Matrix r;
		int i, j;

		for(i = 0; i<4; i++){
			for(j = 0; j<4; j++){
				r.m[i][j] = m[i][0]*b.m[0][j]+m[i][1]*b.m[1][j]+m[i][2]*b.m[2][j]+m[i][3]*b.m[3][j];
			}
		}
		return r;
	}
	RS2Matrix &operator*=(const RS2Matrix &b){ *this = *this*b; return *this; }
	RS2Matrix &operator+=(const RS2Matrix &b){ int i; for(i = 0; i<16; i++) (&_11)[i] += (&b._11)[i]; return *this; }
	RS2Matrix &operator-=(const RS2Matrix &b){ int i; for(i = 0; i<16; i++) (&_11)[i] -= (&b._11)[i]; return *this; }
	RS2Matrix &operator*=(float f){ int i; for(i = 0; i<16; i++) (&_11)[i] *= f; return *this; }
	RS2Matrix &operator/=(float f){ const float inv = 1.0f/f; int i; for(i = 0; i<16; i++) (&_11)[i] *= inv; return *this; }

	RS2Matrix operator+() const{ return *this; }
	RS2Matrix operator-() const{ RS2Matrix r(*this); r *= -1.0f; return r; }
	RS2Matrix operator+(const RS2Matrix &b) const{ RS2Matrix r(*this); r += b; return r; }
	RS2Matrix operator-(const RS2Matrix &b) const{ RS2Matrix r(*this); r -= b; return r; }
	RS2Matrix operator*(float f) const{ RS2Matrix r(*this); r *= f; return r; }
	RS2Matrix operator/(float f) const{ RS2Matrix r(*this); r /= f; return r; }
	friend RS2Matrix operator*(float f, const RS2Matrix &a){ return a*f; }

	bool operator==(const RS2Matrix &b) const{ int i; for(i = 0; i<16; i++) if((&_11)[i]!=(&b._11)[i]) return false; return true; }
	bool operator!=(const RS2Matrix &b) const{ return !(*this==b); }
};

struct RS2Quaternion
{
	float x, y, z, w;

	RS2Quaternion(){}
	RS2Quaternion(const float *f): x(f[0]), y(f[1]), z(f[2]), w(f[3]){}
	RS2Quaternion(float fx, float fy, float fz, float fw): x(fx), y(fy), z(fz), w(fw){}

	operator float *(){ return &x; }
	operator const float *() const{ return &x; }

	RS2Quaternion &operator+=(const RS2Quaternion &q){ x += q.x; y += q.y; z += q.z; w += q.w; return *this; }
	RS2Quaternion &operator-=(const RS2Quaternion &q){ x -= q.x; y -= q.y; z -= q.z; w -= q.w; return *this; }
	RS2Quaternion &operator*=(float f){ x *= f; y *= f; z *= f; w *= f; return *this; }
	RS2Quaternion &operator/=(float f){ const float inv = 1.0f/f; x *= inv; y *= inv; z *= inv; w *= inv; return *this; }

	RS2Quaternion operator+() const{ return *this; }
	RS2Quaternion operator-() const{ return RS2Quaternion(-x, -y, -z, -w); }
	RS2Quaternion operator+(const RS2Quaternion &q) const{ return RS2Quaternion(x+q.x, y+q.y, z+q.z, w+q.w); }
	RS2Quaternion operator-(const RS2Quaternion &q) const{ return RS2Quaternion(x-q.x, y-q.y, z-q.z, w-q.w); }
	RS2Quaternion operator*(float f) const{ return RS2Quaternion(x*f, y*f, z*f, w*f); }
	RS2Quaternion operator/(float f) const{ const float inv = 1.0f/f; return RS2Quaternion(x*inv, y*inv, z*inv, w*inv); }
	friend RS2Quaternion operator*(float f, const RS2Quaternion &q){ return q*f; }

	bool operator==(const RS2Quaternion &q) const{ return x==q.x && y==q.y && z==q.z && w==q.w; }
	bool operator!=(const RS2Quaternion &q) const{ return !(*this==q); }
};

struct RS2Plane
{
	float a, b, c, d;

	RS2Plane(){}
	RS2Plane(float fa, float fb, float fc, float fd): a(fa), b(fb), c(fc), d(fd){}
};

////////////////////////////////////////////////////////////////////////////////
//	Vectors
////////////////////////////////////////////////////////////////////////////////

inline float RS2Vec2Length(const RS2Vector2 *v){ return sqrtf(v->x*v->x+v->y*v->y); }
inline float RS2Vec2Dot(const RS2Vector2 *a, const RS2Vector2 *b){ return a->x*b->x+a->y*b->y; }
RS2Vector2 *RS2Vec2Normalize(RS2Vector2 *out, const RS2Vector2 *v);

inline float RS2Vec3Length(const RS2Vector3 *v){ return sqrtf(v->x*v->x+v->y*v->y+v->z*v->z); }
inline float RS2Vec3LengthSq(const RS2Vector3 *v){ return v->x*v->x+v->y*v->y+v->z*v->z; }
inline float RS2Vec3Dot(const RS2Vector3 *a, const RS2Vector3 *b){ return a->x*b->x+a->y*b->y+a->z*b->z; }
inline RS2Vector3 *RS2Vec3Cross(RS2Vector3 *out, const RS2Vector3 *a, const RS2Vector3 *b){
	const RS2Vector3 r(a->y*b->z-a->z*b->y, a->z*b->x-a->x*b->z, a->x*b->y-a->y*b->x);

	*out = r;
	return out;
}
RS2Vector3 *RS2Vec3Normalize(RS2Vector3 *out, const RS2Vector3 *v);
//	(x, y, z, 1) * M, divided by the resulting w
RS2Vector3 *RS2Vec3TransformCoord(RS2Vector3 *out, const RS2Vector3 *v, const RS2Matrix *m);
//	(x, y, z, 0) * M
RS2Vector3 *RS2Vec3TransformNormal(RS2Vector3 *out, const RS2Vector3 *v, const RS2Matrix *m);

////////////////////////////////////////////////////////////////////////////////
//	Matrices
////////////////////////////////////////////////////////////////////////////////

RS2Matrix *RS2MatrixIdentity(RS2Matrix *out);
RS2Matrix *RS2MatrixTranslation(RS2Matrix *out, float x, float y, float z);
RS2Matrix *RS2MatrixScaling(RS2Matrix *out, float x, float y, float z);
RS2Matrix *RS2MatrixRotationX(RS2Matrix *out, float angle);
RS2Matrix *RS2MatrixRotationY(RS2Matrix *out, float angle);
RS2Matrix *RS2MatrixRotationZ(RS2Matrix *out, float angle);
RS2Matrix *RS2MatrixRotationAxis(RS2Matrix *out, const RS2Vector3 *axis, float angle);
//	roll about z, then pitch about x, then yaw about y
RS2Matrix *RS2MatrixRotationYawPitchRoll(RS2Matrix *out, float yaw, float pitch, float roll);
RS2Matrix *RS2MatrixRotationQuaternion(RS2Matrix *out, const RS2Quaternion *q);
RS2Matrix *RS2MatrixLookAtLH(RS2Matrix *out, const RS2Vector3 *eye, const RS2Vector3 *at, const RS2Vector3 *up);
RS2Matrix *RS2MatrixPerspectiveFovLH(RS2Matrix *out, float fovy, float aspect, float zn, float zf);
RS2Matrix *RS2MatrixPerspectiveOffCenterLH(RS2Matrix *out, float l, float r, float b, float t, float zn, float zf);
//	NULL (out untouched) when m is singular; the determinant goes to *determinant if given
RS2Matrix *RS2MatrixInverse(RS2Matrix *out, float *determinant, const RS2Matrix *m);

////////////////////////////////////////////////////////////////////////////////
//	Planes, quaternions, probes
////////////////////////////////////////////////////////////////////////////////

//	The plane through three points, unit normal (b - a) x (c - a).
RS2Plane *RS2PlaneFromPoints(RS2Plane *out, const RS2Vector3 *a, const RS2Vector3 *b, const RS2Vector3 *c);
inline float RS2PlaneDotCoord(const RS2Plane *p, const RS2Vector3 *v){ return p->a*v->x+p->b*v->y+p->c*v->z+p->d; }

RS2Quaternion *RS2QuaternionSlerp(RS2Quaternion *out, const RS2Quaternion *a, const RS2Quaternion *b, float t);

//	Does the ray (origin, direction) hit the sphere / the axis-aligned box?
bool RS2SphereBoundProbe(const RS2Vector3 *center, float radius, const RS2Vector3 *origin, const RS2Vector3 *direction);
bool RS2BoxBoundProbe(const RS2Vector3 *boxMin, const RS2Vector3 *boxMax, const RS2Vector3 *origin, const RS2Vector3 *direction);

#endif	//	RS2MATH_H_INCLUDED
