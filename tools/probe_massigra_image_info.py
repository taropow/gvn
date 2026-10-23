"""Observe Image Info of probe-owned MassiGra0.45 children on a private desktop."""
import ctypes as c
from ctypes import wintypes as w
import json,shutil,sys,uuid,zipfile
from pathlib import Path
from PIL import Image
import benchmark_navigation as native
root=Path(__file__).resolve().parents[1];read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
cp=root/'.cache/goal-continuation-0.39.0.json';state=read(cp);assert not state['source_freeze'] and not state['live_other_sessions']
protected=read(root/'.cache/current-distribution-protection-0.39.json');assert all(native.sha(root/n)==v for n,v in protected.items())
output=root/'.cache/image-info-reference'/('run-'+uuid.uuid4().hex);output.mkdir(parents=True);reference=output/'reference'
archive=root/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
with zipfile.ZipFile(archive) as package:
    for name in package.namelist():
        if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
            target=(reference/name).resolve();assert target.is_relative_to(reference.resolve());target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
assert native.sha(reference/'MassiGra.exe')=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
assets=output/'assets';assets.mkdir()
cases=[('rgb','RGB',[(1,2,3),(1,2,3),(20,40,60),(70,80,90)]),('rgba-same','RGBA',[(20,40,60,a) for a in [0,64,128,255]]),('rgba-hidden','RGBA',[(1,2,3,0),(20,40,60,0),(1,2,3,255),(20,40,60,255)]),('rgba-low','RGBA',[(10,20,30,1),(11,21,31,1),(200,201,202,1),(10,20,30,128)])]
for name,mode,values in cases:
    image=Image.new(mode,(4,1));image.putdata(values);image.save(assets/(name+'.png'))
for name,indices in [('palette-duplicate',[0,1,0,1]),('palette-unused',[0,0,0,0])]:
    image=Image.new('P',(4,1));image.putpalette([20,40,60,20,40,60]+[0]*762);image.putdata(indices);image.save(assets/(name+'.png'),transparency=bytes([0,255]))
shutil.copyfile(root/'tests/fixtures/disposal.gif',assets/'animation.gif')
U,K=native.U,native.K;native.api(U,'IsWindowEnabled',w.BOOL,[native.P]);native.api(U,'IsWindow',w.BOOL,[native.P])
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in [0,0x80]
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station);flags=(w.DWORD*3)();needed=w.DWORD();native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
name=c.create_unicode_buffer(512);native.check(U.GetUserObjectInformationW(station,2,name,c.sizeof(name),c.byref(needed)));native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-ImageInfo-'+uuid.uuid4().hex;native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
sources={str(p.relative_to(root)):native.sha(p) for p in [Path(__file__),Path(native.__file__)]};report=dict(passed=False,noninteractive=True,user_clipboard_or_windows_used=False,sources=sources,cases=[],reference_sha256=native.sha(reference/'MassiGra.exe'))
state.update(source_freeze=True,live_other_sessions=['image-info-reference']);write(cp,state)
try:
    for source in sorted(assets.iterdir()):
        if len(sys.argv)>1 and source.stem not in sys.argv[1:]:continue
        child=native.Child(reference/'MassiGra.exe',[source],name.value+'\\'+desktop_name)
        try:
            def text(h):
                buffer=c.create_unicode_buffer(32768);child.send(child.own(h),0xd,len(buffer),c.addressof(buffer));return buffer.value
            main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:source.name in native.window_text(main));child.post(main,0x111,40041)
            dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '画像の情報' in native.window_text(h)),None));child.ping(dialog)
            auto=child.own(U.GetDlgItem(dialog,2001));colors=child.own(U.GetDlgItem(dialog,2039))
            row=dict(file=source.name,sha256=native.sha(source),pid=child.info.pid,auto_enabled=bool(U.IsWindowEnabled(auto)),initial_checked=bool(child.send(auto,0xf0)),initial_colors=text(colors),fields={str(key):text(U.GetDlgItem(dialog,key)) for key in range(2031,2045)})
            if row['auto_enabled']:
                if not row['initial_checked']:child.send(auto,0xf5)
                row['calculated_colors']=child.wait(lambda:text(colors) if '色' in text(colors) else None)
                child.send(auto,0xf5);row['unchecked_colors']=text(colors)
            report['cases'].append(row);child.post(dialog,0x111,1);child.wait(lambda:not U.IsWindow(dialog))
        finally:child.close()
    report['passed']=True
finally:
    report['source_guard']=all(native.sha(root/n)==v for n,v in sources.items());report['distribution_and_profile_guard']=all(native.sha(root/n)==v for n,v in protected.items());write(output/'result.json',report)
    state.update(source_freeze=False,live_other_sessions=[]);write(cp,state);K.ReleaseMutex(mutex);K.CloseHandle(mutex);print(output,flush=True)
    print(json.dumps([dict(file=r['file'],enabled=r['auto_enabled'],checked=r['initial_checked'],colors=r.get('calculated_colors',r['initial_colors']),unchecked=r.get('unchecked_colors')) for r in report['cases']],ensure_ascii=False),flush=True)
assert report['passed'] and report['source_guard'] and report['distribution_and_profile_guard']
