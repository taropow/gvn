"""Export UI alpha fixtures from native RGB goldens and native color-free permutations."""
from pathlib import Path
from PIL import Image
import hashlib,json

root=Path(__file__).resolve().parents[1];directory=root/'tests/fixtures/mask-reference'
out=directory/'png';assert not out.exists();out.mkdir()
manifest=json.loads((directory/'manifest.json').read_text());width,height=manifest['width'],manifest['height']
source=(directory/'source.rgb').read_bytes();expected=(directory/'expected.rgb').read_bytes()
colors=[tuple(source[i:i+3]) for i in range(0,len(source),3)];indices={pixel:i for i,pixel in enumerate(colors)}
assert len(indices)==width*height
cases=[]
for line in (directory/'cases.tsv').read_text().splitlines():
 f=line.split('\t');cases.append(dict(label=f[0],kind=f[1],size=int(f[2]),swap=int(f[3]),negative=int(f[4]),xor=int(f[5]),q0=int(f[6]),remainder=int(f[7]),offset=int(f[9]),bytes=int(f[10])))
def pixels(case):
 data=expected[case['offset']:case['offset']+case['bytes']]
 return [tuple(data[i:i+3]) for i in range(0,len(data),3)]
def choose(kind,**values):return next(c for c in cases if c['kind']==kind and all(c[k]==v for k,v in values.items()))
def save(name,rgb,mapping=None):
 image=Image.new('RGBA',(width,height));image.putdata([(*pixel,(indices[mapping[i]] if mapping else i)&255) for i,pixel in enumerate(rgb)]);image.save(out/(name+'.png'))
save('source-alpha',colors)
selected={'negative-green':choose('negative',negative=2),'xor-red':choose('xor',xor=1),'swap-backward':choose('swap',swap=4),
          'glass-v-tail':choose('vertical',size=3,remainder=1),'glass-h-tail':choose('horizontal',size=3,remainder=1),
          'q0-all':choose('q0',size=3,remainder=1,q0=63,negative=7,xor=7),
          'q0-components':choose('q0',size=3,remainder=1,q0=63,negative=1,xor=2),
          'win':choose('win'),'image-negative':choose('image_negative')}
for name,case in selected.items():
 rgb=pixels(case);mapping=None
 if case['kind']=='q0':mapping=pixels(choose('q0',size=case['size'],remainder=case['remainder'],q0=case['q0']&~12))
 elif case['kind'] in {'vertical','horizontal','win'}:mapping=rgb
 save(name,rgb,mapping)
manifest['ui_png_cases']={name:c['label'] for name,c in selected.items()}
manifest['alpha_rule']='Source alpha is original pixel index modulo256; spatial mapping comes from actual color-free native results.'
for path in out.glob('*.png'):manifest['files'][path.relative_to(directory).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
(directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Exported alpha source and',len(selected),'native-output UI fixtures')
