"""Owned, temporary file associations -> actual normal GVN Shell verbs. No user desktop input."""
from pathlib import Path
from ctypes import wintypes as w
import argparse, ctypes as c, hashlib, json, os, re, shutil, sys, time, uuid, winreg
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import benchmark_navigation as native


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def prepare(args):
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / '.cache') and not out.exists()
    out.mkdir(parents=True)
    nonce = uuid.uuid4().hex
    viewer = out / 'viewer 日本語'
    receiver = viewer / '受信 app/receiver 日本語.exe'
    receiver.parent.mkdir(parents=True)
    exe = viewer / 'GVN.exe'
    shutil.copyfile(args.executable, exe)
    shutil.copyfile(args.receiver, receiver)
    extension, progid = '.gvn' + nonce, 'GVN.AssocProbe.' + nonce
    asset = out / ('alpha & 日本語 image' + extension)
    image = Image.new('RGBA', (3, 2))
    image.putdata([(7,11,13,0), (31,59,61,1), (67,71,73,127),
                   (79,83,89,128), (97,101,103,254), (107,109,113,255)])
    image.save(asset, format='PNG')
    profile = out / 'settings 日本語.ini'
    lines = ['[Viewer]', 'allow_multiple=1', 'reopen_last=0', 'prefetch_enabled=0',
             'confirm_unsaved=0', 'title_mode=0', '[ExternalOptions]', 'Version=1']
    for index, verb in enumerate(['', 'open', 'open', 'gvn-custom', 'gvn-broken', 'edit'], 1):
        values = dict(present=1, name='"Association '+str(index)+'"', command='""',
                      verb='"'+verb+'"', send=1, show=0, quit=int(index in (5,6)),
                      separator=0, archive_member=0, archive_folder=0)
        lines.extend(str(index)+'.'+key+'='+str(value) for key,value in values.items())
    profile.write_text('\n'.join(lines)+'\n', encoding='utf-16')
    commands = {verb: '"'+str(receiver)+'" --gvn-assoc-'+label+' "%1"'
                for verb,label in [('open','open'), ('edit','edit'), ('gvn-custom','custom')]}
    commands['gvn-broken'] = '"'+str(viewer/'missing owned receiver.exe')+'" "%1"'
    plan = dict(directory=str(out), nonce=nonce, extension=extension, progid=progid,
                registry_roots=['Software\\Classes\\'+extension, 'Software\\Classes\\'+progid],
                commands=commands, executable=str(exe), receiver=str(receiver), asset=str(asset), profile=str(profile),
                executable_sha256=native.sha(exe), receiver_sha256=native.sha(receiver), asset_sha256=native.sha(asset),
                initial_profile_sha256=native.sha(profile), controller_sha256=native.sha(__file__),
                helper_sha256=native.sha(native.__file__), command_header_sha256=native.sha(ROOT/'include/gvn/commands.h'),
                existing_associations_modified=False, user_desktop_or_clipboard_used=False)
    write(out/'plan.json', plan)
    print(json.dumps(dict(prepared=True, plan=str(out/'plan.json'), registry_roots=plan['registry_roots']), ensure_ascii=True))


def association_snapshot():
    def tree(key, path):
        try:
            with winreg.OpenKey(key, path, 0, winreg.KEY_READ) as handle:
                children, values, _ = winreg.QueryInfoKey(handle)
                result = dict(values=[], children={})
                for i in range(values):
                    name, value, kind = winreg.EnumValue(handle, i)
                    result['values'].append((name, value.hex() if isinstance(value, bytes) else value, kind))
                for i in range(children):
                    name = winreg.EnumKey(handle, i)
                    result['children'][name] = tree(handle, name)
                return result
        except FileNotFoundError:
            return 'absent'
        except PermissionError:
            return 'unreadable'
    states = {}
    for ext in ['.png','.jpg','.bmp','.gif','.apng','.psd']:
        for prefix in ['Software\\Classes\\', 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\']:
            key = prefix + ext
            states[key] = hashlib.sha256(json.dumps(tree(winreg.HKEY_CURRENT_USER, key), sort_keys=True).encode()).hexdigest()
    return states


class OwnedAssociation:
    def __init__(self, plan):
        self.plan, self.created = plan, []
        self.advapi = c.WinDLL('advapi32', use_last_error=True)
        native.api(self.advapi, 'RegCreateKeyExW', w.LONG,
                   [native.P,w.LPCWSTR,w.DWORD,w.LPWSTR,w.DWORD,w.DWORD,native.P,c.POINTER(native.P),c.POINTER(w.DWORD)])

    def __enter__(self):
        try:
            for path in self.plan['registry_roots']:
                handle, disposition = native.P(), w.DWORD()
                status = self.advapi.RegCreateKeyExW(native.P(-2147483647), path, 0, None, 0, winreg.KEY_ALL_ACCESS,
                                                     None, c.byref(handle), c.byref(disposition))
                if status: raise c.WinError(status)
                try:
                    assert disposition.value == 1, 'Refusing to alter an existing registry root'
                    try:
                        winreg.SetValueEx(handle.value, 'GVNProbeOwner', 0, winreg.REG_SZ, self.plan['nonce'])
                    except BaseException:
                        # RegCreateKeyEx confirmed this exact UUID root was newly created and empty.
                        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
                        raise
                    self.created.append(path)
                finally:
                    winreg.CloseKey(handle.value)
            self.value(self.plan['registry_roots'][0], '', self.plan['progid'])
            base = self.plan['registry_roots'][1]
            self.value(base, '', 'GVN temporary owned association')
            self.value(base+'\\shell', '', 'open')
            for verb, command in self.plan['commands'].items():
                self.value(base+'\\shell\\'+verb+'\\command', '', command)
            return self
        except BaseException:
            self.__exit__(None,None,None)
            raise

    @staticmethod
    def value(path, name, value):
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_WRITE) as key:
            winreg.SetValueEx(key, name, 0, winreg.REG_SZ, value)

    def __exit__(self, *_):
        for path in reversed(self.created):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_READ) as key:
                assert winreg.QueryValueEx(key, 'GVNProbeOwner')[0] == self.plan['nonce'], 'Registry ownership changed'
            self.remove(path)
        self.created.clear()

    @classmethod
    def remove(cls, path):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_READ) as key:
            children = [winreg.EnumKey(key,i) for i in range(winreg.QueryInfoKey(key)[0])]
        for child in children: cls.remove(path+'\\'+child)
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)


def execute(args):
    plan = read(args.plan.resolve()); out = Path(plan['directory'])
    assert args.allow_owned_association_registration and out.is_relative_to(ROOT/'.cache')
    nonce = plan['nonce']; assert re.fullmatch('[0-9a-f]{32}', nonce)
    assert plan['extension'] == '.gvn'+nonce and plan['progid'] == 'GVN.AssocProbe.'+nonce
    assert plan['registry_roots'] == ['Software\\Classes\\'+plan['extension'], 'Software\\Classes\\'+plan['progid']]
    exe, receiver, asset, profile = [Path(plan[k]) for k in ['executable','receiver','asset','profile']]
    assert all(p.resolve().is_relative_to(out.resolve()) for p in [exe,receiver,asset,profile])
    expected_commands={verb:'"'+str(receiver)+'" --gvn-assoc-'+label+' "%1"'
                       for verb,label in [('open','open'),('edit','edit'),('gvn-custom','custom')]}
    expected_commands['gvn-broken']='"'+str(exe.parent/'missing owned receiver.exe')+'" "%1"'
    assert plan['commands']==expected_commands
    assert native.sha(__file__) == plan['controller_sha256'] and native.sha(native.__file__) == plan['helper_sha256']
    assert native.sha(exe)==plan['executable_sha256'] and native.sha(receiver)==plan['receiver_sha256']
    assert native.sha(asset)==plan['asset_sha256'] and native.sha(profile)==plan['initial_profile_sha256']
    assert native.sha(ROOT/'include/gvn/commands.h') == plan['command_header_sha256']
    assert not (out/'result.json').exists()
    receipts=out/'receipts'; receipts.mkdir()
    snapshot=association_snapshot(); write(out/'existing-association-hashes-before.json',snapshot)
    U,K=native.U,native.K
    native.api(U,'IsWindow',w.BOOL,[native.P]); native.api(U,'RegisterClipboardFormatW',w.UINT,[w.LPCWSTR])
    native.api(U,'GetDlgCtrlID',c.c_int,[native.P])
    native.api(K,'GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)])
    shell=c.WinDLL('shell32',use_last_error=True)
    native.api(shell,'SHChangeNotify',None,[c.c_long,w.UINT,native.P,native.P])
    mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard'); native.check(mutex)
    assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    station=U.CreateWindowStationW(None,0,0x37f,None); native.check(station)
    flags=(w.DWORD*3)(); needed=w.DWORD()
    native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed))); assert not flags[2]&1
    name=c.create_unicode_buffer(512); native.check(U.GetUserObjectInformationW(station,2,name,c.sizeof(name),c.byref(needed)))
    native.check(U.SetProcessWindowStation(station))
    desktop_name='GVN-Association-'+uuid.uuid4().hex
    native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None); native.check(native.desktop)
    native.check(U.SetThreadDesktop(native.desktop)); desktop=name.value+'\\'+desktop_name
    ids={}; number=0
    for item in (ROOT/'include/gvn/commands.h').read_text().split('enum Command {',1)[1].split('};',1)[0].split(','):
        match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
        if match:
            label,explicit=match.groups()
            if explicit: number=int(explicit)
            ids[label]=number; number+=1
    children=[]; parent=None; window=None
    result=dict(passed=False,plan_sha256=native.sha(args.plan),executable_sha256=plan['executable_sha256'],
                receiver_sha256=plan['receiver_sha256'],private_station=True,user_desktop_or_clipboard_used=False,
                registry_roots=plan['registry_roots'],cases=[],parents=[])
    original=Image.open(asset).convert('RGBA')

    def bounded(predicate,timeout=12):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            value=predicate()
            if value: return value
            time.sleep(.003)
        raise TimeoutError('Owned association operation timed out')

    def start():
        nonlocal parent,window
        old={k:os.environ.get(k) for k in ['GVN_OWNED_RECEIVER_OUTPUT','GVN_OWNED_RECEIVER_TOKEN']}
        os.environ['GVN_OWNED_RECEIVER_OUTPUT']=str(receipts); os.environ['GVN_OWNED_RECEIVER_TOKEN']=nonce
        try: parent=native.Child(exe,['--settings',profile,asset],desktop)
        finally:
            for key,value in old.items():
                if value is None: os.environ.pop(key,None)
                else: os.environ[key]=value
        children.append(parent); result['parents'].append(parent.info.pid)
        window=parent.wait(lambda:next((h for h in parent.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None))
        parent.wait(lambda:asset.name in native.window_text(window) and '%' in native.window_text(window)); parent.ping(window)

    def close(expected_exit_code=0):
        parent.post(window,0x10); assert K.WaitForSingleObject(parent.info.process,10000)==0
        code=w.DWORD(); native.check(K.GetExitCodeProcess(parent.info.process,c.byref(code)))
        result.setdefault('closed_parents',[]).append(dict(pid=parent.info.pid,exit_code=code.value,expected_exit_code=expected_exit_code))
        assert code.value==expected_exit_code
        parent.close(); parent.info.process=None

    def action_ui(value=None):
        parent.post(window,0x111,ids['ConfigureExternal'])
        dialog=parent.wait(lambda:next((h for h in parent.windows() if native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1015)),None))
        page=parent.own(U.GetDlgItem(dialog,1015))
        parent.wait(lambda:U.GetDlgItem(page,420) and parent.send(U.GetDlgItem(page,420),0x146)==9)
        selector=parent.own(U.GetDlgItem(page,420)); assert parent.send(selector,0x14e,2)==2
        parent.send(page,0x111,420|(1<<16),selector)
        control=parent.own(U.GetDlgItem(page,463))
        def text():
            buf=c.create_unicode_buffer(1024); parent.send(control,0xd,len(buf),c.addressof(buf)); return buf.value
        before=text()
        if value is not None:
            parent.send(dialog,0x28,control,1); buf=c.create_unicode_buffer(value)
            parent.send(control,0xc,0,c.addressof(buf)); assert text()==value
        parent.post(U.GetDlgItem(dialog,1 if value is not None else 2),0xf5)
        parent.wait(lambda:not U.IsWindow(dialog)); parent.ping(window)
        return before

    def copy_png():
        sequence=U.GetClipboardSequenceNumber(); parent.post(window,0x111,ids['Copy'])
        parent.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(U.RegisterClipboardFormatW('PNG')))
        bounded(lambda:U.OpenClipboard(None))
        try:
            data=U.GetClipboardData(U.RegisterClipboardFormatW('PNG')); native.check(data)
            ptr=K.GlobalLock(data); native.check(ptr)
            try: raw=c.string_at(ptr,K.GlobalSize(data))
            finally: K.GlobalUnlock(data)
        finally: U.CloseClipboard()
        import io
        with Image.open(io.BytesIO(raw)) as decoded:
            assert decoded.size==original.size and decoded.convert('RGBA').tobytes()==original.tobytes()
        return U.GetClipboardSequenceNumber()

    try:
        with OwnedAssociation(plan):
            write(out/'registration-active.json',dict(registry_roots=plan['registry_roots'],owner=nonce))
            shell.SHChangeNotify(0x08000000,0,None,None)
            result['phase']='settings_edit'
            print('Owned associations registered; checking real settings edit',flush=True)
            start(); assert action_ui('edit')=='open'
            assert '3.verb="edit"' in profile.read_text(encoding='utf-16')
            close(); start(); assert action_ui()=='edit'
            result['actual_settings_edit_saved_and_reloaded']=True
            for slot,label in [(0,'default'),(1,'open'),(2,'edit'),(3,'custom'),(4,'broken'),(5,'edit-quit')]:
                result['phase']=label
                before=set(receipts.glob('*.json')); sequence=copy_png()
                parent.post(window,0x111,ids['ExternalBase']+slot)
                if label=='broken':
                    dialog=parent.wait(lambda:next((h for h in parent.windows() if native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1)),None))
                    def failure_contents():
                        texts=[]; controls=[]
                        @native.Callback
                        def text_child(h,_):
                            controls.append(dict(hwnd=h,id=U.GetDlgCtrlID(h),class_name=native.window_text(h,True),text=native.window_text(h)))
                            if native.window_text(h,True)=='Static':
                                buf=c.create_unicode_buffer(4096); parent.send(h,0xd,len(buf),c.addressof(buf)); texts.append(buf.value)
                            return True
                        U.EnumChildWindows(dialog,text_child,0)
                        result['failure_dialog']=dict(title=native.window_text(dialog),texts=texts,controls=controls)
                        return texts if 'Cannot launch external application' in '\n'.join(texts) else None
                    # The first button can exist before the message's Static controls are created.
                    texts=bounded(failure_contents)
                    assert 'Cannot launch external application' in '\n'.join(texts)
                    buttons=[item for item in result['failure_dialog']['controls'] if item['class_name']=='Button' and item['text']=='OK']
                    assert len(buttons)==1
                    parent.post(buttons[0]['hwnd'],0xf5); parent.wait(lambda:not U.IsWindow(dialog)); parent.ping(window)
                    assert K.WaitForSingleObject(parent.info.process,0)==258 and set(receipts.glob('*.json'))==before
                    assert U.GetClipboardSequenceNumber()==sequence
                    result['cases'].append(dict(label=label,passed=True,actual_error_dialog=True,quit_requested_but_parent_remains=True))
                    # report_exception intentionally remembers an error for the process exit status.
                    # Verify that status, then exercise successful exit on a clean sender as well.
                    close(expected_exit_code=1); start()
                else:
                    paths=bounded(lambda:[p for p in receipts.glob('*.json') if p not in before]); assert len(paths)==1
                    receipt=read(paths[0]); marker='edit' if label=='edit-quit' else 'open' if label=='default' else label
                    assert receipt['passed'] and receipt['token']==nonce and Path(receipt['module']).resolve()==receiver
                    assert receipt['station']==name.value and receipt['desktop']==desktop_name and not receipt['station_visible']
                    assert receipt['argv'][1:]==['--gvn-assoc-'+marker,str(asset)]
                    assert Path(receipt['cwd']).resolve()==exe.parent
                    assert receipt['clipboard_sequence']==sequence
                    data=next(f for f in receipt['clipboard_formats'] if f['name']=='PNG')
                    with Image.open(receipts/data['file']) as received:
                        assert received.size==original.size and received.convert('RGBA').tobytes()==original.tobytes()
                    if label=='edit-quit':
                        assert K.WaitForSingleObject(parent.info.process,10000)==0
                        code=w.DWORD(); native.check(K.GetExitCodeProcess(parent.info.process,c.byref(code))); assert code.value==0
                        result['successful_quit_exit_code']=code.value
                        parent.close(); parent.info.process=None
                    else:
                        assert K.WaitForSingleObject(parent.info.process,0)==258; parent.ping(window)
                    result['cases'].append(dict(label=label,passed=True,receipt=str(paths[0].relative_to(out)),arguments=receipt['argv'][1:],alpha_equal=True))
                print('PASS actual association '+label,flush=True)
            assert native.sha(asset)==plan['asset_sha256']
        result['owned_registry_roots_removed']=True
        for path in plan['registry_roots']:
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path): pass
            except FileNotFoundError: continue
            raise AssertionError('Owned registry root remains')
        result['passed']=True; result['phase']='terminal'
    except BaseException as error:
        result['error']=type(error).__name__+': '+str(error)
        raise
    finally:
        for child in reversed(children):
            if child.info.process: child.close(); child.info.process=None
        shell.SHChangeNotify(0x08000000,0,None,None)
        result['existing_association_hashes_unchanged']=association_snapshot()==snapshot
        def absent(path):
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path): return False
            except FileNotFoundError: return True
        result['owned_registry_roots_removed']=all(absent(path) for path in plan['registry_roots'])
        assert native.sha(exe)==plan['executable_sha256'] and native.sha(receiver)==plan['receiver_sha256']
        write(out/'result.json',result); K.ReleaseMutex(mutex); K.CloseHandle(mutex)
        print('Evidence: '+str(out),flush=True)
    assert result['passed'] and result['existing_association_hashes_unchanged']


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); commands=parser.add_subparsers(dest='mode',required=True)
    p=commands.add_parser('prepare'); p.add_argument('executable',type=Path); p.add_argument('receiver',type=Path); p.add_argument('--output',type=Path,required=True)
    p=commands.add_parser('run'); p.add_argument('plan',type=Path); p.add_argument('--allow-owned-association-registration',action='store_true')
    args=parser.parse_args(); prepare(args) if args.mode=='prepare' else execute(args)
