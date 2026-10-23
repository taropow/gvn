"""Observe the official font chooser on an owned noninteractive desktop."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c
import json, shutil, sys, time, uuid, zipfile
import benchmark_navigation as native
import probe_massigra_caption_paths as api_helpers
from probe_gvn_performance import station

U,K=native.U,native.K
native.api(U,'SetWindowTextW',w.BOOL,[native.P,w.LPCWSTR])
root=Path(__file__).resolve().parents[1]
out=(Path(sys.argv[1]) if len(sys.argv)>1 else root/'.cache/font-reference')/('run-'+uuid.uuid4().hex)
out.mkdir(parents=True)
archive=root/'.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference=out/'reference'
with zipfile.ZipFile(archive) as z:
    for name in z.namelist():
        if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
            target=(reference/name).resolve();assert target.is_relative_to(reference.resolve())
            target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(name))
exe=reference/'MassiGra.exe'
assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
shutil.copyfile(__file__,out/'probe.py')
child=None
report=dict(passed=False,private_station=True,user_windows_or_clipboard_used=False,
            reference_sha256=native.sha(exe),cases=[])
try:
    child=native.Child(exe,[],station())
    main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
    child.post(main,0x111,40101)
    prefs=child.wait(lambda:next((h for h in child.windows() if h!=main and '設定' in native.window_text(h)),None))
    def descendants(parent):
        result=[]
        @native.Callback
        def visit(h,_):result.append(child.own(h));return True
        U.EnumChildWindows(child.own(parent),visit,0);return result
    tab=next(h for h in descendants(prefs) if native.window_text(h,True)=='SysTabControl32')
    button=None
    for page in range(child.send(tab,0x1304)):
        remote=K.VirtualAllocEx(child.info.process,None,16,0x3000,4);native.check(remote)
        try:
            native.check(child.send(tab,0x130a,page,remote));rect=(c.c_int*4)()
            native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None))
            point=((rect[0]+rect[2])//2)|(((rect[1]+rect[3])//2)<<16)
            child.post(tab,0x201,1,point);child.post(tab,0x202,0,point)
        finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
        child.wait(lambda:child.send(tab,0x130b)==page);child.ping(prefs)
        button=next((h for h in descendants(prefs) if 'フォント選択' in native.window_text(h)),None)
        if button:
            report['fullscreen_tab_index']=page;break
    assert button,'Native font button not found'
    def control_text(h):
        buf=c.create_unicode_buffer(256);child.send(h,0xd,len(buf),c.addressof(buf));return buf.value
    for points in [1,1639,32767,32768]:
        child.post(button,0xf5)
        font=child.wait(lambda:next((h for h in child.windows() if U.GetDlgItem(h,0x470) and U.GetDlgItem(h,0x472)),None))
        family=U.GetDlgItem(font,0x470);size=U.GetDlgItem(font,0x472)
        if report['cases'] and report['cases'][-1]['accepted']:
            report['cases'][-1]['retained_points']=control_text(size)
        count=child.send(family,0x146);names=[]
        for i in range(count):
            buf=c.create_unicode_buffer(256);child.send(family,0x148,i,c.addressof(buf));names.append(buf.value)
        if 'families' not in report:
            report['families']=dict(count=count,raster=[n for n in names if n in ['Fixedsys','Terminal','System']],vertical=[n for n in names if n.startswith('@')][:12])
        # Arial is scalable: failures here reflect chooser bounds rather than bitmap strikes.
        index=child.send(family,0x158,-1,c.addressof(c.create_unicode_buffer('Arial')))
        assert index!=-1
        child.send(family,0x14e,index);child.send(font,0x111,0x470|(1<<16),family)
        style=U.GetDlgItem(font,0x471)
        child.send(style,0x14e,0);child.send(font,0x111,0x471|(1<<16),style)
        time.sleep(.15)  # Allow the native chooser to rebuild sizes after the face/style change.
        text=c.create_unicode_buffer(str(points))
        native.check(child.send(size,0xc,0,c.addressof(text)))
        report['input']={'family':control_text(family),'size':control_text(size),'style':control_text(style)}
        assert control_text(size)==str(points),report['input']
        child.post(font,0x111,1)
        error=child.wait(lambda:True if not U.IsWindow(font) else next((h for h in child.windows() if h not in [main,prefs,font] and native.window_text(h,True)=='#32770'),None))
        row=dict(points=points,accepted=not bool(U.IsWindow(font)))
        if not row['accepted']:
            row['error']=[native.window_text(h) for h in descendants(error) if native.window_text(h)]
            child.post(error,0x111,1);child.wait(lambda:not U.IsWindow(error))
            child.post(font,0x111,2);child.wait(lambda:not U.IsWindow(font))
        report['cases'].append(row)
    if report['cases'][-1]['accepted']:
        child.post(button,0xf5)
        font=child.wait(lambda:next((h for h in child.windows() if U.GetDlgItem(h,0x470) and U.GetDlgItem(h,0x472)),None))
        report['cases'][-1]['retained_points']=control_text(U.GetDlgItem(font,0x472))
        child.post(font,0x111,2);child.wait(lambda:not U.IsWindow(font))
    child.post(prefs,0x111,2);child.wait(lambda:not U.IsWindow(prefs))
    child.post(main,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0
    report['passed']=True
except Exception as error:
    report['error']=repr(error);raise
finally:
    if child:child.close()
    (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)
    print('Evidence: '+str(out),flush=True)
