"""Check frozen synthetic GIF reference artifacts; does not run a viewer."""
from pathlib import Path
import hashlib
import json
import statistics


root = Path(__file__).resolve().parents[1]
directory = root / 'docs/verification/gif-reference-0.45'
summary = json.loads((directory.parent / (directory.name + '.json')).read_text(encoding='utf-8'))
assert summary['clock_replacement'] is False
for name, expected in summary['files'].items():
    path = (directory / name).resolve()
    assert path.is_relative_to(directory.resolve())
    assert path.suffix.lower() not in ('.exe', '.dll', '.bin')
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name

for kind in ('timing', 'loops', 'background'):
    report = json.loads((directory / kind / 'result.json').read_text(encoding='utf-8'))
    assert report['passed'] and report['child_terminal'] and report['noninteractive']
    assert report['default_mode'] == 0 and len(report['choices']) == 5
    assert not report['observer_build']['clock_replacement']
    assert len(report['cases']) == {'timing': 5, 'loops': 25, 'background': 60}[kind]
    for case in report['cases']:
        if kind == 'timing':
            transitions = []
            for row in case['timer_calls']:
                rgb = [row[5] & 255, (row[5] >> 8) & 255, (row[5] >> 16) & 255]
                if row[2] in (4, 5) and rgb in report['colors']:
                    transitions.append((row[0], report['colors'].index(rgb)))
            delays = {i: [] for i in range(11)}
            for before, after in zip(transitions, transitions[1:]):
                delays[before[1]].append(after[0] - before[0])
            measured = [round(statistics.median(value), 3) for value in delays.values()]
            frozen = summary['groups']['timing']['measurements'][case['mode']]
            assert measured == frozen['observed_medians_ms']
        elif kind == 'loops':
            value, frames = case['loop_field'], case['painted_frames']
            assert all(frame == index % 3 for index, frame in enumerate(frames))
            if value == 0:
                assert len(frames) > 12
            else:
                passes = 1 if value is None else value if case['mode'] in (2, 3) else value + 1
                assert len(frames) == passes * 3
        else:
            assert len(case['frames']) == 3
            method = summary['groups']['background']['reserved_disposal_map'][case['mode']][case['disposal']]
            left = {0: [20, 210, 60], 2: [128, 128, 128], 3: [220, 20, 40]}[method]
            expected = [
                [[220, 20, 40]] * 32,
                ([[20, 210, 60]] * 4 + [[220, 20, 40]] * 4) * 4,
                ([left] * 4 + [[30, 50, 220]] * 4) * 4,
            ]
            assert [frame['pixels'] for frame in case['frames']] == expected

print(json.dumps({'passed': True, 'cases': 90, 'composed_frames': 180,
                  'rgb_pixels': 5760, 'files': len(summary['files']),
                  'gvn_implementation_complete': False}))
