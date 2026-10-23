"""Independent tiny GIF streams that do/don't admit one original-number palette."""
from pathlib import Path
import hashlib,json,struct
from PIL import Image
root=Path(__file__).resolve().parents[1];folder=root/'tests/fixtures/animation-indices';folder.mkdir(exist_ok=True)
def raster(values):
    codes=[v for pixel in values for v in [4,pixel]]+[5];value=bits=0;data=bytearray()
    for code in codes:
        value|=code<<bits;bits+=3
        while bits>=8:data.append(value&255);value>>=8;bits-=8
    if bits:data.append(value&255)
    assert len(data)<256;return bytes([2,len(data)])+data+b'\0'
palette=bytes([0,0,0,200,0,0,200,0,0,0,200,0]);first=[1,2,0,0,1,2,0,0]
def frame(left,top,width,height,pixels,local=None):
    gce=b'!\xf9\x04\x05\x0a\0\0\0';descriptor=b','+struct.pack('<HHHHB',left,top,width,height,0x81 if local else 0)
    return gce+descriptor+(local or b'')+raster(pixels)
manifest=[]
for name,local,pixel,representable in [('local-compatible',bytes([0,0,0,200,0,0,200,0,0,0,0,200]),2,True),('local-conflict',bytes([0,0,0,0,0,200,0,0,200,0,200,0]),1,False)]:
    path=folder/(name+'.gif');path.write_bytes(b'GIF89a'+struct.pack('<HHBBB',4,2,0x81,0,0)+palette+frame(0,0,4,2,first)+frame(2,0,1,1,[pixel],local)+b';')
    with Image.open(path) as image:
        assert image.n_frames==2
        for n in range(2):image.seek(n);image.convert('RGBA').save(folder/(name+f'-{n}.png'))
    manifest.append(dict(file=path.name,representable=representable,first_indices=first,second_indices=first[:2]+[pixel]+first[3:],sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
(folder/'cases.json').write_text(json.dumps(dict(cases=manifest,writer='Independent clear/literal GIF LZW,raw duplicate1/2numbers and different local tables;PillowRGBA frame references.'),indent=2)+'\n',encoding='utf-8')
print('Generated2 GIF/4independent RGBA frame references')
