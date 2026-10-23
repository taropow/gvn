"""Compare actual official MassiGra/GVN archive external paths using owned native receivers.

The official ZIP decoder is used only inside the owned reference directory. It
is never added to GVN or distributed with these results. No global input, user
clipboard, registry registration, private artwork, or existing profile is used.
"""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,os,re,shutil,struct,sys,time,uuid,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station

ROOT=Path(__file__).resolve().parents[1]
U,K,P=native.U,native.K,native.P
for name,result,arguments in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),
    ('GetMenuItemCount',c.c_int,[P]),('GetMenuItemID',w.UINT,[P,c.c_int]),
    ('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),
    ('GetDlgCtrlID',c.c_int,[P]),('IsWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P]),
    ('RegisterClipboardFormatW',w.UINT,[w.LPCWSTR])]:native.api(U,name,result,arguments)
for name,result,arguments in [('VirtualAllocEx',P,[P,P,native.UP,w.DWORD,w.DWORD]),
    ('VirtualFreeEx',w.BOOL,[P,P,native.UP,w.DWORD]),('WriteProcessMemory',w.BOOL,[P,P,P,native.UP,P]),
    ('GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])]:native.api(K,name,result,arguments)

def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def bounded(predicate,timeout=12):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        result=predicate()
        if result:return result
        time.sleep(.003)
    raise TimeoutError('Owned archive operation timed out')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path);parser.add_argument('receiver',type=Path)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists()
    out.mkdir(parents=True);reference=out/'reference';reference.mkdir()
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip'
    assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                target=(reference/name).resolve();assert target.is_relative_to(reference)
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
    reference_exe=reference/'MassiGra.exe'
    assert native.sha(reference_exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    plugin=ROOT/'.cache/susie-reference/axzip.spi'
    assert native.sha(plugin)=='88a2ebd24ab7068119c7d54cea537b73b742d6c47b026ee23823c2e94d92cca9'
    plugins=reference/'plugins';plugins.mkdir();shutil.copyfile(plugin,plugins/'axzip.spi')
    gvn_dir=out/'gvn';gvn_dir.mkdir();gvn=gvn_dir/'GVN.exe';shutil.copyfile(args.executable,gvn)
    for directory in [reference,gvn_dir]:shutil.copyfile(args.receiver,directory/'receive.exe')
    profile=gvn_dir/'settings.ini'
    profile.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\nreopen_last=0\n',encoding='utf-16')
    image=Image.new('RGB',(3,2));image.putdata([(7,11,13),(31,59,61),(67,71,73),(79,83,89),(97,101,103),(107,109,113)])
    bmp=io.BytesIO();image.save(bmp,format='BMP');work=out/'外側 path &';work.mkdir()
    assets=[]
    for label,member in [('root','root & image.bmp'),('nested','nested folder/deeper/child & image.bmp')]:
        path=work/(label+' 日本語.zip')
        with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:z.writestr(member,bmp.getvalue())
        assets.append(dict(label=label,path=path,member=member,sha256=native.sha(path)))
    sources=out/'sources';sources.mkdir()
    for path in [Path(__file__),Path(native.__file__),ROOT/'tools/probe_gvn_performance.py']:
        shutil.copyfile(path,sources/path.name)
    report=dict(passed=False,private_station=True,user_desktop_clipboard_or_artwork_used=False,
        gvn_sha256=native.sha(gvn),receiver_sha256=native.sha(args.receiver),reference_sha256=native.sha(reference_exe),
        plugin_sha256=native.sha(plugin),plugin_used_in_reference_only=True,product_plugin_support_added=False,
        tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),
        assets=[{**v,'path':str(v['path'])} for v in assets],cases=[],parents=[])
    write(out/'prepared.json',report)
    ids={};number=0
    for item in (ROOT/'include/gvn/commands.h').read_text().split('enum Command {',1)[1].split('};',1)[0].split(','):
        match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
        if match:
            if match[2]:number=int(match[2])
            ids[match[1]]=number;number+=1
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
    assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    desktop=station();station_name,desktop_name=desktop.split('\\',1)
    token=uuid.uuid4().hex;receipts=out/'receipts';receipts.mkdir()
    child=None;window=None;engine=None;expected={};last_fields={}

    def start(executable,path=None):
        nonlocal child,window
        names=['GVN_OWNED_RECEIVER_OUTPUT','GVN_OWNED_RECEIVER_TOKEN'];old={n:os.environ.get(n) for n in names}
        os.environ[names[0]]=str(receipts);os.environ[names[1]]=token
        arguments=([path] if path else []) if engine=='reference' else ['--settings',profile,path]
        try:child=native.Child(executable,arguments,desktop)
        finally:
            for name,value in old.items():
                if value is None:os.environ.pop(name,None)
                else:os.environ[name]=value
        report['parents'].append(dict(engine=engine,pid=child.info.pid))
        cls='TF811202_MassiGra_Main' if engine=='reference' else 'GVN.ImageViewer'
        window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)==cls),None))
        child.ping(window)

    def close():
        nonlocal child
        child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0
        code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
        report['parents'][-1].update(normal_close=True,exit_code=code.value)
        child.close();child=None

    def control(dialog,cid):return child.own(U.GetDlgItem(dialog,cid))
    def text(handle):
        buf=c.create_unicode_buffer(32768);child.send(handle,0xd,len(buf),c.addressof(buf));return buf.value
    def set_text(dialog,cid,value):
        handle=control(dialog,cid);child.send(dialog,0x28,handle,1)
        buf=c.create_unicode_buffer(value);child.send(handle,0xc,0,c.addressof(buf));assert text(handle)==value
    def choice(dialog,cid,index):
        handle=control(dialog,cid);assert child.send(handle,0x14e,index)==index
        child.send(dialog,0x111,cid|(1<<16),handle)
    def check(dialog,cid,wanted):
        handle=control(dialog,cid);assert U.IsWindowEnabled(handle)
        if bool(child.send(handle,0xf0))!=bool(wanted):child.send(handle,0xf5)
        assert bool(child.send(handle,0xf0))==bool(wanted)
    def finish(dialog):
        child.post(control(dialog,1),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
    def menu_items(menu):
        items=[]
        for index in range(U.GetMenuItemCount(menu)):
            name=c.create_unicode_buffer(512);U.GetMenuStringW(menu,index,name,len(name),0x400)
            items.append(dict(name=name.value,id=U.GetMenuItemID(menu,index)))
            sub=U.GetSubMenu(menu,index)
            if sub:items+=menu_items(sub)
        return items
    def external_menu():
        menu=U.GetMenu(child.own(window));assert menu
        child.send(window,0x116,menu,0)
        def find_path(current):
            if any(U.GetMenuItemID(current,i)==40231 for i in range(U.GetMenuItemCount(current))):return []
            for i in range(U.GetMenuItemCount(current)):
                sub=U.GetSubMenu(current,i)
                if sub:
                    path=find_path(sub)
                    if path is not None:return [(sub,i)]+path
            return None
        path=find_path(menu);assert path is not None
        for sub,index in path:child.send(window,0x117,sub,index)

    def setup_reference_plugin():
        if not U.GetMenu(window):child.send(window,0x111,41008)
        commands=[v['id'] for v in menu_items(U.GetMenu(window)) if 'プラグイン設定' in v['name'].replace('&','')]
        assert len(commands)==1
        child.post(window,0x111,commands[0])
        dialog=child.wait(lambda:next((h for h in child.windows() if 'プラグイン設定' in native.window_text(h)),None))
        child.wait(lambda:U.GetDlgItem(dialog,2031) and U.GetDlgItem(dialog,2191))
        set_text(dialog,2031,'plugins');child.send(control(dialog,2142),0xf5)
        items=control(dialog,2191);child.wait(lambda:child.send(items,0x1004)==1)
        state=child.send(items,0x102c,0,0xf000)
        if state!=0x2000:
            remote=K.VirtualAllocEx(child.info.process,None,64,0x3000,4);native.check(remote);assert remote<=0xffffffff
            try:
                data=c.create_string_buffer(struct.pack('<IIIII',8,0,0,0x2000,0xf000)+bytes(44))
                native.check(K.WriteProcessMemory(child.info.process,remote,data,64,None))
                assert child.send(items,0x102b,0,remote)
            finally:native.check(K.VirtualFreeEx(child.info.process,remote,0,0x8000))
        assert child.send(items,0x102c,0,0xf000)==0x2000
        finish(dialog);report['reference_plugin_set_through_actual_ui']=True

    def configure(fields):
        if engine=='reference':
            if not U.GetMenu(window):child.send(window,0x111,41008)
            child.post(window,0x111,40231)
            dialog=child.wait(lambda:next((h for h in child.windows() if 'アプリ' in native.window_text(h) and U.GetDlgItem(h,2023)),None))
            child.wait(lambda:child.send(control(dialog,2023),0x146)==9);page=dialog
            choice(page,2023,0);mapping=dict(name=2031,command=2032,verb=2033,send=2021,show=2022,quit=2001,separator=2002,member=2003,folder=2004)
        else:
            child.post(window,0x111,ids['ConfigureExternal'])
            dialog=child.wait(lambda:next((h for h in child.windows() if U.GetDlgItem(h,1015)),None))
            page=control(dialog,1015);child.wait(lambda:U.GetDlgItem(page,420) and child.send(control(page,420),0x146)==9)
            choice(page,420,0);mapping=dict(name=460,command=421,verb=463,send=461,show=462,quit=464,separator=465,member=466,folder=467)
        def current():
            return {key:text(control(page,cid)) if key in ['name','command','verb'] else child.send(control(page,cid),0x147 if key in ['send','show'] else 0xf0) for key,cid in mapping.items()}
        if engine in last_fields:assert current()==last_fields[engine],(engine,current(),last_fields[engine])
        for key in ['name','command','verb']:set_text(page,mapping[key],fields[key])
        for key in ['send','show']:choice(page,mapping[key],fields[key])
        for key in ['quit','separator','member','folder']:check(page,mapping[key],fields[key])
        assert current()==fields,(current(),fields)
        finish(dialog);last_fields[engine]=fields.copy()
        if engine=='reference':external_menu()

    def clipboard_bytes(fmt):
        bounded(lambda:U.OpenClipboard(None))
        try:
            data=U.GetClipboardData(fmt);native.check(data);pointer=K.GlobalLock(data);native.check(pointer)
            try:return c.string_at(pointer,K.GlobalSize(data))
            finally:K.GlobalUnlock(data)
        finally:U.CloseClipboard()
    def copy_pixels():
        before=U.GetClipboardSequenceNumber();child.post(window,0x111,40025 if engine=='reference' else ids['Copy'])
        fmt=8 if engine=='reference' else U.RegisterClipboardFormatW('PNG')
        child.wait(lambda:U.GetClipboardSequenceNumber()!=before and U.IsClipboardFormatAvailable(fmt))
        raw=clipboard_bytes(fmt)
        if engine=='reference':
            header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',raw)
            assert (width,abs(height))==image.size and planes==1 and bits in [24,32] and compression in [0,3]
            offset=header+(12 if header==40 and compression==3 else 0)+struct.unpack_from('<I',raw,32)[0]*4
            stride=((width*bits+31)//32)*4;pixels=[]
            for y in range(abs(height)):
                row=offset+(y if height<0 else abs(height)-1-y)*stride
                for x in range(width):
                    b,g,r=raw[row+x*(bits//8):row+x*(bits//8)+3];pixels.append((r,g,b))
            assert pixels==list(image.getdata())
        else:
            with Image.open(io.BytesIO(raw)) as copied:assert copied.size==image.size and copied.convert('RGBA').tobytes()==image.convert('RGBA').tobytes()
        return U.GetClipboardSequenceNumber()

    try:
        engine='reference';report['phase']='reference_plugin_setup';start(reference_exe);setup_reference_plugin();close()
        for engine,executable in [('reference',reference_exe),('gvn',gvn)]:
            for asset in assets:
                report['phase']=engine+'_'+asset['label'];start(executable,asset['path'])
                basename=Path(asset['member']).name
                child.wait(lambda:basename in native.window_text(window) and ('<書庫>' if engine=='reference' else '%') in native.window_text(window));copy_pixels()
                report.setdefault('opened_documents',[]).append(dict(engine=engine,asset=asset['label'],title=native.window_text(window),all_source_pixels_equal=True))
                for label,send,member,folder,macros in [('file-off',1,0,0,False),('file-on',1,1,0,False),
                    ('folder-off',2,0,0,False),('folder-on',2,0,1,False),('macros-off',0,0,0,True),('macros-on',0,1,1,True)]:
                    receiver=executable.parent/'receive.exe'
                    command='"'+str(receiver)+'" /archive "($f)" "($d)"' if macros else str(receiver)
                    fields=dict(name='Archive probe',command=command,verb='',send=send,show=0,quit=0,separator=0,member=member,folder=folder)
                    configure(fields);before=set(receipts.glob('*.json'));sequence=U.GetClipboardSequenceNumber()
                    child.post(window,0x111,40221 if engine=='reference' else ids['ExternalBase'])
                    paths=bounded(lambda:[p for p in receipts.glob('*.json') if p not in before]);assert len(paths)==1
                    received=read(paths[0]);arguments=received['argv'][1:]
                    assert received['passed'] and received['token']==token and Path(received['module']).resolve()==receiver
                    assert received['parent_pid']==child.info.pid and received['station']==station_name and received['desktop']==desktop_name and not received['station_visible']
                    assert Path(received['cwd']).resolve()==executable.parent and received['clipboard_sequence']==sequence and U.GetClipboardSequenceNumber()==sequence
                    key=asset['label']+'_'+label
                    if engine=='reference':
                        if label=='file-off':assert arguments==[str(asset['path'])]
                        elif label=='folder-off':assert arguments==[str(asset['path'].parent)]
                        elif label=='folder-on':assert arguments==[str(asset['path'])]
                        elif label=='file-on':assert len(arguments)==1 and str(asset['path']) in arguments[0] and basename in arguments[0]
                        elif label=='macros-off':assert arguments==['/archive',str(asset['path']),str(asset['path'].parent)]
                        elif label=='macros-on':assert arguments==['/archive',expected[asset['label']+'_file-on'][0],str(asset['path'])]
                        expected[key]=arguments
                    else:assert arguments==expected[key],(key,arguments,expected[key])
                    row=dict(engine=engine,asset=asset['label'],label=label,arguments=arguments,fields=fields,
                             receipt=str(paths[0].relative_to(out)),passed=True,clipboard_unchanged=True)
                    report['cases'].append(row);child.ping(window);assert K.WaitForSingleObject(child.info.process,0)==258
                    print('PASS '+key+' '+engine,flush=True)
                copy_pixels();close()
        assert len(report['cases'])==24
        assert all(native.sha(v['path'])==v['sha256'] for v in assets)
        assert native.sha(gvn)==report['gvn_sha256'] and native.sha(reference_exe)==report['reference_sha256'] and native.sha(plugin)==report['plugin_sha256']
        report['passed']=True;report['phase']='terminal'
    except BaseException as error:report['error']=type(error).__name__+': '+str(error);raise
    finally:
        if child:child.close()
        write(out/'result.json',report);K.ReleaseMutex(mutex);K.CloseHandle(mutex)
        print('Evidence: '+str(out),flush=True)

if __name__=='__main__':main()
