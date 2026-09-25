"""Compare two -ximportdump files (or a dump and tools/ximport_reference.py).

Per file it reports:

  counts     vertex / face / material counts, layout, bounds (bit-exact and
             within tolerance)
  materials  every material field and texture name
  geometry   the triangles as an order-independent multiset: each triangle is
             its three corners' full vertex attributes (position, normal,
             diffuse, texture coordinates) rotated to a canonical start while
             keeping the winding, tagged with its material.  "exact" means
             bit-identical floats; "tolerant" allows --tolerance (relative)
  order      whether faces and vertices also come in the same order

Usage:
    python tools/ximport_compare.py oracle.bin candidate.bin [--tolerance 1e-6] [--details N]
"""

import argparse
import collections
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ximport_dump import read_dump  # noqa: E402


def fb(x):
    return struct.pack('<f', x)


def corner_exact(v):
    return (b''.join(fb(x) for x in v['pos']),
            b''.join(fb(x) for x in v['normal']) if v['normal'] is not None else None,
            v['diffuse'],
            tuple(b''.join(fb(x) for x in uv) for uv in v['uv']))


def corner_nonormal(v):
    e = corner_exact(v)
    return (e[0], e[2], e[3])


def tris(m, keyf):
    out = collections.Counter()
    for f, mat in zip(m['faces'], m['faceMaterials']):
        c = [keyf(m['vertices'][i]) for i in f]
        r = min(tuple(c[i:] + c[:i]) for i in range(3))
        out[(mat,) + r] += 1
    return out


def tris_normals(m):
    """Triangles keyed exactly without normals; the normals (in the same
    canonical corner order) are the values, compared with a tolerance."""
    out = collections.defaultdict(list)
    for f, mat in zip(m['faces'], m['faceMaterials']):
        c = [(corner_nonormal(m['vertices'][i]), m['vertices'][i]['normal']) for i in f]
        r = min((tuple(x[0] for x in c[i:] + c[:i]), tuple(x[1] for x in c[i:] + c[:i])) for i in range(3))
        out[(mat,) + r[0]].append(r[1])
    return out


def normals_close(na, nb, tol):
    if na == nb:
        return True
    if na is None or nb is None:
        return False
    for a, b in zip(na, nb):
        if a is None or b is None:
            return a is b
        if any(abs(x - y) > tol * max(1.0, abs(x)) for x, y in zip(a, b)):
            return False
    return True


def tolerant_equal(a, b, tol):
    A, B = tris_normals(a), tris_normals(b)
    if set(A) != set(B):
        return False, 0.0
    worst = 0.0
    for k in A:
        la, lb = sorted(A[k], key=repr), sorted(B[k], key=repr)
        if len(la) != len(lb):
            return False, 0.0
        used = [False] * len(lb)
        for na in la:
            for j, nb in enumerate(lb):
                if not used[j] and normals_close(na, nb, tol):
                    used[j] = True
                    if na and nb and na[0] is not None:
                        worst = max(worst, max(abs(x - y) for p, q in zip(na, nb) for x, y in zip(p, q)))
                    break
            else:
                return False, 0.0
    return True, worst


def compare(a, b, tol):
    res = collections.OrderedDict()
    res['counts'] = (a['vertexCount'] == b['vertexCount'] and len(a['faces']) == len(b['faces'])
                     and len(a['materials']) == len(b['materials']))
    res['layout'] = a['layout'] == b['layout']
    res['bounds exact'] = a['boundsMin'] == b['boundsMin'] and a['boundsMax'] == b['boundsMax']
    res['bounds tolerant'] = all(abs(x - y) <= tol * max(1.0, abs(x)) for x, y in
                                 zip(a['boundsMin'] + a['boundsMax'], b['boundsMin'] + b['boundsMax']))
    res['materials'] = a['materials'] == b['materials']
    res['textures'] = [m['texture'] for m in a['materials']] == [m['texture'] for m in b['materials']]
    ta, tb = tris(a, corner_exact), tris(b, corner_exact)
    res['triangles exact'] = ta == tb
    res['normal worst'] = 0.0
    if not res['triangles exact']:
        ok, worst = tolerant_equal(a, b, tol)
        res['triangles tolerant'] = ok
        res['normal worst'] = worst
    else:
        res['triangles tolerant'] = True
    res['subsets'] = a['subsets'] == b['subsets']
    res['face order'] = (a['faces'] == b['faces'] and a['faceMaterials'] == b['faceMaterials'])
    res['vertex order'] = [corner_exact(v) for v in a['vertices']] == [corner_exact(v) for v in b['vertices']]
    return res, (ta, tb)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('oracle')
    ap.add_argument('candidate')
    ap.add_argument('--tolerance', type=float, default=1e-6)
    ap.add_argument('--details', type=int, default=5)
    ap.add_argument('--only', default='')
    args = ap.parse_args()
    A = {m['path'].lower(): m for m in read_dump(args.oracle)}
    B = {m['path'].lower(): m for m in (read_dump(args.candidate))}
    keys = sorted(set(A) | set(B))
    total = collections.Counter()
    worst_normal = [0.0]
    shown = 0
    for k in keys:
        if args.only and args.only.lower() not in k:
            continue
        a, b = A.get(k), B.get(k)
        if not a or not b:
            total['missing in %s' % ('oracle' if not a else 'candidate')] += 1
            print('MISSING', k, 'oracle' if not a else 'candidate')
            continue
        if not a['ok'] or not b['ok']:
            total['import ok %s/%s' % (a['ok'], b['ok'])] += 1
            if a['ok'] != b['ok']:
                print('IMPORT', k, 'oracle ok=%s candidate ok=%s' % (a['ok'], b['ok']))
            continue
        res, (ta, tb) = compare(a, b, args.tolerance)
        worst_normal[0] = max(worst_normal[0], res.pop('normal worst'))
        for n, v in res.items():
            total['%s %s' % (n, 'same' if v else 'DIFF')] += 1
        if (not res['triangles tolerant'] or not res['counts'] or not res['materials'] or not res['layout']) and shown < args.details:
            shown += 1
            print('==', a['path'], {n: v for n, v in res.items() if not v})
            print('   counts v %d/%d f %d/%d' % (a['vertexCount'], b['vertexCount'], len(a['faces']), len(b['faces'])))
            miss, extra = ta - tb, tb - ta
            print('   triangles only in oracle %d, only in candidate %d' % (sum(miss.values()), sum(extra.values())))
    print()
    for n in sorted(total):
        print('%-40s %d' % (n, total[n]))
    print('%-40s %.3g' % ('largest normal difference', worst_normal[0]))
    bad = sum(v for n, v in total.items() if n.endswith('DIFF') and not n.startswith(('face order', 'vertex order', 'subsets', 'bounds exact', 'triangles exact'))) \
        + sum(v for n, v in total.items() if n.startswith(('missing', 'import')))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
