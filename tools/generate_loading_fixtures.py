"""Small generated loading-option inputs with explicit colors and independent EXIF oracles."""
from pathlib import Path
import json
import struct
import zlib
import hashlib
from PIL import Image, ImageOps

root = Path(__file__).resolve().parents[1] / 'tests/fixtures/loading'
root.mkdir(parents=True, exist_ok=True)
pixels = [(255, 0, 0, 255), (0, 255, 0, 128), (0, 0, 255, 0),
          (10, 20, 30, 64), (250, 240, 230, 255), (19, 28, 37, 0)]

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

def png(name, depth, color, raw, background, palette=b'', transparency=b''):
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 3, 2, depth, color, 0, 0, 0))
    if palette:
        data += chunk(b'PLTE', palette)
    if transparency:
        data += chunk(b'tRNS', transparency)
    data += chunk(b'bKGD', background) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    (root / name).write_bytes(data)

png('rgba-background.png', 8, 6,
    b''.join(b'\0' + bytes(v for p in pixels[y*3:y*3+3] for v in p) for y in range(2)), struct.pack('>3H', 18, 52, 86))
png('indexed-background.png', 8, 3, b'\0\0\1\2\0\2\1\0', b'\x01',
    bytes([10,20,30,18,52,86,200,100,50]), bytes([0,255,128]))
png('gray2-background.png', 2, 0, b'\0\x18\0\xe4', struct.pack('>H', 2), transparency=struct.pack('>H', 0))
png('gray16-background.png', 16, 4,
    b''.join(b'\0'+b''.join(struct.pack('>2H', g, a) for g,a in row) for row in
             [[(0x1111,0),(0x8888,0x8080),(0xffff,0xffff)],[(0,0xffff),(0x4444,0),(0xaaaa,0xffff)]]), struct.pack('>H', 0x3333))
png('rgb16-trns-background.png', 16, 2,
    b''.join(b'\0'+b''.join(struct.pack('>3H',*p) for p in row) for row in
             [[(0x1111,0x2222,0x3333),(0xffff,0,0),(0,0xffff,0)],[(0,0,0xffff),(0,0,0),(0xffff,0xffff,0xffff)]]),
    struct.pack('>3H',0x1212,0x3434,0x5656),transparency=struct.pack('>3H',0x1111,0x2222,0x3333))
apng=(root.parent/'blend.apng').read_bytes()
(root/'background.apng').write_bytes(apng[:33]+chunk(b'bKGD',struct.pack('>3H',18,52,86))+apng[33:])
for header_size in [108,124]:
    header = bytearray(header_size)
    struct.pack_into('<IiiHHIIiiII', header, 0, header_size, 3, 2, 1, 32, 3, 24, 0, 0, 0, 0)
    struct.pack_into('<5I', header, 40, 0xff0000, 0xff00, 0xff, 0xff000000, 0x73524742)
    raw = bytes(v for y in [1,0] for p in pixels[y*3:y*3+3] for v in [p[2],p[1],p[0],p[3]])
    (root/f'alpha-v{4 if header_size==108 else 5}.bmp').write_bytes(
        b'BM'+struct.pack('<IHHI', 14+header_size+len(raw),0,0,14+header_size)+header+raw)
palette=[10,20,30,255,0,0,18,52,86]+[0]*(768-9)
first=Image.new('P',(3,2));first.putpalette(palette);first.putdata([0,1,2,2,1,0])
first.save(root/'static-background.gif',transparency=0,background=2,optimize=False)
second=first.copy();second.putdata([1,2,0,0,2,1])
first.save(root/'animated-background.gif',save_all=True,append_images=[second],duration=[40,80],transparency=0,background=2,disposal=2,optimize=False)
partial=bytearray((root/'static-background.gif').read_bytes())
struct.pack_into('<HH',partial,6,6,4)
offset=13+3*(1<<((partial[10]&7)+1))
while partial[offset]==0x21:
    offset+=2
    while partial[offset]:offset+=1+partial[offset]
    offset+=1
assert partial[offset]==0x2c
struct.pack_into('<HH',partial,offset+1,2,1)
(root/'first-offset.gif').write_bytes(partial)
rgb=Image.new('RGB',(3,2));rgb.putdata([p[:3] for p in pixels])
for orientation in range(1,9):
    exif=Image.Exif();exif[274]=orientation
    name=f'exif-{orientation}.jpg';rgb.save(root/name,quality=100,subsampling=0,exif=exif)
    with Image.open(root/name) as decoded:
        ImageOps.exif_transpose(decoded).save(root/f'exif-{orientation}-expected.png')
manifest={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.iterdir()) if p.suffix!='.json'}
(root/'manifest.json').write_text(json.dumps(dict(generator='tools/generate_loading_fixtures.py',files=manifest),indent=2)+'\n',encoding='utf-8')
print('Generated',len(manifest),'loading fixtures')
