"""Generate owned Alpha/ROI inputs and a focused actual color-adjustment UI scenario."""
from pathlib import Path
import colorsys,hashlib,json
from PIL import Image
root=Path(__file__).resolve().parents[1];folder=root/'tests/fixtures/color-adjustment-ui';folder.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
reference=root/'tests/fixtures/color-adjustment'
with Image.open(reference/'source.png') as image:source=image.convert('RGBA')
source.putalpha(Image.frombytes('L',source.size,bytes((x*13+y*17)%256 for y in range(source.height) for x in range(source.width))))
source.save(folder/'source-alpha.png');roi=(5,4,16,11)
for name,sample in [('combined','combined-desaturate'),('unified','unify-light'),('selected','hue90')]:
    with Image.open(reference/(sample+'.png')) as image:expected=image.convert('RGBA')
    expected.putalpha(source.getchannel('A'))
    if name=='selected':
        selected=source.copy();selected.paste(expected.crop(roi),roi[:2]);expected=selected
    expected.save(folder/(name+'.png'))
with Image.open(root/'tests/fixtures/disposal.gif') as image:
    frames=image.n_frames;image.seek(1);frame=image.convert('RGBA')
def hue90(pixel):
    r,g,b,a=pixel;h,l,s=colorsys.rgb_to_hls(r/255,g/255,b/255)
    rgb=colorsys.hls_to_rgb((h+.25)%1,l,s);return tuple(max(0,min(255,int(v*255+.5))) for v in rgb)+(a,)
adjusted=Image.new('RGBA',frame.size);adjusted.putdata([hue90(p) for p in frame.getdata()]);adjusted.save(folder/'animation.png')
steps=[]
def op(*values):steps.append('\t'.join(map(str,values)))
def dialog():op('menu','ColorAdjust')
def save(name):
    op('save_target','pixels/'+name+'.png');op('command','SaveAs');op('save_type','Png');op('save_set','PngTransparency',0)
    op('save_accept');op('wait_save');op('expect_saved','pixels/'+name+'.png');op('persisted')
def sliders():
    for name,value in [('Hue',75),('Saturation',-40),('Lightness',15)]:op('adjust_scroll',name,value)
op('expect_menu_enabled','ColorAdjust','false');op('open','${fixtures}/color-adjustment-ui/source-alpha.png');op('wait');op('remember','original')
dialog();op('expect','adjust.hue',0);op('expect','adjust.gray',2);op('adjust_range','Hue',-180,180,0);op('adjust_range','Saturation',-100,100,0)
op('adjust_set','Hue','empty');op('expect','adjust.valid','false');op('expect','adjust.ok_enabled','false');op('adjust_set','Hue',0);sliders()
op('adjust_range','Hue',-180,180,75);op('adjust_range','Saturation',-100,100,-40);op('adjust_range','Lightness',-100,100,15)
op('adjust_snapshot','adjust-alpha.png');op('adjust_cancel');op('expect_same','original','true');op('expect','modified','false')
dialog();sliders();op('adjust_preview','previews/combined.png');op('adjust_accept');op('expect_image','${fixtures}/color-adjustment-ui/combined.png',1);op('pixel_copy');save('combined')
op('command','Undo');op('expect_same','original','true');op('rect_dialog')
for name,value in [('Left',5),('Top',4),('Width',11),('Height',7)]:op('rect_set',name,value)
op('rect_click','OK');dialog();op('adjust_set','Hue',90);op('adjust_preview','previews/selected.png');op('adjust_accept')
op('expect','selection_w',11);op('expect','selection_h',7);op('expect_image','${fixtures}/color-adjustment-ui/selected.png',1)
op('command','ClearSelection');op('pixel_copy');save('selected');op('command','Undo');op('expect_same','original','true')
dialog();op('adjust_unified','true');op('expect','adjust.hue',0);op('expect','adjust.saturation',25);op('adjust_range','Hue',0,360,0);op('adjust_range','Saturation',0,100,25)
op('adjust_set','Hue',45);op('adjust_set','Saturation',60);op('adjust_set','Lightness',20);op('adjust_gray',1)
op('adjust_preview','previews/unified.png');op('adjust_snapshot','adjust-unified.png');op('adjust_accept');op('expect_image','${fixtures}/color-adjustment-ui/unified.png',1);op('pixel_copy');save('unified')
op('remember','unified');dialog();op('expect','adjust.unified','true');op('expect','adjust.hue',45);op('expect','adjust.saturation',60);op('expect','adjust.lightness',20);op('expect','adjust.gray',1)
op('adjust_unified','false');op('adjust_set','Hue',90);op('adjust_reset','Hue');op('adjust_gray',0);op('adjust_accept');op('expect_same','unified','true');op('persisted')
op('expect','settings.color_adjust_unified','false');op('expect','settings.color_adjust_hue',45);op('expect','settings.color_adjust_saturation',60);op('expect','settings.color_adjust_lightness',20)
dialog();op('expect','adjust.hue',0);op('expect','adjust.saturation',0);op('expect','adjust.lightness',0);op('expect','adjust.gray',0);op('adjust_cancel');op('command','Undo');op('expect_same','original','true')
op('open','${fixtures}/disposal.gif');op('wait');op('command','Play');op('seek',1);op('answer','Cancel');dialog();op('expect','adjust.open','false');op('expect','frames',frames);op('expect','frame',1)
op('answer','OK');dialog();op('adjust_scroll','Hue',90);op('adjust_accept');op('expect','frames',1);op('expect_image','${fixtures}/color-adjustment-ui/animation.png',1);op('pixel_copy');save('animation')
op('command','Undo');op('expect','frames',frames);op('expect','frame',1);op('expect','playing','false');op('expect','error_count',0);op('snapshot','adjust-animation-undo.png')
scenario=root/'tests/autopilot/90-color-adjustment.gvn';scenario.write_text('\n'.join(steps)+'\n',encoding='utf-8')
outputs=[dict(file=name+'.png',reference=name+'.png',selection=list(roi) if name=='selected' else None) for name in ['combined','selected','unified','animation']]
inputs=[folder/'source-alpha.png',root/'tests/fixtures/disposal.gif',reference/'source.png']+[reference/(name+'.png') for name in ['combined-desaturate','unify-light','hue90']]+[folder/r['reference'] for r in outputs]
(folder/'cases.json').write_text(json.dumps(dict(outputs=outputs,preview_files=['combined.png','selected.png','unified.png'],source_sha256={p.relative_to(root/'tests/fixtures').as_posix():sha(p) for p in inputs},reference_rgb_max_difference=1,alpha_and_untouched_roi_exact=True),indent=2)+'\n',encoding='utf-8')
print('Generated',len(steps),'native actions,4saved outputs and3comparison previews')
