"""Freeze or compare full-RGBA frame hashes using an owned decoder probe."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from probe_gvn_performance import station

ROOT = Path(__file__).resolve().parents[1]
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()


def apng(path):
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return False
    at = 8
    while at+12 <= len(data):
        size = int.from_bytes(data[at:at+4], 'big')
        if size > len(data)-at-12:
            return False
        kind = data[at+4:at+8]
        if kind == b'acTL':
            return True
        if kind in {b'IDAT', b'IEND'}:
            return False
        at += size+12
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--performance-assets', type=Path)
    args = parser.parse_args()
    probe, output = args.probe.resolve(), args.output.resolve()
    assert probe.is_file() and not output.exists()
    output.mkdir(parents=True)
    baseline = json.loads(args.baseline.read_text(encoding='utf-8')) if args.baseline else None
    if baseline:
        cases = baseline['cases']
    else:
        files = {}
        folders = [ROOT/'tests/fixtures', ROOT/'docs/verification/gif-reference-0.45']
        if args.performance_assets:
            folders.append(args.performance_assets.resolve())
        for folder in folders:
            for path in folder.rglob('*'):
                if path.is_file() and (path.suffix.lower() in {'.gif', '.apng'} or
                                       path.suffix.lower() == '.png' and apng(path)):
                    files.setdefault(sha(path), path.resolve())
        cases = []
        for digest, path in sorted(files.items()):
            for style in range(5) if path.suffix.lower() == '.gif' else [0]:
                cases.append(dict(input=str(path), input_sha256=digest, style=style))
    assert cases
    frozen = {probe:sha(probe), Path(__file__):sha(__file__)}
    for case in cases:
        path = Path(case['input']); assert sha(path) == case['input_sha256']
        frozen[path] = case['input_sha256']
    info = subprocess.STARTUPINFO(); info.lpDesktop = station()
    rows = []
    for index, case in enumerate(cases):
        report = output/(str(index)+'.json')
        child = subprocess.run([str(probe), case['input'], str(report), str(case['style'])],
                               startupinfo=info, creationflags=subprocess.CREATE_NO_WINDOW,
                               capture_output=True, timeout=60)
        data = json.loads(report.read_text(encoding='utf-8'))
        assert child.returncode == (0 if data['passed'] else 1)
        if baseline:
            assert data == case['result'], (index, case['input'], case['style'], 'Decoder output changed')
        rows.append(dict(input=case['input'], input_sha256=case['input_sha256'], style=case['style'], result=data))
        if index % 20 == 0 or index+1 == len(cases):
            print('PASS', index+1, '/', len(cases), flush=True)
    assert all(sha(path) == digest for path, digest in frozen.items())
    summary = dict(passed=True, probe_sha256=sha(probe), tool_sha256=sha(__file__),
                   method='One owned noninteractive process per input/style; exact SHA256 over every full RGBA frame including transparentRGB, durations/loops/compensation/imagebytes. No pixel tolerance; frozen old-decoder comparison complements independent GIF/PNG reference tests.',
                   baseline_sha256=sha(args.baseline) if args.baseline else None, cases=rows)
    (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print('Complete', len(rows), 'cases;', sum(len(row['result'].get('frame_data', [])) for row in rows), 'frames', flush=True)


if __name__ == '__main__':
    main()
