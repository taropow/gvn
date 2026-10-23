"""Check native dialog saves using Pillow and the actual reference16bit raster values."""
from pathlib import Path
import argparse,hashlib,json,struct
from PIL import Image
root=Path(__file__).resolve().parents[1];read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser();parser.add_argument('latest',type=Path);parser.add_argument('executable',type=Path);parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
run=read(args.latest);scenario=root/'tests/autopilot/89-color-depth.gvn';case=next(r for r in run['scenarios'] if r['name']==scenario.name)
assert run['passed'] and case['passed'] and run['executable_sha256']==sha(args.executable) and case['scenario_sha256']==sha(scenario)
directory=Path(run['run_directory'])/scenario.stem/'pixels';manifest=read(root/'tests/fixtures/color-depth-ui/cases.json')
assert all(sha(root/'tests/fixtures'/n)==v for n,v in manifest['source_sha256'].items())
assert {p.name for p in directory.iterdir()}=={r['file'] for r in manifest['outputs']}
def codes(data):
    assert data[28]==16;w,h=struct.unpack_from('<ii',data,18);offset=struct.unpack_from('<I',data,10)[0];stride=(w*2+3)//4*4
    return [struct.unpack_from('<H',data,offset+(abs(h)-1-y if h>0 else y)*stride+x*2)[0] for y in range(abs(h)) for x in range(w)]
verified=[];pixels=0;values=0
for row in manifest['outputs']:
    path=directory/row['file'];reference=root/'tests/fixtures'/row['reference'];raw=path.read_bytes()
    with Image.open(path) as actual,Image.open(reference) as expected:
        actual.load();expected.load();assert actual.size==expected.size;rgba=actual.convert('RGBA');target=expected.convert('RGBA');pixels+=actual.width*actual.height
        if row.get('reduction'):
            assert actual.mode=='RGBA' and rgba.getchannel('A').tobytes()==target.getchannel('A').tobytes()
            colors=set(actual.convert('RGB').getdata());assert len(colors)<=256 and rgba.tobytes()!=target.tobytes()
            assert any(a==0 and (r,g,b)!=(255,255,255) for r,g,b,a in rgba.getdata())
        else:
            assert rgba.tobytes()==target.tobytes(),('RGBA',row)
            if row.get('bpp'):
                actual_codes=codes(raw);assert actual_codes==codes(reference.read_bytes());values+=len(actual_codes)
                assert struct.unpack_from('<I',raw,30)[0]==(3 if '565' in row['file'] else 0)
            else:
                assert raw[24]==row['bits'] and raw[25]==(3 if row['palette'] else 6)
                if expected.mode=='P' and row['palette']:
                    assert list(actual.getdata())==list(expected.getdata()) and actual.getpalette()[:48]==expected.getpalette()[:48]
        verified.append(dict(file=row['file'],sha256=sha(path),size=list(actual.size),all_rgba_equal=not row.get('reduction',False),all_alpha_equal=True,quantized_rgb_limit_verified=row.get('reduction',False)))
report=dict(passed=True,outputs=len(verified),compared_pixels=pixels,reference_component_codes_compared=values,native_steps=case['steps'],captures=len(case['captures']),
    executable_sha256=run['executable_sha256'],scenario_sha256=sha(scenario),verifier_sha256=sha(Path(__file__)),cases=verified)
args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='cases'}),flush=True)
