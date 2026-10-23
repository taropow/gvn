"""Independently compare actual menu/dialog PNG saves to official RGB observations and owned Alpha."""
from pathlib import Path
import argparse,hashlib,json
from PIL import Image
root=Path(__file__).resolve().parents[1];read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser();parser.add_argument('latest',type=Path);parser.add_argument('executable',type=Path);parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
run=read(args.latest);scenario=root/'tests/autopilot/90-color-adjustment.gvn';case=next(r for r in run['scenarios'] if r['name']==scenario.name)
assert run['passed'] and case['passed'] and run['executable_sha256']==sha(args.executable) and case['scenario_sha256']==sha(scenario)
folder=root/'tests/fixtures/color-adjustment-ui';manifest=read(folder/'cases.json')
assert all(sha(root/'tests/fixtures'/n)==v for n,v in manifest['source_sha256'].items())
directory=Path(run['run_directory'])/scenario.stem
assert {p.name for p in (directory/'pixels').iterdir()}=={r['file'] for r in manifest['outputs']}
assert {p.name for p in (directory/'previews').iterdir()}==set(manifest['preview_files'])
with Image.open(folder/'source-alpha.png') as image:source=image.convert('RGBA');source.load()
verified=[];pixels=0;preview_pixels=0;outside_pixels=0;max_difference=0
def compare(actual,expected,row):
    global outside_pixels,max_difference
    assert actual.size==expected.size
    assert actual.getchannel('A').tobytes()==expected.getchannel('A').tobytes(),('Alpha',row)
    difference=max(abs(a-b) for a,b in zip(actual.convert('RGB').tobytes(),expected.convert('RGB').tobytes()))
    assert difference<=manifest['reference_rgb_max_difference'],('RGB',difference,row);max_difference=max(max_difference,difference)
    if row.get('selection'):
        left,top,right,bottom=row['selection'];original=list(source.getdata());values=list(actual.getdata())
        for y in range(actual.height):
            for x in range(actual.width):
                if not(left<=x<right and top<=y<bottom):assert values[y*actual.width+x]==original[y*actual.width+x];outside_pixels+=1
    return difference
for row in manifest['outputs']:
    path=directory/'pixels'/row['file']
    with Image.open(path) as image,Image.open(folder/row['reference']) as expected:
        actual=image.convert('RGBA');expected=expected.convert('RGBA');difference=compare(actual,expected,row);pixels+=actual.width*actual.height
        assert path.read_bytes()[25] in (3,6)
        if row['file']!='animation.png':assert 0 in actual.getchannel('A').getdata() and any(0<a<255 for a in actual.getchannel('A').getdata())
        verified.append(dict(file=row['file'],sha256=sha(path),size=list(actual.size),max_rgb_difference=difference,all_alpha_equal=True))
for name in manifest['preview_files']:
    with Image.open(directory/'previews'/name) as image,Image.open(folder/name) as expected:
        actual=image.convert('RGBA');expected=expected.convert('RGBA');row=next(r for r in manifest['outputs'] if r['file']==name);compare(actual,expected,row)
        assert actual.width<=256 and actual.height<=192;preview_pixels+=actual.width*actual.height
report=dict(passed=True,outputs=len(verified),compared_pixels=pixels,comparison_previews=len(manifest['preview_files']),preview_pixels=preview_pixels,untouched_roi_pixels_compared_exact=outside_pixels,
    max_rgb_difference=max_difference,all_alpha_exact=True,native_steps=case['steps'],captures=len(case['captures']),executable_sha256=run['executable_sha256'],scenario_sha256=sha(scenario),verifier_sha256=sha(Path(__file__)),cases=verified)
args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='cases'}),flush=True)
