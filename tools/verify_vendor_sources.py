"""Check every file in the fixed saving-codec archives against the recorded tree hash."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root/'third_party/manifest.json').read_text(encoding='utf-8'))
total = 0
names = []
for package in manifest:
    if 'source_files_sha256' not in package:
        continue
    folder = root/'third_party'/package['name']
    files = {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in folder.rglob('*') if p.is_file()}
    actual = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    assert len(files) == package['files'] and actual == package['source_files_sha256'], package['name']+' source tree changed'
    total += len(files)
    names.append(package['name'])
assert set(names) == {'zlib', 'libpng', 'libjpeg-turbo', 'giflib', 'picojson'}
print(json.dumps(dict(passed=True, files=total, libraries=names)))
