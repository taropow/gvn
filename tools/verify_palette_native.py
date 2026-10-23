"""Independent palette/RGBA comparison of files saved through the actual native App UI."""
import hashlib, json, struct, sys
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
sha = lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
run = json.loads((root/'.cache/autopilot/latest.json').read_text(encoding='utf-8'))
assert run['passed'] and run['executable_sha256'] == sha(Path(sys.argv[1]).resolve())
scenario = root/'tests/autopilot/28-palette-preservation.gvn'
case = next(c for c in run['scenarios'] if c['name']==scenario.name)
assert case['passed'] and case['scenario_sha256']==sha(scenario)
folder = Path(run['run_directory'])/scenario.stem/'pixels'
expected_cases = json.loads((root/'tests/palette-native-cases.json').read_text())
manifest = json.loads((root/'tests/palette-cases.json').read_text())
fixtures = {c['name']:c for c in manifest['cases']}
assert {p.name for p in folder.iterdir()} == {c['file'] for c in expected_cases}
formats = {}; hashes = {}
for saved in expected_cases:
    source = fixtures[saved['source']]
    reference = Image.new('P',(manifest['width'],manifest['height']))
    reference.putdata(source['indices'])
    if saved['op'] in (2,8): reference=reference.transpose(Image.Transpose.ROTATE_270)
    palette = [tuple(c) for c in source['palette']]
    if saved['format']=='Bmp':
        palette = [tuple((c*a+255*(255-a)+127)//255 for c in (r,g,b))+(255,) for r,g,b,a in palette]
    expected = [palette[index] for index in reference.getdata()]
    if saved['op']==8: expected=[(255-r,255-g,255-b,a) for r,g,b,a in expected]
    path=folder/saved['file']; raw=path.read_bytes()
    with Image.open(path) as actual:
        actual.load()
        assert actual.size == reference.size,saved
        assert actual.convert('RGBA').tobytes()==bytes(v for pixel in expected for v in pixel),('RGBA',saved)
        if saved['op']!=8:
            assert actual.mode=='P' and list(actual.getdata())==list(reference.getdata()),('indices',saved)
            assert actual.getpalette()[:len(palette)*3]==[v for p in palette for v in p[:3]],('palette',saved)
        else: assert actual.mode=='RGBA'
    if saved['format']=='Png':
        assert raw[25] == (6 if saved['op']==8 else 3)
        assert raw[24] == (8 if saved['op']==8 else source['bits'])
    if saved['format']=='Bmp': assert struct.unpack_from('<H',raw,28)[0]==(4 if source['bits']==2 else source['bits'])
    formats[saved['format']]=formats.get(saved['format'],0)+1; hashes[saved['file']]=sha(path)
report=dict(passed=True,verified_at=datetime.now(timezone.utc).isoformat(),outputs=len(expected_cases),formats=formats,
            native_steps=case['steps'],palette_index_errors=0,rgba_pixel_errors=0,executable_sha256=run['executable_sha256'],
            scenario_sha256=sha(scenario),cases_sha256=sha(root/'tests/palette-native-cases.json'),verifier_sha256=sha(Path(__file__)),
            method='Native App open, palette assertion, actual format save controls, RotateRight/Negative/Undo and private RGBA clipboard; independent fixture palette/index transform and Pillow saved-file decode',
            run_directory=str(folder),file_sha256=hashes)
(root/'.cache/palette-native-oracle-latest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='file_sha256'}))
