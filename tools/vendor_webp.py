"""Restore the pinned official WebP source without modifying other dependencies."""
from pathlib import Path
from urllib.request import Request, urlopen
import hashlib, io, json, tarfile
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'third_party/manifest.json').read_text(encoding='utf-8'))
package=next(p for p in manifest if p['name']=='libwebp')
cache=root/'.cache'/('libwebp-'+package['version']+'.tar.gz')
if cache.exists():
    data=cache.read_bytes()
else:
    with urlopen(Request(package['archive_url'],headers={'User-Agent':'GVN-build'}),timeout=90) as response:
        data=response.read(32*1024*1024+1)
    assert len(data)<=32*1024*1024
    assert hashlib.sha256(data).hexdigest()==package['archive_sha256']
    cache.parent.mkdir(exist_ok=True);cache.write_bytes(data)
assert hashlib.sha256(data).hexdigest()==package['archive_sha256']
target=root/'third_party/libwebp'
files={}
with tarfile.open(fileobj=io.BytesIO(data)) as archive:
    for member in archive:
        assert not member.issym() and not member.islnk()
        if not member.isfile():continue
        relative='/'.join(member.name.split('/')[1:])
        output=(target/relative).resolve()
        assert relative and output.is_relative_to(target.resolve()) and relative not in files
        content=archive.extractfile(member).read()
        files[relative]=hashlib.sha256(content).hexdigest()
        output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(content)
assert len(files)==package['files']
assert hashlib.sha256(json.dumps(files,sort_keys=True).encode()).hexdigest()==package['source_files_sha256']
print('Verified pinned libwebp',package['version'],len(files),'files')
