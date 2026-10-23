"""Fetch a pinned official miniz release; record provenance and archive hash."""
import hashlib, io, json, tarfile, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
tag='3.1.2'
request=urllib.request.Request(f'https://api.github.com/repos/richgel999/miniz/git/ref/tags/{tag}',headers={'User-Agent':'GVN-build'})
revision=json.load(urllib.request.urlopen(request))['object']['sha']
blob=urllib.request.urlopen(f'https://codeload.github.com/richgel999/miniz/tar.gz/{revision}').read()
out=root/'third_party'/'miniz';out.mkdir(exist_ok=True)
files=['miniz.c','miniz.h','miniz_common.h','miniz_tdef.c','miniz_tdef.h','miniz_tinfl.c','miniz_tinfl.h','miniz_zip.c','miniz_zip.h','LICENSE','readme.md']
with tarfile.open(fileobj=io.BytesIO(blob)) as archive:
    for name in files:
        entry=next(m for m in archive.getmembers() if m.name.split('/',1)[-1]==name)
        (out/name).write_bytes(archive.extractfile(entry).read())
(out/'miniz_export.h').write_text('#pragma once\n#define MINIZ_EXPORT\n',encoding='utf-8')
manifest=root/'third_party'/'manifest.json'
data=json.loads(manifest.read_text(encoding='utf-8'))
data=[e for e in data if e['name']!='miniz']
data.append(dict(name='miniz',repository='https://github.com/richgel999/miniz',revision=revision,version=tag,archive_sha256=hashlib.sha256(blob).hexdigest(),files=len(files)+1))
manifest.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
print('miniz',tag,revision)
