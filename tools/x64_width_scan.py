"""x64 pointer-width scan (RS2EX v0.3.0).

Two halves, because each finds what the other cannot:

  warnings   Turn an MSBuild log of an x64 build into a de-duplicated table
             and split it into width-related warnings (pointer truncation
             C4311/C4302, integer-to-larger-pointer C4312, size_t narrowing
             C4267, and C4244/C4242/C4838/C4477 whenever a pointer-sized or
             64-bit type is involved) and architecture-neutral ones.
  source     Scan every file RailSim2.vcxproj builds for the hazards MSVC does
             not warn about: non-Ptr window-long APIs, narrowing casts of
             WPARAM/LPARAM, pointer-to-32-bit casts of `this` or addresses,
             _findfirst handles, ftell, sizeof applied to a pointer, inline
             assembly, x86 intrinsics, #pragma pack, dynamic loading, and the
             save-file pointer syntax (%p / AsgnPointer / HexPointer).

Comments and #if 0 blocks are stripped before the source is matched.

Usage:
    python tools/x64_width_scan.py warnings <msbuild.log> <out.md>
    python tools/x64_width_scan.py source <out.md>
"""

import collections
import io
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

WIDTH_CODES = {'C4311', 'C4312', 'C4302', 'C4267'}
WIDE_TYPES = r"__int64|size_t|ptrdiff|intptr|uintptr|LONG_PTR|INT_PTR|UINT_PTR|DWORD_PTR|ULONG_PTR|SIZE_T|LPARAM|WPARAM|LRESULT"


def warnings(log, out):
    text = io.open(log, encoding='utf-8', errors='replace').read()
    rx = re.compile(r'^\s*(?P<file>[A-Za-z]:\\[^(]+)\((?P<line>\d+)(?:,\d+)?\): warning (?P<code>C\d+): (?P<msg>.*?)(?:\s+\[[A-Za-z]:.*\])?$', re.M)
    seen, rows = set(), []
    for m in rx.finditer(text):
        f = re.sub(r'^.*?\\railsim2\\', '', m.group('file'), flags=re.I)
        msg = m.group('msg').strip()
        if msg in ('with', '[', ']') or re.match(r'^_Ty=', msg):
            continue
        key = (f, m.group('line'), m.group('code'), msg)
        if key not in seen:
            seen.add(key)
            rows.append(dict(file=f, line=int(m.group('line')), code=m.group('code'), msg=msg))

    def is_width(r):
        if r['code'] in WIDTH_CODES:
            return True
        return r['code'] in ('C4244', 'C4242', 'C4838', 'C4477') and bool(re.search(WIDE_TYPES, r['msg']))

    w = [r for r in rows if is_width(r)]
    n = [r for r in rows if not is_width(r)]
    with io.open(out, 'w', encoding='utf-8') as fp:
        fp.write('# x64 warning table (%s)\n\nwidth-related: %d, other: %d\n\n' % (os.path.basename(log), len(w), len(n)))
        fp.write('width by code: %s\n\n' % dict(collections.Counter(r['code'] for r in w)))
        for title, group in (('width-related', w), ('other', n)):
            fp.write('## %s\n\n| file | line | code | message |\n|---|---|---|---|\n' % title)
            for r in sorted(group, key=lambda r: (r['file'], r['line'])):
                fp.write('| %s | %d | %s | %s |\n' % (r['file'], r['line'], r['code'], r['msg'].replace('|', '\\|')))
            fp.write('\n')
    print('width %d %s' % (len(w), dict(collections.Counter(r['code'] for r in w))))
    print('other %d %s' % (len(n), dict(collections.Counter(r['code'] for r in n))))


SOURCE_CHECKS = collections.OrderedDict([
    ('SetWindowLong/GetWindowLong (non-Ptr)', r'\b[SG]etWindowLong[AW]?\s*\(|\b[SG]etClassLong[AW]?\s*\('),
    ('GWL_/GCL_/DWL_ indices', r'\bGWL_\w+|\bGCL_\w+|\bDWL_\w+'),
    ('inherited DWORD_PTR typedef / __int3264', r'__int3264|typedef[^;]*DWORD_PTR'),
    ('WPARAM/LPARAM casts to a narrow type', r'\((?:int|long|DWORD|UINT|WORD|short|unsigned int|unsigned long)\)\s*\(?\s*[lw]Param\b'),
    ('pointer-to-32-bit casts of this / an address', r'\((?:DWORD|int|long|UINT|unsigned int|unsigned long|LONG|ULONG)\)\s*(?:&|this\b)'),
    ('(DWORD) casts', r'\(\s*DWORD\s*\)'),
    ('_findfirst/_findnext/_findclose', r'\b_findfirst\w*\s*\(|\b_findnext\w*\s*\(|\b_findclose\s*\('),
    ('ftell / fseek', r'\bftell\s*\(|\bfseek\s*\('),
    ('inline asm', r'\b__asm\b|\b_asm\b'),
    ('naked functions', r'__declspec\s*\(\s*naked\s*\)'),
    ('x86 intrinsics', r'\b_mm_\w+|\b__cpuid\b|\b__?rdtsc\b|#\s*include\s*[<"](?:xmmintrin|emmintrin|intrin|mmintrin)\.h'),
    ('#pragma pack', r'#\s*pragma\s+pack'),
    ('dynamic loading', r'\bLoadLibrary\w*\s*\(|\bGetProcAddress\s*\(|\bFreeLibrary\s*\('),
    ('%p in format strings', r'"[^"\n]*%p[^"\n]*"'),
    ('AsgnPointer / HexPointer', r'\bAsgnPointer\s*\(|\bHexPointer\s*\('),
])


def code_of(s):
    s = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return re.sub(r'^\s*#\s*if\s+0\b.*?^\s*#\s*endif', lambda m: '\n' * m.group(0).count('\n'), s, flags=re.S | re.M)


def built_files():
    proj = io.open(os.path.join(ROOT, 'RailSim2.vcxproj'), encoding='utf-8-sig').read()
    names = set(p.replace('\\', '/') for p in re.findall(r'<Cl(?:Compile|Include) Include="([^"]+)"', proj))
    return sorted(names | {'lib/headers.h', 'lib/udx.h', 'lib/libraries.h'})


def source(out):
    hits = collections.defaultdict(list)
    sizeof_ptr = []
    files = built_files()
    for f in files:
        path = os.path.join(ROOT, f)
        if not os.path.exists(path):
            continue
        lines = code_of(io.open(path, 'rb').read().decode('cp932', 'replace')).split('\n')
        for name, pat in SOURCE_CHECKS.items():
            rx = re.compile(pat)
            hits[name].extend('%s:%d: %s' % (f, i, l.strip()[:140]) for i, l in enumerate(lines, 1) if rx.search(l))
        for i, l in enumerate(lines):
            for m in re.finditer(r'sizeof\s*\(\s*(\w+)\s*\)', l):
                back = '\n'.join(lines[max(0, i - 80):i + 1])
                v = m.group(1)
                if re.search(r'\b(?:char|BYTE|void|WCHAR|wchar_t|unsigned char|int|DWORD|float)\s*\*+\s*%s\b' % v, back) \
                   and not re.search(r'\b%s\s*\[' % v, back):
                    sizeof_ptr.append('%s:%d: %s' % (f, i + 1, l.strip()[:140]))
    hits['sizeof applied to a pointer'] = sizeof_ptr
    with io.open(out, 'w', encoding='utf-8') as fp:
        fp.write('# x64 source width scan (%d built files)\n\n' % len(files))
        for name in list(SOURCE_CHECKS) + ['sizeof applied to a pointer']:
            fp.write('## %s — %d\n\n' % (name, len(hits[name])))
            fp.writelines('- `%s`\n' % x.replace('`', "'") for x in hits[name])
            fp.write('\n')
    for name in list(SOURCE_CHECKS) + ['sizeof applied to a pointer']:
        print('%-50s %d' % (name, len(hits[name])))


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'warnings':
        warnings(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == 'source':
        source(sys.argv[2])
    else:
        sys.exit(__doc__)
