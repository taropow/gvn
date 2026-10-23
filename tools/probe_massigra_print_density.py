"""Read an owned official print preview; never accept the dialog or send a print job."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,struct,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station

ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),
    ('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),
    ('GetDlgCtrlID',c.c_int,[P]),('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P]),('RedrawWindow',w.BOOL,[P,P,P,w.UINT]),
    ('GetDC',P,[P]),('ReleaseDC',c.c_int,[P,P])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])
G=c.WinDLL('gdi32',use_last_error=True)
for name,result,args in [('CreateCompatibleDC',P,[P]),('DeleteDC',w.BOOL,[P]),
    ('CreateDIBSection',P,[P,P,w.UINT,c.POINTER(P),P,w.DWORD]),('SelectObject',P,[P,P]),
    ('DeleteObject',w.BOOL,[P]),('GdiFlush',w.BOOL,[])]:native.api(G,name,result,args)
S=c.WinDLL('winspool.drv',use_last_error=True);native.api(S,'GetDefaultPrinterW',w.BOOL,[w.LPWSTR,c.POINTER(w.DWORD)])

def default_printer():
    size=w.DWORD();S.GetDefaultPrinterW(None,c.byref(size))
    if not size.value:return None
    value=c.create_unicode_buffer(size.value);native.check(S.GetDefaultPrinterW(value,c.byref(size)));return value.value

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--phase',choices=['preview','pdf'],default='preview');parser.add_argument('--grid',choices=['gray','color'],default='gray');parser.add_argument('--densities',type=int,nargs='+',default=[0,-5,5]);args=parser.parse_args()
    assert len(set(args.densities))==len(args.densities) and all(-5<=n<=5 for n in args.densities)
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference=out/'reference'
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                path=(reference/name).resolve();assert path.is_relative_to(reference.resolve());path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(package.read(name))
    exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    patches=[(n,n,n) if args.grid=='gray' else (n,(n*73+19)%256,(n*151+37)%256) for n in range(256)]
    source=out/(args.grid+'-grid.png');image=Image.new('RGB',(512,512));image.putdata([patches[(y//32)*16+x//32] for y in range(512) for x in range(512)]);image.save(source)
    report=dict(passed=False,method='Owned official native print dialog; PrintWindow flags0 plus WM_PRINT client/children; no OK, printer changes, output job or API replacement.',reference_sha256=native.sha(exe),tool_sha256=native.sha(__file__),source_sha256=native.sha(source),private_station=True,user_desktop_clipboard_or_artwork_used=False,print_jobs_requested=0,print_dialog_accepts=0,default_printer_before=default_printer(),samples=[])
    report.update(grid=args.grid,patches=patches,requested_densities=args.densities)
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();child=None;dialog=None
    def text(hwnd):
        value=c.create_unicode_buffer(2048);child.send(hwnd,0xd,len(value),c.addressof(value));return value.value
    def rect(hwnd):
        value=(c.c_long*4)();native.check(U.GetWindowRect(child.own(hwnd),value));return list(value)
    def controls(hwnd):
        result=[]
        @c.WINFUNCTYPE(w.BOOL,P,native.SP)
        def visit(h,_):
            child.own(h);row=dict(id=U.GetDlgCtrlID(h),cls=native.window_text(h,True),text=text(h),bounds=rect(h),style=U.GetWindowLongW(h,-16)&0xffffffff)
            if row['cls']=='ComboBox':
                count=child.send(h,0x146);assert count<100;values=[]
                for n in range(count):value=c.create_unicode_buffer(512);child.send(h,0x148,n,c.addressof(value));values.append(value.value)
                row.update(values=values,selected=child.send(h,0x147))
            result.append(row);return True
        U.EnumChildWindows(child.own(hwnd),visit,0);return result
    def menus(menu):
        result=[]
        for n in range(U.GetMenuItemCount(menu)):
            label=c.create_unicode_buffer(512);U.GetMenuStringW(menu,n,label,len(label),0x400);result.append((U.GetMenuItemID(menu,n),label.value.replace('&','')))
            sub=U.GetSubMenu(menu,n)
            if sub:result+=menus(sub)
        return result
    def copied(name):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,40025);child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8));child.wait(lambda:U.OpenClipboard(None))
        try:
            handle=U.GetClipboardData(8);pointer=K.GlobalLock(handle);native.check(pointer)
            try:raw=c.string_at(pointer,K.GlobalSize(handle))
            finally:K.GlobalUnlock(handle)
        finally:U.CloseClipboard()
        header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw);assert planes==1 and compression in (0,3)
        colors=struct.unpack_from('<I',raw,32)[0] or (1<<bits if bits<=8 else 0);offset=header+colors*4+(12 if header==40 and compression==3 else 0)
        data=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+offset)+raw
        with Image.open(io.BytesIO(data)) as bitmap:result=bitmap.convert('RGB');result.save(out/(name+'.png'));return result.tobytes()
    def capture(name):
        bounds=rect(dialog);width,height=bounds[2]-bounds[0],bounds[3]-bounds[1];assert 0<width<4096 and 0<height<4096
        dc=U.GetDC(dialog);native.check(dc);memory=G.CreateCompatibleDC(dc);native.check(memory);bits=P()
        info=c.create_string_buffer(struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,width*height*4,0,0,0,0)+bytes(4));bitmap=G.CreateDIBSection(dc,info,0,c.byref(bits),None,0);native.check(bitmap and bits)
        previous=G.SelectObject(memory,bitmap)
        try:
            native.check(U.RedrawWindow(dialog,None,None,0x185));native.check(U.PrintWindow(dialog,memory,0));child.send(dialog,0x317,memory,0x1e);native.check(G.GdiFlush())
            captured=Image.frombytes('RGB',(width,height),c.string_at(bits,width*height*4),'raw','BGRX',0,1);captured.save(out/(name+'.png'))
            return dict(file=name+'.png',sha256=native.sha(out/(name+'.png')),window_bounds=bounds,controls=controls(dialog))
        finally:G.SelectObject(memory,previous);G.DeleteObject(bitmap);G.DeleteDC(memory);U.ReleaseDC(dialog,dc)
    def pdf_printer():
        if text(child.own(U.GetDlgItem(dialog,2031)))=='Microsoft Print to PDF':return
        child.post(child.own(U.GetDlgItem(dialog,2061)),0xf5)
        def choice():
            for hwnd in child.windows():
                if hwnd in (window,dialog):continue
                for row in controls(hwnd):
                    if row['cls']=='ComboBox' and 'Microsoft Print to PDF' in row.get('values',[]):return hwnd,child.own(U.GetDlgItem(hwnd,row['id'])),row['values'].index('Microsoft Print to PDF')
            return None
        setup,combo,index=child.wait(choice);report.setdefault('printer_setups',[]).append(dict(title=text(setup),controls=controls(setup)))
        child.send(combo,0x14e,index);child.send(setup,0x111,(1<<16)|U.GetDlgCtrlID(combo),combo);child.post(setup,0x111,1)
        child.wait(lambda:not U.IsWindow(setup) and text(child.own(U.GetDlgItem(dialog,2031)))=='Microsoft Print to PDF');child.ping(dialog)
    def save_pdf(density):
        target=out/('density-'+str(density)+'.pdf');assert not target.exists() and target.parent==out
        assert text(child.own(U.GetDlgItem(dialog,2031)))=='Microsoft Print to PDF' and text(child.own(U.GetDlgItem(dialog,2033)))=='1'
        configured=controls(dialog);child.post(child.own(U.GetDlgItem(dialog,1)),0xf5);report['print_dialog_accepts']+=1;report['print_jobs_requested']+=1
        def filename_dialog():
            for hwnd in child.windows():
                if hwnd==window or native.window_text(hwnd,True)!='#32770':continue
                caption=text(hwnd)
                if '保存' not in caption and 'Save Print' not in caption:continue
                candidates=[]
                @c.WINFUNCTYPE(w.BOOL,P,native.SP)
                def visit(h,_):
                    if native.window_text(h,True)=='Edit' and U.GetDlgCtrlID(h) in (1001,1148):candidates.append(child.own(h))
                    return True
                U.EnumChildWindows(child.own(hwnd),visit,0)
                if len(candidates)==1 and U.IsWindowEnabled(child.own(U.GetDlgItem(hwnd,1))):return hwnd,candidates[0]
            return None
        saving,field=child.wait(filename_dialog);child.ping(saving)
        report.setdefault('save_dialogs',[]).append(dict(title=text(saving),window=saving,field=field,field_class=native.window_text(field,True),field_id=U.GetDlgCtrlID(field)))
        buffer=c.create_unicode_buffer(str(target));child.send(field,0xc,0,c.addressof(buffer))
        saving,field=child.wait(filename_dialog);assert text(field)==str(target)
        save_title=text(saving);child.post(saving,0x111,1)
        def finished():
            try:data=target.read_bytes();return data.startswith(b'%PDF-') and b'%%EOF' in data[-4096:] and not U.IsWindow(saving)
            except (FileNotFoundError,PermissionError):return False
        child.wait(finished,60);child.wait(lambda:U.IsWindowEnabled(window) or U.IsWindow(dialog) and U.GetDlgItem(dialog,2023),30)
        if U.IsWindow(dialog) and U.GetDlgItem(dialog,2023):child.post(dialog,0x111,2);child.wait(lambda:not U.IsWindow(dialog))
        child.wait(lambda:U.IsWindowEnabled(window));child.ping(window)
        return dict(density=density,file=target.name,sha256=native.sha(target),bytes=target.stat().st_size,configured_controls=configured,save_dialog_title=save_title,file_name_control=U.GetDlgCtrlID(field) if U.IsWindow(field) else None)
    try:
        child=native.Child(exe,[source],desktop);report['pid']=child.info.pid
        window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:source.name in native.window_text(window));child.ping(window)
        if not U.GetMenu(window):child.send(window,0x111,41008)
        assert copied('before')==image.tobytes();matches=[n for n,label in menus(U.GetMenu(window)) if label.startswith('印刷')];assert len(matches)==1;report['command']=matches[0]
        child.post(window,0x111,matches[0]);dialog=child.wait(lambda:next((h for h in child.windows() if '印刷' in native.window_text(h) and U.GetDlgItem(h,2023)),None))
        combo=child.own(U.GetDlgItem(dialog,2023));child.wait(lambda:child.send(combo,0x146)==11);report['initial_controls']=controls(dialog)
        for sample_index,density in enumerate(args.densities):
            if args.phase=='pdf':
                if sample_index:
                    child.post(window,0x111,matches[0]);dialog=child.wait(lambda:next((h for h in child.windows() if '印刷' in native.window_text(h) and U.GetDlgItem(h,2023)),None));combo=child.own(U.GetDlgItem(dialog,2023));child.wait(lambda:child.send(combo,0x146)==11)
                pdf_printer()
            selected=density+5;assert child.send(combo,0x14e,selected)!=0xffffffffffffffff;child.send(dialog,0x111,(1<<16)|2023,combo);child.ping(dialog)
            assert child.send(combo,0x147)==selected
            report['samples'].append(save_pdf(density) if args.phase=='pdf' else dict(density=density,**capture('density-'+str(density))))
        if args.phase=='preview':child.post(child.own(U.GetDlgItem(dialog,2)),0xf5);child.wait(lambda:not U.IsWindow(dialog))
        dialog=None;child.ping(window)
        assert copied('after-cancel')==image.tobytes();report['source_rgba_unchanged_after_cancel']=True
        child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
        report.update(normal_close=True,exit_code=0,default_printer_after=default_printer());assert report['default_printer_after']==report['default_printer_before'];report['passed']=True
        if args.phase=='pdf':report['method']='Actual official MassiGra PrintDlgW selection of Microsoft Print to PDF and real save-output dialogs. Only generated gray grid; no driver/API substitutions and no physical printer jobs.'
    except BaseException as error:
        report['error']=type(error).__name__+': '+str(error)
        if child:report['error_owned_windows']=[dict(window=h,title=native.window_text(h),cls=native.window_text(h,True)) for h in child.windows()]
        raise
    finally:
        if child:child.close()
        report['default_printer_after_cleanup']=default_printer()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)

if __name__=='__main__':main()
