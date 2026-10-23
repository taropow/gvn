"""Independent Pillow comparison of actual native saves against original GIF index fixtures."""
from pathlib import Path
import argparse,hashlib,json,struct
from PIL import Image

root=Path(__file__).resolve().parents[1]
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser();parser.add_argument('latest',type=Path);parser.add_argument('executable',type=Path)
parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
run=read(args.latest);assert run['passed'] and run['executable_sha256']==sha(args.executable)
scenario=root/'tests/autopilot/87-gif-logical-palette.gvn'
case=next(v for v in run['scenarios'] if v['name']==scenario.name)
assert case['passed'] and case['scenario_sha256']==sha(scenario)
directory=Path(run['run_directory'])/scenario.stem/'pixels'
fixtures={v['name']:v for v in read(root/'tests/fixtures/gif-canvas/cases.json')}
outputs=read(root/'tests/fixtures/gif-canvas/save-cases.json')
assert {p.name for p in directory.iterdir()}=={v['file'] for v in outputs}
verified=[];total=0
for row in outputs:
    original=fixtures[row['source']];size=(original['width'],original['height']);rgba=Image.new('RGBA',size)
    rgba.putdata([tuple(p) for p in original['rgba']])
    indexed=Image.new('P',size);indexed.putdata(original['indices'] if original['keep_indexed'] else [0]*(size[0]*size[1]))
    if row['operation']:
        rgba=rgba.transpose(Image.Transpose.ROTATE_270);indexed=indexed.transpose(Image.Transpose.ROTATE_270)
    if row['operation']==2:rgba.putdata([(255-r,255-g,255-b,a) for r,g,b,a in rgba.getdata()])
    path=directory/row['file'];raw=path.read_bytes()
    keep=original['keep_indexed'] and row['operation']!=2
    with Image.open(path) as actual:
        actual.load();assert actual.size==rgba.size and actual.convert('RGBA').tobytes()==rgba.tobytes(),('RGBA',row)
        if keep:
            assert actual.mode=='P' and list(actual.getdata())==list(indexed.getdata()),('indices',row)
            assert actual.getpalette()[:len(original['palette'])*3]==[v for p in original['palette'] for v in p[:3]],('palette',row)
        else:assert actual.mode=='RGBA'
    if row['format']=='Png':assert raw[25]==(3 if keep else 6)
    if row['format']=='Bmp':assert struct.unpack_from('<H',raw,28)[0]==8
    total+=rgba.width*rgba.height;verified.append(dict(**row,sha256=sha(path),size=list(rgba.size),indices_equal=keep,all_rgba_equal=True))
report=dict(passed=True,outputs=len(verified),compared_pixels=total,native_steps=case['steps'],captures=len(case['captures']),
    executable_sha256=run['executable_sha256'],scenario_sha256=sha(scenario),verifier_sha256=sha(Path(__file__)),
    fixture_manifest_sha256=sha(root/'tests/fixtures/gif-canvas/cases.json'),cases=verified)
args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='cases'}),flush=True)
