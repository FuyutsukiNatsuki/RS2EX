"""Read an -ximportdump file (RS2XImportDump.cpp) into Python objects.

    from ximport_dump import read_dump
    for mesh in read_dump('ximport-dump.bin'): ...

Each mesh is a dict with path, ok and, when ok: boundsMin/boundsMax, layout
(stride, position, normal, diffuse, texcoords [(offset, components)]),
vertexCount, vertices (list of dicts: pos, normal, diffuse, uv list),
faces [(i0, i1, i2)], faceMaterials, subsets [(material, firstIndex, count)],
materials [dict(diffuse, ambient, specular, emissive, power, texture)].
"""

import struct


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, n):
        v = self.d[self.p:self.p + n]
        if len(v) != n:
            raise ValueError('truncated dump at %d' % self.p)
        self.p += n
        return v

    def u32(self):
        return struct.unpack('<I', self.take(4))[0]

    def i32(self):
        return struct.unpack('<i', self.take(4))[0]

    def f32s(self, n):
        return struct.unpack('<%df' % n, self.take(4 * n))


def read_dump(path):
    r = Reader(open(path, 'rb').read())
    if r.take(8) != b'RS2XDUMP':
        raise ValueError('not an -ximportdump file')
    version = r.u32()
    if version != 1:
        raise ValueError('unknown dump version %d' % version)
    out = []
    while True:
        if r.d[r.p:r.p + 8] == b'RS2XEND!':
            break
        n = r.u32()
        m = {'path': r.take(n).decode('cp932'), 'ok': bool(r.u32())}
        out.append(m)
        if not m['ok']:
            continue
        m['boundsMin'] = r.f32s(3)
        m['boundsMax'] = r.f32s(3)
        stride, pos, nrm, dif = r.u32(), r.i32(), r.i32(), r.i32()
        tc = [(r.i32(), r.u32()) for _ in range(r.u32())]
        m['layout'] = {'stride': stride, 'position': pos, 'normal': nrm, 'diffuse': dif, 'texcoords': tc}
        vc = r.u32()
        vb = r.take(vc * stride)
        verts = []
        for i in range(vc):
            base = i * stride
            v = {'pos': struct.unpack_from('<3f', vb, base + pos)}
            v['normal'] = struct.unpack_from('<3f', vb, base + nrm) if nrm >= 0 else None
            v['diffuse'] = struct.unpack_from('<I', vb, base + dif)[0] if dif >= 0 else None
            v['uv'] = [struct.unpack_from('<%df' % c, vb, base + o) for o, c in tc]
            verts.append(v)
        m['vertexCount'], m['vertices'] = vc, verts
        fc = r.u32()
        idx = struct.unpack('<%dI' % (fc * 3), r.take(fc * 12))
        m['faces'] = [idx[i * 3:i * 3 + 3] for i in range(fc)]
        m['faceMaterials'] = list(struct.unpack('<%dI' % fc, r.take(fc * 4)))
        m['subsets'] = [(r.u32(), r.u32(), r.u32()) for _ in range(r.u32())]
        mats = []
        for _ in range(r.u32()):
            mat = {'diffuse': r.f32s(4), 'ambient': r.f32s(4), 'specular': r.f32s(4),
                   'emissive': r.f32s(4), 'power': r.f32s(1)[0]}
            tn = r.i32()
            mat['texture'] = r.take(tn).decode('cp932') if tn >= 0 else None
            mats.append(mat)
        m['materials'] = mats
    return out
