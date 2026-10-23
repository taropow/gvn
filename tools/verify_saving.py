"""Independently decode every new PNG/JPEG option combination using Pillow."""
import hashlib
import json
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image, ImageChops, JpegImagePlugin

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
folder = root/'.cache/saving-oracle'/('run-'+uuid.uuid4().hex)
folder.mkdir(parents=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
executable_hash = sha(exe)
paths = ['CMakeLists.txt', 'cmake/SaveCodecs.cmake', 'include/gvn/saving.h', 'src/saving.cpp',
         'src/saving_indexed.cpp', 'src/saving_file.cpp', 'src/saving_file.h',
         'tests/saving_tests.cpp', 'tools/verify_saving.py',
         'tools/verify_vendor_sources.py', 'third_party/manifest.json']
source_hashes = {p: sha(root/p) for p in paths}
result = subprocess.run([str(exe), '--oracle', str(folder)], capture_output=True, timeout=60)
assert result.returncode == 0, result.stdout + result.stderr
report = json.loads(result.stdout)
assert report['source_unchanged']
source = Image.frombytes('RGBA', (report['width'], report['height']), (folder/'source.rgba').read_bytes())
flat = Image.alpha_composite(Image.new('RGBA', source.size, (18, 52, 86, 255)), source)
errors = []
for path in sorted(folder.glob('png-*.png')):
    with Image.open(path) as decoded:
        if decoded.size != source.size or decoded.convert('RGBA').tobytes() != source.tobytes():
            errors.append(path.name)
with Image.open(folder/'opaque.png') as decoded:
    assert decoded.convert('RGBA').tobytes() == flat.tobytes(), 'Opaque PNG differs from independent compositing'
expected = bytearray(flat.tobytes())
for index in range(0, len(expected), 4):
    if expected[index:index+3] == b'\xff\0\0':
        expected[index+3] = 0
with Image.open(folder/'color.png') as decoded:
    assert decoded.convert('RGBA').tobytes() == expected, 'PNG RGB transparent key differs'
for corner, coordinate in enumerate([(0,0),(source.width-1,0),(0,source.height-1),(source.width-1,source.height-1)]):
    key = flat.getpixel(coordinate)[:3]
    expected = bytearray(flat.tobytes())
    for index in range(0, len(expected), 4):
        if tuple(expected[index:index+3]) == key:
            expected[index+3] = 0
    with Image.open(folder/f'corner-{corner}.png') as decoded:
        assert decoded.convert('RGBA').tobytes() == expected, 'PNG corner key differs'
quality_errors = {}
for quality in (0, 1, 15, 50, 93, 95, 100):
    baseline = None
    for progressive in (0, 1):
        with Image.open(folder/f'jpeg-{quality}-{progressive}.jpg') as decoded:
            assert decoded.size == source.size
            assert bool(decoded.info.get('progressive')) == bool(progressive)
            assert JpegImagePlugin.get_sampling(decoded) == 0, 'JPEG is not 4:4:4'
            actual = decoded.convert('RGB').tobytes()
            if baseline is None:
                baseline = actual
            else:
                assert actual == baseline, 'Baseline and progressive pixels differ'
            reference = flat.convert('RGB').tobytes()
            quality_errors[quality] = sum((a-b)**2 for a,b in zip(actual, reference))
assert quality_errors[95] < quality_errors[15]
assert not errors, errors
assert sha(exe) == executable_hash
assert source_hashes == {p: sha(root/p) for p in paths}
report.update(passed=True, method='Native libpng/libjpeg-turbo encode; independent Pillow PNG/JPEG decode; independent source-over compositing; no viewer UI or user clipboard',
              verified_at=datetime.now(timezone.utc).isoformat(), executable_sha256=executable_hash,
              source_sha256=source_hashes, png_rgba_errors=0, jpeg_baseline_progressive_pixel_errors=0,
              jpeg_444_verified=True, jpeg_lossy=True, quality_squared_errors=quality_errors,
              file_sha256={p.name: sha(p) for p in folder.iterdir() if p.is_file()},
              run_directory=str(folder))
(folder/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
(root/'.cache/saving-oracle/latest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({k: report[k] for k in ['passed','png_options','png_transparency','jpeg_options','png_rgba_errors','jpeg_baseline_progressive_pixel_errors','run_directory']}))
