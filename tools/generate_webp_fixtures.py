"""Generate deterministic WebP inputs and RGBA oracles using Pillow, outside GVN."""
from pathlib import Path
from PIL import Image, ImageCms, ImageOps
import json, struct, zipfile
root = Path(__file__).resolve().parents[1]
out = root/'tests/fixtures/webp'
out.mkdir(exist_ok=True)
cases = []
def record(name, oriented=False):
    with Image.open(out/name) as image:
        frames=[]
        for n in range(getattr(image,'n_frames',1)):
            image.seek(n)
            frame=ImageOps.exif_transpose(image) if oriented else image.convert('RGBA')
            frame=frame.convert('RGBA')
            rgba=name+'.'+str(n)+'.rgba'
            (out/rgba).write_bytes(frame.tobytes())
            frames.append(dict(width=frame.width,height=frame.height,duration=image.info.get('duration',100),rgba=rgba))
        cases.append(dict(file=name,frames=frames,loop=image.info.get('loop',1)))
alpha=Image.new('RGBA',(3,2))
alpha.putdata([(255,0,0,255),(0,255,0,128),(0,0,255,0),(10,20,30,64),(250,240,230,255),(20,40,60,0)])
icc=bytearray(ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes())
icc[24:36]=struct.pack('>6H',2026,1,1,0,0,0)
icc=bytes(icc)
alpha.save(out/'alpha.webp',lossless=True,exact=True,icc_profile=icc)
record('alpha.webp')
rgb=Image.new('RGB',(32,24))
rgb.putdata([((x*7+y*3)%256,(x*3+y*11)%256,(x*13+y*5)%256) for y in range(24) for x in range(32)])
rgb.save(out/'lossy.webp',quality=80,method=6)
record('lossy.webp')
frames=[]
for index,color in enumerate([(255,0,0,255),(0,0,255,255),(255,255,0,255)]):
    image=Image.new('RGBA',(4,4),(0,0,0,0))
    image.putpixel((index,0),color)
    image.putpixel((1,1),(0,255,0,128))
    frames.append(image)
frames[0].save(out/'animation.webp',save_all=True,append_images=frames[1:],duration=[40,80,120],loop=2,lossless=True,exact=True,background=(0,0,0,0),minimize_size=True)
record('animation.webp')
frames[0].save(out/'infinite.webp',save_all=True,append_images=frames[1:],duration=[40,80,120],loop=0,lossless=True,exact=True)
record('infinite.webp')
base=Image.new('RGB',(2,3))
base.putdata([(255,0,0),(0,255,0),(0,0,255),(255,255,0),(255,0,255),(0,255,255)])
for orientation in range(1,9):
    exif=Image.Exif();exif[274]=orientation;exif[282]=300;exif[283]=150;exif[296]=2
    name=f'exif-{orientation}.webp'
    base.save(out/name,lossless=True,exif=exif)
    record(name,True)
with zipfile.ZipFile(out/'images.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
    for filename,data in [('日本語/alpha.WEBP',(out/'alpha.webp').read_bytes()),('animation.webp',(out/'animation.webp').read_bytes()),('ignored.txt',b'Not an image')]:
        info=zipfile.ZipInfo(filename,(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
        archive.writestr(info,data)
(out/'oracle.json').write_text(json.dumps(dict(generator='Pillow independent of GVN',cases=cases),indent=2)+'\n',encoding='utf-8')
print('Generated',len(cases),'WebP cases with per-frame RGBA oracles')
