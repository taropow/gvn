"""Run GVN's native, private-desktop autopilot and retain reproducible evidence."""
import argparse
import hashlib
import json
import subprocess
import uuid
from datetime import datetime,timezone
from pathlib import Path
from PIL import Image

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('executable',type=Path)
parser.add_argument('--output',type=Path,default=root/'.cache/autopilot')
parser.add_argument('--scenario',type=Path,action='append')
args=parser.parse_args()
exe=args.executable.resolve()
run=args.output.resolve()/('run-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'-'+uuid.uuid4().hex[:8])
run.mkdir(parents=True)
scenarios=args.scenario or sorted((root/'tests/autopilot').glob('*.gvn'))
assert scenarios,'No autopilot scenarios'
summary={'method':'GVN in-process native UI autopilot; private window station/clipboard; no ComputerUse, Sky, global input or external app handles; private-desktop cursor position supplied by scenarios',
         'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
         'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'run_directory':str(run),'scenarios':[]}
scenario_hashes={p.resolve():hashlib.sha256(p.read_bytes()).hexdigest() for p in scenarios}
failures=[]
for scenario in scenarios:
    directory=run/scenario.stem
    command=[str(exe),str(scenario.resolve()),str(directory),str(root/'tests/fixtures')]
    try:
        result=subprocess.run(command,capture_output=True,timeout=270)
        assert hashlib.sha256(exe.read_bytes()).hexdigest()==summary['executable_sha256'],'Executable changed during run'
        assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==summary['runner_sha256'],'Runner changed during run'
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==expected for p,expected in scenario_hashes.items()),'Scenario changed during run'
        directory.mkdir(exist_ok=True)
        (directory/'stdout.txt').write_bytes(result.stdout)
        (directory/'stderr.txt').write_bytes(result.stderr)
        data=json.loads((directory/'result.json').read_text(encoding='utf-8'))
        assert result.returncode==0 and data['passed'],result.stderr.decode('utf-8',errors='replace')
        assert all(s['completed'] and not s['error'] for s in data['steps'])
        # Compact native TaskDialogs are deliberately smaller than the settings/main window.
        # Derive the capture kind from the last completed native capture operation; keep the
        # original minimum for every other capture rather than padding the product for a test.
        capture_kinds = {}
        small_sizes = {}
        for step in data['steps']:
            action = step['action'].split('\t')
            if action[0] in {'snapshot','snapshot_small','fit_snapshot','font_snapshot','color_snapshot','save_snapshot',
                             'transfer_snapshot','boundary_snapshot','about_snapshot','info_snapshot','grid_snapshot','rect_snapshot','print_snapshot','resize_snapshot','canvas_snapshot','jump_snapshot','rainbow_snapshot','cp_snapshot','picker_snapshot'}:
                capture_kinds[Path(action[1]).name] = action[0]
                if action[0] == 'snapshot_small':
                    assert len(action) == 4 and int(action[2]) > 0 and int(action[3]) > 0
                    small_sizes[Path(action[1]).name] = (int(action[2]),int(action[3]))
        captures=[]
        for path in sorted(directory.glob('*.png')):
            with Image.open(path) as image:
                kind = capture_kinds.get(path.name,'window')
                minimum_height = 100 if kind in {'boundary_snapshot','about_snapshot'} else 200
                if kind == 'fit_snapshot':
                    assert image.width>=64 and image.height>=64,('Fitted window capture too small',path)
                elif kind == 'snapshot_small':
                    assert image.size == small_sizes[path.name],('Small capture dimensions differ',path,image.size)
                else:
                    assert image.width>=300 and image.height>=minimum_height,('Capture too small',path)
                assert len(image.convert('RGB').getcolors(image.width*image.height) or [])>=8,('Blank/monochrome window capture',path)
                captures.append({'name':path.name,'kind':kind,'size':list(image.size),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        summary['scenarios'].append({'name':scenario.name,'scenario_sha256':scenario_hashes[scenario.resolve()],
                                     'passed':True,'steps':len(data['steps']),'captures':captures})
        print(f'PASS {scenario.name}: {len(data["steps"])} native operations/assertions, {len(captures)} window captures',flush=True)
    except Exception as error:
        failures.append(scenario.name)
        summary['scenarios'].append({'name':scenario.name,'passed':False,'error':str(error)})
        print(f'FAIL {scenario.name}: {error}',flush=True)
summary['passed']=not failures
(run/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
args.output.mkdir(parents=True,exist_ok=True)
(args.output/'latest.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'Evidence: {run}',flush=True)
raise SystemExit(1 if failures else 0)
