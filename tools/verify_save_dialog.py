"""Independently inspect the files produced through the real App and native saving controls."""
import hashlib
import io
import json
import struct
import sys
import zlib
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image, JpegImagePlugin

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
summary = json.loads((root/'.cache/autopilot/latest.json').read_text(encoding='utf-8'))
assert summary['passed'] and summary['executable_sha256'] == sha(exe)
name = '27-saving-options.gvn'
scenario = next(s for s in summary['scenarios'] if s['name'] == name)
assert scenario['passed'] and scenario['scenario_sha256'] == sha(root/'tests/autopilot'/name)
folder = Path(summary['run_directory'])/Path(name).stem
cases = json.loads((root/'tests/saving-dialog-cases.json').read_text(encoding='utf-8'))
files = {p.name: p for p in (folder/'pixels').iterdir() if p.is_file()}
assert set(files) == {case['name'] for case in cases}, 'Unexpected or missing output, including extension case'
counts = {}

def png_chunks(data):
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, chunks = 8, []
    while pos < len(data):
        size = int.from_bytes(data[pos:pos+4], 'big')
        kind = data[pos+4:pos+8]
        body = data[pos+8:pos+8+size]
        assert zlib.crc32(kind+body) == int.from_bytes(data[pos+8+size:pos+12+size], 'big')
        chunks.append((kind,body)); pos += size+12
    assert pos == len(data) and chunks[-1][0] == b'IEND'
    return chunks

def gif_stream(data):
    pos = 13 + (3*(2 << (data[10] & 7)) if data[10] & 128 else 0)
    while data[pos] == 0x21:
        pos += 2
        while data[pos]: pos += data[pos]+1
        pos += 1
    assert data[pos] == 0x2c
    flags = data[pos+9]; pos += 10
    if flags & 128: pos += 3*(2 << (flags & 7))
    minimum = data[pos]; pos += 1
    stream = bytearray()
    while data[pos]:
        size = data[pos]; stream.extend(data[pos+1:pos+1+size]); pos += size+1
    assert data[pos:] == b'\0;'
    return bool(flags & 64), minimum, stream

for case in cases:
    path = files[case['name']]
    data = path.read_bytes()
    with Image.open(folder/'assets'/case['source']) as opened:
        source = opened.convert('RGBA')
    matte = case.get('matte', 0xffffff)
    flat = Image.alpha_composite(Image.new('RGBA', source.size, ((matte>>16)&255,(matte>>8)&255,matte&255,255)),source)
    with Image.open(path) as decoded:
        assert decoded.size == source.size
        actual = decoded.convert('RGBA').tobytes()
        if case['format'] == 'Png':
            chunks = png_chunks(data); header = chunks[0][1]
            assert bool(header[12]) == case['interlaced']
            mode = case['transparency']
            expected = source.tobytes() if mode == 0 else bytearray(flat.tobytes())
            assert header[9] == (6 if mode == 0 else 2)
            if mode >= 2:
                corners = ((0,0),(source.width-1,0),(0,source.height-1),(source.width-1,source.height-1))
                key = ((case['key']>>16)&255,(case['key']>>8)&255,case['key']&255) if mode == 2 else flat.getpixel(corners[mode-3])[:3]
                assert next(body for kind,body in chunks if kind==b'tRNS') == struct.pack('>HHH',*key)
                for pos in range(0,len(expected),4):
                    if tuple(expected[pos:pos+3])==key: expected[pos+3]=0
            assert actual == expected
            if not case['interlaced']:
                raw = zlib.decompress(b''.join(body for kind,body in chunks if kind==b'IDAT'))
                filters = raw[::source.width*4+1]
                chosen = case['filter']
                assert set(filters) <= ({1,2} if chosen == 6 else {chosen-1} if chosen else set(range(5)))
        elif case['format'] == 'Jpeg':
            assert bool(decoded.info.get('progressive')) == case['progressive']
            assert JpegImagePlugin.get_sampling(decoded) == 0
            reference = io.BytesIO()
            flat.convert('RGB').save(reference,format='JPEG',quality=case['quality'],subsampling=0)
            with Image.open(io.BytesIO(reference.getvalue())) as expected:
                assert decoded.quantization == expected.quantization, 'Native quality edit did not reach JPEG encoder'
        elif case['format'] == 'Bmp':
            bits = int.from_bytes(data[28:30],'little')
            height = int.from_bytes(data[22:26],'little',signed=True)
            compression = int.from_bytes(data[30:34],'little')
            assert bits == case['bits']
            assert compression == ((2 if bits==4 else 1) if case['rle'] else 0)
            assert height == (source.height if compression or not case['top'] else -source.height)
            if bits != 1: assert actual == flat.tobytes()
            else: assert len(decoded.convert('RGB').getcolors(source.width*source.height)) == 2
            if case.get('dither'):
                reference_case = next(c for c in cases if c['name'] == 'bmp-1-plain.bmp')
                assert reference_case['source'] == case['source'] and reference_case['matte'] == case['matte']
                with Image.open(files['bmp-1-plain.bmp']) as plain:
                    assert plain.size == decoded.size
                    assert actual != plain.convert('RGBA').tobytes(), 'Dither toggle did not change reduced pixels'
        else:
            interlaced, minimum, stream = gif_stream(data)
            assert interlaced == case['interlaced']
            # The exact-color source assigns palette indices by first appearance.
            colors, indices = [], bytearray()
            for color in source.getdata():
                rgb = color[:3]
                if rgb not in colors: colors.append(rgb)
                indices.append(colors.index(rgb))
            mode = case['transparency']
            key = [-1,case['index'],indices[0],indices[source.width-1],indices[-source.width],indices[-1]][mode]
            assert decoded.info.get('transparency',-1) == key
            expected = bytes(c for i,index in enumerate(indices) for c in (*colors[index],0 if index==key else 255))
            assert actual == expected and decoded.tobytes() == indices
            if case['uncompressed']:
                width, clear, raster = minimum+1, 1<<minimum, bytearray()
                ended = False
                for bit in range(0,len(stream)*8-width+1,width):
                    value = sum(((stream[(bit+i)//8]>>((bit+i)%8))&1)<<i for i in range(width))
                    if value == clear+1: ended=True; break
                    if value != clear:
                        assert value < clear, 'Uncompressed checkbox produced dictionary codes'
                        raster.append(value)
                rows = range(source.height) if not interlaced else [y for start,step in ((0,8),(4,8),(2,4),(1,2)) for y in range(start,source.height,step)]
                assert ended and raster == b''.join(indices[y*source.width:(y+1)*source.width] for y in rows)
        counts[case['format']] = counts.get(case['format'],0)+1
    if case.get('keep_times'):
        assert path.stat().st_mtime_ns == (folder/'assets'/case['source']).stat().st_mtime_ns

report = dict(passed=True,verified_at=datetime.now(timezone.utc).isoformat(),outputs=len(cases),formats=counts,
              executable_sha256=sha(exe),scenario_sha256=scenario['scenario_sha256'],cases_sha256=sha(root/'tests/saving-dialog-cases.json'),
              verifier_sha256=sha(Path(__file__)), run_directory=str(folder),
              file_sha256={name:sha(path) for name,path in files.items()},
              method='Real App SaveAs, native dialog controls, private desktop; independent Pillow and file-structure checks')
(folder/'independent-save-check.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
(root/'.cache/save-dialog-oracle-latest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(passed=True,outputs=len(cases),formats=counts,run_directory=str(folder))))
