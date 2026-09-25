//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	The .x importer (v0.2.0): text .x files into RS2-owned mesh data, without
//	Direct3D.
//
//	It replaces D3DXLoadMeshFromX + ID3DXMesh::Optimize, and has to produce
//	what those produced, because that is what every model has looked like for
//	twenty years.  The rules below were measured against the D3DX8 importer on
//	the 264 files of the accepted corpus and on synthetic files
//	(docs v0.2.0-x-import-contract, tools/ximport_reference.py is the same
//	model in Python; tools/ximport_compare.py checks both against the frozen
//	D3DX8 output):
//
//	- Every Mesh in the file, in depth-first file order, transformed by the
//	  Frames above it (row vectors, innermost matrix first; normals by the same
//	  matrix, then normalised), merged into one mesh; materials are
//	  concatenated per mesh.
//	- Positions: correctly rounded float, transformed, then one ulp toward
//	  zero.  UVs and material values: correctly rounded.  Normals: correctly
//	  rounded, transformed, normalised (x / length in float).
//	- Vertices split by normal: the original vertex is reused for the same
//	  normal index or the same normalised value, a split copy only for the
//	  same value, and a zero-length normal matches nothing by value.
//	- Polygons: fans (0,1,2), (0,2,3) ...; a triangle whose three vertices are
//	  not distinct after the split is dropped.
//	- Faces sorted by material, stably (the file's face order inside each
//	  material - D3DX8's vertex-cache order is not reproduced, by decision);
//	  a vertex shared by two materials becomes one vertex per material.
//	- Bounds: min / max of the merged vertices before sorting, all but the
//	  last (D3DX8's D3DXComputeBoundingBox never read the last vertex).
//	- Texture names: the string, with "\\" read as "\".
//
//	Supported: text format (xof ....txt), the templates Mesh, MeshNormals,
//	MeshTextureCoords, MeshVertexColors, MeshMaterialList, Material,
//	TextureFilename, Frame, FrameTransformMatrix; Header and template
//	declarations are skipped.  Binary and compressed files are refused by
//	name; references { name } are refused; other templates are skipped and
//	logged.

#include "stdafx.h"
#include "RS2MeshImport.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

////////////////////////////////////////////////////////////////////////////////
//	Tokens and the object tree
////////////////////////////////////////////////////////////////////////////////

struct XValue
{
	bool isString;
	double number;
	std::string text;
};

struct XObject
{
	std::string type, name;
	std::vector<XValue> values;
	std::vector<XObject *> children;

	~XObject(){
		size_t i;
		for(i = 0; i<children.size(); i++) delete children[i];
	}
	const XObject *Child(const char *t) const{
		size_t i;
		for(i = 0; i<children.size(); i++) if(children[i]->type==t) return children[i];
		return 0;
	}
};

class XParser
{
	const char *m_Pos, *m_End;
	std::string m_Error;

	bool IsNameStart(char c) const{ return (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_'; }
	bool IsNameChar(char c) const{ return IsNameStart(c) || (c>='0' && c<='9') || c=='-'; }
	bool IsNumberStart(char c) const{ return (c>='0' && c<='9') || c=='-' || c=='+' || c=='.'; }

	void SkipSpace(){
		while(m_Pos<m_End){
			const char c = *m_Pos;

			if(c==' ' || c=='\t' || c=='\r' || c=='\n'){
				m_Pos++;
			}else if(c=='#' || (c=='/' && m_Pos+1<m_End && m_Pos[1]=='/')){
				while(m_Pos<m_End && *m_Pos!='\n') m_Pos++;
			}else{
				break;
			}
		}
	}

	bool Fail(const char *what){
		if(m_Error.empty()) m_Error = what;
		return false;
	}

	bool ReadName(std::string *out){
		SkipSpace();
		if(m_Pos>=m_End || !IsNameStart(*m_Pos)) return false;

		const char *b = m_Pos;

		while(m_Pos<m_End && IsNameChar(*m_Pos)) m_Pos++;
		out->assign(b, m_Pos);
		return true;
	}

	//	template Name { <guid> members ... [restrictions] }
	bool SkipTemplate(){
		std::string name;

		if(!ReadName(&name)) return Fail("template without a name");
		SkipSpace();
		if(m_Pos>=m_End || *m_Pos!='{') return Fail("template without a body");

		int depth = 0;

		while(m_Pos<m_End){
			if(*m_Pos=='{') depth++;
			else if(*m_Pos=='}' && --depth==0){ m_Pos++; return true; }
			m_Pos++;
		}
		return Fail("template not closed");
	}

	bool ReadNumber(double *out){
		char *end = 0;

		*out = strtod(m_Pos, &end);
		if(!end || end==m_Pos) return false;
		m_Pos = end;
		return true;
	}

	//	The body of a data object, after its '{'.
	bool ReadBody(XObject *obj){
		while(true){
			SkipSpace();
			if(m_Pos>=m_End) return Fail("unexpected end of file");

			const char c = *m_Pos;

			if(c=='}'){ m_Pos++; return true; }
			if(c==';' || c==','){ m_Pos++; continue; }
			if(c=='"'){
				const char *b = ++m_Pos;

				while(m_Pos<m_End && *m_Pos!='"') m_Pos++;
				if(m_Pos>=m_End) return Fail("string not closed");

				XValue v;

				v.isString = true;
				v.number = 0.0;
				v.text.assign(b, m_Pos);
				m_Pos++;
				obj->values.push_back(v);
				continue;
			}
			if(c=='<'){
				while(m_Pos<m_End && *m_Pos!='>') m_Pos++;
				if(m_Pos>=m_End) return Fail("GUID not closed");
				m_Pos++;
				continue;
			}
			if(c=='{'){
				//	A reference { name [<guid>] }: kept as a child so a template
				//	that is skipped anyway (Animation) may hold one.  Where the
				//	importer reads the data, a reference is refused.
				m_Pos++;

				XObject *ref = new XObject;

				ref->type = "{reference}";
				obj->children.push_back(ref);
				if(!ReadName(&ref->name)) return Fail("a reference without a name");
				SkipSpace();
				if(m_Pos<m_End && *m_Pos=='<'){
					while(m_Pos<m_End && *m_Pos!='>') m_Pos++;
					if(m_Pos<m_End) m_Pos++;
					SkipSpace();
				}
				if(m_Pos>=m_End || *m_Pos!='}') return Fail("a reference not closed");
				m_Pos++;
				continue;
			}
			if(IsNumberStart(c)){
				XValue v;

				v.isString = false;
				if(!ReadNumber(&v.number)) return Fail("bad number");
				obj->values.push_back(v);
				continue;
			}
			if(IsNameStart(c)){
				XObject *child = new XObject;

				obj->children.push_back(child);
				if(!ReadObject(child)) return false;
				continue;
			}
			return Fail("unexpected character");
		}
	}

	//	Type [Name] { body }
	bool ReadObject(XObject *obj){
		if(!ReadName(&obj->type)) return Fail("expected a template name");
		SkipSpace();
		if(m_Pos<m_End && IsNameStart(*m_Pos)){
			if(!ReadName(&obj->name)) return false;
			SkipSpace();
		}
		if(m_Pos>=m_End || *m_Pos!='{') return Fail("expected '{'");
		m_Pos++;
		return ReadBody(obj);
	}

public:
	const std::string &Error() const{ return m_Error; }

	bool Parse(const char *data, size_t size, XObject *root){
		m_Pos = data;
		m_End = data+size;
		m_Error.clear();
		while(true){
			SkipSpace();
			if(m_Pos>=m_End) return true;

			std::string word;
			const char *save = m_Pos;

			if(!ReadName(&word)) return Fail("unexpected character at the top level");
			if(word=="template"){
				if(!SkipTemplate()) return false;
				continue;
			}
			m_Pos = save;

			XObject *obj = new XObject;

			root->children.push_back(obj);
			if(!ReadObject(obj)) return false;
		}
	}
};

////////////////////////////////////////////////////////////////////////////////
//	The D3DX8 numeric rules
////////////////////////////////////////////////////////////////////////////////

float RS2XTowardZero(float f){
	if(f==0.0f) return 0.0f;

	unsigned int bits;

	memcpy(&bits, &f, 4);
	bits--;
	memcpy(&f, &bits, 4);
	return f;
}

struct XVec3 { float x, y, z; };

XVec3 RS2XNormalize(XVec3 n){
	const float ss = n.x*n.x+n.y*n.y+n.z*n.z;
	const float l = sqrtf(ss);

	if(l==0.0f) return n;

	XVec3 r = { n.x/l, n.y/l, n.z/l };
	return r;
}

bool RS2XIsZero(const XVec3 &n){ return n.x==0.0f && n.y==0.0f && n.z==0.0f; }

bool RS2XSameValue(const XVec3 &a, const XVec3 &b){
	//	A zero-length normal normalises to NaN inside D3DX8 and matches nothing.
	if(RS2XIsZero(a) || RS2XIsZero(b)) return false;
	return a.x==b.x && a.y==b.y && a.z==b.z;
}

typedef float XMatrix[16];

void RS2XMatMul(const float *a, const float *b, float *out){
	float r[16];
	int row, col, k;

	for(row = 0; row<4; row++){
		for(col = 0; col<4; col++){
			float acc = 0.0f;

			for(k = 0; k<4; k++) acc = acc+a[row*4+k]*b[k*4+col];
			r[row*4+col] = acc;
		}
	}
	memcpy(out, r, sizeof(r));
}

XVec3 RS2XTransformPoint(const XVec3 &p, const float *m){
	XVec3 r;

	r.x = p.x*m[0]+p.y*m[4]+p.z*m[8]+m[12];
	r.y = p.x*m[1]+p.y*m[5]+p.z*m[9]+m[13];
	r.z = p.x*m[2]+p.y*m[6]+p.z*m[10]+m[14];
	return r;
}

XVec3 RS2XTransformNormal(const XVec3 &n, const float *m){
	XVec3 r;

	r.x = n.x*m[0]+n.y*m[4]+n.z*m[8];
	r.y = n.x*m[1]+n.y*m[5]+n.z*m[9];
	r.z = n.x*m[2]+n.y*m[6]+n.z*m[10];
	return r;
}

unsigned int RS2XPackColor(double r, double g, double b, double a){
	double c[4] = { a, r, g, b };
	unsigned int out = 0;
	int i;

	for(i = 0; i<4; i++){
		int v = (int)(c[i]*255.0);

		if(v<0) v = 0;
		if(v>255) v = 255;
		out = (out<<8)|(unsigned int)v;
	}
	return out;
}

////////////////////////////////////////////////////////////////////////////////
//	One Mesh, loaded the way D3DXLoadMeshFromX loaded it
////////////////////////////////////////////////////////////////////////////////

struct XLoadedVertex
{
	XVec3 pos, normal;
	bool hasNormal, hasColor, hasUV;
	unsigned int color;
	float u, v;
};

struct XTriangle
{
	unsigned int material;	//	merged material index
	unsigned int v[3];		//	merged loaded-vertex indices
};

struct XImportMaterial
{
	RS2Material material;
	bool hasTexture;
	std::string texture;
};

class XMeshReader
{
	const XObject *m_Obj;
	const std::vector<XValue> *m_V;
	size_t m_Pos;
	std::string *m_Error;

public:
	XMeshReader(const XObject *obj, std::string *error): m_Obj(obj), m_V(&obj->values), m_Pos(0), m_Error(error){}

	bool Number(double *out){
		if(m_Pos>=m_V->size() || (*m_V)[m_Pos].isString){
			*m_Error = m_Obj->type+": fewer values than its counts say";
			return false;
		}
		*out = (*m_V)[m_Pos++].number;
		return true;
	}
	bool Count(unsigned int *out, unsigned int limit = 0x7fffffff){
		double d;

		if(!Number(&d)) return false;
		if(d<0 || d!=floor(d) || d>limit){
			*m_Error = m_Obj->type+": a count or index is not a valid whole number";
			return false;
		}
		*out = (unsigned int)d;
		return true;
	}
	bool Done(){
		if(m_Pos!=m_V->size()){
			*m_Error = m_Obj->type+": more values than its counts say";
			return false;
		}
		return true;
	}
};

void RS2XUnescape(std::string *s){
	std::string out;
	size_t i;

	for(i = 0; i<s->size(); i++){
		out += (*s)[i];
		if((*s)[i]=='\\' && i+1<s->size() && (*s)[i+1]=='\\') i++;
	}
	*s = out;
}

/*
 *	Load one Mesh.  The loaded vertices, triangles and materials are appended
 *	to the merged lists, indices offset by what is already there.
 */
bool RS2XLoadMesh(
	const XObject *mesh, const float *matrix,
	std::vector<XLoadedVertex> *loaded, std::vector<XTriangle> *tris,
	std::vector<XImportMaterial> *mats, std::string *error, std::string *ignored
){
	XMeshReader r(mesh, error);
	unsigned int nv, nf, i, j;

	//	Positions and faces.
	if(!r.Count(&nv)) return false;

	std::vector<XVec3> pos(nv);
	for(i = 0; i<nv; i++){
		double x, y, z;

		if(!r.Number(&x) || !r.Number(&y) || !r.Number(&z)) return false;

		XVec3 p = { (float)x, (float)y, (float)z };

		if(matrix) p = RS2XTransformPoint(p, matrix);
		p.x = RS2XTowardZero(p.x);
		p.y = RS2XTowardZero(p.y);
		p.z = RS2XTowardZero(p.z);
		pos[i] = p;
	}
	if(!r.Count(&nf)) return false;

	std::vector<std::vector<unsigned int> > faces(nf);
	for(i = 0; i<nf; i++){
		unsigned int n;

		if(!r.Count(&n, 1024)) return false;
		if(n<3){ *error = "Mesh: a face with fewer than 3 corners"; return false; }
		faces[i].resize(n);
		for(j = 0; j<n; j++){
			if(!r.Count(&faces[i][j])) return false;
			if(faces[i][j]>=nv){ *error = "Mesh: a face index is out of range"; return false; }
		}
	}
	if(!r.Done()) return false;

	//	Children.
	std::vector<XVec3> normals;
	std::vector<std::vector<unsigned int> > normalFaces;
	std::vector<float> uv;
	std::vector<unsigned int> colors;
	std::vector<unsigned int> faceMaterial;
	std::vector<XImportMaterial> meshMats;
	bool hasNormals = false, hasUV = false, hasColors = false, hasMaterials = false;

	for(i = 0; i<mesh->children.size(); i++){
		const XObject *c = mesh->children[i];

		if(c->type=="MeshNormals"){
			XMeshReader n(c, error);
			unsigned int nn, nnf, k;

			if(!n.Count(&nn)) return false;
			normals.resize(nn);
			for(k = 0; k<nn; k++){
				double x, y, z;

				if(!n.Number(&x) || !n.Number(&y) || !n.Number(&z)) return false;

				XVec3 v = { (float)x, (float)y, (float)z };

				if(matrix) v = RS2XTransformNormal(v, matrix);
				normals[k] = RS2XNormalize(v);
			}
			if(!n.Count(&nnf)) return false;
			if(nnf!=nf){ *error = "MeshNormals: face count differs from the Mesh"; return false; }
			normalFaces.resize(nnf);
			for(k = 0; k<nnf; k++){
				unsigned int cnt, m;

				if(!n.Count(&cnt, 1024)) return false;
				if(cnt!=faces[k].size()){ *error = "MeshNormals: a face has a different corner count"; return false; }
				normalFaces[k].resize(cnt);
				for(m = 0; m<cnt; m++){
					if(!n.Count(&normalFaces[k][m])) return false;
					if(normalFaces[k][m]>=nn){ *error = "MeshNormals: a normal index is out of range"; return false; }
				}
			}
			if(!n.Done()) return false;
			hasNormals = true;
		}else if(c->type=="MeshTextureCoords"){
			XMeshReader t(c, error);
			unsigned int n, k;

			if(!t.Count(&n)) return false;
			if(n<nv){ *error = "MeshTextureCoords: fewer coordinates than vertices"; return false; }
			uv.resize(n*2);
			for(k = 0; k<n*2; k++){
				double d;

				if(!t.Number(&d)) return false;
				uv[k] = (float)d;
			}
			if(!t.Done()) return false;
			hasUV = true;
		}else if(c->type=="MeshVertexColors"){
			XMeshReader t(c, error);
			unsigned int n, k;

			if(!t.Count(&n)) return false;
			colors.assign(nv, 0);
			for(k = 0; k<n; k++){
				unsigned int idx;
				double cr, cg, cb, ca;

				if(!t.Count(&idx) || !t.Number(&cr) || !t.Number(&cg) || !t.Number(&cb) || !t.Number(&ca)) return false;
				if(idx>=nv){ *error = "MeshVertexColors: an index is out of range"; return false; }
				colors[idx] = RS2XPackColor(cr, cg, cb, ca);
			}
			if(!t.Done()) return false;
			hasColors = true;
		}else if(c->type=="MeshMaterialList"){
			XMeshReader t(c, error);
			unsigned int nm, nfi, k;

			if(!t.Count(&nm) || !t.Count(&nfi)) return false;
			faceMaterial.resize(nfi);
			for(k = 0; k<nfi; k++){
				if(!t.Count(&faceMaterial[k])) return false;
				if(faceMaterial[k]>=nm){ *error = "MeshMaterialList: a material index is out of range"; return false; }
			}
			if(!t.Done()) return false;

			size_t m;
			for(m = 0; m<c->children.size(); m++){
				const XObject *mo = c->children[m];

				if(mo->type=="{reference}"){
					*error = "MeshMaterialList: material references ({ "+mo->name+" }) are not supported";
					return false;
				}
				if(mo->type!="Material"){
					*ignored += " MeshMaterialList/"+mo->type;
					continue;
				}

				XMeshReader mr(mo, error);
				double v[11];
				int q;

				for(q = 0; q<11; q++) if(!mr.Number(&v[q])) return false;
				if(!mr.Done()) return false;

				XImportMaterial im;

				im.material.Diffuse = RS2MakeColor4((float)v[0], (float)v[1], (float)v[2], (float)v[3]);
				im.material.Ambient = RS2MakeColor4(0.0f, 0.0f, 0.0f, 1.0f);
				im.material.Power = (float)v[4];
				im.material.Specular = RS2MakeColor4((float)v[5], (float)v[6], (float)v[7], 1.0f);
				im.material.Emissive = RS2MakeColor4((float)v[8], (float)v[9], (float)v[10], 1.0f);
				im.hasTexture = false;

				const XObject *tf = mo->Child("TextureFilename");

				if(tf){
					if(tf->values.size()!=1 || !tf->values[0].isString){
						*error = "TextureFilename: expected one string";
						return false;
					}
					im.hasTexture = true;
					im.texture = tf->values[0].text;
					RS2XUnescape(&im.texture);
				}
				size_t q2;
				for(q2 = 0; q2<mo->children.size(); q2++)
					if(mo->children[q2]->type!="TextureFilename") *ignored += " Material/"+mo->children[q2]->type;
				meshMats.push_back(im);
			}
			if(meshMats.size()!=nm){ *error = "MeshMaterialList: material count differs from its declaration"; return false; }
			hasMaterials = true;
		}else if(c->type=="{reference}"){
			*error = "Mesh: references ({ "+c->name+" }) are not supported";
			return false;
		}else{
			*ignored += " Mesh/"+c->type;
		}
	}

	if(!hasMaterials){
		//	D3DX8's material when a mesh has none.
		XImportMaterial im;

		im.material.Diffuse = RS2MakeColor4(0.5f, 0.5f, 0.5f, 0.0f);
		im.material.Ambient = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
		im.material.Specular = RS2MakeColor4(0.5f, 0.5f, 0.5f, 0.0f);
		im.material.Emissive = RS2MakeColor4(0.0f, 0.0f, 0.0f, 0.0f);
		im.material.Power = 0.0f;
		im.hasTexture = false;
		meshMats.push_back(im);
	}
	//	Faces past the end of the list use its last entry.
	if(faceMaterial.empty()) faceMaterial.assign(nf, 0);
	while(faceMaterial.size()<nf) faceMaterial.push_back(faceMaterial.back());

	//	Split vertices by normal (the D3DX8 rule, see the file comment).
	const unsigned int base = (unsigned int)loaded->size();
	const unsigned int materialBase = (unsigned int)mats->size();
	std::vector<unsigned int> lvPos(nv);
	std::vector<XVec3> lvNormal(nv);
	std::vector<int> origNormalIndex(nv, -1);
	std::vector<std::vector<unsigned int> > copies(nv);
	std::vector<std::vector<unsigned int> > cornerVertex(nf);

	for(i = 0; i<nv; i++){
		lvPos[i] = i;
		lvNormal[i].x = lvNormal[i].y = lvNormal[i].z = 0.0f;
	}
	for(i = 0; i<nf; i++){
		cornerVertex[i].resize(faces[i].size());
		for(j = 0; j<faces[i].size(); j++){
			const unsigned int pi = faces[i][j];

			if(!hasNormals){
				cornerVertex[i][j] = pi;
				continue;
			}

			const unsigned int ni = normalFaces[i][j];
			const XVec3 &nv_ = normals[ni];

			if(origNormalIndex[pi]<0){
				origNormalIndex[pi] = (int)ni;
				lvNormal[pi] = nv_;
				cornerVertex[i][j] = pi;
				continue;
			}
			if((unsigned int)origNormalIndex[pi]==ni || RS2XSameValue(lvNormal[pi], nv_)){
				cornerVertex[i][j] = pi;
				continue;
			}

			unsigned int hit = 0xffffffffu;
			size_t k;

			for(k = 0; k<copies[pi].size(); k++){
				if(RS2XSameValue(lvNormal[copies[pi][k]], nv_)){
					hit = copies[pi][k];
					break;
				}
			}
			if(hit==0xffffffffu){
				hit = (unsigned int)lvPos.size();
				lvPos.push_back(pi);
				lvNormal.push_back(nv_);
				copies[pi].push_back(hit);
			}
			cornerVertex[i][j] = hit;
		}
	}

	for(i = 0; i<lvPos.size(); i++){
		const unsigned int pi = lvPos[i];
		XLoadedVertex v;

		v.pos = pos[pi];
		v.normal = lvNormal[i];
		v.hasNormal = hasNormals;
		v.hasColor = hasColors;
		v.color = hasColors ? colors[pi] : 0;
		v.hasUV = hasUV;
		v.u = hasUV ? uv[pi*2] : 0.0f;
		v.v = hasUV ? uv[pi*2+1] : 0.0f;
		loaded->push_back(v);
	}

	//	Fans; a triangle without three distinct vertices is dropped.
	for(i = 0; i<nf; i++){
		const std::vector<unsigned int> &cv = cornerVertex[i];

		for(j = 1; j+1<cv.size(); j++){
			if(cv[0]==cv[j] || cv[0]==cv[j+1] || cv[j]==cv[j+1]) continue;

			XTriangle t;

			t.material = materialBase+faceMaterial[i];
			t.v[0] = base+cv[0];
			t.v[1] = base+cv[j];
			t.v[2] = base+cv[j+1];
			tris->push_back(t);
		}
	}
	mats->insert(mats->end(), meshMats.begin(), meshMats.end());
	return true;
}

//	Every Mesh with the matrices of the Frames above it (innermost first),
//	depth-first in file order.
typedef std::vector<std::vector<float> > XChain;

void RS2XCollect(
	const XObject *obj, const XChain &chain,
	std::vector<std::pair<const XObject *, XChain> > *out, std::string *ignored
){
	size_t i;

	for(i = 0; i<obj->children.size(); i++){
		const XObject *c = obj->children[i];

		if(c->type=="Mesh"){
			out->push_back(std::make_pair(c, chain));
		}else if(c->type=="Frame"){
			const XObject *ft = c->Child("FrameTransformMatrix");

			if(ft && ft->values.size()>=16){
				XChain inner;
				std::vector<float> local(16);
				int k;

				for(k = 0; k<16; k++) local[k] = (float)ft->values[k].number;
				inner.push_back(local);
				inner.insert(inner.end(), chain.begin(), chain.end());
				RS2XCollect(c, inner, out, ignored);
			}else{
				//	A Frame without a matrix is the identity: it adds nothing.
				RS2XCollect(c, chain, out, ignored);
			}
		}else if(c->type=="FrameTransformMatrix" && obj->type=="Frame"){
		}else if(c->type=="Header" && obj->type=="(root)"){
		}else{
			*ignored += " "+(obj->type=="(root)" ? std::string("") : obj->type+"/")+c->type;
		}
	}
}

//	v * inner * ... * outer, multiplied in that order (float rounding follows
//	the order, and this is the order D3DX8's results match).
bool RS2XCombine(const XChain &chain, float *out){
	if(chain.empty()) return false;
	memcpy(out, &chain[0][0], 16*sizeof(float));

	size_t i;
	for(i = 1; i<chain.size(); i++) RS2XMatMul(out, &chain[i][0], out);
	return true;
}

}	//	namespace

////////////////////////////////////////////////////////////////////////////////
//	Entry point
////////////////////////////////////////////////////////////////////////////////

bool RS2ImportXMesh(const char *path, CRS2MeshImportResult *out){
	if(!out) return false;
	out->Free();

	FILE *file = fopen(path, "rb");

	if(!file){
		Debug("[RS2EX Import] %s: cannot open\n", path);
		return false;
	}

	std::vector<char> data;
	{
		fseek(file, 0, SEEK_END);
		const long size = ftell(file);
		fseek(file, 0, SEEK_SET);
		if(size>0){
			data.resize(size);
			if(fread(&data[0], 1, size, file)!=(size_t)size) data.clear();
		}
		fclose(file);
	}
	if(data.size()<16 || memcmp(&data[0], "xof ", 4)){
		Debug("[RS2EX Import] %s: not a .x file\n", path);
		return false;
	}
	if(memcmp(&data[8], "txt ", 4)){
		//	Binary ("bin ") and compressed ("tzip" / "bzip") are outside the
		//	v0.2.0 contract: refused by name rather than misread.
		Debug("[RS2EX Import] %s: .x format \"%.4s\" is not supported (text only)\n", path, &data[8]);
		return false;
	}

	XObject root;
	XParser parser;

	root.type = "(root)";
	if(!parser.Parse(&data[0]+16, data.size()-16, &root)){
		Debug("[RS2EX Import] %s: %s\n", path, parser.Error().c_str());
		return false;
	}

	std::string ignored, error;
	std::vector<std::pair<const XObject *, XChain> > meshes;

	RS2XCollect(&root, XChain(), &meshes, &ignored);
	if(meshes.empty()){
		Debug("[RS2EX Import] %s: no Mesh\n", path);
		return false;
	}

	std::vector<XLoadedVertex> loaded;
	std::vector<XTriangle> tris;
	std::vector<XImportMaterial> mats;
	size_t i;

	for(i = 0; i<meshes.size(); i++){
		float combined[16];
		const float *matrix = RS2XCombine(meshes[i].second, combined) ? combined : 0;

		if(!RS2XLoadMesh(meshes[i].first, matrix, &loaded, &tris, &mats, &error, &ignored)){
			Debug("[RS2EX Import] %s: %s\n", path, error.c_str());
			return false;
		}
	}
	if(!ignored.empty()) Debug("[RS2EX Import] %s: skipped:%s\n", path, ignored.c_str());
	if(tris.empty() || loaded.empty()){
		Debug("[RS2EX Import] %s: no faces\n", path);
		return false;
	}

	//	Layout: every attribute any merged mesh has.
	bool hasNormal = false, hasColor = false, hasUV = false;

	for(i = 0; i<loaded.size(); i++){
		hasNormal |= loaded[i].hasNormal;
		hasColor |= loaded[i].hasColor;
		hasUV |= loaded[i].hasUV;
	}

	RS2MeshVertexLayout layout;
	unsigned int offset = 12;

	layout.positionOffset = 0;
	if(hasNormal){ layout.normalOffset = offset; offset += 12; }
	if(hasColor){ layout.diffuseOffset = offset; offset += 4; }
	if(hasUV){
		layout.texCoordCount = 1;
		layout.texCoord[0].offset = offset;
		layout.texCoord[0].components = 2;
		offset += 8;
	}
	layout.stride = offset;

	//	Bounds: D3DX8's box, all but the last merged vertex.
	{
		const size_t n = loaded.size()>1 ? loaded.size()-1 : loaded.size();
		XVec3 mn = loaded[0].pos, mx = loaded[0].pos;

		for(i = 1; i<n; i++){
			const XVec3 &p = loaded[i].pos;

			if(p.x<mn.x) mn.x = p.x;
			if(p.y<mn.y) mn.y = p.y;
			if(p.z<mn.z) mn.z = p.z;
			if(p.x>mx.x) mx.x = p.x;
			if(p.y>mx.y) mx.y = p.y;
			if(p.z>mx.z) mx.z = p.z;
		}
		out->boundsMin = VEC3(mn.x, mn.y, mn.z);
		out->boundsMax = VEC3(mx.x, mx.y, mx.z);
	}

	//	Faces grouped by material, the file's order kept inside each group;
	//	one vertex per loaded vertex and material, in first-use order.
	std::vector<unsigned int> order(tris.size());
	{
		const unsigned int materialCount = (unsigned int)mats.size();
		std::vector<unsigned int> start(materialCount+1, 0);

		for(i = 0; i<tris.size(); i++) start[tris[i].material+1]++;
		for(i = 1; i<=materialCount; i++) start[i] += start[i-1];
		for(i = 0; i<tris.size(); i++) order[start[tris[i].material]++] = (unsigned int)i;
	}

	std::vector<unsigned int> vertexOf(loaded.size()*1, 0xffffffffu);
	std::vector<unsigned int> vertexMaterial(loaded.size(), 0xffffffffu);
	std::vector<std::vector<std::pair<unsigned int, unsigned int> > > extra(loaded.size());
	std::vector<unsigned int> outLoaded;
	unsigned int *indices = new unsigned int[tris.size()*3];
	unsigned int *faceMaterials = new unsigned int[tris.size()];

	for(i = 0; i<order.size(); i++){
		const XTriangle &t = tris[order[i]];
		int k;

		for(k = 0; k<3; k++){
			const unsigned int lv = t.v[k];
			unsigned int idx = 0xffffffffu;

			if(vertexMaterial[lv]==t.material){
				idx = vertexOf[lv];
			}else if(vertexMaterial[lv]==0xffffffffu){
				vertexMaterial[lv] = t.material;
				vertexOf[lv] = idx = (unsigned int)outLoaded.size();
				outLoaded.push_back(lv);
			}else{
				size_t q;

				for(q = 0; q<extra[lv].size(); q++) if(extra[lv][q].first==t.material) idx = extra[lv][q].second;
				if(idx==0xffffffffu){
					idx = (unsigned int)outLoaded.size();
					outLoaded.push_back(lv);
					extra[lv].push_back(std::make_pair(t.material, idx));
				}
			}
			indices[i*3+k] = idx;
		}
		faceMaterials[i] = t.material;
	}

	unsigned char *bytes = new unsigned char[outLoaded.size()*layout.stride];

	for(i = 0; i<outLoaded.size(); i++){
		const XLoadedVertex &v = loaded[outLoaded[i]];
		unsigned char *d = bytes+i*layout.stride;
		const float p[3] = { v.pos.x, v.pos.y, v.pos.z };

		memcpy(d, p, 12);
		if(hasNormal){
			const float n[3] = { v.hasNormal ? v.normal.x : 0.0f, v.hasNormal ? v.normal.y : 0.0f, v.hasNormal ? v.normal.z : 0.0f };

			memcpy(d+layout.normalOffset, n, 12);
		}
		if(hasColor){
			const unsigned int c = v.hasColor ? v.color : 0xffffffffu;

			memcpy(d+layout.diffuseOffset, &c, 4);
		}
		if(hasUV){
			const float t[2] = { v.u, v.v };

			memcpy(d+layout.texCoord[0].offset, t, 8);
		}
	}

	if(!out->geometry.Build(bytes, (unsigned int)outLoaded.size(), layout,
			indices, (unsigned int)tris.size(), faceMaterials, (unsigned int)mats.size())){
		Debug("[RS2EX Import] %s: the mesh could not be built\n", path);
		out->Free();
		return false;
	}
	out->AllocMaterials((unsigned int)mats.size());
	for(i = 0; i<mats.size(); i++)
		out->SetMaterial((unsigned int)i, mats[i].material, mats[i].hasTexture ? mats[i].texture.c_str() : 0);
	return true;
}
