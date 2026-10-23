"""Measure the native viewer with saved assets, fresh processes and retained raw samples."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('assets', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--samples', type=int, default=3)
args = parser.parse_args()
assert 1 <= args.samples <= 20
exe, assets, output = args.executable.resolve(), args.assets.resolve(), args.output.resolve()
version = re.search(r'project\(GVN VERSION ([\d.]+) ', (root / 'CMakeLists.txt').read_text()).group(1)
paths = sorted(p for p in assets.iterdir() if p.suffix.lower() in {'.png', '.jpg', '.jpeg'})
assert paths and assets != output
output.mkdir(parents=True, exist_ok=True)
summary = {
    'version': version, 'gvn_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
    'method': f'{args.samples} fresh processes per saved asset, hidden window, default Linear; '
              'OS file cache not cleared. ready_ms ends at adoption before rendering. '
              'display_ready_ms ends after initial Direct2D draw returns; first_draw_ms covers that draw. '
              'All exclude process creation, OleInitialize and DWM presentation; '
              'idle CPU measured for 2 seconds. result is the sample with median display_ready_ms. '
              'Neighbor prefetch is asynchronous. Hardware DPI, high-quality filters and '
              'real-photo performance remain separate work.',
    'assets': [],
}
for path in paths:
    target = output / path.name
    shutil.copyfile(path, target)
    samples = []
    with tempfile.TemporaryDirectory(prefix='GVN-profile-') as temporary:
        for index in range(args.samples):
            report = Path(temporary) / f'{index}.json'
            result = subprocess.run([str(exe), '--profile', str(target), str(report)], timeout=30)
            data = json.loads(report.read_text())
            assert result.returncode == 0 and data['rendered'] and not data['error'], data
            assert data['width'] > 0 and data['height'] > 0 and data['frames'] == 1
            assert data['first_draw_succeeded'] and data['display_ready_ms'] >= data['ready_ms']
            assert data['first_draw_ms'] > 0
            samples.append(data)
    summary['assets'].append({
        'asset': path.name, 'sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
        'result': sorted(samples, key=lambda s: s['display_ready_ms'])[len(samples) // 2], 'samples': samples,
        'ready_ms_range': [min(s['ready_ms'] for s in samples), max(s['ready_ms'] for s in samples)],
        'display_ready_ms_range': [min(s['display_ready_ms'] for s in samples), max(s['display_ready_ms'] for s in samples)],
        'working_set_range': [min(s['working_set'] for s in samples), max(s['working_set'] for s in samples)],
    })
    print(path.name, 'load_adopted_ms', [s['ready_ms'] for s in samples],
          'display_ready_ms', [s['display_ready_ms'] for s in samples],
          'first_draw_ms', [s['first_draw_ms'] for s in samples], 'working_set_MiB',
          [round(s['working_set'] / 1048576, 1) for s in samples], 'idle_cpu_ms',
          [s['idle_cpu_ms'] for s in samples], flush=True)
(output / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
