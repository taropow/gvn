"""Observe actual saving availability on owned static/animated reference images.

Private station/profile/clipboard only. Never dispatch a disabled command.
"""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,shutil,struct,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),('GetMenuState',w.UINT,[P,w.UINT,w.UINT]),('IsWindow',w.BOOL,[P])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])
native.api(U,'GetDlgCtrlID',c.c_int,[P])
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--static-from',type=Path);args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference=out/'reference'
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                target=(reference/name).resolve();assert target.is_relative_to(reference.resolve());target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
    exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    assets=out/'assets';assets.mkdir();shutil.copyfile(ROOT/'tests/fixtures/palette/png-4-opaque.png',assets/'static.png');shutil.copyfile(ROOT/'tests/fixtures/disposal.gif',assets/'composited.gif')
    palette=[channel for i in range(256) for channel in ((i*31+20)%256,(i*53+40)%256,(i*97+60)%256)];palette[3:6]=palette[:3]
    frames=[]
    for index in range(2):
        image=Image.new('P',(9,7));image.putpalette(palette);image.putdata([(x+y*9+index*80)%256 for y in range(7) for x in range(9)]);frames.append(image)
    frames[0].save(assets/'full-frame.gif',save_all=True,append_images=frames[1:],optimize=False,duration=60000,loop=0,disposal=1)
    if args.static_from:
        previous=args.static_from.resolve();assert previous.is_relative_to(ROOT/'.cache')
        prior=json.loads((previous/'result.json').read_text(encoding='utf-8'));assert prior['passed'] and prior['private_station']
        configs=list((previous/'reference/user').rglob('MassiGra.cfg'));assert len(configs)==1
        data=configs[0].read_text(encoding='utf-16');assert data.count('GifAni=1')==1
        config=reference/configs[0].relative_to(previous/'reference');config.parent.mkdir(parents=True,exist_ok=True);config.write_text(data.replace('GifAni=1','GifAni=0',1),encoding='utf-16')
    inputs={p.name:native.sha(p) for p in assets.iterdir()}
    report=dict(passed=False,private_station=True,user_desktop_clipboard_or_artwork_used=False,reference_sha256=native.sha(exe),archive_sha256=native.sha(archive),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),input_sha256=inputs,cases=[],scope='Actual menu availability only. No claim of animation output or raw-number preservation.')
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();child=None
    try:
        cases=[('full-frame.gif',False),('composited.gif',False)] if args.static_from else [('static.png',False),('full-frame.gif',True),('composited.gif',True)]
        for name,animated in cases:
            path=assets/name;child=native.Child(exe,[path],desktop)
            window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
            child.wait(lambda:name in native.window_text(window) and ('<アニメ>' in native.window_text(window))==animated);child.ping(window)
            if not U.GetMenu(window):child.send(window,0x111,41008)
            menu=U.GetMenu(window);assert menu;child.send(window,0x116,menu,0)
            rows=[]
            def visit(current):
                for index in range(U.GetMenuItemCount(current)):
                    label=c.create_unicode_buffer(512);U.GetMenuStringW(current,index,label,len(label),0x400)
                    command=U.GetMenuItemID(current,index);sub=U.GetSubMenu(current,index)
                    if sub:child.send(window,0x117,sub,index);visit(sub)
                    elif command not in (0,0xffffffff):
                        state=U.GetMenuState(current,command,0);assert state!=0xffffffff
                        rows.append(dict(command=command,label=label.value,state=state,enabled=not bool(state&3)))
            visit(menu)
            save_as=next(r for r in rows if r['command']==40008);copy=next(r for r in rows if r['command']==40025)
            saving=[r for r in rows if '保存' in r['label']];assert len(saving)>=2
            row=dict(file=name,pid=child.info.pid,animated=animated,title=native.window_text(window),saving=saving,save_as=save_as,copy=copy,menu=rows)
            if not animated:assert save_as['enabled'] and copy['enabled']
            if args.static_from and name=='full-frame.gif':
                sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,40025)
                child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8));child.wait(lambda:U.OpenClipboard(None))
                try:
                    handle=U.GetClipboardData(8);ptr=K.GlobalLock(handle);native.check(ptr)
                    try:raw=c.string_at(ptr,K.GlobalSize(handle))
                    finally:K.GlobalUnlock(handle)
                finally:U.CloseClipboard()
                header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw);assert planes==1 and bits==8 and compression==0
                count=struct.unpack_from('<I',raw,32)[0] or 256;bmp=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+header+count*4)+raw
                (out/'copied-first-frame.bmp').write_bytes(bmp)
                with Image.open(io.BytesIO(bmp)) as actual,Image.open(path) as expected:
                    expected.seek(0);assert actual.mode==expected.mode=='P' and actual.size==expected.size
                    assert list(actual.getdata())==list(expected.getdata()) and actual.getpalette()[:768]==expected.getpalette()[:768]
                    assert actual.convert('RGBA').tobytes()==expected.convert('RGBA').tobytes();expected.save(out/'expected-first-frame.png')
                child.post(window,0x111,40008)
                dialog=child.wait(lambda:next((h for h in child.windows() if h!=window and '保存' in native.window_text(h)),None));child.ping(dialog)
                def descendants(parent):
                    result=[]
                    @native.Callback
                    def add(h,_):result.append(child.own(h));return True
                    U.EnumChildWindows(parent,add,0);return result
                filetype=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='ComboBox' and U.GetDlgCtrlID(h)==1136),None))
                child.send(filetype,0x14f,1);child.post(filetype,0x100,0x24,1)
                for _ in range(3):child.post(filetype,0x100,0x28,1)
                child.post(filetype,0x100,0xd,1);child.wait(lambda:child.send(filetype,0x147)==3)
                child.ping(dialog);filename=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='Edit' and U.GetDlgCtrlID(h)==1152),None))
                target=assets/'saved-first-frame.png';buffer=c.create_unicode_buffer(str(target));child.send(filename,0xc,0,c.addressof(buffer));child.post(dialog,0x111,1)
                child.wait(lambda:target.exists() and not U.IsWindow(dialog));child.ping(window)
                with Image.open(target) as actual,Image.open(out/'expected-first-frame.png') as expected:
                    assert actual.mode==expected.mode=='P' and actual.size==expected.size
                    assert list(actual.getdata())==list(expected.getdata()) and actual.getpalette()==expected.getpalette()
                    assert actual.convert('RGBA').tobytes()==expected.convert('RGBA').tobytes()
                row.update(first_frame_copy_indices_and_palette_exact=True,first_frame_saved_png_indices_and_palette_exact=True,saved_sha256=native.sha(target))
            child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0
            code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
            row.update(normal_close=True,exit_code=0);report['cases'].append(row);child.close();child=None
        assert all(native.sha(assets/name)==digest for name,digest in inputs.items()) and native.sha(exe)==report['reference_sha256'];report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex)
        print('Evidence: '+str(out),flush=True)
    print(json.dumps(dict(passed=True,cases=[dict(file=r['file'],animated=r['animated'],save_as_enabled=r['save_as']['enabled'],copy_enabled=r['copy']['enabled']) for r in report['cases']]),ensure_ascii=False),flush=True)
if __name__=='__main__':main()
