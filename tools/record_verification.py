"""Freeze successful CTest evidence beside the exact release/source hashes."""
import hashlib
import json
import re
import shutil
import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
version = re.search(r'project\(GVN VERSION ([\d.]+) ', (root/'CMakeLists.txt').read_text()).group(1)
xml_path = build/'ctest-results.xml'
tree = ET.parse(xml_path).getroot()
tests = list(tree.iter('testcase'))
expected_tests=set(re.findall(r'add_test\(NAME (\w+)',(root/'CMakeLists.txt').read_text()))
assert len(tests) == len(expected_tests) and {t.attrib['name'] for t in tests}==expected_tests, 'Expected the complete configured CTest run'
assert all(t.find('failure') is None and t.find('error') is None and t.find('skipped') is None for t in tests)
report = json.loads((root/'.cache/app-audit.json').read_text(encoding='utf-8'))
expected_cases=set(re.findall(r'^(?:APP_TEST|LEGACY_WIDE_TEST)\((\w+)\)',(root/'tests/app_tests.cpp').read_text(encoding='utf-8'),re.M))
assert report['failures'] == 0 and len(report['cases']) == len(expected_cases) and {c['name'] for c in report['cases']}==expected_cases and all(c['passed'] for c in report['cases'])

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

files = [root/'CMakeLists.txt', root/'third_party/manifest.json']
for directory in ['src', 'include', 'tests', 'cmake']:
    files.extend(path for path in (root/directory).rglob('*') if path.is_file())
files.extend(path for path in (root/'tools').rglob('*') if path.is_file() and path.suffix in {'.py','.cpp','.ps1','.def'})
manifest = {p.relative_to(root).as_posix(): sha(p) for p in sorted(files)}
source_hash = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
exe = build/'Release/GVN.exe'
assert exe.is_file()
assert max(p.stat().st_mtime for p in files) < xml_path.stat().st_mtime, 'Sources changed after the test run'
report.update(version=version, recorded_at=datetime.now(timezone.utc).isoformat(),
              gvn_sha256=sha(exe), gvn_bytes=exe.stat().st_size, sources_sha256=source_hash,
              ctest=[dict(name=t.attrib['name'],seconds=float(t.attrib.get('time',0))) for t in tests])
output = root/'docs/verification'
output.mkdir(exist_ok=True)
autopilot=json.loads((root/'.cache/autopilot/latest.json').read_text(encoding='utf-8'))
scenario_names={p.name for p in (root/'tests/autopilot').glob('*.gvn')}
assert autopilot['passed'] and len(autopilot['scenarios'])==len(scenario_names) and {s['name'] for s in autopilot['scenarios']}==scenario_names
assert autopilot['executable_sha256']==sha(build/'Release/gvn_autopilot.exe')
evidence=output/f'{version}-autopilot'
evidence.mkdir(exist_ok=True)
run=Path(autopilot['run_directory'])
for scenario in autopilot['scenarios']:
    name=scenario['name']
    assert scenario['passed'] and scenario['scenario_sha256']==sha(root/'tests/autopilot'/name)
    source=run/Path(name).stem
    destination=evidence/Path(name).stem
    destination.mkdir(exist_ok=True)
    for capture in scenario['captures']:
        assert sha(source/capture['name'])==capture['sha256']
        shutil.copyfile(source/capture['name'],destination/capture['name'])
    for name in ['result.json','stdout.txt','stderr.txt']:
        shutil.copyfile(source/name,destination/name)
shutil.copyfile(run/'summary.json',evidence/'summary.json')
report['autopilot']={'summary':f'{version}-autopilot/summary.json','scenarios':len(autopilot['scenarios']),
                     'steps':sum(s['steps'] for s in autopilot['scenarios']),
                     'captures':sum(len(s['captures']) for s in autopilot['scenarios']),
                     'executable_sha256':autopilot['executable_sha256']}
(output/f'{version}-contract.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(output/f'{version}-source-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
shutil.copyfile(xml_path,output/f'{version}-ctest.xml')
print(json.dumps(dict(version=version,ctest=len(tests),cases=len(report['cases']),commands=len(report['commands']),bytes=report['gvn_bytes'],sha256=report['gvn_sha256'])))
