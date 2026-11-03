"""Validate GVN's WebP output through Pillow, independent of GVN's decoder."""
import subprocess,sys,tempfile
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='GVN-WebP-oracle-') as temporary:
    folder=Path(temporary)
    subprocess.run([sys.argv[1],str(folder)],check=True)
    expected=(root/'tests/fixtures/webp/alpha.webp.0.rgba').read_bytes()
    with Image.open(folder/'lossless.webp') as image:
        assert image.format=='WEBP' and image.size==(3,2)
        assert image.convert('RGBA').tobytes()==expected
    with Image.open(folder/'opaque.webp') as image:
        actual=list(image.convert('RGBA').getdata())
        original=Image.frombytes('RGBA',(3,2),expected)
        reference=Image.alpha_composite(Image.new('RGBA',(3,2),(0x12,0x34,0x56,255)),original)
        assert all(abs(a-b)<=1 for p,q in zip(actual,reference.getdata()) for a,b in zip(p,q))
    with Image.open(folder/'dpi.webp') as image:
        exif=image.getexif()
        assert abs(float(exif[282])-254)<.0001 and abs(float(exif[283])-127)<.0001
        assert exif[296]==2 and 274 not in exif
        assert image.convert('RGBA').tobytes()==expected
print('PASS independent WebP output: lossless RGBA, opaque matte and EXIF DPI')
