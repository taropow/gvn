"""Pillow expectations for original-index palette UI editing, independent of GVN."""
from pathlib import Path
import hashlib,json
from PIL import Image

root=Path(__file__).resolve().parents[1]
folder=root/'tests/fixtures/palette-dialog';folder.mkdir(exist_ok=True)
source=root/'tests/fixtures/palette/png-4-alpha.png'
with Image.open(source) as image:
    image.load();original=image.copy();palette=image.getpalette();alpha=bytes(image.info['transparency'])
indices=list(original.getdata());assert palette[:3]==palette[3:6] and {0,1}<=set(indices)
states={'applied':{1:[11,22,33]},'draft':{1:[201,22,33]},'accepted':{0:[18,52,86],1:[11,22,33]}}
outputs=[]
for name,changes in states.items():
    colors=palette.copy()
    for index,rgb in changes.items():colors[index*3:index*3+3]=rgb
    picture=original.copy();picture.putpalette(colors);picture.info['transparency']=alpha
    picture.save(folder/(name+'.png'),bits=4,optimize=False,transparency=alpha)
    if name!='draft':outputs.append(dict(file=name+'.png',reference=name+'.png',alpha=True))
outputs.append(dict(file='applied.bmp',reference='applied.png',alpha=False))
(folder/'cases.json').write_text(json.dumps(dict(source='palette/png-4-alpha.png',source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),outputs=outputs),indent=2)+'\n',encoding='utf-8')
steps=[]
def op(*values):steps.append('\t'.join(map(str,values)))
def save(name,fmt):
    op('save_target','pixels/'+name);op('command','SaveAs');op('save_type',fmt)
    if fmt=='Png':op('save_set','PngTransparency',0)
    else:op('save_set','BmpBits',2)
    if fmt=='Bmp':op('answer','OK')
    op('save_accept');op('wait_save')
    if fmt=='Png':op('expect_saved','pixels/'+name)
op('expect_menu_enabled','PaletteEdit','false')
op('open','${fixtures}/palette/png-4-alpha.png');op('wait');op('remember','original')
op('expect_indexed',4);op('expect_menu_enabled','PaletteEdit','true');op('menu','PaletteEdit')
op('expect','palette.index',0);op('expect','palette.preview','true');op('expect','palette.apply','false')
op('pal_grid',1);op('expect','palette.index',1);op('pal_set','Red',11);op('pal_set','Green',22);op('pal_slide','Blue',33)
op('pal_rgb',11,22,33,alpha[1]);op('pal_expect','${fixtures}/palette-dialog/applied.png')
op('expect_same','original','true');op('expect','modified','false');op('pal_snapshot','palette-preview.png')
op('pal_apply');op('expect','palette.apply','false');op('expect','modified','true');op('expect','undo_available','true')
op('pal_set','Red',201);op('pal_expect','${fixtures}/palette-dialog/draft.png');op('pal_cancel')
op('expect_image','${fixtures}/palette-dialog/applied.png',0);op('pixel_copy')
save('applied.bmp','Bmp');save('applied.png','Png')
op('command','Undo');op('expect_image','${fixtures}/palette/png-4-alpha.png',0)
op('command','Undo');op('expect_image','${fixtures}/palette-dialog/applied.png',0)
op('menu','PaletteEdit');op('pal_preview','false');op('pal_grid',0);op('pal_pick')
op('pal_picker_rgb',18,52,86);op('pal_picker_snapshot','palette-color-picker.png');op('pal_picker_accept')
op('pal_rgb',18,52,86,0);op('pal_expect','${fixtures}/palette-dialog/applied.png')
op('pal_pick');op('pal_picker_rgb',201,203,207);op('pal_picker_cancel');op('pal_rgb',18,52,86,0)
op('pal_accept');op('expect_image','${fixtures}/palette-dialog/accepted.png',0)
op('expect','settings.palette_preview','false');op('persisted');op('pixel_copy');save('accepted.png','Png')
op('menu','PaletteEdit');op('expect','palette.index',0);op('expect','palette.preview','false')
op('pal_set','Index',999);op('expect','palette.valid','false');op('pal_set','Index',1)
op('expect','palette.valid','true');op('pal_preview','true');op('pal_cancel')
op('expect','settings.palette_preview','false');op('persisted');op('expect_image','${fixtures}/palette-dialog/accepted.png',0)
op('expect','error_count',0);op('snapshot','palette-result.png')
(root/'tests/autopilot/88-palette-editor.gvn').write_text('\n'.join(steps)+'\n',encoding='utf-8')
print('Generated',len(states),'expected images and',len(steps),'native palette actions')
