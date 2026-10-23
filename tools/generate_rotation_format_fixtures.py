"""Build independent RGBA/coordinate fixtures from public generated reference assets."""
from pathlib import Path
from PIL import Image
import hashlib,json,shutil

root=Path(__file__).resolve().parents[1]
packet=root/'docs/verification/rotation-formats-reference-0.45'
reference=packet.parent/(packet.name+'.json')
output=root/'tests/fixtures/view-rotation-formats'
assert not output.exists(), 'Do not overwrite frozen fixtures'
summary=json.loads(reference.read_text(encoding='utf-8'))
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
assert summary['passed'] and summary['cases']==120
for name,digest in summary['files'].items():assert sha(packet/name)==digest
output.mkdir()
rows=[]
for kind,group in summary['groups'].items():
    report=json.loads((packet/kind/'result.json').read_text(encoding='utf-8'))
    for baseline in report['baselines']:
        shape=baseline['shape'];source=packet/kind/'assets'/baseline['input']
        destination=output/kind/source.name;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,destination)
        with Image.open(source) as image:
            width,height=image.size
            frames=[]
            for frame in range(image.n_frames):
                image.seek(frame);frames.append(image.convert('RGBA').tobytes())
        for mode in range(8):
            left=mode==1 or mode==3 and width<height or mode==5 and width>height
            right=mode==2 or mode==4 and width<height or mode==6 and width>height
            out_width,out_height=(height,width) if left or right else (width,height)
            permutation=[None]*(width*height)
            for y in range(height):
                for x in range(width):
                    xx,yy=(y,width-1-x) if left else (height-1-y,x) if right else (width-1-x,height-1-y) if mode==7 else (x,y)
                    permutation[yy*out_width+xx]=y*width+x
            assert sorted(permutation)==list(range(width*height))
            prefix=f'{kind}/{shape}-{mode}'
            index_path=output/(prefix+'.indices')
            index_path.write_bytes(b''.join(index.to_bytes(2,'little') for index in permutation))
            for frame,data in enumerate(frames):
                (output/(prefix+f'-frame{frame}.rgba')).write_bytes(b''.join(data[4*index:4*index+4] for index in permutation))
            rows.append('\t'.join(map(str,[kind,shape,mode,destination.relative_to(output).as_posix(),out_width,out_height,len(frames),index_path.relative_to(output).as_posix(),prefix])))
assert len(rows)==120
(output/'cases.tsv').write_text('\n'.join(rows)+'\n',encoding='utf-8')
manifest=dict(reference='../../../docs/verification/rotation-formats-reference-0.45.json',reference_sha256=sha(reference),generator_sha256=sha(Path(__file__)),cases=120,frames=168,
              columns=['kind','shape','mode','source','width','height','frame_count','coordinate_indices_le16','expected_rgba_prefix'],
              alpha_contract='PNG includes hidden RGB. GIF compares independent decoded Alpha and visible RGB; each full decoded RGBA including hidden RGB must also follow the independent coordinate permutation exactly.',
              files={path.relative_to(output).as_posix():sha(path) for path in sorted(output.rglob('*')) if path.is_file()})
(output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(cases=120,frames=168,files=len(manifest['files']))))
