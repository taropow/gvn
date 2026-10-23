"""Vendor the fixed official GIFLIB release; never runs during an application build."""
import hashlib
import io
import json
import subprocess
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
url = 'https://downloads.sourceforge.net/project/giflib/giflib-6.x/giflib-6.1.3.tar.gz'
expected = 'b65b66b99f0424b93525f987386f22fc5efb9da2bfc92ad4a532249aaffbab0e'
archive_path = root/'.cache/giflib-6.1.3.tar.gz'
archive_path.parent.mkdir(parents=True, exist_ok=True)
if not archive_path.exists():
    subprocess.run(['curl.exe', '-fL', '--max-time', '60', '--max-filesize', '4194304',
                    url, '-o', str(archive_path)], check=True, timeout=65)
blob = archive_path.read_bytes()
assert len(blob) <= 4*1024*1024 and hashlib.sha256(blob).hexdigest() == expected
target = root/'third_party/giflib'
assert not target.exists(), 'Refuse to replace an existing vendor source tree'
files = {}
with tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz') as archive:
    for member in archive.getmembers():
        if not member.isfile():
            continue
        relative = '/'.join(member.name.split('/')[1:])
        if not relative:
            continue
        output = (target/relative).resolve()
        assert output.is_relative_to(target.resolve())
        assert relative not in files
        data = archive.extractfile(member).read()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
        files[relative] = hashlib.sha256(data).hexdigest()
package = dict(name='giflib', repository='https://sourceforge.net/p/giflib/code/', version='6.1.3',
               revision='edff4aed17f857442ab0cac31566572ba08f93d3', archive_url=url,
               archive_sha256=expected, files=len(files),
               source_files_sha256=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest())
manifest_path = root/'third_party/manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
assert not any(item['name'] == package['name'] for item in manifest)
manifest.append(package)
manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
print(json.dumps(package))
