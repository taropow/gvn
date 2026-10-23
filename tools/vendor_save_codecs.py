"""Vendor fixed official PNG/JPEG save libraries; builds never download dependencies."""
import hashlib
import io
import json
import tarfile
from pathlib import Path
from urllib.request import Request, urlopen

root = Path(__file__).resolve().parents[1]
packages = [
    dict(name='zlib', repository='https://github.com/madler/zlib', version='1.3.2',
         revision='da607da739fa6047df13e66a2af6b8bec7c2a498',
         archive_sha256='b9258cf6254e7f7c37f1cd61dba943a1c5ea3cff5718c789834dac359094f5f7',
         archive_url='https://codeload.github.com/madler/zlib/tar.gz/da607da739fa6047df13e66a2af6b8bec7c2a498'),
    dict(name='libpng', repository='https://github.com/pnggroup/libpng', version='1.6.59',
         revision='cd952f49f95bb27154ae77dbb103032d95f6e580',
         archive_sha256='ef1bccad2b2a58eb3c09e06ffe3b72b8604abda8b5011e4c4bb5391989040f26',
         archive_url='https://codeload.github.com/pnggroup/libpng/tar.gz/cd952f49f95bb27154ae77dbb103032d95f6e580'),
    dict(name='libjpeg-turbo', repository='https://github.com/libjpeg-turbo/libjpeg-turbo', version='3.2.0',
         revision='c85e6b905bf237038faa936dab160ebfc5da0344',
         archive_url='https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz',
         archive_sha256='6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e'),
]
manifest_path = root/'third_party/manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
for package in packages:
    request = Request(package['archive_url'], headers={'User-Agent': 'GVN-build'})
    with urlopen(request, timeout=90) as response:
        blob = response.read(32*1024*1024+1)
    assert len(blob) <= 32*1024*1024, 'Unexpectedly large source archive'
    digest = hashlib.sha256(blob).hexdigest()
    expected = package.get('archive_sha256')
    if expected:
        assert digest == expected, package['name']+' archive hash differs'
    target = root/'third_party'/package['name']
    count = 0
    files = {}
    with tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz') as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            relative = '/'.join(member.name.split('/')[1:])
            if not relative:
                continue
            output = (target/relative).resolve()
            assert output.is_relative_to(target.resolve()), 'Unsafe source archive path'
            output.parent.mkdir(parents=True, exist_ok=True)
            data = archive.extractfile(member).read()
            output.write_bytes(data)
            files[relative] = hashlib.sha256(data).hexdigest()
            count += 1
    package.update(archive_sha256=digest, files=count,
                   source_files_sha256=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest())
    manifest = [item for item in manifest if item['name'] != package['name']]
    manifest.append(package)
    manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(package), flush=True)
