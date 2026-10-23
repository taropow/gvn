"""Create fixed synthetic performance inputs, each in a one-image folder."""
import argparse
import gc
import hashlib
import json
import random
import struct
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, __version__ as pillow_version


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    assert not output.exists(), 'Use a fresh asset directory; retained inputs are immutable'
    output.mkdir(parents=True)
    assets = []

    def path_for(name, extension):
        folder = output/name
        folder.mkdir()
        return folder/('image.'+extension)

    def record(name, path, width, height, frames, instances):
        with Image.open(path) if path.suffix != '.psd' else open(path, 'rb') as image:
            if path.suffix != '.psd':
                assert image.size == (width, height) and getattr(image, 'n_frames', 1) == frames
        assets.append(dict(name=name, path=path.relative_to(output).as_posix(),
                           sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                           file_bytes=path.stat().st_size, width=width, height=height,
                           frames=frames, rgba_bytes=width*height*frames*4, instances=instances))
        print('ASSET', name, path.stat().st_size, flush=True)

    width, height = 3840, 2160
    noise = Image.frombytes('L', (width, height), random.Random(0x47564e).randbytes(width*height))
    image = Image.merge('RGB', (noise, ImageChops.offset(noise, 137, 89), ImageChops.offset(noise, 37, 197)))
    path = path_for('jpeg-4k', 'jpg')
    image.save(path, quality=93, subsampling=0)
    record('jpeg-4k', path, width, height, 1, [1, 4, 8])
    image.close(); noise.close()
    for name, size, instances in [('alpha-4k', (3840, 2160), [1, 4, 8]),
                                   ('alpha-8k', (7680, 4320), [1, 4])]:
        image = Image.new('RGBA', size)
        draw = ImageDraw.Draw(image)
        alpha = [0, 1, 127, 128, 254, 255]
        for y in range(0, size[1], 64):
            for x in range(0, size[0], 64):
                a = alpha[(x//64+y//64) % len(alpha)]
                draw.rectangle((x, y, x+63, y+63),
                               fill=((x//64*37) % 256, (y//64*53) % 256, (x//64+y//64*7) % 256, a))
        path = path_for(name, 'png')
        image.save(path, compress_level=1)
        record(name, path, *size, 1, instances)
        image.close(); del draw
    path = path_for('psd16-4k', 'psd')
    with path.open('wb') as stream:
        stream.write(b'8BPS'+struct.pack('>H6sHIIHH', 1, b'\0'*6, 4, height, width, 16, 3))
        stream.write(struct.pack('>IIIH', 0, 0, 0, 0))
        for channel in [128, 64, 192, None]:
            row = b''.join(struct.pack('>H', (([0, 1, 127, 128, 254, 255][x//64 % 6]
                                             if channel is None else channel)*257)) for x in range(width))
            for _ in range(height):
                stream.write(row)
    record('psd16-4k', path, width, height, 1, [1, 4])
    count, width, height = 48, 1024, 1024
    for kind in ['gif', 'apng']:
        frames = []
        for index in range(count):
            if kind == 'gif':
                image = Image.new('P', (width, height), index+1)
                image.putpalette([value for color in range(256)
                                  for value in [(color*53) % 256, (color*97) % 256, (color*193) % 256]])
            else:
                image = Image.new('RGBA', (width, height), ((index*53) % 256, (index*97) % 256, (index*193) % 256, 128))
            draw = ImageDraw.Draw(image)
            x, y = index*37 % 800, index*61 % 800
            draw.rectangle((x, y, x+191, y+191), fill=0 if kind == 'gif' else (31, 63, 127, 0))
            frames.append(image)
        path = path_for(kind+'-48', kind)
        if kind == 'gif':
            frames[0].save(path, save_all=True, append_images=frames[1:], duration=100,
                           loop=0, transparency=0, disposal=2, optimize=False)
        else:
            frames[0].save(path, format='PNG', save_all=True, append_images=frames[1:],
                           duration=100, loop=0, disposal=0, blend=0, compress_level=1)
        record(kind+'-48', path, width, height, count, [1, 2])
        for frame in frames:
            frame.close()
        del frames, draw
        gc.collect()
    manifest = dict(version=1, generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    pillow=pillow_version, description='Fixed synthetic inputs, not representative real photos; one image per folder prevents neighbor prefetch contamination.', assets=assets)
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print('READY', output/'manifest.json', flush=True)


if __name__ == '__main__':
    main()
