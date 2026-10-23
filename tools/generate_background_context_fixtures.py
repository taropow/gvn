"""Create lossless test fixtures from the independently checked official background contexts."""
from pathlib import Path
import hashlib,json,re,shutil
root=Path(__file__).resolve().parents[1]
packet=root/'docs/verification/background-context-reference-0.45'
summary_path=packet.with_name(packet.name+'.json')
summary=json.loads(summary_path.read_text(encoding='utf-8'))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert summary['passed'] and summary['captures']==52
assert all(sha(packet/name)==digest for name,digest in summary['files'].items())
output=root/'tests/fixtures/window-background-contexts';assert not output.exists();output.mkdir()
shutil.copyfile(packet/'source.png',output/'source.png')
rows=[]
for index,item in enumerate(summary['cases']):
 capture=item['capture'];width,height=capture['size'];client=capture['actual_client']
 roi=capture['roi'] or [0,0,width,height]
 box=item['source_rect'] or [-1,-1,0,0]
 percent=re.search(r'([0-9.]+)\s*%',item['title'])
 name=f'expected-{index:02d}.png';shutil.copyfile(packet/capture['file'],output/name)
 colors=[(r<<16)|(g<<8)|b for r,g,b in item['colors']]
 fields=[item['label'],int(item['tiled']),[8,16,32,64,128].index(item['cell']),*colors,
         client[2],client[3],*roi,*box,percent.group(1) if percent else '100',name]
 assert len(fields)==17
 rows.append('\t'.join(map(str,fields)))
(output/'cases.tsv').write_text('\n'.join(rows)+'\n',encoding='utf-8')
manifest=dict(reference=summary_path.relative_to(root).as_posix(),reference_sha256=sha(summary_path),cases=52,
 scope='Background RGB outside reference/current image rectangles with2px edge margin; real original RGB source retained. Image resize pixels are covered separately by filter and source-copy tests.',
 files={p.name:sha(p) for p in output.iterdir() if p.is_file()})
(output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print('Generated52official background context fixtures')
