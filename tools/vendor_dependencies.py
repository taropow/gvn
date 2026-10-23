from pathlib import Path
from urllib.request import Request, urlopen
import hashlib
import io
import json
import tarfile

root = Path(__file__).resolve().parents[1]
packages = [
    ('libplum', 'aaaaaa123456789/libplum', '89414559c9a8ecccd4fd1ffd2a31c440662c9dd2', ('src/', 'header/', 'docs/', 'LICENSE', 'README.md')),
    ('psd_sdk', 'MolecularMatters/psd_sdk', 'bfa117317e8bb15bfc9f76bad5800da603fddd1d', ('src/', 'LICENSE', 'README.md')),
]
records = []
for name, repo, revision, prefixes in packages:
    url = 'https://codeload.github.com/' + repo + '/tar.gz/' + revision
    with urlopen(Request(url, headers={'User-Agent': 'GVN-build'}), timeout=90) as response:
        data = response.read()
    destination = root / 'third_party' / name
    count = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            relative = '/'.join(member.name.split('/')[1:])
            if not any(relative == prefix or relative.startswith(prefix) for prefix in prefixes):
                continue
            target = (destination / relative).resolve()
            if not target.is_relative_to(destination.resolve()):
                raise RuntimeError('Unsafe archive path')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(member).read())
            count += 1
    records.append({'name': name, 'repository': 'https://github.com/' + repo, 'revision': revision,
                    'archive_sha256': hashlib.sha256(data).hexdigest(), 'files': count})
    print(name, count, flush=True)
(root / 'third_party' / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
