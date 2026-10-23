"""Compare native palette saves with Pillow RGBA, complete tables and original indices."""
from pathlib import Path
import argparse,hashlib,json,struct
from PIL import Image

root=Path(__file__).resolve().parents[1]
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser();parser.add_argument('latest',type=Path);parser.add_argument('executable',type=Path)
parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
run=read(args.latest);assert run['passed'] and run['executable_sha256']==sha(args.executable)
scenario=root/'tests/autopilot/88-palette-editor.gvn'
case=next(v for v in run['scenarios'] if v['name']==scenario.name)
assert case['passed'] and case['scenario_sha256']==sha(scenario)
folder=root/'tests/fixtures/palette-dialog';manifest=read(folder/'cases.json')
source=root/'tests/fixtures'/manifest['source'];assert sha(source)==manifest['source_sha256']
with Image.open(source) as image:original_indices=list(image.getdata());size=image.size
directory=Path(run['run_directory'])/scenario.stem/'pixels'
assert {p.name for p in directory.iterdir()}=={v['file'] for v in manifest['outputs']}
verified=[]
for row in manifest['outputs']:
    path=directory/row['file']
    with Image.open(folder/row['reference']) as expected,Image.open(path) as actual:
        actual.load();expected.load();rgba=expected.convert('RGBA')
        colors=expected.getpalette()[:48]
        if not row['alpha']:
            matte=Image.new('RGBA',size,'white');matte.alpha_composite(rgba);rgba=matte
            transparency=expected.info['transparency'];colors=[]
            for index in range(16):
                rgb=expected.getpalette()[index*3:index*3+3];alpha=transparency[index]
                colors.extend((value*alpha+255*(255-alpha)+127)//255 for value in rgb)
        assert actual.size==size and actual.mode=='P' and list(actual.getdata())==original_indices,('indices',row)
        assert actual.getpalette()[:48]==colors,('palette',row)
        assert actual.convert('RGBA').tobytes()==rgba.tobytes(),('RGBA',row)
        raw=path.read_bytes()
        if row['alpha']:assert raw[24:26]==bytes([4,3])
        else:assert struct.unpack_from('<H',raw,28)[0]==4
        verified.append(dict(file=row['file'],sha256=sha(path),all_indices_equal=True,entire_palette_equal=True,all_rgba_equal=True))
report=dict(passed=True,outputs=len(verified),compared_pixels=size[0]*size[1]*len(verified),native_steps=case['steps'],
    captures=len(case['captures']),source_sha256=sha(source),scenario_sha256=sha(scenario),verifier_sha256=sha(Path(__file__)),
    executable_sha256=run['executable_sha256'],cases=verified)
args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='cases'}),flush=True)
