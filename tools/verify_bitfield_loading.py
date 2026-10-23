"""Compare native outputs to independently decoded source/reference files."""
from pathlib import Path
import argparse,hashlib,json,struct
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def rgba(path):
    with Image.open(path) as image:return image.convert('RGBA')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('scenario',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();directory=args.scenario.resolve();assert directory.is_relative_to(ROOT/'.cache');rows=[]
    expected=rgba(ROOT/'tests/fixtures/auto-bmp/rgb565.bmp');off=directory/'pixels/off-auto.bmp';on=directory/'pixels/on-auto.bmp'
    assert struct.unpack_from('<H',off.read_bytes(),28)[0]==16 and rgba(off).tobytes()==expected.tobytes();rows.append(dict(file=off.name,bpp=16,source_colors_exact=True))
    expected=rgba(ROOT/'tests/fixtures/bitfield-loading/on-rgb565.png');assert struct.unpack_from('<H',on.read_bytes(),28)[0]==24 and rgba(on).tobytes()==expected.tobytes();rows.append(dict(file=on.name,bpp=24,reference_colors_exact=True))
    expected=rgba(ROOT/'tests/fixtures/loading/alpha-v5.bmp')
    for name in ['alpha-on.png','alpha-off.png']:
        actual=rgba(directory/'pixels'/name);assert actual.tobytes()==expected.tobytes();rows.append(dict(file=name,rgba_exact=True,alpha_values=sorted(set(actual.getchannel('A').getdata()))))
    result=dict(passed=True,files=rows,tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),private_source_or_user_artwork_used=False);args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(result),flush=True)
if __name__=='__main__':main()
