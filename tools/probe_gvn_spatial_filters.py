"""Exercise normal GVN filter menus/dialogs using owned windows and private PNG clipboard."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,re
from PIL import Image,ImageChops
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P]),('RegisterClipboardFormatW',w.UINT,[w.LPCWSTR])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])
def main():
    parser=argparse.ArgumentParser();parser.add_argument('gvn',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
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
        with Image.open(ROOT/'tests/fixtures/spatial-filter/colors'/(label+'.png')) as image:expected=image.convert('RGBA')
        difference=ImageChops.difference(actual,expected);maximum=max(v[1] for v in difference.getextrema());assert maximum<=2,(label,maximum);return maximum
    def opened(command):
        child.post(window,0x111,commands[command]);dialog=child.wait(lambda:next((h for h in child.windows() if h!=window and U.GetDlgItem(h,701) and ('ガウスぼかし' in text(h) or 'アンシャープマスク' in text(h))),None));child.wait(lambda:bool(U.IsWindowEnabled(U.GetDlgItem(dialog,1))));return dialog
    def set_value(dialog,key,value):
        h=child.own(U.GetDlgItem(dialog,key));buffer=c.create_unicode_buffer(value);child.send(h,0xc,0,c.addressof(buffer));assert text(h)==value
    def finish(dialog,accept=False):
        if accept:child.wait(lambda:bool(U.IsWindowEnabled(U.GetDlgItem(dialog,1))))
        child.post(child.own(U.GetDlgItem(dialog,1 if accept else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    try:
        for kind in ['opaque','alpha']:
            with Image.open(ROOT/'tests/fixtures/spatial-filter/colors/source.png') as image:original=image.convert('RGBA')
            if kind=='alpha':original.putdata([(r,g,b,n%256) for n,(r,g,b,a) in enumerate(original.getdata())])
            source=out/(kind+'-source.png');original.save(source);source_hash=native.sha(source);config=out/(kind+'.ini');config.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\n',encoding='utf-16')
            child=native.Child(args.gvn.resolve(),['--settings',config,source],desktop);row=dict(kind=kind,pid=child.info.pid,source_sha256=source_hash,operations=[]);report['processes'].append(row)
            window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None));child.wait(lambda:source.name in text(window));assert copied(kind+'-before').tobytes()==original.tobytes()
            presets=[('Sharpen','sharpen'),('SharpenStrong','sharpen-strong'),('SharpenEdges','sharpen-edges'),('Blur','blur'),('BlurStrong','blur-strong'),('BlurSoft','blur-soft')] if kind=='opaque' else [('Sharpen','sharpen'),('BlurSoft','blur-soft')]
            for command,label in presets:
                child.send(window,0x111,commands[command]);child.ping(window);actual=copied(kind+'-'+label)
                if kind=='opaque':error=matches(actual,label)
                else:
                    alphas=list(actual.getchannel('A').getdata());assert min(alphas)<255 and max(alphas)>0
                    if command=='Sharpen':assert actual.getchannel('A').tobytes()==original.getchannel('A').tobytes()
                    error=None
                restore(kind+'-'+label);row['operations'].append(dict(command=command,reference_max_difference=error,undo_exact=True))
            if kind=='opaque':
                for command,values,label in [('Gaussian',['5.0','65'],'blur-soft'),('Unsharp',['0.6','150','15'],'sharpen-edges')]:
                    dialog=opened(command);initial=[text(U.GetDlgItem(dialog,701+n)) for n in range(len(values))];set_value(dialog,701,'0');assert not U.IsWindowEnabled(U.GetDlgItem(dialog,1));finish(dialog);assert copied(command+'-cancel').tobytes()==original.tobytes()
                    dialog=opened(command);assert [text(U.GetDlgItem(dialog,701+n)) for n in range(len(values))]==initial
                    for key,value in enumerate(values,701):set_value(dialog,key,value)
                    finish(dialog,True);error=matches(copied(command+'-accept'),label);restore(command+'-accept');row['operations'].append(dict(command=command,initial_values=initial,values=values,invalid_blocks_ok=True,cancel_exact=True,reference_max_difference=error,undo_exact=True))
            child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0;row.update(normal_close=True,exit_code=0,source_unchanged=native.sha(source)==source_hash);assert row['source_unchanged'];child.close();child=None
        report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)
if __name__=='__main__':main()
