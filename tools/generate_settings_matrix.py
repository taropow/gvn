"""Render the settings audit and reject unsupported completion claims."""
import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
rows = json.loads((root/'docs/gvn-settings-coverage.json').read_text(encoding='utf-8'))
assert len({r['id'] for r in rows}) == len(rows)
allowed = {'実装済み', '部分対応', '固定動作', '代替実装', '要照合', '未実装', '対象外', '見出し'}
app_cases = set(re.findall(r'^(?:APP_TEST|LEGACY_WIDE_TEST)\((\w+)\)', (root/'tests/app_tests.cpp').read_text(encoding='utf-8'), re.M))
settings_cases = set(re.findall(r'^TEST\((\w+)\)', (root/'tests/settings_tests.cpp').read_text(encoding='utf-8'), re.M))
unit_cases = {'settings': settings_cases}
for kind, filename in [('render', 'render_tests.cpp'), ('core', 'core_tests.cpp'),
                       ('codecs', 'codec_tests.cpp'), ('gif', 'gif_behavior_tests.cpp'),
                       ('loader', 'loader_tests.cpp'),
                       ('view_rotation', 'view_rotation_tests.cpp'),
                       ('file_transfer_ui', 'file_transfer_dialog_tests.cpp'),
                       ('jump_ui', 'jump_dialog_tests.cpp'), ('comic', 'comic_tests.cpp'),
                       ('external_plan', 'external_tests.cpp'), ('external_settings', 'external_settings_tests.cpp'),
                       ('display_policy', 'display_policy_tests.cpp')]:
    unit_cases[kind] = set(re.findall(r'^TEST\((\w+)\)', (root/'tests'/filename).read_text(encoding='utf-8'), re.M))
if args.check:
    report = json.loads((root/'.cache/app-audit.json').read_text(encoding='utf-8'))
    assert report['failures'] == 0
    assert {c['name'] for c in report['cases'] if c['passed']} == app_cases
    autopilot = json.loads((root/'.cache/autopilot/latest.json').read_text(encoding='utf-8'))
    assert autopilot['passed']
    native_cases = {s['name']: s for s in autopilot['scenarios']}
else:
    native_cases = {}
for i, row in enumerate(rows, 1):
    assert row['id'] == f'S{i:03}' and row['state'] in allowed
    assert row['setting'] and row['reference_behavior'] and row['note']
    for location in row['implementation']:
        assert (root/location.split(':', 1)[0]).is_file(), location
    if row['state'] == '実装済み':
        assert row['implementation'] and row['evidence'], row['id']
    for evidence in row['evidence']:
        kind, case = evidence.split(':', 1)
        if kind == 'app':
            assert case in app_cases, evidence
        elif kind in unit_cases:
            assert case in unit_cases[kind], evidence
        elif kind == 'autopilot':
            script = root/'tests/autopilot'/case
            assert script.is_file(), evidence
            if args.check:
                result = native_cases[case]
                assert result['passed'] and result['scenario_sha256'] == hashlib.sha256(script.read_bytes()).hexdigest()
        else:
            raise AssertionError(evidence)
counts = Counter(r['state'] for r in rows)
lines = ['# MassiGra設定とGVNの対応表', '',
         '参照表187行と、本文にあるキー編集の4項目を記録する。分類見出し5行と対象外のプラグイン13行も含むため、191個の独立した設定を実装したという意味ではない。', '',
         'タブは画面構成であり、同じカスタマイズ性の達成を示さない。「固定動作」は切り替えがない項目、「部分対応」は元の一部だけ、「要照合」は詳細の確認が残る項目。設定名だけで完了と判定しない。', '',
         '[参照仕様](massigra-settings.md)、[編集台帳](gvn-settings-coverage.json)、[継続作業](gvn-goal-plan.md)を参照。証拠欄は実行シナリオ名で、最新の結果とソース・バイナリの対応は[検証記録](gvn-verification.md)で確認する。', '',
         '状態: ' + '、'.join(f'{s} {n}行' for s, n in sorted(counts.items())) + '。', '']
for section in dict.fromkeys(r['section'] for r in rows):
    lines += ['## '+section, '', '| ID | 参照項目 | 状態 | GVNの動作と残差分 | 根拠 |', '| --- | --- | --- | --- | --- |']
    for row in rows:
        if row['section'] != section:
            continue
        sources = []
        for value in row['implementation']:
            filename, _, symbol = value.partition(':')
            sources.append(f'[{symbol or filename}](../{filename})')
        evidence = ', '.join('`'+e+'`' for e in row['evidence'])
        cells = [row['id'], row['setting'], row['state'], row['note'], ' '.join(sources)+((' / '+evidence) if evidence else '')]
        lines.append('| '+' | '.join(c.replace('|', ' / ') for c in cells)+' |')
    lines.append('')
text = '\n'.join(lines)
target = root/'docs/gvn-settings-matrix.md'
if args.check:
    assert target.read_text(encoding='utf-8') == text, 'Regenerate the settings matrix after reviewing the audit'
else:
    target.write_text(text, encoding='utf-8')
print(f'PASS settings traceability: {len(rows)} explicit reference rows' if args.check else f'Rendered {len(rows)} settings reference rows')
