"""Observe official level controls and RGB output using owned HWNDs on a private station."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,hashlib,io,json,struct,zipfile
from PIL import Image
import probe_massigra_brightness as shared
import probe_gvn_brightness as graphics
native=shared.native;U,K,P=shared.U,shared.K,shared.P
ROOT=Path(__file__).resolve().parents[1]
native.api(U,'GetClientRect',w.BOOL,[P,P])
native.api(U,'GetMenuState',w.UINT,[P,w.UINT,w.UINT])

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--phase',choices=['inspect','samples'],default='inspect');parser.add_argument('--kind',choices=['ramp','narrow'],default='narrow');parser.add_argument('--cases',nargs='+');args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    folder=out/'reference'
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                path=(folder/name).resolve();assert path.is_relative_to(folder.resolve());path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(z.read(name))
    exe=folder/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    source=out/'source.png';image=Image.new('RGB',(256,3))
    image.putdata([(x,x,x) if y==0 else (x,255-x,(x*73)%256) if y==1 else ((x*13)%256,x,255-x) for y in range(3) for x in range(256)] if args.kind=='ramp' else [(20+x*180//255,50+(x if y!=1 else 255-x)*130//255,80+(x if y!=2 else 255-x)*80//255) for y in range(3) for x in range(256)])
    image.save(source)
    report=dict(passed=False,tool_sha256=native.sha(__file__),shared_tools={n:native.sha(ROOT/'tools'/n) for n in ['probe_massigra_brightness.py','probe_gvn_brightness.py']},reference_sha256=native.sha(exe),source_sha256=native.sha(source),source_kind=args.kind,private_station=True,user_desktop_clipboard_or_artwork_used=False,parents=[])
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=shared.station();child=None;window=None;command=None
    def text(h):
        value=c.create_unicode_buffer(2048);child.send(child.own(h),0xd,len(value),c.addressof(value));return value.value
    def menus(menu):
        items=[]
        for i in range(U.GetMenuItemCount(menu)):
            value=c.create_unicode_buffer(512);U.GetMenuStringW(menu,i,value,len(value),0x400);items.append((U.GetMenuItemID(menu,i),value.value.replace('&','')))
            sub=U.GetSubMenu(menu,i)
            if sub:items+=menus(sub)
        return items
    def field(dialog,key):return child.own(U.GetDlgItem(dialog,key))
    def controls(dialog):
        rows=[]
        @c.WINFUNCTYPE(w.BOOL,P,native.SP)
        def visit(h,_):
            child.own(h);cls=native.window_text(h,True);bounds=(c.c_long*4)();native.check(U.GetWindowRect(h,bounds));row=dict(id=U.GetDlgCtrlID(h),label=text(h),cls=cls,enabled=bool(U.IsWindowEnabled(h)),bounds=list(bounds))
            if cls=='Button':row['checked']=child.send(h,0xf0)
            rows.append(row);return True
        U.EnumChildWindows(dialog,visit,0);return rows
    def values(dialog):return [text(field(dialog,key)) for key in range(2031,2036)]
    def checked(dialog,key):return bool(child.send(field(dialog,key),0xf0))
    def toggle(dialog,key,wanted):
        if checked(dialog,key)!=wanted:child.send(field(dialog,key),0xf5)
        assert checked(dialog,key)==wanted
    def select(dialog,channel):child.send(field(dialog,2141+channel),0xf5);child.ping(dialog)
    def channels(dialog):
        rows=[]
        for n in range(3):select(dialog,n);rows.append(values(dialog))
        return rows
    def set_value(dialog,key,value):
        h=field(dialog,key);child.send(dialog,0x28,h,1);buffer=c.create_unicode_buffer(str(value));child.send(h,0xc,0,c.addressof(buffer));child.send(dialog,0x28,field(dialog,1),1);child.ping(dialog);return text(h)
    def configure(dialog,v):
        for key,value in [(2031,v[0]),(2033,v[2]),(2032,v[1]),(2034,v[3]),(2035,v[4])]:set_value(dialog,key,value)
    def start():
        nonlocal child,window,command
        child=native.Child(exe,[source],desktop);report['parents'].append(dict(pid=child.info.pid));window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:source.name in native.window_text(window));child.ping(window)
        if not U.GetMenu(window):child.send(window,0x111,41008)
        candidates=[n for n,label in menus(U.GetMenu(window)) if label.startswith('レベル調整')];assert len(candidates)==1;command=candidates[0]
    def opened():
        child.post(window,0x111,command);dialog=child.wait(lambda:next((h for h in child.windows() if 'レベル調整' in native.window_text(h)),None));child.wait(lambda:len(controls(dialog))>=25);child.ping(dialog);return dialog
    def finish(dialog,accept=False):child.post(field(dialog,1 if accept else 2),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    def close():
        nonlocal child
        child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0;report['parents'][-1].update(normal_close=True,exit_code=0);child.close();child=None
    def copied(label):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,40025);child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8));child.wait(lambda:U.OpenClipboard(None))
        try:
            handle=U.GetClipboardData(8);pointer=K.GlobalLock(handle);native.check(pointer)
            try:raw=c.string_at(pointer,K.GlobalSize(handle))
            finally:K.GlobalUnlock(handle)
        finally:U.CloseClipboard()
        header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw);assert planes==1 and compression in [0,3];count=struct.unpack_from('<I',raw,32)[0] or (1<<bits if bits<=8 else 0);offset=header+count*4+(12 if header==40 and compression==3 else 0);bmp=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+offset)+raw
        with Image.open(io.BytesIO(bmp)) as decoded:rgb=decoded.convert('RGB');rgb.save(out/(label+'.png'));return rgb.tobytes()
    def undo_flags():
        main=U.GetMenu(window)
        for index in range(U.GetMenuItemCount(main)):
            menu=U.GetSubMenu(main,index)
            if menu and any(U.GetMenuItemID(menu,n)==undo for n in range(U.GetMenuItemCount(menu))):
                child.send(window,0x117,menu,index);flags=U.GetMenuState(menu,undo,0);assert flags!=0xffffffff;return flags
        raise AssertionError('Undo parent menu not found')
    def capture(dialog):
        bounds=(c.c_long*4)();native.check(U.GetWindowRect(dialog,bounds));width,height=bounds[2]-bounds[0],bounds[3]-bounds[1];dc=U.GetDC(dialog);native.check(dc);G=graphics.G;memory=G.CreateCompatibleDC(dc);native.check(memory);pixels=P();header=graphics.Header(c.sizeof(graphics.Header),width,-height,1,32,0,width*height*4,0,0,0,0);bitmap=G.CreateDIBSection(dc,c.byref(header),0,c.byref(pixels),None,0);native.check(bitmap);previous=G.SelectObject(memory,bitmap)
        try:
            returned=bool(U.PrintWindow(dialog,memory,0));snapshot=Image.frombytes('RGBA',(width,height),c.string_at(pixels,width*height*4),'raw','BGRA').convert('RGB');varied=any(lo!=hi for lo,hi in snapshot.getextrema());snapshot.save(out/'levels-dialog.png');report['capture_diagnostic']=dict(print_returned=returned,varied=varied,sha256=native.sha(out/'levels-dialog.png'))
        finally:G.SelectObject(memory,previous);G.DeleteObject(bitmap);G.DeleteDC(memory);U.ReleaseDC(dialog,dc)
    try:
        start();report['command']=command;undo=next(n for n,label in menus(U.GetMenu(window)) if label.startswith('元に戻す'));assert copied('before')==image.tobytes();dialog=opened();report['initial_controls']=controls(dialog)
        if args.phase=='inspect':capture(dialog)
        report['spin_ranges']=[dict(id=key,packed=child.send(field(dialog,key),0x466)) for key in range(2161,2166)];finish(dialog);report['initial_undo_flags']=undo_flags()
        if args.phase=='inspect':
            dialog=opened();toggle(dialog,2001,False);configure(dialog,[17,'1.300',225,10,240]);report['combined_configured']=values(dialog);toggle(dialog,2001,True);report['individual_after_combined']=channels(dialog)
            for n,v in enumerate([[20,'1.200',220,5,230],[30,'1.400',210,6,240],[40,'0.800',200,7,220]]):select(dialog,n);configure(dialog,v)
            report['individual_configured']=channels(dialog);toggle(dialog,2001,False);report['combined_after_individual']=values(dialog);toggle(dialog,2001,True);report['individual_after_roundtrip']=channels(dialog);finish(dialog)
            report['auto_setting']=[]
            for individual in [False,True]:
                dialog=opened();toggle(dialog,2001,individual);child.send(field(dialog,2061),0xf5);child.ping(dialog);report['auto_setting'].append(dict(individual=individual,values=channels(dialog) if individual else values(dialog)));finish(dialog)
            dialog=opened();report['numeric_bounds']=[]
            for key,value in [(2031,-1),(2031,256),(2033,-1),(2033,256),(2032,'0.001'),(2032,'1000.000'),(2034,300),(2035,-2)]:
                observed=set_value(dialog,key,value);report['numeric_bounds'].append(dict(id=key,requested=value,observed=observed,all_values=values(dialog)))
            finish(dialog)
            dialog=opened();report['after_cancel']=dict(values=values(dialog),individual=checked(dialog,2001),preview=checked(dialog,2002));toggle(dialog,2001,True);toggle(dialog,2002,False);finish(dialog);dialog=opened();report['preferences_after_cancel']=dict(values=values(dialog),individual=checked(dialog,2001),preview=checked(dialog,2002));toggle(dialog,2001,True);toggle(dialog,2002,False);finish(dialog,True);assert copied('neutral-preferences')==image.tobytes()
        else:
            report['samples']=[]
            cases=[('identity',[0,'1.000',255,0,255]),('input',[30,'1.000',220,0,255]),('gamma',[0,'1.300',255,0,255]),('output',[0,'1.000',255,20,230]),('inverted-output',[0,'1.000',255,240,10]),('combined',[20,'1.250',225,10,240]),('inverted-combined',[20,'0.750',225,230,15]),('gamma001',[0,'0.001',255,0,255]),('gamma1000',[0,'1000.000',255,0,255]),('gamma10',[0,'10.000',255,0,255]),('gamma20',[0,'20.000',255,0,255])]
            if args.cases:
                assert len(set(args.cases))==len(args.cases) and set(args.cases)<={x[0] for x in cases}|{'rgb'};cases=[x for x in cases if x[0] in args.cases]
            for label,v in cases:
                dialog=opened();toggle(dialog,2001,False);configure(dialog,v);configured=values(dialog);finish(dialog,True);output=copied(label);flags=undo_flags();report['samples'].append(dict(label=label,individual=False,values=configured,output_sha256=native.sha(out/(label+'.png')),undo_flags=flags,undo_enabled=not bool(flags&3)))
                if output!=image.tobytes() or not flags&3:child.post(window,0x111,undo);child.ping(window);assert copied(label+'-undo')==image.tobytes()
            if not args.cases or 'rgb' in args.cases:
                dialog=opened();toggle(dialog,2001,True)
                for n,v in enumerate([[20,'1.200',220,5,230],[30,'1.400',210,6,240],[40,'0.800',200,7,220]]):select(dialog,n);configure(dialog,v)
                configured=channels(dialog);finish(dialog,True);output=copied('rgb');report['samples'].append(dict(label='rgb',individual=True,values=configured,output_sha256=native.sha(out/'rgb.png')));child.post(window,0x111,undo);child.ping(window);assert copied('rgb-undo')==image.tobytes()
            if args.kind=='narrow':
                report['auto_from_configured']=[]
                for individual in [False,True]:
                    dialog=opened();toggle(dialog,2001,individual)
                    for n in range(3 if individual else 1):
                        if individual:select(dialog,n)
                        configure(dialog,[10,'1.300',230,15,240])
                    child.send(field(dialog,2061),0xf5);child.ping(dialog);report['auto_from_configured'].append(dict(individual=individual,values=channels(dialog) if individual else values(dialog)));finish(dialog)
        close();start();dialog=opened();report['after_restart']=dict(values=values(dialog),individual=checked(dialog,2001),preview=checked(dialog,2002),channels=channels(dialog));finish(dialog);close();assert native.sha(source)==report['source_sha256'];report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)
if __name__=='__main__':main()
