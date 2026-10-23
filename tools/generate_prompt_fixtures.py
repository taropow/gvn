"""Small synthetic PNG metadata fixtures; never touches external source images."""
import json, struct, zlib, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'tests/fixtures/prompts';out.mkdir(exist_ok=True)
base=(root/'tests/fixtures/alpha.png').read_bytes()
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
def itxt(key,text,compressed=False):return chunk(b'iTXt',key.encode()+b'\0'+bytes([compressed,0])+b'\0\0'+(zlib.compress(text.encode()) if compressed else text.encode()))
def text(key,value,compressed=False):return chunk(b'zTXt' if compressed else b'tEXt',key.encode()+b'\0'+(b'\0'+zlib.compress(value.encode('latin-1')) if compressed else value.encode('latin-1')))
def save(name,chunks,after=False,source=base):
    at=len(source)-12 if after else 33
    (out/name).write_bytes(source[:at]+b''.join(chunks)+source[at:])
parts={'version':1,'profile':'sdxl','slot':{'prompt':'classroom, (morning light:1.2)','jp':'朝の光 ☀','jp_translated':'morning light','negative':'blurry'},'main':{'prompt':'masterpiece, best quality','negative':'lowres'},'tab':{'index':0,'id':'synthetic-tab','name':'黒髪の子','prompt':'1girl, black hair','jp':'笑顔','jp_translated':'smile','toggles':[{'title':'制服','prompt':'school uniform','enabled':True},{'title':'帽子','prompt':'hat','enabled':False}]},'extended':[{'tab':0,'tab_name':'画風','title':'柔らかい光','prompt':'soft lighting','weight':1.2},{'tab':1,'tab_name':'','title':'通常','prompt':'sharp','weight':1.0},{'tab':1,'prompt':'default zero'}],'lora':{'speed':'<lora:speed:1>','boost':'','extra':'<lora:extra:0.5>'},'adetailer':'detailed face','final':{'prompt':'RESOLVED POSITIVE\n朝の光, <lora:extra:0.5>','negative':'RESOLVED NEGATIVE\nlowres'},'memo':'採用候補'}
raw=json.dumps(parts,ensure_ascii=False,separators=(',',':'))
params='UPSTREAM POSITIVE\nNegative prompt: upstream negative\nSteps: 20, Sampler: Euler, CFG scale: 7, Seed: 123, Size: 3x2'
save('parts.png',[itxt('inpainter-parts',raw),text('parameters',params),text('Generation time','3.71s'),text('generation_backend','synthetic')])
save('parts-only.png',[itxt('inpainter-parts',raw)])
save('compressed-limit.png',[itxt('inpainter-parts',' '*(1024*1024+1),True)])
save('latin1.png',[text('prompt','café'),chunk(b'tEXt',b'Autor\xe9\0Ren\xe9')])
save('software-only.png',[text('Software','test encoder')])
save('a1111.png',[text('parameters',params)])
save('empty-parameters.png',[text('parameters',''),text('prompt','must not be used')])
save('compressed.png',[itxt('inpainter-parts',raw,True),text('parameters',params,True)])
save('post-idat.png',[itxt('inpainter-parts',raw)],True)
save('duplicate.png',[itxt('inpainter-parts',raw),itxt('inpainter-parts','{"version":99}'),text('parameters','old'),text('parameters',params)])
save('legacy-parts.png',[chunk(b'tEXt',b'inpainter-parts\0'+raw.encode())])
save('invalid.png',[itxt('inpainter-parts','{broken'),text('parameters',params)])
save('invalid-type.png',[itxt('inpainter-parts','{"version":1,"slot":null}'),text('parameters',params)])
save('unknown.png',[itxt('inpainter-parts','{"version":2,"final":{"prompt":"do not interpret"}}'),text('parameters',params)])
save('missing-fields.png',[itxt('inpainter-parts','{"version":1,"ignored":null}')])
save('trailing-json.png',[itxt('inpainter-parts','{"version":1}garbage'),text('parameters',params)])
save('animation.apng',[itxt('inpainter-parts',raw)],source=(root/'tests/fixtures/blend.apng').read_bytes())
with zipfile.ZipFile(out/'bundle.zip','w',zipfile.ZIP_DEFLATED) as archive:archive.writestr('parts.png',(out/'parts.png').read_bytes())
print(f'Generated {len(list(out.iterdir()))} synthetic prompt fixtures')
