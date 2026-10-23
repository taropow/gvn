"""Inspect actual official color-depth controls and copied results on a private desktop."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,hashlib,io,json,shutil,struct,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station

ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),
    ('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),
    ('GetDlgCtrlID',c.c_int,[P]),('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--cases',nargs='+');args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference=out/'reference'
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                p=(reference/name).resolve();assert p.is_relative_to(reference.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(name))
    exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    image=Image.new('RGB',(33,17));image.putdata([((x*19+y*7)%256,(x*5+y*11)%256,(x*3+y*31)%256) for y in range(17) for x in range(33)]);image.save(out/'gradient.png')
    image=Image.new('RGB',(17,9));image.putdata([(11,37,83) if (x+y)%2 else (211,233,251) for y in range(9) for x in range(17)]);image.save(out/'two-color.png')
    shutil.copyfile(ROOT/'tests/fixtures/palette/png-4-opaque.png',out/'indexed.png')
    shutil.copyfile(ROOT/'tests/fixtures/palette/png-4-alpha.png',out/'alpha.png')
    report=dict(passed=False,tool_sha256=native.sha(__file__),reference_sha256=native.sha(exe),private_station=True,user_desktop_clipboard_or_artwork_used=False,cases=[],parents=[])
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();child=None;window=None;command=None
    def menus(menu):
        items=[]
        for i in range(U.GetMenuItemCount(menu)):
            label=c.create_unicode_buffer(512);U.GetMenuStringW(menu,i,label,len(label),0x400);items.append((U.GetMenuItemID(menu,i),label.value.replace('&','')))
            sub=U.GetSubMenu(menu,i)
            if sub:items+=menus(sub)
        return items
    def controls(dialog):
        result=[]
        @c.WINFUNCTYPE(w.BOOL,P,native.SP)
        def visit(h,_):
            child.own(h);result.append(dict(id=U.GetDlgCtrlID(h),label=native.window_text(h),cls=native.window_text(h,True),enabled=bool(U.IsWindowEnabled(h)),checked=child.send(h,0xf0) if native.window_text(h,True)=='Button' else None));return True
        child.own(dialog);U.EnumChildWindows(dialog,visit,0);return result
    def copy(label):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,40025)
        child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8));child.wait(lambda:U.OpenClipboard(None))
        try:
            handle=U.GetClipboardData(8);ptr=K.GlobalLock(handle);native.check(ptr)
            try:raw=c.string_at(ptr,K.GlobalSize(handle))
            finally:K.GlobalUnlock(handle)
        finally:U.CloseClipboard()
        header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw);assert planes==1 and compression in [0,3]
        count=struct.unpack_from('<I',raw,32)[0] or (1<<bits if bits<=8 else 0);offset=header+count*4+(12 if header==40 and compression==3 else 0)
        bmp=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+offset)+raw;p=out/(label+'.bmp');p.write_bytes(bmp)
        with Image.open(io.BytesIO(bmp)) as pixels:decoded=pixels.convert('RGB');decoded.save(out/(label+'.png'))
        return dict(file=p.name,bpp=bits,compression=compression,sha256=native.sha(p),rgb_sha256=hashlib.sha256(decoded.tobytes()).hexdigest(),colors=len(set(decoded.getdata())))
    try:
        cases=[('cancel','gradient.png','8 BPP',False,False),('rgb555','gradient.png','555',False,True),('rgb565','gradient.png','565',False,True),
            ('indexed8','gradient.png','8 BPP',False,True),('indexed8-dither','gradient.png','8 BPP',True,True),
            ('promote24','indexed.png','24 BPP',False,True),('optimal-two','two-color.png','最適な',False,True),('alpha24','alpha.png','24 BPP',False,True),
            ('rgb555-dither','gradient.png','555',True,True),('rgb565-dither','gradient.png','565',True,True)]
        if args.cases:
            assert len(set(args.cases))==len(args.cases) and set(args.cases)<={row[0] for row in cases}
            cases=[row for row in cases if row[0] in args.cases]
        for label,source,choice,dither,accept in cases:
            child=native.Child(exe,[out/source],desktop);parent=dict(pid=child.info.pid);report['parents'].append(parent)
            window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
            child.wait(lambda:source in native.window_text(window));child.ping(window)
            if not U.GetMenu(window):child.send(window,0x111,41008)
            matches=[n for n,text in menus(U.GetMenu(window)) if '減色/増色' in text];assert len(matches)==1;command=matches[0]
            before=copy(label+'-before');child.post(window,0x111,command)
            dialog=child.wait(lambda:next((h for h in child.windows() if '減色/増色' in native.window_text(h)),None))
            child.wait(lambda:len(controls(dialog))>=8);child.ping(dialog);initial=controls(dialog);wheel=[]
            if label=='cancel':
                for delta in [120,-120]:
                    child.send(dialog,0x20a,(delta&0xffff)<<16,0);child.ping(dialog)
                    wheel.append(dict(delta=delta,checked=[row['id'] for row in controls(dialog) if row['checked']==1]))
            selected=[r for r in initial if choice in r['label'].replace('&','')];assert len(selected)==1,(choice,initial)
            target=selected[0];enabled=target['enabled']
            if enabled:child.send(child.own(U.GetDlgItem(dialog,target['id'])),0xf5)
            diff=next(r for r in controls(dialog) if '誤差拡散' in r['label'])
            if diff['enabled'] and bool(diff['checked'])!=dither:child.send(child.own(U.GetDlgItem(dialog,diff['id'])),0xf5)
            configured=controls(dialog);child.post(child.own(U.GetDlgItem(dialog,1 if accept and enabled else 2)),0xf5)
            child.wait(lambda:not U.IsWindow(dialog));child.ping(window);after=copy(label+'-after')
            row=dict(label=label,source=source,source_sha256=native.sha(out/source),command=command,initial_controls=initial,configured_controls=configured,wheel_observations=wheel,target_enabled=enabled,accepted=accept and enabled,before=before,after=after)
            if not row['accepted']:assert before['rgb_sha256']==after['rgb_sha256'] and before['bpp']==after['bpp']
            report['cases'].append(row);child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0
            code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0;parent.update(normal_close=True,exit_code=0);child.close();child=None
        report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)

if __name__=='__main__':main()
