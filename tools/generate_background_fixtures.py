"""Freeze the actual normal-viewer RGB reference frames for independent GVN checks."""
from pathlib import Path
import hashlib,json,shutil
root=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
folder=root/'docs/verification/background-reference-0.45'
summary=json.loads((folder.parent/(folder.name+'.json')).read_text(encoding='utf-8'))
assert summary['passed'] and summary['cases']==20 and summary['validated_captures']==42
for name,digest in summary['files'].items():assert sha(folder/name)==digest
report=json.loads((folder/'verified/result.json').read_text(encoding='utf-8'))
out=root/'tests/fixtures/window-background';assert not out.exists();out.mkdir()
shutil.copyfile(folder/'verified/assets/01.png',out/'source.png')
rows=[]
for index,case in enumerate(report['measure_cases']):
    expected=f'expected-{index:02d}.bmp';shutil.copyfile(folder/'verified'/case['immediate']['file'],out/expected)
    colors=case['colors'];values=[case['palette'],case['preset'],int(case['tiled']),
                                 (colors[0][0]<<16)|(colors[0][1]<<8)|colors[0][2],
                                 (colors[1][0]<<16)|(colors[1][1]<<8)|colors[1][2],expected]
    rows.append('\t'.join(map(str,values)))
(out/'cases.tsv').write_text('\n'.join(rows)+'\n',encoding='utf-8')
manifest=dict(reference='../../../docs/verification/background-reference-0.45.json',cases=20,
              scope='Actual opaque source and complete normal client RGB. Captured DIB unused Alpha bytes are not an Alpha oracle.',
              columns=['palette','preset','tiled','first_rgb','second_rgb','expected_bmp'],
              files={p.relative_to(out).as_posix():sha(p) for p in sorted(out.rglob('*')) if p.is_file()})
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print('Generated20whole-client RGB reference fixtures')
