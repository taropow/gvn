"""Observe real palette-index editing/apply/cancel on a private official MassiGra copy."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,shutil,struct,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station

ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),
    ('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),
    ('GetMenuState',w.UINT,[P,w.UINT,w.UINT]),('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip'
    assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference=out/'reference'
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                target=(reference/name).resolve();assert target.is_relative_to(reference.resolve())
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
    exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    source=out/'source.png';shutil.copyfile(ROOT/'tests/fixtures/palette/png-4-opaque.png',source)
    original=Image.open(source);original.load();assert original.mode=='P'
    original_rgb=original.convert('RGB');original_indices=list(original.getdata())
    report=dict(passed=False,reference_sha256=native.sha(exe),source_sha256=native.sha(source),
        tool_sha256=native.sha(__file__),private_station=True,user_desktop_clipboard_or_artwork_used=False,cases=[],parents=[])
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
    assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();child=None;window=None;command=None
    def start(path):
        nonlocal child,window
        child=native.Child(exe,[path],desktop);report['parents'].append(dict(pid=child.info.pid))
        window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
        child.wait(lambda:path.name in native.window_text(window));child.ping(window)
        if not U.GetMenu(window):child.send(window,0x111,41008)
    def close():
        nonlocal child
        child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0
        code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
        report['parents'][-1].update(normal_close=True,exit_code=0);child.close();child=None
    def items(menu):
        result=[]
        for i in range(U.GetMenuItemCount(menu)):
            label=c.create_unicode_buffer(512);U.GetMenuStringW(menu,i,label,len(label),0x400)
            result.append(dict(label=label.value,id=U.GetMenuItemID(menu,i)))
            sub=U.GetSubMenu(menu,i)
            if sub:result+=items(sub)
        return result
    def initialize(target=None):
        target=command if target is None else target
        menu=U.GetMenu(window);child.send(window,0x116,menu,0)
        def visit(current):
            if any(U.GetMenuItemID(current,i)==target for i in range(U.GetMenuItemCount(current))):return True
            for i in range(U.GetMenuItemCount(current)):
                sub=U.GetSubMenu(current,i)
                if sub and visit(sub):child.send(window,0x117,sub,i);return True
            return False
        assert visit(menu)
        return U.GetMenuState(menu,target,0)
    def text(handle):
        value=c.create_unicode_buffer(2048);child.send(handle,0xd,len(value),c.addressof(value));return value.value
    def field(dialog,cid):return child.own(U.GetDlgItem(dialog,cid))
    def set_value(dialog,cid,value):
        handle=field(dialog,cid);child.send(dialog,0x28,handle,1);buf=c.create_unicode_buffer(str(value));child.send(handle,0xc,0,c.addressof(buf))
        assert text(handle)==str(value)
    def fields(dialog):
        return dict(index=text(field(dialog,2031)),red=text(field(dialog,2032)),green=text(field(dialog,2033)),blue=text(field(dialog,2034)),
            preview=child.send(field(dialog,2001),0xf0),apply_enabled=bool(U.IsWindowEnabled(field(dialog,2061))))
    def opened():
        assert not initialize()&3;child.post(window,0x111,command)
        dialog=child.wait(lambda:next((h for h in child.windows() if 'パレット編集' in native.window_text(h)),None))
        child.wait(lambda:U.GetDlgItem(dialog,2034) and U.GetDlgItem(dialog,2061));child.ping(dialog);return dialog
    def finish(dialog,accept=False):
        child.post(field(dialog,1 if accept else 2),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    def copied(label):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,40025)
        child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8))
        child.wait(lambda:U.OpenClipboard(None))
        try:
            handle=U.GetClipboardData(8);ptr=K.GlobalLock(handle);native.check(ptr)
            try:raw=c.string_at(ptr,K.GlobalSize(handle))
            finally:K.GlobalUnlock(handle)
        finally:U.CloseClipboard()
        header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw)
        assert planes==1 and compression in [0,3]
        count=struct.unpack_from('<I',raw,32)[0] or (1<<bits if bits<=8 else 0)
        offset=header+count*4+(12 if header==40 and compression==3 else 0)
        bmp=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+offset)+raw
        (out/(label+'.bmp')).write_bytes(bmp)
        with Image.open(io.BytesIO(bmp)) as pixels:
            if bits<=8:assert pixels.mode=='P' and list(pixels.getdata())==original_indices
            decoded=pixels.convert('RGB');assert decoded.size==original.size
            path=out/(label+'.png');decoded.save(path)
        return decoded,bits
    try:
        start(source);matches=[v['id'] for v in items(U.GetMenu(window)) if 'パレット編集' in v['label'].replace('&','')]
        assert len(matches)==1;command=matches[0];report['palette_command']=command
        before,bits=copied('before');assert before.tobytes()==original_rgb.tobytes();report['copy_bpp']=bits
        dialog=opened();report['initial_fields']=fields(dialog)
        set_value(dialog,2031,7);child.send(dialog,0x28,field(dialog,2032),1)
        palette=original.getpalette();expected_rgb=palette[7*3:7*3+3]
        child.wait(lambda:[fields(dialog)[key] for key in ['red','green','blue']]==list(map(str,expected_rgb)))
        report['selected7_fields']=fields(dialog)
        set_value(dialog,2031,1)
        for cid,value in [(2032,11),(2033,22),(2034,33)]:set_value(dialog,cid,value)
        child.send(dialog,0x28,field(dialog,2061),1);child.wait(lambda:U.IsWindowEnabled(field(dialog,2061)))
        child.send(field(dialog,2061),0xf5);child.ping(dialog);assert U.IsWindow(dialog)
        report['after_apply_fields']=fields(dialog);finish(dialog)
        after,bits=copied('after-apply-cancel')
        expected=Image.new('RGB',original.size);base=list(original_rgb.getdata())
        expected.putdata([(11,22,33) if index==1 else pixel for index,pixel in zip(original_indices,base)])
        assert after.tobytes()==expected.tobytes()
        report['cases'].append(dict(label='apply_then_cancel',passed=True,duplicate_index0_unchanged=True,only_index1_rgb_changed=True))
        dialog=opened();set_value(dialog,2031,1)
        for cid,value in [(2032,101),(2033,103),(2034,107)]:set_value(dialog,cid,value)
        finish(dialog);cancelled,_=copied('cancel-only');assert cancelled.tobytes()==expected.tobytes()
        report['cases'].append(dict(label='cancel_without_apply',passed=True,prior_apply_retained=True))
        dialog=opened();preview=field(dialog,2001)
        if child.send(preview,0xf0):child.send(preview,0xf5)
        assert child.send(preview,0xf0)==0;finish(dialog,True);close()
        start(source);dialog=opened();report['after_restart_fields']=fields(dialog);finish(dialog);close()
        animation=out/'animation.gif';shutil.copyfile(ROOT/'tests/fixtures/disposal.gif',animation);start(animation)
        # A running official animation does not provide this copy operation.
        # Require its actual animation marker before observing palette availability.
        child.wait(lambda:'<アニメ>' in native.window_text(window));report['animated_title']=native.window_text(window)
        report['animated_copy_menu_state']=initialize(40025)
        state=initialize();report['animated_palette_menu_state']=state;assert state&3;close()
        assert native.sha(source)==report['source_sha256'] and native.sha(exe)==report['reference_sha256']
        report['passed']=True
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)

if __name__=='__main__':main()
