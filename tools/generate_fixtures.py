from pathlib import Path
from PIL import Image
import struct
import zlib

root = Path(__file__).resolve().parents[1] / 'tests' / 'fixtures'
root.mkdir(parents=True, exist_ok=True)
pixels = [(255, 0, 0, 255), (0, 255, 0, 128), (0, 0, 255, 0),
          (10, 20, 30, 64), (250, 240, 230, 255), (0, 0, 0, 0)]
image = Image.new('RGBA', (3, 2)); image.putdata(pixels)
image.save(root / 'alpha.png')
image.convert('RGB').save(root / 'rgb.bmp')
image.convert('RGB').save(root / 'rgb.jpg', quality=98)
exif = Image.Exif(); exif[274] = 6
image.convert('RGB').save(root / 'rotated.jpg', exif=exif)

def psd(bits=8, compression=0, gray=False):
    channels = 2 if gray else 4
    rows = []
    for channel in range(channels):
        source_channel = (0 if channel == 0 else 3) if gray else channel
        for y in range(2):
            row = b''.join(bytes([pixels[y * 3 + x][source_channel]]) if bits == 8
                           else struct.pack('>H', pixels[y * 3 + x][source_channel] * 257) for x in range(3))
            rows.append(row)
    header = b'8BPS' + struct.pack('>H', 1) + b'\0' * 6 + struct.pack('>HIIHH', channels, 2, 3, bits, 1 if gray else 3) + b'\0' * 12
    raw = b''.join(rows)
    if compression == 1:
        packed = [bytes([len(row) - 1]) + row for row in rows]
        raw = b''.join(struct.pack('>H', len(row)) for row in packed) + b''.join(packed)
    elif compression in (2, 3):
        if compression == 3:
            predicted = []
            for row in rows:
                values = list(row) if bits == 8 else list(struct.unpack('>3H', row))
                deltas = [(value - (values[i-1] if i else 0)) % (1 << bits) for i, value in enumerate(values)]
                predicted.append(bytes(deltas) if bits == 8 else struct.pack('>3H', *deltas))
            raw = b''.join(predicted)
        raw = zlib.compress(raw)
    name = f'psd-{bits}-{compression}' + ('-gray' if gray else '')
    (root / (name + '.psd')).write_bytes(header + struct.pack('>H', compression) + raw)

for bits in (8, 16):
    for compression in range(4): psd(bits, compression)
psd(8, 1, True)
(root / 'truncated.psd').write_bytes((root / 'psd-8-0.psd').read_bytes()[:42])
(root / 'invalid.bin').write_bytes(b'not an image')
print('Generated static and PSD fixtures')

palette = [0,0,0,255,0,0,0,255,0,0,0,255,255,255,255] + [0] * (768-15)
gif_frames = []
for index in range(4):
    frame = Image.new('P', (6, 4), 0); frame.putpalette(palette)
    for y in range(index % 2, index % 2 + 2):
        for x in range(index, index + 2): frame.putpixel((x,y), index+1)
    gif_frames.append(frame)
gif_frames[0].save(root/'disposal.gif',save_all=True,append_images=gif_frames[1:],duration=[40,120,80,60],loop=0,transparency=0,disposal=[1,2,3,1],optimize=False)
apng_frames=[]
for index in range(4):
    frame=Image.new('RGBA',(6,4),(0,0,0,0))
    for y in range(index%2,index%2+2):
        for x in range(index,index+2):frame.putpixel((x,y),(20+index*60,200-index*30,40,index*60+64))
    apng_frames.append(frame)
apng_frames[0].save(root/'blend.apng',save_all=True,append_images=apng_frames[1:],duration=[40,120,80,60],loop=0,disposal=[0,1,2,0],blend=[0,1,0,1])
cover=Image.new('RGBA',(6,4),(255,0,255,255))
cover.save(root/'excluded.png',save_all=True,append_images=apng_frames,duration=[40,120,80,60],loop=0,default_image=True,disposal=[0,1,2,0],blend=[0,1,0,1])
for name in ('disposal.gif','blend.apng','excluded.png'):
    if name.endswith(('.apng','.png')):
        from apng_oracle import render
        expected=[]
        for index,(frame,duration) in enumerate(render((root/name).read_bytes())):
            frame.save(root/(name+f'-frame-{index}.png'));expected.append(duration)
        (root/(name+'.durations')).write_text(' '.join(map(str,expected)))
        continue
    with Image.open(root/name) as loaded:
        start=1 if loaded.info.get('default_image') else 0
        expected=[]
        for index in range(start,loaded.n_frames):
            loaded.seek(index)
            loaded.convert('RGBA').save(root/(name+f'-frame-{index-start}.png'))
            expected.append(int(loaded.info.get('duration',100)))
        (root/(name+'.durations')).write_text(' '.join(map(str,expected)))
print('Generated GIF/APNG and independent Pillow frame oracles')
import zipfile
with zipfile.ZipFile(root/'images.zip','w',zipfile.ZIP_DEFLATED) as archive:
    archive.write(root/'alpha.png','画像/2.png')
    archive.write(root/'psd-16-3.psd','画像/10.psd')
    archive.writestr('readme.txt','not an image')
    archive.write(root/'alpha.png','../../traversal.png')
left=Image.new('RGBA',(120,180),(20,100,210,255))
right=Image.new('RGBA',(100,200),(250,80,35,170))
left.save(root/'page1.png');right.save(root/'page2.png')
display=Image.new('RGBA',(512,384),(0,0,0,0))
from PIL import ImageDraw
draw=ImageDraw.Draw(display)
draw.rounded_rectangle((24,24,236,180),24,fill=(30,130,240,255))
draw.ellipse((160,64,440,340),fill=(245,95,65,128))
draw.rectangle((40,240,140,340),fill=(55,190,110,64))
display.save(root/'display-alpha.png')
print('Generated ZIP, comic and GUI fixtures')
with zipfile.ZipFile(root/'dates.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for name,date in [('new.png',(2025,1,2,0,0,0)),('old.png',(2000,1,2,0,0,0))]:
        entry=zipfile.ZipInfo(name,date)
        entry.compress_type=zipfile.ZIP_DEFLATED
        archive.writestr(entry,(root/'alpha.png').read_bytes())
with Image.open(root/'alpha.png') as image:
    image.save(root/'alpha.tiff',save_all=True,append_images=[Image.new('RGBA',image.size,'white')])
icon=Image.new('RGBA',(16,16),(0,0,0,0))
ImageDraw.Draw(icon).rectangle((2,2,13,13),fill=(20,100,200,128))
icon.save(root/'alpha.ico',sizes=[(16,16)])
