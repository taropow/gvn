"""Generate deterministic native save-dialog cases and their independent output contract."""
import json
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
fixtures = root/'tests/fixtures/saving'
fixtures.mkdir(exist_ok=True)
rgba = Image.new('RGBA', (67,41))
rgba.putdata([((x*19+y*7)%256,(x*5+y*11)%256,(x*3+y*31)%256,(x*13+y*17)%256)
              for y in range(41) for x in range(67)])
rgba.save(fixtures/'rgba.png')
palette = [(0,0,0),(255,0,0),(0,255,0),(0,0,255),(255,255,0),(255,0,255),(0,255,255),(255,255,255)]
runs = Image.new('RGB',(513,29))
runs.putdata([palette[(x//128+y)%8] for y in range(29) for x in range(513)])
runs.save(fixtures/'runs.png')
steps, cases = [], []
def op(*values): steps.append('\t'.join(map(str,values)))
op('copy_fixture','saving/rgba.png','assets/rgba.png')
op('copy_fixture','saving/runs.png','assets/runs.png')
op('open','${output}/assets/rgba.png'); op('wait')
op('remember_saving','initial')
op('save_target','pixels/cancelled.png'); op('command','SaveAs')
op('save_layout'); op('save_expect_control','PngCompression',6)
op('save_enabled','PngColor','false'); op('save_enabled','PngMatte','false')
op('save_set','PngCompression',3); op('save_set','PngInterlaced','true')
op('save_type','Jpeg'); op('save_visible','Png','false'); op('save_visible','Jpeg','true')
op('save_set','JpegQuality',91); op('save_set','JpegProgressive','true')
op('save_type','Bmp'); op('save_set','BmpBits',2); op('save_enabled','BmpRle','true')
op('save_set','BmpRle','true'); op('save_type','Gif')
op('save_set','GifTransparency',1); op('save_enabled','GifIndex','true'); op('save_set','GifIndex',5)
op('save_type','Png'); op('save_expect_control','PngCompression',3)
op('save_expect_control','PngInterlaced','true'); op('save_layout')
op('save_cancel'); op('wait_save'); op('expect_saving','initial'); op('persisted')

def save(name, fmt, settings, expected, source='rgba.png', capture=False, auto=False, initial_format=None):
    op('open','${output}/assets/'+source); op('wait')
    op('save_target','pixels/'+name); op('command','SaveAs')
    if initial_format: op('save_expect_type',initial_format)
    op('save_type',fmt)
    op('save_set','KeepTimes','false'); op('save_set','Uppercase','false')
    for key,value in settings:
        op('save_set',key,value)
        op('save_expect_control',key,value)
    op('save_layout')
    if auto: op('save_filename',name)
    if capture: op('save_snapshot','save-'+fmt.lower()+'.png')
    # Existing alpha-loss warning is an App boundary. Queue only for actual alpha fixtures.
    if fmt != 'Png' and source == 'rgba.png': op('answer','OK')
    op('save_accept'); op('wait_save')
    op('persisted'); op('expect','error_count',0)
    cases.append(dict(name=expected.pop('file',name), format=fmt,source=source,**expected))

for i in range(7):
    save(f'png-filter-{i}.png','Png',[('PngTransparency',0),('PngInterlaced','false'),('PngFilter',i),('PngCompression',6)],dict(filter=i,level=6,transparency=0,interlaced=False),capture=i==6)
for i in range(10):
    save(f'png-level-{i}.png','Png',[('PngCompression',i)],dict(level=i,transparency=0,interlaced=False,filter=6))
for i in range(7):
    settings=[('PngCompression',6),('PngFilter',6),('PngInterlaced','true'),('PngTransparency',i)]
    if i != 0: settings.append(('PngMatte','123456'))
    if i == 2: settings.append(('PngColor','FF0000'))
    save(f'png-trans-{i}.png','Png',settings,dict(level=6,transparency=i,matte=0x123456,key=0xff0000,interlaced=True,filter=6))
for quality,progressive in [(0,False),(90,True),(100,False)]:
    save(f'jpeg-{quality}.jpg','Jpeg',[('JpegQuality',quality),('JpegProgressive',str(progressive).lower()),('JpegMatte','123456')],dict(quality=quality,progressive=progressive,matte=0x123456),capture=quality==90)
for index,bits,top,rle in [(0,24,True,False),(1,8,True,True),(2,4,True,True),(3,1,True,False)]:
    settings=[('BmpBits',index),('BmpTopDown',str(top).lower()),('BmpMatte','123456')]
    if bits in (4,8): settings.append(('BmpRle','true'))
    if bits!=24: settings.append(('Dither','false'))
    save(f'bmp-{bits}.bmp','Bmp',settings,dict(bits=bits,top=top,rle=rle,matte=0x123456),source='runs.png',capture=bits==8)
for i in range(6):
    settings=[('GifTransparency',i),('GifInterlaced',str(bool(i%2)).lower()),('GifUncompressed',str(bool(i%2)).lower()),('GifMatte','123456'),('Dither','false')]
    if i == 1: settings.append(('GifIndex',5))
    save(f'gif-trans-{i}.gif','Gif',settings,dict(transparency=i,index=5,interlaced=bool(i%2),uncompressed=bool(i%2)),source='runs.png',capture=i==1)

# Rejection keeps the modal dialog and the accepted settings. Correction then saves.
op('open','${output}/assets/runs.png'); op('wait'); op('remember_saving','before-invalid')
op('save_target','pixels/jpeg-corrected.jpg'); op('command','SaveAs')
op('save_type','Jpeg'); op('save_set','JpegQuality',101); op('save_accept')
op('save_error_contains','JPEG画質'); op('expect_saving','before-invalid')
op('save_set','JpegQuality',-1); op('save_accept'); op('save_error_contains','JPEG画質')
op('save_set','JpegQuality',50); op('save_set','JpegMatte','notrgb'); op('save_accept')
op('save_error_contains','6桁'); op('save_set','JpegMatte','123456')
op('save_accept'); op('wait_save'); op('persisted'); op('expect','error_count',0)
cases.append(dict(name='jpeg-corrected.jpg',format='Jpeg',source='runs.png',quality=50,progressive=False,matte=0x123456))
op('open','${output}/assets/runs.png'); op('wait'); op('remember_saving','before-extension')
op('save_target','pixels/mismatch.png'); op('command','SaveAs')
op('save_filename','typed.jpeg'); op('save_accept')
op('save_error_contains','拡張子'); op('save_visible','Jpeg','true'); op('save_expect_type','Jpeg')
op('expect_saving','before-extension'); op('save_set','JpegQuality',75); op('save_set','JpegProgressive','true')
op('save_accept'); op('wait_save'); op('persisted'); op('expect','error_count',0)
cases.append(dict(name='typed.jpeg',format='Jpeg',source='runs.png',quality=75,progressive=True,matte=0x123456))
op('open','${output}/assets/runs.png'); op('wait'); op('remember_saving','before-index')
op('save_target','pixels/gif-unused.gif'); op('command','SaveAs'); op('save_set','GifTransparency',1)
op('save_set','GifIndex',256); op('save_accept'); op('save_error_contains','透過色番号'); op('expect_saving','before-index')
op('save_set','GifIndex',255); op('save_accept'); op('wait_save'); op('persisted'); op('expect','error_count',0)
cases.append(dict(name='gif-unused.gif',format='Gif',source='runs.png',transparency=1,index=255,interlaced=True,uncompressed=True))
save('bmp-1-plain.bmp','Bmp',[('BmpBits',3),('BmpTopDown','true'),('BmpMatte','123456'),('Dither','false')],dict(bits=1,top=True,rle=False,matte=0x123456))
save('bmp-1-dither.bmp','Bmp',[('BmpBits',3),('BmpTopDown','true'),('BmpMatte','123456'),('Dither','true')],dict(bits=1,top=True,rle=False,matte=0x123456,dither=True))
save('auto','Png',[('PngTransparency',0),('Uppercase','true'),('KeepTimes','true')],dict(file='auto.PNG',transparency=0,interlaced=True,filter=6,level=6,keep_times=True),auto=True,initial_format='Bmp')
save('mixed.pNg','Png',[('Uppercase','true')],dict(transparency=0,interlaced=True,filter=6,level=6))
op('expect','error_count',0)
(root/'tests/autopilot/27-saving-options.gvn').write_text('\n'.join(steps)+'\n',encoding='utf-8')
(root/'tests/saving-dialog-cases.json').write_text(json.dumps(cases,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(steps=len(steps),outputs=len(cases))))
