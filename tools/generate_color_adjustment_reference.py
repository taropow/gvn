"""Retain generated-source official RGB observations; never bundle the reference executable."""
from pathlib import Path
import hashlib,json,shutil
root=Path(__file__).resolve().parents[1];read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
output=root/'tests/fixtures/color-adjustment';output.mkdir(exist_ok=True)
rows=[];source_digest=None
for stage in [2,3]:
    directory=root/f'.cache/color-adjustment-related-0.51-stage{stage}/reference';report=read(directory/'result.json');assert report['passed']
    if source_digest:assert sha(directory/'source.png')==source_digest
    else:source_digest=sha(directory/'source.png');shutil.copyfile(directory/'source.png',output/'source.png')
    for row in report['samples']:
        fields={r['id']:r for r in row['configured_controls']};path=directory/(row['label']+'.png');shutil.copyfile(path,output/path.name)
        rows.append(dict(name=row['label'],hue=int(fields[2031]['label']),saturation=int(fields[2032]['label']),lightness=int(fields[2033]['label']),unified=bool(fields[2001]['checked']),gray=fields[2021]['selected'],sha256=sha(path),reference_report=f'docs/verification/0.51.0-color-adjustment-foundation/stage{stage}/reference/result.json'))
assert len(rows)==16 and len({r['name'] for r in rows})==16
manifest=dict(reference='Official MassiGra0.45 owned private processes, source generated independently. RGB samples only, no user image/clipboard.',reference_sha256=report['reference_sha256'],source_sha256=source_digest,cases=rows)
(output/'cases.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
header=['// Generated from actual reference control values and RGB outputs.','#pragma once','#include "gvn/color_adjustment.h"','namespace gvn_test {','struct ColorReference {const wchar_t *file;gvn::ColorAdjustment choice;};','inline const ColorReference color_references[]{']
methods=['Middle','Average','Luma']
for row in rows:header.append('    {L"'+row['name']+'.png",{'+','.join([str(row['hue']),str(row['saturation']),str(row['lightness']),str(row['unified']).lower(),'gvn::GrayMethod::'+methods[row['gray']]])+'}},')
header.extend(['};','} // namespace gvn_test','']);(root/'tests/color_adjustment_reference.h').write_text('\n'.join(header),encoding='utf-8')
print('Retained',len(rows),'actual generated-image observations')
