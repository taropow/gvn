"""Observe owned reference filters on a private window station/clipboard."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,re,struct,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),
    ('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),
    ('GetDlgCtrlID',c.c_int,[P]),('IsWindow',w.BOOL,[P]),('GetScrollInfo',w.BOOL,[P,c.c_int,P])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])
class Scroll(c.Structure):
    _fields_=[('size',w.UINT),('mask',w.UINT),('minimum',c.c_int),('maximum',c.c_int),('page',w.UINT),('position',c.c_int),('track',c.c_int)]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--pattern',choices=['colors','impulse'],default='colors');parser.add_argument('--calibrate',action='store_true');args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    folder=out/'reference'
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                path=(folder/name).resolve();assert path.is_relative_to(folder.resolve());path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(z.read(name))
    exe=folder/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    source=out/'source.png';image=Image.new('RGB',(33,23));image.putdata([((x*19+y*7)%256,(x*5+y*11)%256,(x*3+y*31)%256) for y in range(23) for x in range(33)])
    if args.pattern=='impulse':image=Image.new('RGB',(65,65));image.putpixel((32,32),(255,255,255))
    image.save(source)
    report=dict(passed=False,tool_sha256=native.sha(__file__),reference_sha256=native.sha(exe),source_sha256=native.sha(source),private_station=True,global_input_or_user_clipboard_used=False,samples=[])
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();child=None
    def text(h):
        value=c.create_unicode_buffer(2048);child.send(child.own(h),0xd,len(value),c.addressof(value));return value.value
    def menus(menu):
        result=[]
        for i in range(U.GetMenuItemCount(menu)):
            label=c.create_unicode_buffer(512);U.GetMenuStringW(menu,i,label,len(label),0x400);title=re.sub(r'[\s&]','',label.value.split('\t')[0]);result.append((U.GetMenuItemID(menu,i),re.sub(r'\([A-Za-z]\)','',title)))
            sub=U.GetSubMenu(menu,i)
            if sub:result+=menus(sub)
        return result
    def controls(dialog):
        result=[]
        @c.WINFUNCTYPE(w.BOOL,P,native.SP)
        def visit(h,_):
            child.own(h);cls=native.window_text(h,True);row=dict(id=U.GetDlgCtrlID(h),cls=cls,label=text(h))
            if cls=='ScrollBar':
                info=Scroll();info.size=c.sizeof(info);info.mask=0x17;native.check(U.GetScrollInfo(h,2,c.byref(info)));row.update(minimum=info.minimum,maximum=info.maximum,page=info.page,position=info.position)
            result.append(row);return True
        U.EnumChildWindows(child.own(dialog),visit,0);return result
    def opened(command,title):
        child.post(window,0x111,command);dialog=child.wait(lambda:next((h for h in child.windows() if h!=window and title in text(h) and U.GetDlgItem(h,2031)),None));child.ping(dialog);return dialog
    def finish(dialog,accept=False):
        child.post(child.own(U.GetDlgItem(dialog,1 if accept else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    def copied(label):
        sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,copy);child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(8));child.wait(lambda:U.OpenClipboard(None))
        try:
            h=U.GetClipboardData(8);ptr=K.GlobalLock(h);native.check(ptr)
            try:raw=c.string_at(ptr,K.GlobalSize(h))
            finally:K.GlobalUnlock(h)
        finally:U.CloseClipboard()
        header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw);assert planes==1 and compression in [0,3]
        count=struct.unpack_from('<I',raw,32)[0] or (1<<bits if bits<=8 else 0);offset=header+count*4+(12 if header==40 and compression==3 else 0)
        bmp=b'BM'+struct.pack('<IHHI',14+len(raw),0,0,14+offset)+raw
        with Image.open(io.BytesIO(bmp)) as decoded:rgb=decoded.convert('RGB');assert rgb.size==image.size;rgb.save(out/(label+'.png'));return rgb.tobytes()
    def restore(label):
        child.post(window,0x111,undo);child.ping(window);assert copied(label+'-undo')==image.tobytes()
    try:
        child=native.Child(exe,[source],desktop);report['pid']=child.info.pid
        window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:source.name in text(window));child.ping(window)
        if not U.GetMenu(window):child.send(window,0x111,41008)
        items=menus(U.GetMenu(window));report['filter_menu']=[dict(command=n,label=t) for n,t in items if 'シャープ' in t or 'ぼかし' in t]
        def one(predicate):
            matches=[n for n,t in items if predicate(t)];assert len(matches)==1,matches;return matches[0]
        copy=one(lambda t:t=='コピー');undo=one(lambda t:t.startswith('元に戻す'));assert copied('before')==image.tobytes()
        dialogs={'unsharp':(one(lambda t:t.startswith('アンシャープマスク')),'アンシャープマスク'),'gaussian':(one(lambda t:t.startswith('ガウスぼかし')),'ガウスぼかし')}
        report['dialogs']={}
        for kind,(command,title) in dialogs.items():
            dialog=opened(command,title);report['dialogs'][kind]=controls(dialog);finish(dialog);assert copied(kind+'-cancel')==image.tobytes()
        cases=[('sharpen',lambda t:t=='シャープ','unsharp',['0.6','100','0']),('sharpen-strong',lambda t:t.startswith('シャープ') and '強' in t,'unsharp',['0.8','200','0']),
            ('sharpen-edges',lambda t:t.startswith('シャープ') and '輪郭だけ' in t,'unsharp',['0.6','150','15']),('blur',lambda t:t=='ぼかし','gaussian',['0.4','0']),
            ('blur-strong',lambda t:t.startswith('ぼかし') and '強' in t,'gaussian',['0.8','0']),('blur-soft',lambda t:t.startswith('ぼかし') and 'ソフト' in t,'gaussian',['5.0','65'])]
        if args.calibrate:cases=[('gaussian-'+r,None,'gaussian',[r,'0']) for r in ['0.6','1.0','2.0','5.0']]
        for label,predicate,kind,values in cases:
            preset=one(predicate) if predicate else None;expected=None
            if preset:child.send(window,0x111,preset);child.ping(window);expected=copied(label);restore(label)
            dialog=opened(*dialogs[kind]);initial=controls(dialog)
            for key,value in enumerate(values,2031):
                h=child.own(U.GetDlgItem(dialog,key));child.send(dialog,0x28,h,1);buffer=c.create_unicode_buffer(value);child.send(h,0xc,0,c.addressof(buffer));assert text(h)==value
            child.send(dialog,0x28,child.own(U.GetDlgItem(dialog,1)),1);configured=controls(dialog);finish(dialog,True);actual=copied(label+'-dialog')
            if preset:assert actual==expected,(label,'preset differs from documented dialog parameters')
            restore(label+'-dialog');report['samples'].append(dict(label=label,command=preset,kind=kind,values=values,initial_controls=initial,configured_controls=configured,preset_equals_configured_dialog=bool(preset),output_sha256=native.sha(out/(label+'-dialog.png'))))
        report['after_controls']={}
        for kind,pair in dialogs.items():
            dialog=opened(*pair);report['after_controls'][kind]=controls(dialog);finish(dialog)
        child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
        assert native.sha(source)==report['source_sha256'];report.update(passed=True,normal_close=True,exit_code=0)
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)
if __name__=='__main__':main()
