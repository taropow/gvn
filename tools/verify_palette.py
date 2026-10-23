"""Compare loaded/edited/saved palettes and indices against independent Pillow fixtures."""
import hashlib, json, struct, subprocess, sys, uuid, zlib
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
folder = root/'.cache/palette-oracle'/('run-'+uuid.uuid4().hex)
folder.mkdir(parents=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
paths = ['CMakeLists.txt','include/gvn/core.h','include/gvn/edit.h','include/gvn/saving.h',
         'src/core.cpp','src/codecs.cpp','src/indexed_loading.h','src/loading.h','src/edit.cpp',
         'src/edit_advanced.cpp','src/saving.cpp','src/saving_indexed.cpp','src/saving_file.cpp',
         'tests/palette_tests.cpp','tests/palette-cases.json','tools/verify_palette.py']
sources = {p:sha(root/p) for p in paths}
exe_sha = sha(exe)
proc = subprocess.run([str(exe),'--oracle',str(folder)],capture_output=True,timeout=70)
assert proc.returncode == 0, proc.stdout+proc.stderr
run = json.loads(proc.stdout)
assert run['passed'] and len(run['cases']) == run['outputs'] == 567
manifest = json.loads((root/'tests/palette-cases.json').read_text())
cases = {c['name']:c for c in manifest['cases']}

def transformed(source, op):
    im = Image.new('P',(manifest['width'],manifest['height']))
    im.putdata(source['indices'])
    if op == 1: im = im.crop((3,2,20,15))
    elif op == 2: im = im.transpose(Image.Transpose.ROTATE_270)
    elif op == 3: im = im.transpose(Image.Transpose.ROTATE_180)
    elif op == 4: im = im.transpose(Image.Transpose.ROTATE_90)
    elif op == 5: im = im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    elif op == 6: im = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    elif op == 7: im = im.resize((53,31),Image.Resampling.NEAREST)
    else: assert op == 0
    return im

def flattened(color):
    r,g,b,a = color
    return tuple((c*a+255*(255-a)+127)//255 for c in (r,g,b))+(255,)

formats = dict(png=0,bmp=0,gif=0)
files = {}
for case in run['cases']:
    source = cases[case['source']]
    expected = transformed(source,case['op'])
    indices = list(expected.getdata())
    palette = [tuple(c) for c in source['palette']]
    if case['format'] != 'png' or case['transparency'] != 0:
        palette = [flattened(c) for c in palette]
    if case['format'] == 'png' and case['transparency'] >= 2:
        mode = case['transparency']
        corner = {3:0,4:expected.width-1,5:(expected.height-1)*expected.width,6:expected.width*expected.height-1}
        key = palette[indices[corner[mode]]][:3] if mode >= 3 else (0,0,0)
        palette = [c[:3]+(0 if c[:3] == key else 255,) for c in palette]
    path = folder/case['file']
    data = path.read_bytes()
    with Image.open(path) as actual:
        actual.load()
        assert actual.mode == 'P' and actual.size == expected.size,case
        assert list(actual.getdata()) == indices,('index mismatch',case)
        colors = actual.getpalette()
        assert colors[:len(palette)*3] == [v for c in palette for v in c[:3]],('palette mismatch',case)
        assert actual.convert('RGBA').tobytes() == bytes(v for index in indices for v in palette[index]),('RGBA mismatch',case)
    if case['format'] == 'png':
        assert data[24] == source['bits'] and data[25] == 3 and data[28] == case['interlaced'],case
        pos = 8; chunks = {}
        while pos < len(data):
            size = int.from_bytes(data[pos:pos+4],'big'); kind = data[pos+4:pos+8]
            payload = data[pos+8:pos+8+size]
            assert zlib.crc32(kind+payload) == int.from_bytes(data[pos+8+size:pos+12+size],'big')
            chunks.setdefault(kind,bytearray()).extend(payload); pos += size+12
        assert len(chunks[b'PLTE']) == len(palette)*3
        if case['interlaced'] == 0:
            raw = zlib.decompress(chunks[b'IDAT']); stride=(expected.width*source['bits']+7)//8+1
            assert len(raw)==stride*expected.height
            filters={raw[y*stride] for y in range(expected.height)}
            if case['filter'] in (1,2,3,4,5): assert filters=={case['filter']-1},case
            if case['filter']==6: assert filters <= {1,2},case
    elif case['format'] == 'bmp':
        assert struct.unpack_from('<H',data,28)[0] == (4 if source['bits']==2 else source['bits']),case
    else:
        assert data[:6] in (b'GIF89a',b'GIF87a')
    formats[case['format']] += 1
    files[case['file']] = sha(path)
assert sha(exe)==exe_sha
assert all(sha(root/p)==h for p,h in sources.items())
report = dict(passed=True,verified_at=datetime.now(timezone.utc).isoformat(),outputs=run['outputs'],formats=formats,
              source_fixtures=len(cases),palette_index_errors=0,rgba_pixel_errors=0,packed_depth_and_palette_verified=True,
              method='Native WIC source indices, geometry and libpng/BMP/GIF saves; independent fixture palette/indices, Pillow transforms/decode, PNG CRC/IHDR/PLTE/filter and BMP depth checks',
              run_directory=str(folder),executable_sha256=exe_sha,source_sha256=sources,file_sha256=files)
(folder.parent/'latest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('file_sha256','source_sha256')}))
