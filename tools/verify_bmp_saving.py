"""Independent Pillow verification of native BMP saves; no desktop or clipboard access."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image


def verify(path, source, bits, top):
    data = path.read_bytes()
    assert data[:2] == b'BM' and struct.unpack_from('<I', data, 2)[0] == len(data)
    assert struct.unpack_from('<HHI', data, 26) == (1, bits, 0)
    assert struct.unpack_from('<ii', data, 18) == (source.width, -source.height if top else source.height)
    matte = Image.new('RGBA', source.size, (18, 52, 86, 255))
    expected = Image.alpha_composite(matte, source.convert('RGBA')).convert('RGB')
    if bits == 16:
        expected = expected.point([((v // 8) * 255) // 31 for v in range(256)] * 3)
    with Image.open(path) as image:
        actual = image.convert('RGB')
        assert actual.size == source.size and actual.tobytes() == expected.tobytes(), path
        dpi = image.info.get('dpi')
    return dict(file=path.name, bits=bits, top_down=top, pixels=source.width * source.height,
                decoded_rgb_equal=True, dpi=dpi, sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--native', type=Path, help='85-bmp16-save scenario directory')
    args = parser.parse_args()
    directory = args.directory.resolve()
    raw = (directory / 'source.rgba').read_bytes()
    expected_source = bytes(c for i in range(15) for c in
                            (i * 29 % 256, (i * 71 + 9) % 256, (i * 13 + 3) % 256,
                             (0, 1, 63, 127, 128, 254, 255)[i % 7]))
    assert raw == expected_source
    source = Image.frombytes('RGBA', (5, 3), raw)
    records = []
    for bits in (16, 24):
        for top in (False, True):
            for rle in (False, True):
                record = verify(directory / f'{bits}-{int(top)}-{int(rle)}.bmp', source, bits, top)
                assert all(abs(actual - expected) < .013 for actual, expected in
                           zip(record['dpi'], (144.5, 288.25)))
                record['rle_option'] = rle
                records.append(record)
    if args.native:
        native = args.native.resolve()
        report = json.loads((native / 'result.json').read_text(encoding='utf-8'))
        assert report['passed'] and all(step['completed'] and not step['error'] for step in report['steps'])
        with Image.open(native / 'source/alpha.png') as image:
            source = image.convert('RGBA')
        assert not (native / 'pixels/cancelled.bmp').exists()
        for name, bits, top in [('top16.bmp', 16, True), ('bottom16.bmp', 16, False),
                                ('top24.bmp', 24, True)]:
            records.append(verify(native / 'pixels' / name, source, bits, top))
    result = dict(passed=True, method='Independent Pillow decode and alpha composite; all RGB bytes',
                  files=len(records), compared_pixels=sum(row['pixels'] for row in records), cases=records)
    (directory / 'oracle.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n',
                                          encoding='utf-8')
    print(json.dumps(dict(passed=True, files=result['files'], compared_pixels=result['compared_pixels'])))


if __name__ == '__main__':
    main()
