"""Independently render actual Windows PDF output; no viewer renderer or colour helpers."""
from pathlib import Path
import argparse, hashlib, json, math
import pypdfium2 as pdfium

parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);args=parser.parse_args()
root=args.directory.resolve();native=json.loads((root/'native.json').read_text(encoding='utf-8'))
assert native['passed'] and native['private_station'] and native['default_printer_unchanged']
colors=[(240,30,20,255),(10,220,40,128),(40,60,240,0),(64,96,128,64),(255,192,24,254),(12,80,200,1)]
reference_density=json.loads((Path(__file__).resolve().parents[1]/'tests/fixtures/printing/reference-density.json').read_text())['model']['inverse_gamma_thousandths_by_density']
def expected_color(rgba,density):
    *rgb,a=rgba
    blended=[(c*a+255*(255-a)+127)//255 for c in rgb]
    return tuple(math.floor(255*(c/255)**(1000/reference_density[str(density)] )+.5) if density else c for c in blended)
report=dict(passed=False,method='Independent PDFium rendering and scalar alpha/gamma/physical-placement expectation',cases=[],pages=0,samples=0)
for case in native['cases']:
    path=(root/case['file']).resolve();assert path.parent==root and path.is_file() and path.stat().st_size>0
    paper_w,paper_h=native['paper'];rotated=case['quarter_turns']==1
    assert case['quarter_turns'] in [0,1]
    w,h=(60,90) if rotated else (60,40)
    x,y=(paper_w-10-w,paper_h-10-h) if rotated else ((paper_w-w)/2,(paper_h-h)/2)
    assert all(abs(a-b)<1e-7 for a,b in zip(case['box'],[x,y,w,h]))
    expected_indices=[[3,0],[4,1],[5,2]] if rotated else [[0,1,2],[3,4,5]]
    doc=pdfium.PdfDocument(path);assert len(doc)==case['copies']
    row=dict(file=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),page_count=len(doc),pages=[])
    for page_index in range(len(doc)):
        page=doc[page_index];pw,ph=page.get_size()
        assert abs(pw-paper_w*72/25.4)<1 and abs(ph-paper_h*72/25.4)<1
        bitmap=page.render(scale=2);image=bitmap.to_pil().convert('RGB')
        samples=[]
        for gy,indices in enumerate(expected_indices):
            for gx,index in enumerate(indices):
                mm_x=x+w*(gx+.5)/len(indices);mm_y=y+h*(gy+.5)/len(expected_indices)
                pixel=(round(mm_x*144/25.4),round(mm_y*144/25.4))
                actual=image.getpixel(pixel);expected=expected_color(colors[index],case['density'])
                difference=max(abs(a-b) for a,b in zip(actual,expected))
                assert difference<=3,(path.name,page_index,pixel,actual,expected,difference)
                samples.append(dict(pixel=pixel,actual=actual,expected=expected,max_channel_difference=difference))
        for mm_x,mm_y in [(5,5),(paper_w-5,5),(5,paper_h-5),(paper_w-5,paper_h-5)]:
            pixel=(round(mm_x*144/25.4),round(mm_y*144/25.4));actual=image.getpixel(pixel)
            assert min(actual)>=253,(path.name,'paper margin',pixel,actual)
        if page_index==0:image.save(root/(path.stem+'-render.png'))
        row['pages'].append(dict(index=page_index,samples=samples,white_margins_checked=4))
        report['pages']+=1;report['samples']+=len(samples)+4
        image.close();bitmap.close();page.close()
    doc.close();report['cases'].append(row)
assert report['pages']==3 and native['cases'][0]['copies']==2 and native['cases'][1]['copies']==1
cancel=native['cancel'];assert cancel['job_id']>0 and cancel['cancelled'] and cancel['aborted'] and not cancel['completed'] and cancel['pages_ended']<100
partial=root/'cancelled.pdf';report['cancelled_output_bytes']=partial.stat().st_size if partial.exists() else None
report['cancel_trace']=cancel;report['passed']=True
(root/'oracle.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f"PASS actual PDF oracle: {report['pages']} pages, {report['samples']} independent samples",flush=True)
