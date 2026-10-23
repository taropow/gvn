"""Counterbalance two frozen GVN binaries using owned native profile processes."""
import argparse
import hashlib
import json
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--instances', type=int, nargs='+', default=[1, 4])
    parser.add_argument('--samples', type=int, default=3)
    args = parser.parse_args()
    assert 1 <= args.samples <= 10 and all(1 <= n <= 16 for n in args.instances)
    assert len(set(args.instances)) == len(args.instances)
    helper = Path(__file__).with_name('profile_instances.py')
    paths = {role: path.resolve() for role, path in [('baseline', args.baseline), ('candidate', args.candidate)]}
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    frozen = {str(path): sha(path) for path in [*paths.values(), helper, Path(__file__)]}
    image, output = args.image.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    runs = []
    for count in args.instances:
        for sample in range(args.samples):
            order = ['baseline', 'candidate'] if sample % 2 == 0 else ['candidate', 'baseline']
            result = {'instances': count, 'sample': sample, 'order': order}
            for role in order:
                directory = output / f'{count}-{sample}-{role}'
                subprocess.run([sys.executable, str(helper), str(paths[role]), str(image), str(directory),
                                '--instances', str(count), '--samples', '1'], check=True)
                result[role] = json.loads((directory / 'summary.json').read_text())
                assert result[role]['gvn_sha256'] == frozen[str(paths[role])]
            runs.append(result)
    aggregate = []
    for count in args.instances:
        item = {'instances': count}
        for role in paths:
            groups = [r[role]['cohorts'][0] for r in runs if r['instances'] == count]
            item[role] = {
                'private_MiB': [g['sum_private_bytes'] / 1048576 for g in groups],
                'working_set_MiB': [g['sum_working_set_bytes'] / 1048576 for g in groups],
                'ready_ms': [d['ready_ms'] for g in groups for d in g['results']],
                'display_ready_ms': [d['display_ready_ms'] for g in groups for d in g['results']
                                     if 'display_ready_ms' in d],
                'first_draw_ms': [d['first_draw_ms'] for g in groups for d in g['results']
                                  if 'first_draw_ms' in d],
                'idle_cpu_ms': [d['idle_cpu_ms'] for g in groups for d in g['results']]}
        old = statistics.median(item['baseline']['private_MiB'])
        new = statistics.median(item['candidate']['private_MiB'])
        item['median_private_reduction_MiB'] = old - new
        item['median_private_reduction_percent'] = (old - new) * 100 / old
        aggregate.append(item)
    assert all(sha(Path(path)) == digest for path, digest in frozen.items()), 'Measured code changed'
    result = {'verified_at': datetime.now(timezone.utc).isoformat(), 'frozen_sha256': frozen,
              'asset_sha256': sha(image), 'method': 'Serial counterbalanced baseline/candidate cohorts. '
              'ready_ms is adoption before drawing; candidate display_ready_ms ends after first draw. '
              'Native --profile excludes process creation/OleInitialize/DWM presentation. '
              'OS file cache remains warm; no MassiGra runs. Memory is sampled per process after two '
              'idle seconds, not simultaneous peak. See every child summary for conditions.',
              'aggregate': aggregate, 'runs': runs}
    (output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(aggregate), flush=True)


if __name__ == '__main__':
    main()
