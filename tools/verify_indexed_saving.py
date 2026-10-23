"""Independent Pillow decode and file-structure checks for BMP/GIF save settings."""
import hashlib
import json
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
folder = root/'.cache/indexed-saving-oracle'/('run-'+uuid.uuid4().hex)
folder.mkdir(parents=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
paths = ['CMakeLists.txt', 'cmake/SaveCodecs.cmake', 'include/gvn/saving.h', 'src/saving.cpp',
         'src/saving_indexed.cpp', 'src/saving_file.cpp', 'src/saving_file.h',
         'tests/indexed_saving_tests.cpp', 'tools/verify_indexed_saving.py',
         'tools/verify_vendor_sources.py', 'third_party/manifest.json']
sources = {p: sha(root/p) for p in paths}
executable_hash = sha(exe)
run = subprocess.run([str(exe), '--oracle', str(folder)], capture_output=True, timeout=60)
assert run.returncode == 0, run.stdout + run.stderr
report = json.loads(run.stdout)
assert report['passed'] and report['source_unchanged']

def gif_stream(data):
    assert data[:6] == b'GIF89a'
    pos = 13 + (3*(2 << (data[10] & 7)) if data[10] & 128 else 0)
    while True:
        record = data[pos]
        pos += 1
        if record == 0x21:
            pos += 1
            while data[pos]:
                pos += data[pos]+1
            pos += 1
        else:
            assert record == 0x2c
            flags = data[pos+8]
            pos += 9 + (3*(2 << (flags & 7)) if flags & 128 else 0)
            minimum = data[pos]
            pos += 1
            stream = bytearray()
            while data[pos]:
                count = data[pos]
                stream.extend(data[pos+1:pos+1+count])
                pos += count+1
            assert data[pos:] == b'\0;'
            return bool(flags & 64), minimum, stream

def literals(minimum, stream):
    width = minimum+1
    clear = 1 << minimum
    count = len(stream)*8
    raster = bytearray()
    saw_clear = False
    for pos in range(0, count-width+1, width):
        value = sum(((stream[(pos+i)//8] >> ((pos+i)%8)) & 1) << i for i in range(width))
        if value == clear+1:
            assert saw_clear
            return bytes(raster)
        if value == clear:
            saw_clear = True
        else:
            assert saw_clear and value < clear, 'Dictionary code present in uncompressed GIF'
            raster.append(value)
    raise AssertionError('Missing GIF end code')

bmp_count = gif_count = 0
for bits in (1,4,8):
    for kind, size in enumerate(((37,23),(512,27),(7,3))):
        prefix = f'{bits}-{kind}'
        palette = (folder/(prefix+'.palette')).read_bytes()
        indices = (folder/(prefix+'.indices')).read_bytes()
        # Independently reconstruct the fixture instead of trusting an encoder's expected output.
        expected_palette = [(i*37 % 256, i*71 % 256, i*109 % 256,255) for i in range(1 << bits)]
        if bits > 1:
            expected_palette[1] = expected_palette[0]
        assert palette == bytes(c for p in expected_palette for c in p)
        width, height = size
        expected_indices = bytes(((x//128+y) if kind == 1 else (x*7+y*13+x*y)) % (1 << bits)
                                 for y in range(height) for x in range(width))
        assert indices == expected_indices
        expected_rgba = bytes(c for index in indices for c in expected_palette[index])
        for top in (0,1):
            for rle in (0,1):
                path = folder/f'{prefix}-bmp-{top}-{rle}.bmp'
                data = path.read_bytes()
                assert data[:2] == b'BM' and int.from_bytes(data[28:30], 'little') == bits
                compression = int.from_bytes(data[30:34], 'little')
                signed_height = int.from_bytes(data[22:26], 'little', signed=True)
                assert compression in ((0,) if bits == 1 else (0, 2 if bits == 4 else 1))
                assert signed_height == (height if compression or not top else -height)
                if kind == 1 and rle and bits > 1:
                    assert compression == (2 if bits == 4 else 1), 'RLE option did not compress run fixture'
                if not rle:
                    assert compression == 0
                with Image.open(path) as decoded:
                    assert decoded.size == size and decoded.convert('RGBA').tobytes() == expected_rgba
                    assert decoded.mode == 'P' and decoded.tobytes() == indices, 'BMP changed palette indices'
                bmp_count += 1
        for interlaced in (0,1):
            for raw in (0,1):
                for mode in range(6):
                    path = folder/f'{prefix}-gif-{interlaced}-{raw}-{mode}.gif'
                    wire_interlace, minimum, stream = gif_stream(path.read_bytes())
                    assert wire_interlace == bool(interlaced)
                    transparent = [-1,1,indices[0],indices[width-1],indices[-width],indices[-1]][mode]
                    expected = bytearray(expected_rgba)
                    for i, index in enumerate(indices):
                        if index == transparent:
                            expected[i*4+3] = 0
                    with Image.open(path) as decoded:
                        assert decoded.size == size and not getattr(decoded,'is_animated', False)
                        assert decoded.info.get('transparency', -1) == transparent
                        assert decoded.mode == 'P' and decoded.tobytes() == indices, 'GIF changed palette indices'
                        assert decoded.convert('RGBA').tobytes() == expected
                    if raw:
                        rows = range(height) if not interlaced else [y for start, step in ((0,8),(4,8),(2,4),(1,2))
                                                                    for y in range(start,height,step)]
                        assert literals(minimum,stream) == b''.join(indices[y*width:(y+1)*width] for y in rows)
                    gif_count += 1

source = Image.frombytes('RGBA', (67,41), (folder/'gradient.rgba').read_bytes())
flat = Image.alpha_composite(Image.new('RGBA', source.size, (18,52,86,255)), source)
for top in (0,1):
    with Image.open(folder/f'gradient-{top}.bmp') as decoded:
        assert decoded.convert('RGBA').tobytes() == flat.tobytes()
    bmp_count += 1
quantization_errors = {}
for bits in (1,4,8):
    for dither in (0,1):
        prefix = f'quantized-{bits}-{dither}'
        palette = (folder/(prefix+'.palette')).read_bytes()
        indices = (folder/(prefix+'.indices')).read_bytes()
        assert 1 < len(palette)//4 <= (1 << bits) and max(indices) < len(palette)//4
        expected = b''.join(palette[i*4:(i+1)*4] for i in indices)
        with Image.open(folder/(prefix+'.bmp')) as decoded:
            assert decoded.size == source.size and decoded.convert('RGBA').tobytes() == expected
            quantization_errors[prefix] = sum((a-b)**2 for a,b in zip(expected,flat.tobytes()))
        bmp_count += 1
for dither in (0,1):
    assert quantization_errors[f'quantized-8-{dither}'] < quantization_errors[f'quantized-4-{dither}']
    assert quantization_errors[f'quantized-4-{dither}'] < quantization_errors[f'quantized-1-{dither}']
assert bmp_count == report['bmp_outputs'] == 44
assert gif_count == report['gif_outputs'] == 216
assert sha(exe) == executable_hash and sources == {p:sha(root/p) for p in paths}
report.update(verified_at=datetime.now(timezone.utc).isoformat(), executable_sha256=executable_hash,
              source_sha256=sources, bmp_rgba_and_index_errors=0, gif_rgba_and_index_errors=0,
              gif_literal_only_lzw_verified=True, quantization_squared_errors=quantization_errors,
              method='Native BMP/GIF encode, independent Pillow decode, palette indices and file structure; no viewer UI or user clipboard',
              run_directory=str(folder), file_sha256={p.name:sha(p) for p in folder.iterdir() if p.is_file()})
(folder/'result.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
(root/'.cache/indexed-saving-oracle/latest.json').write_text(json.dumps(report,indent=2)+'\n', encoding='utf-8')
print(json.dumps({k:report[k] for k in ('passed','bmp_outputs','gif_outputs','bmp_rgba_and_index_errors',
                                      'gif_rgba_and_index_errors','run_directory')}))
