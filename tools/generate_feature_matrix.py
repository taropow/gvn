"""Render the explicitly reviewed inventory; never infer implementation from a menu category."""
import argparse,html,json,re
from collections import Counter
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--check',action='store_true')
parser.add_argument('--report',type=Path,default=ROOT/'.cache/app-audit.json')
args=parser.parse_args()
rows=json.loads((ROOT/'docs/gvn-feature-audit.json').read_text(encoding='utf-8'))
report=json.loads(args.report.read_text(encoding='utf-8'))
assert report['failures']==0,'Application contract tests have failures'
expected_cases=set(re.findall(r'^(?:APP_TEST|LEGACY_WIDE_TEST)\((\w+)\)',(ROOT/'tests/app_tests.cpp').read_text(encoding='utf-8'),re.M))
assert len(report['cases'])==len(expected_cases) and {c['name'] for c in report['cases']}==expected_cases and all(c['passed'] for c in report['cases'])
cases={c['name']:set(c['commands']) for c in report['cases']}
value=0;commands={}
text=(ROOT/'include/gvn/commands.h').read_text(encoding='utf-8')
for item in text.split('enum Command {',1)[1].split('};',1)[0].split(','):
    match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
    if not match:continue
    name,number=match.groups()
    if number:value=int(number)
    commands[name]=value;value+=1
source_rows=[];section=''
for line in (ROOT/'docs/massigra-menu.md').read_text(encoding='utf-8').splitlines():
    if line.startswith('## '):section=line[3:]
    match=re.match(r'\s*- \[([^\]]+)\]\((http[^)]+)\)',line)
    if match:source_rows.append((section,html.unescape(match[1]),match[2]))
assert len(rows)==len(source_rows)==174
for i,(row,reference) in enumerate(zip(rows,source_rows),1):
    assert row['id']==i
    assert (row['section'],row['name'],row['reference'])==reference,(i,'Reference changed')
    assert row['status'] and row['note'] is not None
    required={commands[c] for c in row['commands']}
    if row['test_cases']:
        exercised=set()
        for case in row['test_cases']:exercised |= cases[case]
        assert required <= exercised,(i,row['name'],'Mapped commands were not exercised',required-exercised)
    if row['status']=='実装済み':
        assert row['test_cases'],(i,'Claim lacks an automatic contract test')
    if row['source']:
        assert (ROOT/row['source'].split(':',1)[0]).is_file(),(i,'Missing implementation source')
    for scenario in row.get('autopilot_cases',[]):
        assert (ROOT/'tests/autopilot'/scenario).is_file(),(i,'Missing native UI scenario')
        if args.check:
            run=json.loads((ROOT/'.cache/autopilot/latest.json').read_text(encoding='utf-8'))
            native={s['name']:s for s in run['scenarios']}
            assert run['passed'] and native[scenario]['passed'],(i,'Native UI scenario did not pass')

counts=Counter(row['status'] for row in rows)
version=re.search(r'project\(GVN VERSION ([\d.]+) ',(ROOT/'CMakeLists.txt').read_text()).group(1)
as_of=report.get('as_of','2026年10月20日')
lines=['# MassiGra機能とGVNの対応表','',
f'{as_of}、GVN {version}の機能照合。174行はサブメニュー見出しを含む。各行を個別に確認した[監査台帳](gvn-feature-audit.json)から生成し、カテゴリ単位で実装済みを推定しない。設定の詳細は[設定対応表](gvn-settings-matrix.md)で別に管理する。','',
'「実装済み」は記載したGVNの操作範囲に対応する。確認欄の「統合試験済み」は実App・HWND・読み込みワーカー・ファイル・隔離OSクリップボードを使い、OSダイアログや外部起動などの境界を試験用に差し替えるケースを含む。実コントロール・通常プロセス・実受信側の証拠は各行と検証記録で別に示す。外部ソフトとのE2EやMassiGraとの画素完全一致を意味しない。一部対応/代替実装の確認は、差分欄に示した部分に限る。','',
f'[監査結果と修正内容](gvn-implementation-audit.md)、[詳細な検証記録](gvn-verification.md)、[機械可読の実行結果](verification/{version}-contract.json)を参照。','',
'状態の内訳: '+ '、'.join(f'{name} {count}行' for name,count in sorted(counts.items()))+'。','']
for group in dict.fromkeys(row['section'] for row in rows):
    lines += ['## '+group,'','| No. | 参照機能 | 実装状態 | 確認 | GVNの操作・差分 / 実装箇所 |','| --- | --- | --- | --- | --- |']
    for row in rows:
        if row['section']!=group:continue
        confirmation='統合試験済み' if row['test_cases'] else row['verification_note'] or ('対象外' if row['status']=='対象外' else '未確認' if row['commands'] else '未実装部分あり' if row['status']!='未実装' else '未実装')
        if row.get('autopilot_cases'):
            confirmation=('統合＋専用UI試験済み' if row['test_cases'] else '専用UI試験済み')
        source=''
        if row['source']:
            path,_,anchor=row['source'].partition(':')
            source=f' [処理](../{path}) {anchor.strip()}'
        note=(row['where']+'。' if row['where'] else '')+row['note']+source
        lines.append(f"| {row['id']:03} | [{row['name']}]({row['reference']}) | {row['status']} | {confirmation} | {note.replace('|',' / ')} |")
    lines.append('')
outputs={ROOT/'docs/gvn-feature-matrix.md':'\n'.join(lines), ROOT/'docs/gvn-feature-matrix.json':json.dumps(rows,ensure_ascii=False,indent=2)+'\n'}
for path,content in outputs.items():
    if args.check:
        assert path.read_text(encoding='utf-8')==content, f'{path.name} differs from the reviewed inventory; regenerate it'
    else:path.write_text(content,encoding='utf-8')
print(f'PASS specification traceability: {len(rows)} explicit reference rows, {len(cases)} passing application cases' if args.check else f'{len(rows)} reviewed rows: {dict(counts)}')
