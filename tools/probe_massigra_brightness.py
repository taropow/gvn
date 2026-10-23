"""Inspect actual brightness/contrast/gamma controls on an owned official reference and private clipboard."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,hashlib,io,json,struct,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),
    ('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),
    ('GetDlgCtrlID',c.c_int,[P]),('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])
native.api(U,'GetScrollInfo',w.BOOL,[P,c.c_int,P])
class Scroll(c.Structure):
    _fields_=[('size',w.UINT),('mask',w.UINT),('minimum',c.c_int),('maximum',c.c_int),('page',w.UINT),('position',c.c_int),('track',c.c_int)]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--phase',choices=['inspect','samples'],default='inspect');parser.add_argument('--cases',nargs='+');args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    folder=out/'reference'
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                path=(folder/name).resolve();assert path.is_relative_to(folder.resolve());path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(z.read(name))
    exe=folder/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    source=out/'source.png';image=Image.new('RGB',(256,3));image.putdata([(x,x,x) if y==0 else (x,255-x,(x*73)%256) if y==1 else ((x*13)%256,x,255-x) for y in range(3) for x in range(256)]);image.save(source)
    report=dict(passed=False,tool_sha256=native.sha(__file__),reference_sha256=native.sha(exe),source_sha256=native.sha(source),private_station=True,user_desktop_clipboard_or_artwork_used=False,parents=[])
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();child=None;window=None;command=None
    def menus(menu):
        items=[]
        for i in range(U.GetMenuItemCount(menu)):
            text=c.create_unicode_buffer(512);U.GetMenuStringW(menu,i,text,len(text),0x400);items.append((U.GetMenuItemID(menu,i),text.value.replace('&','')))
            sub=U.GetSubMenu(menu,i)
            if sub:items+=menus(sub)
        return items
    def text(h):
        value=c.create_unicode_buffer(2048);child.send(h,0xd,len(value),c.addressof(value));return value.value
    def controls(dialog):
        result=[]
        @c.WINFUNCTYPE(w.BOOL,P,native.SP)
        def visit(h,_):
            child.own(h);cls=native.window_text(h,True);bounds=(c.c_long*4)();native.check(U.GetWindowRect(h,bounds))
            row=dict(id=U.GetDlgCtrlID(h),label=text(h),cls=cls,enabled=bool(U.IsWindowEnabled(h)),bounds=list(bounds))
            if cls=='Button':row['checked']=child.send(h,0xf0)
            if cls=='msctls_trackbar32':row.update(minimum=c.c_int(child.send(h,0x401)&0xffffffff).value,maximum=c.c_int(child.send(h,0x402)&0xffffffff).value,position=c.c_int(child.send(h,0x400)&0xffffffff).value)
            if cls=='ScrollBar':
                info=Scroll();info.size=c.sizeof(info);info.mask=0x17;native.check(U.GetScrollInfo(h,2,c.byref(info)));row.update(minimum=info.minimum,maximum=info.maximum,page=info.page,position=info.position)
            if cls=='ComboBox':
                count=child.send(h,0x146);assert count<100;values=[]
                for index in range(count):value=c.create_unicode_buffer(512);child.send(h,0x148,index,c.addressof(value));values.append(value.value)
                row.update(values=values,selected=child.send(h,0x147))
            result.append(row);return True
        child.own(dialog);U.EnumChildWindows(dialog,visit,0);return result
    def start():
        nonlocal child,window,command
        child=native.Child(exe,[source],desktop);report['parents'].append(dict(pid=child.info.pid))
        window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
        child.wait(lambda:source.name in native.window_text(window));child.ping(window)
        if not U.GetMenu(window):child.send(window,0x111,41008)
        matches=[n for n,label in menus(U.GetMenu(window)) if '明るさ調整' in label];assert len(matches)==1;command=matches[0]
    def opened():
        child.post(window,0x111,command);dialog=child.wait(lambda:next((h for h in child.windows() if '明るさ調整' in native.window_text(h)),None))
        child.wait(lambda:len(controls(dialog))>=15);child.ping(dialog);return dialog
    def finish(dialog,accept=False):child.post(child.own(U.GetDlgItem(dialog,1 if accept else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    def close():
        nonlocal child
        child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
        report['parents'][-1].update(normal_close=True,exit_code=0);child.close();child=None
    def copied(label):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,40025);child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8));child.wait(lambda:U.OpenClipboard(None))
        try:
            h=U.GetClipboardData(8);ptr=K.GlobalLock(h);native.check(ptr)
            try:raw=c.string_at(ptr,K.GlobalSize(h))
            finally:K.GlobalUnlock(h)
        finally:U.CloseClipboard()
        header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw);assert planes==1 and compression in [0,3]
        count=struct.unpack_from('<I',raw,32)[0] or (1<<bits if bits<=8 else 0);offset=header+count*4+(12 if header==40 and compression==3 else 0)
        bmp=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+offset)+raw;(out/(label+'.bmp')).write_bytes(bmp)
        with Image.open(io.BytesIO(bmp)) as decoded:rgb=decoded.convert('RGB');rgb.save(out/(label+'.png'));return rgb.tobytes()
    def field(dialog,key):return child.own(U.GetDlgItem(dialog,key))
    def set_value(dialog,key,value):
        h=field(dialog,key);child.send(dialog,0x28,h,1);buffer=c.create_unicode_buffer(str(value));child.send(h,0xc,0,c.addressof(buffer));assert text(h)==str(value)
    try:
        start();assert copied('before')==image.tobytes();dialog=opened();report['command']=command;report['initial_controls']=controls(dialog);report['slider_endpoints']=[]
        for axis in range(3):
            scroll=next(row for row in controls(dialog) if row['id']==2121+axis)
            for point in ([scroll['minimum'],-100,-1,1,100,scroll['maximum']] if axis==2 else [scroll['minimum'],scroll['maximum']]):
                child.send(field(dialog,2121+axis),0xe0,point,1);child.send(dialog,0x114,4|((point&0xffff)<<16),field(dialog,2121+axis));child.ping(dialog);values=[text(field(dialog,2031+n)) for n in range(3)];assert float(values[axis])!=[0,0,1][axis];report['slider_endpoints'].append(dict(axis=axis,position=point,fields=values))
                child.send(field(dialog,2061+axis),0xf5);child.ping(dialog)
        report['after_resets']=controls(dialog);finish(dialog);assert copied('after-cancel')==image.tobytes()
        if args.phase=='samples':
            report['samples']=[];undo=next(n for n,label in menus(U.GetMenu(window)) if label.startswith('元に戻す'))
            cases=[('identity',0,0,'1.000',True),('bright25',25,0,'1.000',True),('bright-minus25',-25,0,'1.000',True),
                ('contrast25',0,25,'1.000',True),('contrast-minus25',0,-25,'1.000',True),('gamma04',0,0,'0.400',True),
                ('gamma13',0,0,'1.300',True),('gamma25',0,0,'2.500',True),('combined',20,35,'1.250',True),
                ('combined-negative',-20,-30,'0.750',True),('bright100',100,0,'1.000',True),('contrast100',0,100,'1.000',True),
                ('reset-buttons',30,40,'1.700',True),('gamma001',0,0,'0.001',True),('gamma1000',0,0,'1000.000',True),
                ('contrast255',0,255,'1.000',True),('contrast-minus255',0,-255,'1.000',True),('preview-off',25,0,'1.000',False)]
            if args.cases:
                assert len(set(args.cases))==len(args.cases) and set(args.cases)<={row[0] for row in cases};cases=[row for row in cases if row[0] in args.cases]
            for label,brightness,contrast,gamma,preview in cases:
                dialog=opened();initial=controls(dialog)
                if bool(child.send(field(dialog,2001),0xf0))!=preview:child.send(field(dialog,2001),0xf5)
                for key,value in [(2031,brightness),(2032,contrast),(2033,gamma)]:
                    if key==2033 and label in ['gamma001','gamma1000']:
                        h=field(dialog,key);child.send(dialog,0x28,h,1);buffer=c.create_unicode_buffer(value);child.send(h,0xc,0,c.addressof(buffer));report.setdefault('numeric_bound_observations',[]).append(dict(requested=value,after_set=text(h)));assert float(text(h))>0
                    else:set_value(dialog,key,value)
                child.send(dialog,0x28,field(dialog,1),1)
                if label=='reset-buttons':
                    for key in [2061,2062,2063]:child.send(field(dialog,key),0xf5)
                    assert [float(text(field(dialog,key))) for key in [2031,2032,2033]]==[0,0,1]
                configured=controls(dialog);finish(dialog,True);output=copied(label)
                report['samples'].append(dict(label=label,requested=[brightness,contrast,gamma],preview=preview,initial_controls=initial,configured_controls=configured,output_sha256=native.sha(out/(label+'.png')),output_differs_from_source=output!=image.tobytes()))
                if output!=image.tobytes():child.post(window,0x111,undo);child.ping(window);assert copied(label+'-undo')==image.tobytes()
        close();start();dialog=opened();report['after_restart_controls']=controls(dialog);before=bool(child.send(field(dialog,2001),0xf0));child.send(field(dialog,2001),0xf5);finish(dialog);dialog=opened();report['preview_cancel_before']=before;report['preview_after_cancel']=bool(child.send(field(dialog,2001),0xf0));finish(dialog);close()
        assert native.sha(source)==report['source_sha256'];report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)

if __name__=='__main__':main()
