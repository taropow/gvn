"""Observe bitfield-to24 option on owned reference processes/private station."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,hashlib,io,json,shutil,struct,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetDlgCtrlID',c.c_int,[P]),('GetParent',P,[P]),('IsWindow',w.BOOL,[P]),('IsWindowVisible',w.BOOL,[P])]:native.api(U,name,result,args)
for name,result,args in [('VirtualAllocEx',P,[P,P,native.UP,w.DWORD,w.DWORD]),('VirtualFreeEx',w.BOOL,[P,P,native.UP,w.DWORD]),('ReadProcessMemory',w.BOOL,[P,P,P,native.UP,P]),('GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])]:native.api(K,name,result,args)
native.api(K,'WriteProcessMemory',w.BOOL,[P,P,P,native.UP,P])
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25';folder=out/'reference'
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                path=(folder/name).resolve();assert path.is_relative_to(folder.resolve());path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(z.read(name))
    exe=folder/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    assets=out/'assets';assets.mkdir();files={}
    for name in ['auto-bmp/rgb555.bmp','auto-bmp/rgb555-fields.bmp','auto-bmp/rgb565.bmp','loading/alpha-v5.bmp']:
        source=assets/Path(name).name;shutil.copyfile(ROOT/'tests/fixtures'/name,source);files[source.name]=native.sha(source)
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80);desktop=station();child=None;window=None
    report=dict(passed=False,tool_sha256=native.sha(__file__),reference_sha256=native.sha(exe),private_station=True,global_input_or_user_clipboard_used=False,source_files=files,processes=[]);loading_index=None
    def text(h):
        buffer=c.create_unicode_buffer(2048);child.send(child.own(h),0xd,len(buffer),c.addressof(buffer));return buffer.value
    def descendants(h):
        result=[]
        @native.Callback
        def visit(window,_):result.append(child.own(window));return True
        U.EnumChildWindows(child.own(h),visit,0);return result
    def preference():
        nonlocal loading_index
        child.post(window,0x111,40101);dialog=child.wait(lambda:next((h for h in child.windows() if h!=window and '設定' in text(h)),None));child.ping(dialog);tab=next(h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32')
        remote=K.VirtualAllocEx(child.info.process,None,1024,0x3000,4);native.check(remote)
        try:
            if loading_index is None:
                labels=[]
                for n in range(child.send(tab,0x1304)):
                    # Official0.45 is x86: TCITEMW32 with an owned remote UTF16 buffer.
                    item=c.create_string_buffer(struct.pack('<IIIIiiI',1,0,0,remote+64,256,0,0));native.check(K.WriteProcessMemory(child.info.process,remote,item,28,None));native.check(child.send(tab,0x133c,n,remote));label=c.create_unicode_buffer(256);native.check(K.ReadProcessMemory(child.info.process,remote+64,label,512,None));labels.append(label.value)
                report['actual_tab_labels']=labels;matches=[n for n,label in enumerate(labels) if 'ファイル読み込み' in ''.join(label.split())];assert len(matches)==1,labels;loading_index=matches[0]
            native.check(child.send(tab,0x130a,loading_index,remote));rect=(c.c_int*4)();native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None));x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2;child.post(tab,0x201,1,x|(y<<16));child.post(tab,0x202,0,x|(y<<16))
        finally:native.check(K.VirtualFreeEx(child.info.process,remote,0,0x8000))
        child.wait(lambda:child.send(tab,0x130b)==loading_index);report.setdefault('active_tab_observations',[]).append(dict(selected=child.send(tab,0x130b),controls=[dict(id=U.GetDlgCtrlID(h),label=text(h),visible=bool(U.IsWindowVisible(h))) for h in descendants(dialog) if native.window_text(h,True)=='Button']))
        # Child startup intentionally hides the root on the private station, so
        # IsWindowVisible is false for every descendant. Verify actual selection,
        # unique resource label and ownership rather than physical visibility.
        fields=[h for h in descendants(dialog) if U.GetDlgCtrlID(h)==2007 and 'ビットフィールド' in text(h)];assert len(fields)==1 and child.send(tab,0x130b)==loading_index;field=fields[0];child.ping(dialog);return dialog,field
    def finish(dialog,accept):
        child.post(child.own(U.GetDlgItem(dialog,1 if accept else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    def copy(label):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,40025);child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8));child.wait(lambda:U.OpenClipboard(None))
        try:
            handle=U.GetClipboardData(8);ptr=K.GlobalLock(handle);native.check(ptr)
            try:raw=c.string_at(ptr,K.GlobalSize(handle))
            finally:K.GlobalUnlock(handle)
        finally:U.CloseClipboard()
        header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw);assert planes==1 and compression in [0,3];count=struct.unpack_from('<I',raw,32)[0] or (1<<bits if bits<=8 else 0);offset=header+count*4+(12 if header==40 and compression==3 else 0)
        bmp=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+offset)+raw;(out/(label+'.bmp')).write_bytes(bmp)
        with Image.open(io.BytesIO(bmp)) as decoded:image=decoded.convert('RGB');image.save(out/(label+'.png'))
        return dict(bpp=bits,compression=compression,rgb_sha256=hashlib.sha256(image.tobytes()).hexdigest(),size=image.size)
    def start(source):
        nonlocal child,window
        child=native.Child(exe,[source],desktop);window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:source.name in text(window));child.ping(window);report['processes'].append(dict(pid=child.info.pid,source=source.name))
    def close():
        nonlocal child
        child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0;report['processes'][-1].update(normal_close=True,exit_code=0);child.close();child=None
    try:
        start(assets/'rgb565.bmp');dialog,field=preference();report['default_checked']=bool(child.send(field,0xf0));report['label']=text(field);finish(dialog,False);report['default_copy']=copy('default');close()
        for enabled in [False,True]:
            start(assets/'rgb565.bmp');dialog,field=preference();before=bool(child.send(field,0xf0))
            if before!=enabled:child.send(field,0xf5)
            assert bool(child.send(field,0xf0))==enabled;finish(dialog,True);report['processes'][-1]['configured_enabled']=enabled;close()
            for name in files:
                start(assets/name);dialog,field=preference();assert bool(child.send(field,0xf0))==enabled;finish(dialog,False);report['processes'][-1].update(enabled=enabled,copied=copy(('on-' if enabled else 'off-')+Path(name).stem));close()
        assert all(native.sha(assets/n)==v for n,v in files.items());report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)
if __name__=='__main__':main()
