"""Compare actual C++ density LUTs through the reference PDF JPEG encoder settings."""
from pathlib import Path
import argparse,hashlib,io,json,subprocess,tempfile
from PIL import Image,JpegImagePlugin
root=Path(__file__).resolve().parents[1];fixtures=root/'tests/fixtures/printing'
parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args()
metadata=json.loads((fixtures/'reference-density.json').read_text());report=dict(passed=False,method='Actual C++ PrintColors LUT, independently projected using reference PDF JPEG quantization/subsampling; no product gamma formula in oracle.',cases=[])
try:
    cache=root/'.cache/print-density-oracle';cache.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='run-',dir=cache) as temporary:
        lut=Path(temporary)/'actual-lut.txt';p=subprocess.run([str(args.executable.resolve()),'--density-lut',str(lut)],check=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),timeout=30)
        tables={d:[None]*256 for d in range(-5,6)}
        for line in lut.read_text().splitlines():
            density,source,target=map(int,line.split());assert density in tables and 0<=source<256 and 0<=target<256 and tables[density][source] is None;tables[density][source]=target
        assert all(all(v is not None for v in row) for row in tables.values())
        for case in metadata['cases']:
            jpeg=fixtures/case['jpeg'];assert hashlib.sha256(jpeg.read_bytes()).hexdigest()==case['jpeg_sha256']
            with Image.open(jpeg) as reference:
                reference.load();assert reference.size==(512,512);quantization=reference.quantization;sampling=JpegImagePlugin.get_sampling(reference);actual=reference.convert('RGB')
            source=Image.new('RGB',(512,512));source.putdata([tuple(case['source_rgb'][(y//32)*16+x//32]) for y in range(512) for x in range(512)])
            transformed=source.point(tables[case['density']]*3);encoded=io.BytesIO();transformed.save(encoded,format='JPEG',qtables=quantization,subsampling=sampling);encoded.seek(0)
            with Image.open(encoded) as expected:
                expected.load();rgb=expected.convert('RGB');maximum=0
                for n in range(256):
                    pixel=(int((n%16+.5)*32),int((n//16+.5)*32));maximum=max(maximum,max(abs(a-b) for a,b in zip(actual.getpixel(pixel),rgb.getpixel(pixel))))
            row=dict(grid=case['grid'],density=case['density'],max_difference=maximum);report['cases'].append(row);assert maximum<=1,row
            actual.close();source.close();transformed.close();rgb.close()
        assert len(report['cases'])==12 and {r['density'] for r in report['cases']}==set(range(-5,6));report.update(passed=True,pixels=3072,max_difference=max(r['max_difference'] for r in report['cases']))
finally:
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='cases'}))
