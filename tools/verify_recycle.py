"""Verify the dedicated Shell probe and retain its exact source/executable hashes."""
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
version = re.search(r'project\(GVN VERSION ([\d.]+) ', (root/'CMakeLists.txt').read_text()).group(1)
sources = [root/name for name in ['tools/recycle_probe.cpp', 'tools/verify_recycle.py',
                                   'src/file_operations.cpp', 'include/gvn/file_operations.h']]
before = {p.relative_to(root).as_posix(): sha(p) for p in sources}
executable_hash = sha(exe)
result = subprocess.run([str(exe)], capture_output=True, timeout=120)
assert result.returncode == 0, result.stdout + result.stderr
report = json.loads(result.stdout)
assert report['passed'] and report['restore_bytes_equal'] and report['cleanup']
assert before == {p.relative_to(root).as_posix(): sha(p) for p in sources}
assert sha(exe) == executable_hash
report.update(version=version, verified_at=datetime.now(timezone.utc).isoformat(),
              executable_sha256=executable_hash, source_sha256=before,
              fixture_sha256=sha(root/'tests/fixtures/alpha.png'),
              owner='Own hidden native HWND; no ShowWindow, foreground activation or global input')
target = root/f'docs/verification/{version}-recycle.json'
target.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=True))
