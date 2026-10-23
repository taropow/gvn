"""Compare retained RGB16 codes and Pillow pixels, independently of the GVN decoder."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests/fixtures/auto-bmp'


def packed(path):
    data = path.read_bytes()
    assert data[:2] == b'BM' and struct.unpack_from('<I', data, 2)[0] == len(data)
    width, height = struct.unpack_from('<ii', data, 18)
    assert struct.unpack_from('<HH', data, 26) == (1, 16)
    compression = struct.unpack_from('<I', data, 30)[0]
    assert compression in (0, 3)
    masks = struct.unpack_from('<III', data, 54) if compression == 3 else (0x7c00, 0x3e0, 0x1f)
    assert masks in ((0x7c00, 0x3e0, 0x1f), (0xf800, 0x7e0, 0x1f))
    offset = struct.unpack_from('<I', data, 10)[0]
    stride = (width * 2 + 3) & ~3
    rows = [list(struct.unpack_from(f'<{width}H', data, offset + y * stride)) for y in range(abs(height))]
    return rows if height < 0 else rows[::-1], masks


def compare(path, source, expected_codes=None, masks=None):
    with Image.open(path) as decoded:
        actual = decoded.convert('RGB')
        assert actual.size == source.size and actual.tobytes() == source.convert('RGB').tobytes(), path
    if expected_codes is not None:
        codes, actual_masks = packed(path)
        assert codes == expected_codes and actual_masks == masks, path
    return dict(file=path.name, pixels=source.width * source.height, full_rgb_equal=True,
                component_codes_equal=expected_codes is not None,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--native', type=Path)
    args = parser.parse_args()
    directory = args.directory.resolve()
    records = []
    for name in ('rgb555.bmp', 'rgb555-fields.bmp', 'rgb565.bmp', 'rgb565-v4.bmp'):
        codes, masks = packed(FIXTURES / name)
        with Image.open(FIXTURES / name) as image:
            source = image.convert('RGB')
        for suffix in ('-top.bmp', '-bottom.bmp'):
            records.append(compare(directory / (name + suffix), source, codes, masks))
    codes, masks = packed(FIXTURES / 'rgb565.bmp')
    with Image.open(FIXTURES / 'rgb565.bmp') as image:
        source = image.convert('RGB')
    rotated_codes = [list(row) for row in zip(*codes[::-1])]
    records.append(compare(directory / 'rotated.bmp', source.transpose(Image.Transpose.ROTATE_270),
                           rotated_codes, masks))
    records.append(compare(directory / 'cropped.bmp', source.crop((1, 0, 62, 3)),
                           [row[1:62] for row in codes], masks))
    with Image.open(directory / 'changed-reference.png') as image:
        records.append(compare(directory / 'changed24.bmp', image))
    assert struct.unpack_from('<H', (directory / 'changed24.bmp').read_bytes(), 28)[0] == 24
    if args.native:
        native = args.native.resolve()
        result = json.loads((native / 'result.json').read_text(encoding='utf-8'))
        assert result['passed'] and all(row['completed'] and not row['error'] for row in result['steps'])
        for name in ('565-auto.bmp', '565-undo.bmp'):
            records.append(compare(native / 'pixels' / name, source, codes, masks))
        records.append(compare(native / 'pixels/565-rotate.bmp', source.transpose(Image.Transpose.ROTATE_270),
                               rotated_codes, masks))
        with Image.open(FIXTURES / 'rgb555.bmp') as image:
            codes555, masks555 = packed(FIXTURES / 'rgb555.bmp')
            records.append(compare(native / 'pixels/555-auto.bmp', image, codes555, masks555))
        with Image.open(native / 'pixels/gray-reference.png') as image:
            records.append(compare(native / 'pixels/gray24.bmp', image))
        assert struct.unpack_from('<H', (native / 'pixels/gray24.bmp').read_bytes(), 28)[0] == 24
    result = dict(passed=True, method='Independent Pillow full RGB and original16bit component code comparison',
                  files=len(records), compared_pixels=sum(row['pixels'] for row in records), cases=records)
    (directory / 'oracle.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in result.items() if key != 'cases'}))


if __name__ == '__main__':
    main()
