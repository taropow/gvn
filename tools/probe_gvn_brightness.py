"""Exercise normal GVN brightness menu/dialog using owned windows and private PNG clipboard."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,re
from PIL import Image,ImageChops
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P]),('RegisterClipboardFormatW',w.UINT,[w.LPCWSTR])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])
G=c.WinDLL('gdi32',use_last_error=True)
for name,result,arguments in [('CreateCompatibleDC',P,[P]),('SelectObject',P,[P,P]),('DeleteObject',w.BOOL,[P]),('DeleteDC',w.BOOL,[P]),('CreateDIBSection',P,[P,P,w.UINT,c.POINTER(P),P,w.DWORD])]:native.api(G,name,result,arguments)
for name,result,arguments in [('GetDC',P,[P]),('ReleaseDC',c.c_int,[P,P]),('PrintWindow',w.BOOL,[P,P,w.UINT]),('ShowWindow',w.BOOL,[P,c.c_int]),('UpdateWindow',w.BOOL,[P])]:native.api(U,name,result,arguments)
class Header(c.Structure):
    _fields_=[('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bits',w.WORD),('compression',w.DWORD),('image_bytes',w.DWORD),('x',w.LONG),('y',w.LONG),('used',w.DWORD),('important',w.DWORD)]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('gvn',type=Path);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--capture',action='store_true');args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    commands={};value=0
    for token in (ROOT/'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {',1)[1].split('};',1)[0].split(','):
        match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',token)
        if match:
            if match[2]:value=int(match[2])
            commands[match[1]]=value;value+=1
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();png=U.RegisterClipboardFormatW('PNG');assert png;child=None
    report=dict(passed=False,tool_sha256=native.sha(__file__),gvn_sha256=native.sha(args.gvn),private_station=True,global_input_or_user_clipboard_used=False,processes=[])
    def text(h):
        buffer=c.create_unicode_buffer(1024);child.send(child.own(h),0xd,len(buffer),c.addressof(buffer));return buffer.value
    def copied(label):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,commands['Copy']);child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(png));child.wait(lambda:U.OpenClipboard(None))
        try:
            handle=U.GetClipboardData(png);ptr=K.GlobalLock(handle);native.check(ptr)
            try:raw=c.string_at(ptr,K.GlobalSize(handle))
            finally:K.GlobalUnlock(handle)
        finally:U.CloseClipboard()
        with Image.open(io.BytesIO(raw)) as decoded:result=decoded.convert('RGBA');assert result.size==original.size;result.save(out/(label+'.png'));return result
    def restore(label):
        child.send(window,0x111,commands['Undo']);child.ping(window);assert copied(label+'-undo').tobytes()==original.tobytes()
    def matches(actual,label):
        with Image.open(ROOT/'tests/fixtures/brightness'/(label+'.png')) as image:expected=image.convert('RGBA')
        difference=ImageChops.difference(actual,expected);maximum=max(v[1] for v in difference.getextrema());assert maximum<=2,(label,maximum);return maximum
    def opened(command):
        child.post(window,0x111,commands[command]);dialog=child.wait(lambda:next((h for h in child.windows() if h!=window and U.GetDlgItem(h,800) and '明るさ調整' in text(h)),None));child.wait(lambda:bool(U.IsWindowEnabled(U.GetDlgItem(dialog,1))));return dialog
    def set_value(dialog,key,value):
        h=child.own(U.GetDlgItem(dialog,key));buffer=c.create_unicode_buffer(value);child.send(h,0xc,0,c.addressof(buffer));assert text(h)==value
    def finish(dialog,accept=False):
        if accept:child.wait(lambda:bool(U.IsWindowEnabled(U.GetDlgItem(dialog,1))))
        child.post(child.own(U.GetDlgItem(dialog,1 if accept else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    def capture(dialog):
        U.ShowWindow(child.own(dialog),4);U.UpdateWindow(dialog);child.ping(dialog);bounds=(c.c_long*4)();native.check(U.GetWindowRect(dialog,bounds));width,height=bounds[2]-bounds[0],bounds[3]-bounds[1];assert 100<width<2000 and 100<height<2000
        dc=U.GetDC(dialog);native.check(dc);memory=G.CreateCompatibleDC(dc);native.check(memory);pixels=P();header=Header(c.sizeof(Header),width,-height,1,32,0,width*height*4,0,0,0,0);bitmap=G.CreateDIBSection(dc,c.byref(header),0,c.byref(pixels),None,0);native.check(bitmap);previous=G.SelectObject(memory,bitmap)
        try:
            native.check(U.PrintWindow(dialog,memory,0));image=Image.frombytes('RGBA',(width,height),c.string_at(pixels,width*height*4),'raw','BGRA').convert('RGB');assert any(low!=high for low,high in image.getextrema());image.save(out/'brightness-dialog.png');report['dialog_capture']=dict(width=width,height=height,sha256=native.sha(out/'brightness-dialog.png'))
        finally:G.SelectObject(memory,previous);G.DeleteObject(bitmap);G.DeleteDC(memory);U.ReleaseDC(dialog,dc)
    try:
        config=out/'viewer.ini';config.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\n',encoding='utf-16')
        for kind in ['opaque','alpha']:
            with Image.open(ROOT/'tests/fixtures/brightness/source.png') as image:original=image.convert('RGBA')
            if kind=='alpha':original.putdata([(r,g,b,n%256) for n,(r,g,b,a) in enumerate(original.getdata())])
            source=out/(kind+'-source.png');original.save(source);source_hash=native.sha(source);child=native.Child(args.gvn.resolve(),['--settings',config,source],desktop);row=dict(kind=kind,pid=child.info.pid,source_sha256=source_hash);report['processes'].append(row)
            window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None));child.wait(lambda:source.name in text(window));assert copied(kind+'-before').tobytes()==original.tobytes()
            dialog=opened('Brightness');assert [float(text(U.GetDlgItem(dialog,key))) for key in [800,801,802]]==[0,0,1];assert bool(child.send(child.own(U.GetDlgItem(dialog,809)),0xf0))==(kind=='opaque')
            if kind=='opaque':
                set_value(dialog,802,'0.001');assert not U.IsWindowEnabled(U.GetDlgItem(dialog,1));finish(dialog);assert copied('invalid-cancel').tobytes()==original.tobytes();dialog=opened('Brightness')
            values=['20','35','1.250'] if kind=='opaque' else ['25','0','1.000']
            for key,value in enumerate(values,800):set_value(dialog,key,value)
            child.wait(lambda:bool(U.IsWindowEnabled(U.GetDlgItem(dialog,1))))
            if kind=='opaque' and args.capture:capture(dialog)
            finish(dialog,True);actual=copied(kind+'-accepted')
            if kind=='opaque':row['reference_max_difference']=matches(actual,'combined')
            else:
                expected=bytes(v for r,g,b,a in original.getdata() for v in [min(r+25,255),min(g+25,255),min(b+25,255),a]);assert actual.tobytes()==expected;row['rgba_brightness_exact']=True
            restore(kind+'-accepted');row['undo_exact']=True
            if kind=='opaque':
                dialog=opened('Brightness');assert [float(text(U.GetDlgItem(dialog,key))) for key in [800,801,802]]==[0,0,1];child.send(child.own(U.GetDlgItem(dialog,809)),0xf5);finish(dialog);dialog=opened('Brightness');assert child.send(child.own(U.GetDlgItem(dialog,809)),0xf0)==1;child.send(child.own(U.GetDlgItem(dialog,809)),0xf5);finish(dialog,True);assert copied('neutral-preference').tobytes()==original.tobytes();row['preview_cancel_does_not_persist']=True;row['neutral_ok_keeps_pixels']=True
            child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0;assert native.sha(source)==source_hash;row.update(normal_close=True,exit_code=0,source_unchanged=True);child.close();child=None
        report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)
if __name__=='__main__':main()
