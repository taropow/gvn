"""Independent indexed source assets, including duplicate RGB entries and palette alpha."""
import json, struct
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
folder = root/'tests/fixtures/palette'
folder.mkdir(exist_ok=True)
width, height = 37, 23
cases = []
for bits in (1, 2, 4, 8):
    count = 1 << bits
    colors = [(i*37 % 256, i*71 % 256, i*109 % 256) for i in range(count)]
    colors[1] = colors[0]
    indices = [(x*7+y*13+x*y) % count for y in range(height) for x in range(width)]
    source = Image.new('P', (width, height))
    source.putpalette(bytes(c for rgb in colors for c in rgb))
    source.putdata(indices)
    for alpha in (False, True):
        name = f'png-{bits}-{"alpha" if alpha else "opaque"}.png'
        transparency = bytes((i*53) % 256 for i in range(count)) if alpha else None
        source.save(folder/name, bits=bits, transparency=transparency)
        cases.append(dict(name=name,bits=bits,palette=[[*c,transparency[i] if alpha else 255] for i,c in enumerate(colors)],indices=indices))
    if bits != 2:
        name = f'bmp-{bits}.bmp'
        stride = ((width*bits+31)//32)*4
        data = bytearray(stride*height)
        for y in range(height):
            for x in range(width):
                bit = x*bits
                data[(height-1-y)*stride+bit//8] |= indices[y*width+x] << (8-bits-bit%8)
        palette = b''.join(bytes((b,g,r,0)) for r,g,b in colors)
        offset = 14+40+len(palette)
        header = b'BM'+struct.pack('<IHHI',offset+len(data),0,0,offset)
        dib = struct.pack('<IiiHHIIiiII',40,width,height,1,bits,0,len(data),0,0,count,0)
        (folder/name).write_bytes(header+dib+palette+data)
        cases.append(dict(name=name,bits=bits,palette=[[*c,255] for c in colors],indices=indices))
        if bits in (4,8):
            encoded = bytearray()
            for y in reversed(range(height)):
                row = indices[y*width:(y+1)*width]
                literal = width if bits == 8 else width-1
                raw = bytes(row) if bits == 8 else bytes((row[x]<<4)|row[x+1] for x in range(0,literal,2))
                encoded.extend((0,literal)); encoded.extend(raw)
                if len(raw)%2: encoded.append(0)
                if bits == 4: encoded.extend((1,row[-1]<<4))
                encoded.extend((0,0))
            encoded.extend((0,1))
            name = f'bmp-{bits}-rle.bmp'
            header = b'BM'+struct.pack('<IHHI',offset+len(encoded),0,0,offset)
            dib = struct.pack('<IiiHHIIiiII',40,width,height,1,bits,2 if bits==4 else 1,len(encoded),0,0,count,0)
            (folder/name).write_bytes(header+dib+palette+encoded)
            cases.append(dict(name=name,bits=bits,palette=[[*c,255] for c in colors],indices=indices))
    if bits == 8:
        for alpha in (False, True):
            name = f'gif-{"alpha" if alpha else "opaque"}.gif'
            opts = dict(transparency=2) if alpha else {}
            source.save(folder/name,optimize=False,**opts)
            cases.append(dict(name=name,bits=8,palette=[[*c,0 if alpha and i==2 else 255] for i,c in enumerate(colors)],indices=indices))
for case in cases:
    with Image.open(folder/case['name']) as im:
        assert list(im.getdata()) == case['indices'], case['name']
(root/'tests/palette-cases.json').write_text(json.dumps(dict(width=width,height=height,cases=cases),indent=2)+'\n',encoding='utf-8')
print(f'Generated {len(cases)} independent indexed source fixtures')

steps, outputs = [], []
def op(*values): steps.append('\t'.join(map(str,values)))
def opened(name):
    op('open','${output}/assets/'+name); op('wait')
def save(name,source,fmt='Png',operation=0):
    op('save_target','pixels/'+name); op('command','SaveAs'); op('save_type',fmt)
    if fmt == 'Png': op('save_set','PngTransparency',0)
    if fmt == 'Bmp':
        op('save_set','BmpBits',4); op('save_expect_control','BmpBits',4)
        op('save_set','BmpRle','true'); op('save_set','BmpTopDown','true')
    if fmt == 'Gif':
        op('save_set','GifTransparency',1); op('save_set','GifIndex',2); op('answer','OK')
    op('save_layout')
    if fmt == 'Bmp' and source == 'png-2-opaque.png': op('save_snapshot','bmp-original-depth.png')
    op('save_accept'); op('wait_save'); op('persisted'); op('expect','error_count',0)
    outputs.append(dict(file=name,source=source,format=fmt,op=operation))
for case in cases:
    name=case['name']; op('copy_fixture','palette/'+name,'assets/'+name)
    opened(name); op('expect_indexed',case['bits']); op('expect_index',1,0,7%(1<<case['bits']))
    save(name+'.png',name)
for name in ('bmp-1.bmp','png-2-opaque.png','bmp-4-rle.bmp','bmp-8-rle.bmp'):
    opened(name); save(name+'.bmp',name,'Bmp')
opened('gif-alpha.gif'); save('gif-alpha.gif','gif-alpha.gif','Gif')
opened('png-4-alpha.png'); op('command','RotateRight'); op('expect_indexed',4)
op('pixel_copy'); save('rotated.png','png-4-alpha.png',operation=2)
op('command','Negative'); op('expect_indexed',0)
save('negative.png','png-4-alpha.png',operation=8)
op('command','Undo'); op('expect_indexed',4); op('pixel_copy')
save('undo.png','png-4-alpha.png',operation=2)
op('snapshot','palette-final.png')
(root/'tests/autopilot/28-palette-preservation.gvn').write_text('\n'.join(steps)+'\n',encoding='utf-8')
(root/'tests/palette-native-cases.json').write_text(json.dumps(outputs,indent=2)+'\n',encoding='utf-8')
print(f'Generated {len(steps)} native operations and {len(outputs)} expected saves')
