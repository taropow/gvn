"""Measure owned hidden GVN instances; never inspect or close existing viewer processes."""
import argparse
import hashlib
import json
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--instances', type=int, default=4)
    parser.add_argument('--samples', type=int, default=3)
    args = parser.parse_args()
    assert 1 <= args.instances <= 16 and 1 <= args.samples <= 10
    exe, image, output = args.executable.resolve(), args.image.resolve(), args.output.resolve()
    assert exe.is_file() and image.is_file()
    output.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    cohorts = []
    for sample in range(args.samples):
        children = []
        started = time.perf_counter()
        try:
            for index in range(args.instances):
                report = output / f'{sample}-{index}.json'
                assert not report.exists(), 'Use a fresh output directory'
                child = subprocess.Popen([str(exe), '--profile', str(image), str(report)])
                children.append((child, report))
            results = []
            for child, report in children:
                assert child.wait(timeout=30) == 0
                data = json.loads(report.read_text())
                assert data['rendered'] and not data['error'] and data['width'] and data['height']
                results.append(data)
            cohorts.append({'sample': sample, 'seconds': time.perf_counter() - started,
                            'results': results,
                            'sum_private_bytes': sum(d['private_bytes'] for d in results),
                            'sum_working_set_bytes': sum(d['working_set'] for d in results)})
            print('cohort', sample, 'ready_ms', [d['ready_ms'] for d in results],
                  'sum_private_MiB', round(cohorts[-1]['sum_private_bytes'] / 1048576, 1), flush=True)
        finally:
            for child, _ in children:
                if child.poll() is None:
                    child.kill()  # Only Popen handles created in this cohort.
                    child.wait()
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == digest, 'Executable changed during measurement'
    result = {'verified_at': datetime.now(timezone.utc).isoformat(), 'gvn_sha256': digest,
              'asset_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
              'instances': args.instances,
              'method': 'Owned hidden native --profile processes launched consecutively per cohort; '
                        'OS file cache not cleared. ready_ms ends before rendering; newer binaries report '
                        'display_ready_ms after initial Direct2D draw and first_draw_ms for the draw itself. '
                        'These exclude process creation and OleInitialize. '
                        'Each process measures two seconds of idle CPU. Memory is sampled per process '
                        'after loading, not simultaneous peak. Summed working sets may double-count '
                        'shared pages. This does not compare MassiGra or measure DWM presentation.',
              'cohorts': cohorts}
    (output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
