"""Independent Pillow inputs/expectations for the actual color-depth menu/dialog routes."""
from pathlib import Path
import hashlib,json
from PIL import Image
root=Path(__file__).resolve().parents[1];folder=root/'tests/fixtures/color-depth-ui';folder.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=root/'tests/fixtures/color-depth/gradient.png'
with Image.open(source) as image:
    for name,bits in [('rgb555',(5,5,5)),('rgb565',(5,6,5))]:
        expected=Image.new('RGB',image.size);pixels=[]
        for p in image.convert('RGB').getdata():
            codes=[(v*((1<<n)-1)+127)//255 for v,n in zip(p,bits)]
            pixels.append(tuple((q<<(8-n))|(q>>(2*n-8)) for q,n in zip(codes,bits)))
        expected.putdata(pixels);expected.save(folder/(name+'.png'))
two=Image.new('RGBA',(17,9));two.putdata([(11,37,83,0) if (x+y)%2 else (211,233,251,117) for y in range(9) for x in range(17)]);two.save(folder/'two-colors.png')
rgba=Image.new('RGBA',(67,41));rgba.putdata([((x*19+y*7)%256,(x*5+y*11)%256,(x*3+y*31)%256,(x*13+y*17)%256) for y in range(41) for x in range(67)]);rgba.save(folder/'rgba-gradient.png')
with Image.open(root/'tests/fixtures/disposal.gif') as image:frame_count=image.n_frames;image.seek(1);image.convert('RGBA').save(folder/'animation-frame.png')
steps=[]
def op(*values):steps.append('\t'.join(map(str,values)))
def open_image(name):op('open','${fixtures}/'+name);op('wait')
def dialog():op('menu','ConvertColorDepth')
def save(name,fmt='Png'):
    op('save_target','pixels/'+name);op('command','SaveAs');op('save_type',fmt)
    if fmt=='Png':op('save_set','PngTransparency',0)
    else:op('save_set','BmpBits',4)
    op('save_accept');op('wait_save');op('persisted')
    if fmt=='Png':op('expect_saved','pixels/'+name)
op('expect_menu_enabled','ConvertColorDepth','false')
open_image('palette/png-4-alpha.png');op('remember','original');dialog();op('depth_current','現在: 4 BPP')
op('expect','depth.choice',4);op('expect','depth.dither','true');op('depth_set','Indexed8');op('depth_dither','false');op('depth_snapshot','depth-alpha.png');op('depth_cancel')
op('expect_same','original','true');op('expect','settings.color_depth_dither','true');dialog();op('expect','depth.dither','true');op('depth_set','Indexed8');op('depth_dither','false');op('depth_accept')
op('expect_indexed',8);op('expect_image','${fixtures}/palette/png-4-alpha.png',0);op('pixel_copy');save('alpha8.png')
dialog();op('depth_current','現在: 8 BPP');op('depth_enabled','Indexed8','false');op('depth_set','Rgb24');op('depth_accept');op('expect_indexed',0);op('pixel_copy');save('alpha24.png')
op('command','Undo');op('expect_indexed',8);op('expect_image','${fixtures}/palette/png-4-alpha.png',0)
for name in ['rgb555','rgb565']:
    open_image('color-depth/gradient.png');dialog();op('depth_current','現在: 24 BPP');op('depth_enabled','Rgb24','false');op('depth_wheel',120);op('expect','depth.choice',2)
    op('depth_wheel',-120,'Rgb565');op('expect','depth.choice',4);op('depth_set','Rgb555' if name=='rgb555' else 'Rgb565');op('depth_dither','false')
    op('depth_snapshot','depth-'+name+'.png');op('depth_accept');op('expect_image','${fixtures}/color-depth-ui/'+name+'.png',0);save(name+'.bmp','Bmp')
    dialog();op('depth_current','現在: 16 BPP '+('RGB555' if name=='rgb555' else 'RGB565'));op('depth_enabled','Rgb555' if name=='rgb555' else 'Rgb565','false');op('depth_cancel')
open_image('color-depth-ui/two-colors.png');dialog();op('expect','depth.choice',4);op('depth_accept');op('expect_indexed',1);op('expect_image','${fixtures}/color-depth-ui/two-colors.png',0);save('optimal.png')
open_image('color-depth-ui/rgba-gradient.png');dialog();op('depth_set','Indexed8');op('depth_dither','true');op('depth_accept');op('expect_indexed',0);op('pixel_copy');save('rgba-reduced.png')
op('expect','settings.color_depth_dither','true')
open_image('disposal.gif');op('command','Play');op('seek',1);op('expect','playing','false');op('expect','frame',1);op('answer','OK');dialog();op('depth_set','Indexed8');op('depth_accept')
op('expect','frames',1);op('expect_image','${fixtures}/color-depth-ui/animation-frame.png',0);op('pixel_copy');save('animation-frame.png')
op('command','Undo');op('expect','frames',frame_count);op('expect','frame',1);op('expect','playing','false');op('expect','error_count',0);op('snapshot','depth-animation-undo.png')
scenario=root/'tests/autopilot/89-color-depth.gvn';scenario.write_text('\n'.join(steps)+'\n',encoding='utf-8')
outputs=[dict(file='alpha8.png',reference='palette/png-4-alpha.png',bits=8,palette=True),dict(file='alpha24.png',reference='palette/png-4-alpha.png',bits=8,palette=False),
    dict(file='rgb555.bmp',reference='color-depth/reference-555.bmp',bpp=16),dict(file='rgb565.bmp',reference='color-depth/reference-565.bmp',bpp=16),
    dict(file='optimal.png',reference='color-depth-ui/two-colors.png',bits=1,palette=True),dict(file='rgba-reduced.png',reference='color-depth-ui/rgba-gradient.png',reduction=True),
    dict(file='animation-frame.png',reference='color-depth-ui/animation-frame.png',bits=8,palette=True)]
(folder/'cases.json').write_text(json.dumps(dict(outputs=outputs,source_sha256={v['reference']:sha(root/'tests/fixtures'/v['reference']) for v in outputs}),indent=2)+'\n',encoding='utf-8')
print('Generated',len(steps),'native actions and',len(outputs),'independent output specifications')
