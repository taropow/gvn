"""Independent Pillow GIF index data with explicit logical screens and local tables."""
from pathlib import Path
import io,json,struct
from PIL import Image

root=Path(__file__).resolve().parents[1];folder=root/'tests/fixtures/gif-canvas';folder.mkdir(exist_ok=True)
manifest=[]
for name,colors,transparent,local,clipped in [('global-duplicate',256,False,False,False),
    ('global-alpha',16,True,False,False),('local-background',16,False,True,False),
    ('local-overflow',256,False,True,False),('cropped-global',256,False,False,True)]:
    palette=[(i,i*71%256,i*109%256) for i in range(colors)]
    if name!='local-overflow':palette[1]=palette[0]
    indices=[(x*3+y*5)%min(colors,16) for y in range(3) for x in range(7)]
    indices[0:3]=[0,1,2]
    picture=Image.new('P',(7,3));picture.putpalette(bytes(c for p in palette for c in p));picture.putdata(indices)
    stream=io.BytesIO();options=dict(optimize=False,background=1,include_color_table=local)
    if transparent:options['transparency']=2
    picture.save(stream,format='GIF',**options);data=bytearray(stream.getvalue())
    global_count=2<<(data[10]&7);assert global_count==colors
    position=13+3*global_count
    while data[position]==0x21:
        position+=2
        while data[position]:position+=data[position]+1
        position+=1
    assert data[position]==0x2c and bool(data[position+9]&0x80)==local
    width,height,left,top=(5,2,0,0) if clipped else (11,7,2,1)
    struct.pack_into('<HH',data,6,width,height);struct.pack_into('<HH',data,position+1,left,top)
    background=(213,215,217) if local else palette[1]
    if local:data[13+3:13+6]=bytes(background)
    path=folder/(name+'.gif');path.write_bytes(data)
    colors_rgba=[list(p)+( [0] if transparent and i==2 else [255]) for i,p in enumerate(palette)]
    clear=list(background)+[0 if transparent else 255]
    keep=name!='local-overflow'
    retained=colors_rgba.copy();background_index=1
    if not clipped:
        if local or transparent:
            background_index=len(retained);retained.append(clear)
    expected_indices=[];pixels=[]
    for y in range(height):
        for x in range(width):
            inside=left<=x<left+7 and top<=y<top+3
            index=indices[(y-top)*7+x-left] if inside else background_index
            expected_indices.append(index)
            pixels.append(colors_rgba[index] if inside else clear)
    manifest.append(dict(name=name+'.gif',width=width,height=height,left=left,top=top,
        keep_indexed=keep,palette=retained if keep else [],indices=expected_indices if keep else [],rgba=pixels))
    # Pillow enlarges its screen for an out-of-bounds descriptor. GVN deliberately
    # clips that negative fixture to the declared logical screen.
    with Image.open(path) as decoded:assert decoded.size==((7,3) if clipped else (width,height))
(folder/'cases.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print('Generated',len(manifest),'GIF logical-screen fixtures')

steps=[];outputs=[]
def op(*values):steps.append('\t'.join(map(str,values)))
def save(name,source,operation=0,fmt='Png'):
    op('save_target','pixels/'+name);op('command','SaveAs');op('save_type',fmt)
    if fmt=='Png':op('save_set','PngTransparency',0)
    elif fmt=='Bmp':op('save_set','BmpBits',4);op('save_expect_control','BmpBits',4)
    op('save_layout');op('save_accept');op('wait_save');op('persisted');op('expect','error_count',0)
    outputs.append(dict(file=name,source=source,operation=operation,format=fmt))
for row in manifest:
    name=row['name'];op('copy_fixture','gif-canvas/'+name,'source/'+name)
    op('open','${output}/source/'+name);op('wait');op('expect_indexed',8 if row['keep_indexed'] else 0);op('pixel_copy')
    save(name+'.png',name)
op('open','${output}/source/global-alpha.gif');op('wait');op('command','RotateRight');op('expect_indexed',8)
save('rotated.png','global-alpha.gif',1)
op('command','Negative');op('expect_indexed',0);save('negative.png','global-alpha.gif',2)
op('command','Undo');op('expect_indexed',8);op('pixel_copy');save('undo.png','global-alpha.gif',1)
op('open','${output}/source/global-duplicate.gif');op('wait')
save('original-depth.bmp','global-duplicate.gif',fmt='Bmp')
save('opaque.gif','global-duplicate.gif',fmt='Gif')
op('pixel_copy');op('snapshot','logical-screen.png')
for row in manifest:op('expect_file','source/'+row['name'],'gif-canvas/'+row['name'])
(folder/'save-cases.json').write_text(json.dumps(outputs,indent=2)+'\n',encoding='utf-8')
(root/'tests/autopilot/87-gif-logical-palette.gvn').write_text('\n'.join(steps)+'\n',encoding='utf-8')
print('Generated',len(steps),'native operations and',len(outputs),'save expectations')
