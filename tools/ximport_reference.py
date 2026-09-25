"""Reference model of the D3DX8 .x import, written as an -ximportdump file.

The rules are the ones measured against the D3DX8 importer in v0.2.0 WP0
(docs: v0.2.0-x-import-contract.md).  This script is the specification the
C++ importer is written from; ximport_compare.py checks both against the
frozen oracle dump.

Usage:
    python tools/ximport_reference.py RUN_DIR out.bin [--normal VARIANT] [--color VARIANT]
"""

import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xfile_text  # noqa: E402


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def bits(x):
    return struct.unpack('<i', struct.pack('<f', x))[0]


def from_bits(b):
    return struct.unpack('<f', struct.pack('<i', b))[0]


def toward_zero(x):
    """Correctly rounded, then one ulp toward zero (0 stays 0)."""
    f = f32(x)
    if f == 0.0:
        return 0.0
    return from_bits(bits(f) - 1)


def fmul(a, b):
    return f32(f32(a) * f32(b))


def fadd(a, b):
    return f32(f32(a) + f32(b))


def fdiv(a, b):
    return f32(f32(a) / f32(b))


def normalize(n, variant):
    x, y, z = n
    if variant == 'none':
        return (x, y, z)
    ss = fadd(fadd(fmul(x, x), fmul(y, y)), fmul(z, z))
    if variant == 'div':
        l = f32(math.sqrt(ss))
        if l == 0:
            return (x, y, z)
        return (fdiv(x, l), fdiv(y, l), fdiv(z, l))
    if variant == 'recip':
        l = f32(math.sqrt(ss))
        if l == 0:
            return (x, y, z)
        r = fdiv(1.0, l)
        return (fmul(x, r), fmul(y, r), fmul(z, r))
    if variant == 'double':
        l = math.sqrt(x * x + y * y + z * z)
        if l == 0:
            return (x, y, z)
        return (f32(x / l), f32(y / l), f32(z / l))
    raise ValueError(variant)


def pack_color(c, variant):
    r, g, b, a = c

    def ch(v):
        if variant == 'trunc':
            n = int(v * 255.0)
        elif variant == 'round':
            n = int(math.floor(v * 255.0 + 0.5))
        elif variant == 'f32trunc':
            n = int(fmul(v, 255.0))
        else:
            raise ValueError(variant)
        return max(0, min(255, n))
    return (ch(a) << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b)


def unescape(s):
    return s.replace(b'\\\\', b'\\')


def build(path, opts):
    src = xfile_text.load(path)
    P = [tuple(toward_zero(x) for x in p) for p in src['positions']]
    F = src['faces']
    NF = src['normalFaces']
    N = [normalize(tuple(opts.nparse(x) for x in n), opts.normal) for n in src['normals']] if src['normals'] else None
    UV = [tuple(f32(x) for x in uv) for uv in src['uv']] if src['uv'] else None
    COL = None
    if src['colors']:
        COL = [0] * len(P)
        for idx, r, g, b, a in src['colors']:
            COL[idx] = pack_color((r, g, b, a), opts.color)
    has_n, has_uv, has_c = N is not None, UV is not None, COL is not None
    stride = 12 + (12 if has_n else 0) + (4 if has_c else 0) + (8 if has_uv else 0)
    layout = {'stride': stride, 'position': 0, 'normal': 12 if has_n else -1,
              'diffuse': (24 if has_n else 12) if has_c else -1,
              'texcoords': [(stride - 8, 2)] if has_uv else []}
    fm_src = src['faceMaterials'] or [0] * len(F)
    if len(fm_src) < len(F):
        fm_src = fm_src + [fm_src[-1]] * (len(F) - len(fm_src))
    # 1. Load: one vertex per position until a corner brings a different
    #    normal.  The original vertex is reused when the corner has the same
    #    normal index or the same normalised value; a split copy only when the
    #    value is the same.  Values compare as floats, so a zero normal (NaN
    #    after normalising) never matches a copy.  Copies are appended in face
    #    order.
    # A zero-length normal normalises to NaN inside D3DX (written out as 0),
    # so by value it matches nothing - not even another zero normal.
    zero = lambda n: n == (0.0, 0.0, 0.0)
    feq = lambda a, b: (a is not None and b is not None and not zero(a) and not zero(b)
                        and all(x == y for x, y in zip(a, b)))
    lv_pos, lv_normal = list(range(len(P))), [None] * len(P)
    orig_ni = [None] * len(P)
    copies = {}
    corner_vertex = []
    for fi, f in enumerate(F):
        cv = []
        for j, pi in enumerate(f):
            if not has_n:
                cv.append(pi)
                continue
            ni = NF[fi][j]
            nv_ = N[ni]
            if orig_ni[pi] is None:
                orig_ni[pi] = ni
                lv_normal[pi] = nv_
                cv.append(pi)
                continue
            if orig_ni[pi] == ni or feq(lv_normal[pi], nv_):
                cv.append(pi)
                continue
            hit = None
            for c in copies.get(pi, ()):
                if feq(lv_normal[c], nv_):
                    hit = c
                    break
            if hit is None:
                hit = len(lv_pos)
                lv_pos.append(pi)
                lv_normal.append(nv_)
                copies.setdefault(pi, []).append(hit)
            cv.append(hit)
        corner_vertex.append(cv)
    # Bounds: over the loaded vertices, all but the last one
    # (D3DX8's D3DXComputeBoundingBox skips the last vertex).
    bsrc = [P[pi] for pi in lv_pos][:-1] if len(lv_pos) > 1 else [P[pi] for pi in lv_pos]
    # 2. Triangles: fan, file order; a triangle whose three loaded vertices
    #    are not distinct is dropped.
    tri = []
    for fi, f in enumerate(F):
        cv = corner_vertex[fi]
        for k in range(1, len(f) - 1):
            t = (cv[0], cv[k], cv[k + 1])
            if len(set(t)) < 3:
                continue
            tri.append((fm_src[fi], fi, (0, k, k + 1), t))
    # 3. Optimise: stable sort by material (ATTRSORT); a loaded vertex used by
    #    two materials becomes one vertex per material; unused vertices go.
    tri.sort(key=lambda t: t[0])
    vmap, verts, faces, fmat = {}, [], [], []
    for mat, fi, corners, t in tri:
        idx = []
        for lv, j in zip(t, corners):
            key = (lv, mat)
            if key not in vmap:
                pi = lv_pos[lv]
                vmap[key] = len(verts)
                verts.append({'pos': P[pi], 'normal': lv_normal[lv] if has_n else None,
                              'diffuse': COL[pi] if has_c else None,
                              'uv': [UV[pi]] if has_uv else []})
            idx.append(vmap[key])
        faces.append(tuple(idx))
        fmat.append(mat)
    subsets = []
    for i, m in enumerate(fmat):
        if not subsets or subsets[-1][0] != m:
            subsets.append([m, i * 3, 0])
        subsets[-1][2] += 1
    mats = []
    for m in src['materials']:
        d = tuple(opts.mparse(x) for x in m['faceColor'])
        mats.append({'diffuse': d, 'ambient': (0.0, 0.0, 0.0, 1.0),
                     'specular': tuple(opts.mparse(x) for x in m['specular']) + (1.0,),
                     'emissive': tuple(opts.mparse(x) for x in m['emissive']) + (1.0,),
                     'power': opts.mparse(m['power']),
                     'texture': unescape(m['texture']).decode('cp932') if m['texture'] is not None else None})
    bmin = tuple(min(p[i] for p in bsrc) for i in range(3))
    bmax = tuple(max(p[i] for p in bsrc) for i in range(3))
    return {'ok': True, 'boundsMin': bmin, 'boundsMax': bmax, 'layout': layout, 'vertices': verts,
            'faces': faces, 'faceMaterials': fmat, 'subsets': [tuple(s) for s in subsets], 'materials': mats}


def write(out, meshes):
    f = open(out, 'wb')
    w = lambda fmt, *v: f.write(struct.pack('<' + fmt, *v))
    f.write(b'RS2XDUMP'); w('I', 1)
    for rel, m in meshes:
        p = rel.encode('cp932')
        w('I', len(p)); f.write(p); w('I', 1 if m else 0)
        if not m:
            continue
        w('3f', *m['boundsMin']); w('3f', *m['boundsMax'])
        L = m['layout']
        w('Iiii', L['stride'], L['position'], L['normal'], L['diffuse'])
        w('I', len(L['texcoords']))
        for o, c in L['texcoords']:
            w('iI', o, c)
        w('I', len(m['vertices']))
        for v in m['vertices']:
            w('3f', *v['pos'])
            if v['normal'] is not None:
                w('3f', *v['normal'])
            if v['diffuse'] is not None:
                w('I', v['diffuse'])
            for uv in v['uv']:
                w('2f', *uv)
        w('I', len(m['faces']))
        for t in m['faces']:
            w('3I', *t)
        for x in m['faceMaterials']:
            w('I', x)
        w('I', len(m['subsets']))
        for s in m['subsets']:
            w('3I', *s)
        w('I', len(m['materials']))
        for mt in m['materials']:
            for k in ('diffuse', 'ambient', 'specular', 'emissive'):
                w('4f', *mt[k])
            w('f', mt['power'])
            if mt['texture'] is None:
                w('i', -1)
            else:
                t = mt['texture'].encode('cp932')
                w('i', len(t)); f.write(t)
    f.write(b'RS2XEND!')
    f.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run')
    ap.add_argument('out')
    ap.add_argument('--normal', default='div')
    ap.add_argument('--nparse', default='toward_zero')
    ap.add_argument('--mparse', default='toward_zero')
    ap.add_argument('--color', default='trunc')
    ap.add_argument('--only', default='')
    opts = ap.parse_args()
    opts.nparse = toward_zero if opts.nparse == 'toward_zero' else f32
    opts.mparse = toward_zero if opts.mparse == 'toward_zero' else f32
    files = []
    for dp, _, fs in os.walk(opts.run):
        for fn in fs:
            if fn.lower().endswith('.x'):
                rel = os.path.relpath(os.path.join(dp, fn), opts.run)
                if not opts.only or opts.only.lower() in rel.lower():
                    files.append(rel)
    files.sort(key=lambda s: s.lower())
    meshes = []
    for rel in files:
        try:
            meshes.append((rel, build(os.path.join(opts.run, rel), opts)))
        except Exception as e:  # noqa: BLE001
            print('failed', rel, e)
            meshes.append((rel, None))
    write(opts.out, meshes)


if __name__ == '__main__':
    main()
