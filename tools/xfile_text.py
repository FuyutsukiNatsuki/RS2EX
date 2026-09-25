"""A small reader for text .x files (xof 0302/0303 txt), for analysis.

Parses the data objects into a tree and interprets the Mesh templates the
RailSim II corpus uses (Mesh, MeshNormals, MeshTextureCoords,
MeshVertexColors, MeshMaterialList, Material, TextureFilename).  Template
declarations are skipped.  This is the analysis twin of the C++ importer,
not a general .x implementation.
"""

import re

TOKEN = re.compile(rb'''
    (?P<ws>[ \t\r\n]+) |
    (?P<comment>(//|\#)[^\n]*) |
    (?P<string>"[^"]*") |
    (?P<guid><[0-9A-Fa-f\-]+>) |
    (?P<num>[-+]?(\d+\.\d*|\.\d+|\d+)([eE][-+]?\d+)?) |
    (?P<name>[A-Za-z_][A-Za-z0-9_\-]*) |
    (?P<punct>[{};,\[\]\.])
''', re.X)


class Obj:
    def __init__(self, kind, name):
        self.kind, self.name, self.values, self.children = kind, name, [], []

    def child(self, kind):
        for c in self.children:
            if c.kind == kind:
                return c
        return None


def tokens(data):
    pos = 0
    while pos < len(data):
        m = TOKEN.match(data, pos)
        if not m:
            raise ValueError('bad character %r at %d' % (data[pos:pos + 1], pos))
        pos = m.end()
        if m.lastgroup in ('ws', 'comment'):
            continue
        yield m.lastgroup, m.group(m.lastgroup)


def parse(data):
    if data[:4] != b'xof ' or data[8:12] != b'txt ':
        raise ValueError('not a text .x file: %r' % data[:16])
    toks = list(tokens(data[16:]))
    root = Obj('(root)', None)
    stack = [root]
    i = 0
    while i < len(toks):
        k, v = toks[i]
        if k == 'name' and v == b'template':
            depth = 0
            while True:
                if toks[i] == ('punct', b'{'):
                    depth += 1
                elif toks[i] == ('punct', b'}'):
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            i += 1
            continue
        if k == 'name':
            j = i + 1
            name = None
            if j < len(toks) and toks[j][0] == 'name':
                name = toks[j][1].decode('latin1')
                j += 1
            if j < len(toks) and toks[j] == ('punct', b'{'):
                o = Obj(v.decode('latin1'), name)
                stack[-1].children.append(o)
                stack.append(o)
                i = j + 1
                continue
            raise ValueError('unexpected name %r' % v)
        if k == 'punct':
            if v == b'}':
                stack.pop()
            elif v == b'{':
                raise ValueError('references are not in the corpus contract')
            i += 1
            continue
        if k == 'num':
            s = v.decode()
            stack[-1].values.append(float(s) if ('.' in s or 'e' in s or 'E' in s) else int(s))
        elif k == 'string':
            stack[-1].values.append(v[1:-1])
        elif k == 'guid':
            pass
        i += 1
    return root


IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)


def meshes_of(root):
    """Every Mesh with the matrices of the Frames above it, innermost first,
    in depth-first file order - the order D3DX8 merges them in."""
    out = []

    def walk(obj, chain):
        local = chain
        ft = obj.child('FrameTransformMatrix') if obj.kind == 'Frame' else None
        if ft is not None:
            local = (tuple(float(x) for x in ft.values[:16]),) + chain
        for c in obj.children:
            if c.kind == 'Mesh':
                out.append((c, local))
            elif c.kind == 'Frame':
                walk(c, local)
    walk(root, ())
    return out


def mesh_of(root):
    """Interpret the single top-level Mesh."""
    m = [c for c in root.children if c.kind == 'Mesh']
    if len(m) != 1:
        raise ValueError('%d top-level Mesh objects' % len(m))
    return interpret(m[0])


def interpret(m):
    v = m.values
    nv = int(v[0])
    pos = [tuple(float(x) for x in v[1 + 3 * i:4 + 3 * i]) for i in range(nv)]
    q = 1 + 3 * nv
    nf = int(v[q]); q += 1
    faces = []
    for _ in range(nf):
        n = int(v[q]); q += 1
        faces.append(tuple(int(x) for x in v[q:q + n])); q += n
    out = {'positions': pos, 'faces': faces, 'normals': None, 'normalFaces': None,
           'uv': None, 'colors': None, 'materials': [], 'faceMaterials': None}
    mn = m.child('MeshNormals')
    if mn:
        w = mn.values
        nn = int(w[0])
        out['normals'] = [tuple(float(x) for x in w[1 + 3 * i:4 + 3 * i]) for i in range(nn)]
        q = 1 + 3 * nn
        nnf = int(w[q]); q += 1
        nfs = []
        for _ in range(nnf):
            n = int(w[q]); q += 1
            nfs.append(tuple(int(x) for x in w[q:q + n])); q += n
        out['normalFaces'] = nfs
    mt = m.child('MeshTextureCoords')
    if mt:
        w = mt.values
        n = int(w[0])
        out['uv'] = [(float(w[1 + 2 * i]), float(w[2 + 2 * i])) for i in range(n)]
    mc = m.child('MeshVertexColors')
    if mc:
        w = mc.values
        n = int(w[0])
        out['colors'] = [(int(w[1 + 5 * i]),) + tuple(float(x) for x in w[2 + 5 * i:6 + 5 * i]) for i in range(n)]
    ml = m.child('MeshMaterialList')
    if ml:
        w = ml.values
        nm, nfi = int(w[0]), int(w[1])
        out['faceMaterials'] = [int(x) for x in w[2:2 + nfi]]
        for c in ml.children:
            if c.kind != 'Material':
                raise ValueError('MeshMaterialList child %s' % c.kind)
            x = c.values
            mat = {'faceColor': tuple(x[0:4]), 'power': x[4], 'specular': tuple(x[5:8]),
                   'emissive': tuple(x[8:11]), 'texture': None}
            tf = c.child('TextureFilename')
            if tf:
                mat['texture'] = tf.values[0]
            out['materials'].append(mat)
        out['declaredMaterials'] = nm
    return out


def load(path):
    return mesh_of(parse(open(path, 'rb').read()))
