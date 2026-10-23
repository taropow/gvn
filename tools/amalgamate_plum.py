from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1] / 'third_party' / 'libplum'
destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)

def merge(path, seen):
    path = path.resolve()
    if path in seen: return ''
    seen.add(path)
    def include(match): return merge(path.parent / match.group(1), seen)
    return re.sub(r'^\s*#include "([^\"]+)"\s*$', include, path.read_text(encoding='utf-8'), flags=re.M) + '\n'

seen = set()
source = '''/* MSVC's C17 headers do not provide max_align_t. Its CRT malloc is 16-byte aligned on x64. */
#include <stddef.h>
#include <stdlib.h>
#if defined(_MSC_VER) && !defined(__clang__)
typedef struct __declspec(align(16)) { long long integer; long double floating; } max_align_t;
static void* gvn_plum_aligned_alloc(size_t alignment, size_t size) { return alignment <= 16 ? malloc(size) : NULL; }
#define aligned_alloc gvn_plum_aligned_alloc
#endif
''' + ''.join(merge(path, seen) for path in sorted((root / 'src').glob('*.c')))
# MSVC accepts C17 restrict pointers, but not C99's qualified array parameters.
# Removing these optimization-only parameter annotations preserves the ABI and dimensions.
source = re.sub(r'\[\s*(?:(?:restrict|static)\s+)+', '[', source)
for name, content in [('libplum.c', source), ('libplum.h', merge(root / 'header' / 'libplum.h', set()))]:
    path = destination / name
    if not path.exists() or path.read_text(encoding='utf-8') != content:
        path.write_text(content, encoding='utf-8')
