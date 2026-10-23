"""Guarded, opt-in CSP receiving test. Prepare mode never touches windows or clipboard.

Execution requires a reviewed plan hash and explicit desktop/clipboard/shortcut consent.
CSP documents are identified by a random filename through local Windows OCR; unknown
tabs are never edited. Test documents remain open for review; existing files are not saved.
"""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,hashlib,io,json,os,re,secrets,subprocess,time
from PIL import Image,ImageDraw
import benchmark_navigation as native

ROOT=Path(__file__).resolve().parents[1]
U,K,P,UP,SP=native.U,native.K,native.P,native.UP,native.SP
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def write(p,value):Path(p).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def fixture(path,phase=0):
    image=Image.new('RGBA',(64,48));pixels=[]
    colors=[(255,0,0),(0,255,0),(0,0,255),(255,255,0),(255,0,255),(0,255,255)]
    alphas=[0,1,63,127,128,254,255]
    for y in range(48):
        for x in range(64):
            alpha=alphas[(x//8+y//8+phase)%7];rgb=colors[(x//8+2*(y//8)+phase)%6]
            pixels.append((*rgb,alpha) if alpha else (0,0,0,0))
    image.putdata(pixels)
    for x,y in [(0,0),(63,0),(0,47),(63,47)]:image.putpixel((x,y),(255,255,255,255))
    image.save(path,dpi=(96,96));return image

def prepare(directory,checkpoint):
    directory=directory.resolve();assert directory.is_relative_to((ROOT/'.cache').resolve()) and not directory.exists()
    directory.mkdir(parents=True)
    state=read(checkpoint);identity=read(ROOT/state['csp_process_metadata'])
    assert identity.get('process_count',1)==1 and identity['read_only'] and not identity['windows_or_clipboard_modified']
    alphabet='ACDEFHJKMNPWXY'
    source_name='GVN'+''.join(secrets.choice(alphabet) for _ in range(6))
    target_name='GVN'+''.join(secrets.choice(alphabet) for _ in range(6))
    source=directory/(source_name+'.png');target=directory/(target_name+'.png')
    image=fixture(source);Image.new('RGBA',image.size,(0,0,0,0)).save(target,dpi=(96,96))
    build=Path(state['build']);gvn=ROOT/f"dist/GVN-{state['current_distribution']}-light/GVN.exe";ocr=build/'Release/gvn_csp_ocr.exe'
    assert gvn.is_file() and ocr.is_file()
    plan=dict(version=1,directory=str(directory),source=str(source),target=str(target),source_sha256=sha(source),target_sha256=sha(target),
              gvn=str(gvn),gvn_sha256=sha(gvn),ocr=str(ocr),ocr_sha256=sha(ocr),controller_sha256=sha(__file__),identity=identity,
              command_header_sha256=sha(ROOT/'include/gvn/commands.h'),native_helper_sha256=sha(ROOT/'tools/benchmark_navigation.py'),
              protected_profile={p:v for p,v in read(ROOT/state['current_distribution_protection']).items() if Path(p).name=='settings.ini'},
              required_default_shortcuts=['Ctrl+O','Ctrl+V','Ctrl+C'],actions=['Open only the generated transparent PNG in CSP',
              'Copy generated alpha image from an owned normal GVN process','Select the exact test tab by local OCR before every CSP edit',
              'Paste into test document,copy its current layer,verify all RGBA through normal GVN paste/copy'],
              side_effects=['Temporarily changes foreground window and cursor','Overwrites shared clipboard with test data',
                            'Leaves the generated test document open for review;does not save or close existing CSP works'],
              computer_use_used=False,execution_authorized=False,ole_drop_test_included=False)
    write(directory/'plan.json',plan)
    print(json.dumps(dict(prepared=True,plan=str(directory/'plan.json'),plan_sha256=sha(directory/'plan.json'),execution_authorized=False,desktop_or_clipboard_touched=False),ensure_ascii=False),flush=True)

class POINT(c.Structure):_fields_=[('x',w.LONG),('y',w.LONG)]
class Mouse(c.Structure):_fields_=[('dx',w.LONG),('dy',w.LONG),('data',w.DWORD),('flags',w.DWORD),('time',w.DWORD),('extra',UP)]
class Keyboard(c.Structure):_fields_=[('vk',w.WORD),('scan',w.WORD),('flags',w.DWORD),('time',w.DWORD),('extra',UP)]
class InputUnion(c.Union):_fields_=[('mouse',Mouse),('key',Keyboard)]
class Input(c.Structure):_fields_=[('type',w.DWORD),('value',InputUnion)]
class LastInput(c.Structure):_fields_=[('size',w.UINT),('time',w.DWORD)]
class GuiThreadInfo(c.Structure):
    _fields_=[('size',w.DWORD),('flags',w.DWORD),('active',P),('focus',P),('capture',P),('menu',P),('move',P),('caret',P),('caret_rect',w.RECT)]

def configure():
    native.api(U,'EnumWindows',w.BOOL,[native.Callback,SP])
    for name,result,args in [('GetForegroundWindow',P,[]),('SetForegroundWindow',w.BOOL,[P]),('IsWindowEnabled',w.BOOL,[P]),
      ('GetWindow',P,[P,w.UINT]),('GetAncestor',P,[P,w.UINT]),('GetWindowLongPtrW',SP,[P,c.c_int]),('GetDlgCtrlID',c.c_int,[P]),
      ('AttachThreadInput',w.BOOL,[w.DWORD,w.DWORD,w.BOOL]),('GetCursorPos',w.BOOL,[c.POINTER(POINT)]),('WindowFromPoint',P,[POINT]),
      ('GetLastInputInfo',w.BOOL,[c.POINTER(LastInput)]),('GetAsyncKeyState',w.SHORT,[c.c_int]),('GetSystemMetrics',c.c_int,[c.c_int]),
      ('GetGUIThreadInfo',w.BOOL,[w.DWORD,c.POINTER(GuiThreadInfo)]),
      ('SendInput',w.UINT,[w.UINT,c.POINTER(Input),c.c_int]),('GetClipboardOwner',P,[]),('RegisterClipboardFormatW',w.UINT,[w.LPCWSTR]),
      ('SetProcessDpiAwarenessContext',w.BOOL,[P]),('IsWindowVisible',w.BOOL,[P])]:native.api(U,name,result,args)
    native.api(K,'GetCurrentThreadId',w.DWORD,[])
    native.api(K,'OpenProcess',P,[w.DWORD,w.BOOL,w.DWORD]);native.api(K,'QueryFullProcessImageNameW',w.BOOL,[P,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)])

def pid_of(window):
    pid=w.DWORD();U.GetWindowThreadProcessId(window,c.byref(pid));return pid.value
def wait_for(predicate,label,seconds=10):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        result=predicate()
        if result:return result
        time.sleep(.03)
    raise RuntimeError('Timed out: '+label)
def windows(pid):
    result=[]
    @native.Callback
    def visit(h,_):
        if pid_of(h)==pid:result.append(h)
        return True
    U.EnumWindows(visit,0);return result
def send(h,pid,message,wp=0,lp=0):
    assert pid_of(h)==pid,'Foreign window rejected'
    value=UP();native.check(U.SendMessageTimeoutW(h,message,wp,lp,2,5000,c.byref(value)));return value.value
def current_desktop():
    native.api(U,'GetProcessWindowStation',P,[]);native.api(U,'GetThreadDesktop',P,[w.DWORD])
    def name(handle):
        buf=c.create_unicode_buffer(512);needed=w.DWORD();native.check(U.GetUserObjectInformationW(handle,2,buf,c.sizeof(buf),c.byref(needed)));return buf.value
    return name(U.GetProcessWindowStation())+'\\'+name(U.GetThreadDesktop(K.GetCurrentThreadId()))

class Controller:
    def __init__(self,plan):
        self.plan=plan;self.directory=Path(plan['directory']);self.pid=plan['identity']['pid'];self.viewer=None;self.viewer_window=None
        self.report=dict(passed=False,computer_use_used=False,approved_plan=True,events=[],existing_csp_works_saved=False,ole_drop_tested=False)
        self.input_tick=self.last_input();self.main=None;self.picker_window=None;self.original_foreground=U.GetForegroundWindow();self.cursor=POINT();U.GetCursorPos(c.byref(self.cursor))
    def last_input(self):
        value=LastInput(c.sizeof(LastInput),0);native.check(U.GetLastInputInfo(c.byref(value)));return value.time
    def quiet(self):
        assert self.last_input()==self.input_tick,'User input detected;stopped before next action'
        assert not any(U.GetAsyncKeyState(key)&0x8000 for key in [1,2,4,5,6,0x10,0x11,0x12]),'Input key/button is held'
    def arm_input_guard(self):
        # Hashing the installed CSP binary can take time. Begin the guarded input
        # interval only after that read-only setup and a short idle interval.
        deadline=time.monotonic()+10;tick=self.last_input();stable=time.monotonic()
        while time.monotonic()<deadline:
            now=self.last_input()
            if now!=tick:tick=now;stable=time.monotonic()
            if time.monotonic()-stable>=1:
                self.input_tick=tick;self.quiet();return
            time.sleep(.03)
        raise RuntimeError('Desktop input stayed busy;no CSP action sent')
    def event(self,name,**fields):
        self.report['events'].append(dict(name=name,**fields));write(self.directory/'run.json',self.report);print(name,flush=True)
    def focus(self,window):
        self.quiet();assert pid_of(window) in [self.pid,self.viewer.pid if self.viewer else 0]
        if U.GetForegroundWindow()!=window:
            foreground=U.GetForegroundWindow();thread=U.GetWindowThreadProcessId(foreground,None)
            own=K.GetCurrentThreadId();attached=bool(thread and thread!=own and U.AttachThreadInput(own,thread,True))
            try:U.SetForegroundWindow(window)
            finally:
                if attached:U.AttachThreadInput(own,thread,False)
            wait_for(lambda:U.GetForegroundWindow()==window,'expected foreground',3)
        time.sleep(.12);self.input_tick=self.last_input()
    def inject(self,items):
        self.quiet();values=(Input*len(items))(*items)
        native.check(U.SendInput(len(items),values,c.sizeof(Input))==len(items));time.sleep(.12);self.input_tick=self.last_input()
    def chord(self,key):
        assert U.GetForegroundWindow()==self.main and pid_of(self.main)==self.pid and U.IsWindowEnabled(self.main)
        self.inject([Input(1,InputUnion(key=Keyboard(vk,0,flags,0,0x47564e46))) for vk,flags in [(0x11,0),(ord(key),0),(ord(key),2),(0x11,2)]])
    def click(self,x,y):
        point=POINT(x,y);assert pid_of(U.WindowFromPoint(point))==self.pid,'OCR hit is not in CSP'
        vx,vy,vw,vh=[U.GetSystemMetrics(i) for i in [76,77,78,79]]
        dx,dy=round((x-vx)*65535/max(1,vw-1)),round((y-vy)*65535/max(1,vh-1))
        self.inject([Input(0,InputUnion(mouse=Mouse(dx,dy,0,0xc001,0,0x47564e46))),Input(0,InputUnion(mouse=Mouse(0,0,0,2,0,0x47564e46))),Input(0,InputUnion(mouse=Mouse(0,0,0,4,0,0x47564e46)))])
    def capture_tile(self,x,width,height,path):
        G=c.WinDLL('gdi32',use_last_error=True)
        for n,r,a in [('CreateCompatibleDC',P,[P]),('CreateDIBSection',P,[P,P,w.UINT,c.POINTER(P),P,w.DWORD]),('SelectObject',P,[P,P]),('BitBlt',w.BOOL,[P,c.c_int,c.c_int,c.c_int,c.c_int,P,c.c_int,c.c_int,w.DWORD]),('GdiFlush',w.BOOL,[]),('DeleteObject',w.BOOL,[P]),('DeleteDC',w.BOOL,[P])]:native.api(G,n,r,a)
        native.api(U,'GetWindowDC',P,[P]);native.api(U,'ReleaseDC',c.c_int,[P,P])
        dc=U.GetWindowDC(self.main);native.check(dc);memory=G.CreateCompatibleDC(dc);native.check(memory)
        import struct
        info=c.create_string_buffer(struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,0,0,0,0,0));bits=P()
        bitmap=G.CreateDIBSection(memory,info,0,c.byref(bits),None,0);native.check(bitmap and bits);old=G.SelectObject(memory,bitmap)
        try:
            native.check(G.BitBlt(memory,0,0,width,height,dc,x,0,0x40cc0020));G.GdiFlush()
            image=Image.frombytes('RGB',(width,height),c.string_at(bits,width*height*4),'raw','BGRX')
            image.resize((width*2,height*2)).save(path)
        finally:G.SelectObject(memory,old);G.DeleteObject(bitmap);G.DeleteDC(memory);U.ReleaseDC(self.main,dc)
    def test_tab(self):
        self.focus(self.main);assert U.IsWindowEnabled(self.main)
        token=Path(self.plan['target']).stem
        if re.search(r'(?<![A-Za-z0-9])'+re.escape(token)+r'(?![A-Za-z0-9])',native.window_text(self.main)):
            self.event('test_document_active_by_exact_window_title',token=token);return
        rc=w.RECT();native.check(U.GetWindowRect(self.main,c.byref(rc)));width,height=rc.right-rc.left,min(320,rc.bottom-rc.top)
        matches=[];tile=self.directory/'transient-ui-tile.png'
        try:
            for x in range(0,width,1100):
                self.quiet();tile_width=min(1200,width-x);self.capture_tile(x,tile_width,height,tile)
                p=subprocess.run([self.plan['ocr'],str(tile),token],capture_output=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
                assert p.returncode==0,'Local OCR failed'
                for a,b,cw,ch in json.loads(p.stdout)['matches']:matches.append((rc.left+x+int((a+cw/2)/2),rc.top+int((b+ch/2)/2)))
        finally:
            if tile.exists():tile.unlink() # No CSP pixels or other OCR text retained.
        assert matches,'Known test filename not detected;no CSP edit sent'
        now=w.RECT();U.GetWindowRect(self.main,c.byref(now));assert bytes(now)==bytes(rc),'CSP moved during document guard'
        x,y=sorted(matches,key=lambda p:(p[1],p[0]))[0];self.click(x,y)
        assert U.GetForegroundWindow()==self.main
        self.event('test_document_selected_by_fixed_token',token=token)
    def owner(self):return pid_of(U.GetClipboardOwner())
    def clipboard_png(self):
        fmt=U.RegisterClipboardFormatW('PNG');native.check(U.OpenClipboard(None))
        try:
            h=U.GetClipboardData(fmt);size=K.GlobalSize(h);assert h and 0<size<8*1024*1024
            pointer=K.GlobalLock(h);native.check(pointer)
            try:return c.string_at(pointer,size)
            finally:K.GlobalUnlock(h)
        finally:U.CloseClipboard()
    def viewer_copy(self):
        sequence=U.GetClipboardSequenceNumber();send(self.viewer_window,self.viewer.pid,0x111,self.commands['Copy'])
        wait_for(lambda:U.GetClipboardSequenceNumber()!=sequence and self.owner()==self.viewer.pid,'owned GVN clipboard copy')
        return self.clipboard_png()
    def run(self):
        self.quiet();identity=self.plan['identity'];handle=K.OpenProcess(0x1000,False,self.pid);native.check(handle)
        try:
            buf=c.create_unicode_buffer(32768);size=w.DWORD(len(buf));native.check(K.QueryFullProcessImageNameW(handle,0,buf,c.byref(size)))
            assert buf.value.casefold()==identity['path'].casefold() and sha(buf.value)==identity['executable_sha256']
        finally:K.CloseHandle(handle)
        self.main=next(h for h in windows(self.pid) if native.window_text(h,True)=='742DEA58-ED6B-4402-BC11-20DFC6D08040' and U.GetWindowLongPtrW(h,-16)&0x10000000)
        assert U.IsWindowEnabled(self.main),'CSP has an existing modal dialog'
        self.arm_input_guard()
        self.focus(self.main);self.chord('O')
        def picker():
            return next((h for h in windows(self.pid) if U.GetWindow(h,4)==self.main and native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1) and (U.GetDlgItem(h,0x480) or U.GetDlgItem(h,0x47c))),None)
        dialog=wait_for(picker,'owned CSP file picker');self.picker_window=dialog
        def filename_control():
            self.quiet();combo=U.GetDlgItem(dialog,0x47c)
            try:edit=send(combo,self.pid,0x407) if combo and native.window_text(combo,True)=='ComboBoxEx32' else U.GetDlgItem(dialog,0x480)
            except (AssertionError,OSError):return None
            return edit if edit and pid_of(edit)==self.pid and native.window_text(edit,True)=='Edit' and U.IsWindowVisible(edit) and U.IsWindowEnabled(edit) else None
        edit=wait_for(filename_control,'visible filename Edit control')
        send(dialog,self.pid,0x28,edit,1)
        edit=wait_for(filename_control,'filename Edit control after focus initialization')
        assert U.GetForegroundWindow()==dialog,'CSP file picker is not foreground'
        info=GuiThreadInfo();info.size=c.sizeof(info)
        native.check(U.GetGUIThreadInfo(U.GetWindowThreadProcessId(dialog,None),c.byref(info)))
        assert info.focus==edit,'CSP filename field does not have keyboard focus'
        # Common dialogs need real edit notifications; WM_SETTEXT alone can close
        # the picker without opening the filename shown in its nested ComboBoxEx.
        self.inject([Input(1,InputUnion(key=Keyboard(vk,0,flags,0,0x47564e46))) for vk,flags in [(0x11,0),(ord('A'),0),(ord('A'),2),(0x11,2)]])
        self.inject([Input(1,InputUnion(key=Keyboard(0,ord(char),flags,0,0x47564e46))) for char in self.plan['target'] for flags in (4,6)])
        def filename_complete():
            if U.GetForegroundWindow()!=dialog:return False
            current=filename_control()
            if not current:return False
            observed=c.create_unicode_buffer(32768);send(current,self.pid,0xd,len(observed),c.addressof(observed))
            return observed.value==self.plan['target']
        wait_for(filename_complete,'complete generated filename after queued key input',15)
        self.event('generated_file_path_set',dialog_owned_by_csp=True,edit_class='Edit')
        button=U.GetDlgItem(dialog,1);assert button and pid_of(button)==self.pid and U.IsWindowEnabled(button)
        self.inject([Input(1,InputUnion(key=Keyboard(0x0d,0,flags,0,0x47564e46))) for flags in (0,2)])
        wait_for(lambda:not picker() and U.IsWindowEnabled(self.main),'CSP generated file opened');time.sleep(.4)
        self.picker_window=None
        self.event('generated_file_picker_closed',document_identity_not_yet_verified=True)
        self.test_tab() # Must succeed before touching shared clipboard or pasting.
        tmp=self.directory/'viewer-temp';tmp.mkdir();config=self.directory/'viewer.ini'
        config.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\nreopen_last=0\ntitle_mode=0\n',encoding='utf-16')
        startup=subprocess.STARTUPINFO();startup.lpDesktop=current_desktop();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=4
        self.viewer=subprocess.Popen([self.plan['gvn'],'--settings',str(config),self.plan['source']],env={**os.environ,'TEMP':str(tmp),'TMP':str(tmp)},startupinfo=startup)
        self.viewer_window=wait_for(lambda:next((h for h in windows(self.viewer.pid) if native.window_text(h,True)=='GVN.ImageViewer' and Path(self.plan['source']).name in native.window_text(h) and '%' in native.window_text(h)),None),'owned normal GVN source')
        self.commands={};value=0
        for item in (ROOT/'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {',1)[1].split('};',1)[0].split(','):
            match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
            if match:
                if match[2]:value=int(match[2])
                self.commands[match[1]]=value;value+=1
        expected=Image.open(self.plan['source']).convert('RGBA')
        data=self.viewer_copy()
        with Image.open(io.BytesIO(data)) as copied:assert copied.convert('RGBA').tobytes()==expected.tobytes()
        self.event('normal_gvn_source_copy_rgba_exact')
        self.test_tab();self.chord('V');time.sleep(.25)
        self.test_tab();sequence=U.GetClipboardSequenceNumber();self.chord('C')
        wait_for(lambda:U.GetClipboardSequenceNumber()!=sequence and self.owner()==self.pid,'CSP test layer copy')
        self.event('actual_csp_layer_copied')
        csp_sequence=U.GetClipboardSequenceNumber()
        send(self.viewer_window,self.viewer.pid,0x111,self.commands['CloseImage'])
        wait_for(lambda:'%' not in native.window_text(self.viewer_window),'source GVN image cleared before receiver check')
        assert U.GetClipboardSequenceNumber()==csp_sequence and self.owner()==self.pid,'CSP clipboard changed before receive'
        send(self.viewer_window,self.viewer.pid,0x111,self.commands['Paste']);time.sleep(.15)
        data=self.viewer_copy()
        with Image.open(io.BytesIO(data)) as copied:
            actual=copied.convert('RGBA');assert actual.size==expected.size,'CSP roundtrip dimensions differ'
            alpha_equal=actual.getchannel('A').tobytes()==expected.getchannel('A').tobytes()
            rgb_differences=sum(a!=b for a,b in zip(actual.tobytes(),expected.tobytes()))
            self.report.update(alpha_exact=alpha_equal,rgba_different_bytes=rgb_differences,dimensions=actual.size)
            assert alpha_equal and rgb_differences==0,'CSP roundtrip changed test RGBA'
            actual.save(self.directory/'verified-csp-roundtrip.png')
        self.report.update(passed=True,test_document_left_open=True,clipboard_contains_test_image=True)
        self.event('actual_csp_and_normal_gvn_rgba_roundtrip_passed')
    def cleanup(self):
        if self.picker_window and pid_of(self.picker_window)==self.pid and U.GetWindow(self.picker_window,4)==self.main:
            button=U.GetDlgItem(self.picker_window,2)
            if button and pid_of(button)==self.pid:
                send(button,self.pid,0xf5)
                self.report['own_file_picker_cancelled']=True
        if self.viewer and self.viewer.poll() is None:
            if self.viewer_window and pid_of(self.viewer_window)==self.viewer.pid:U.PostMessageW(self.viewer_window,0x10,0,0)
            try:self.viewer.wait(timeout=5)
            except subprocess.TimeoutExpired:self.viewer.terminate();self.viewer.wait(timeout=5)
        if self.last_input()==self.input_tick and self.original_foreground:
            U.SetForegroundWindow(self.original_foreground)
        write(self.directory/'run.json',self.report)

def main():
    parser=argparse.ArgumentParser();sub=parser.add_subparsers(dest='mode',required=True)
    p=sub.add_parser('prepare');p.add_argument('--output',type=Path,required=True);p.add_argument('--checkpoint',type=Path,default=ROOT/'.cache/goal-continuation-0.48.0.json')
    r=sub.add_parser('run');r.add_argument('--plan',type=Path,required=True);r.add_argument('--approved-plan-sha256',required=True)
    r.add_argument('--allow-interactive-csp',action='store_true');r.add_argument('--allow-clipboard-overwrite',action='store_true');r.add_argument('--confirm-default-shortcuts',action='store_true')
    args=parser.parse_args()
    if args.mode=='prepare':prepare(args.output,args.checkpoint);return
    if not(args.allow_interactive_csp and args.allow_clipboard_overwrite and args.confirm_default_shortcuts):
        parser.error('Explicit desktop/clipboard/standard-shortcut authorization is required')
    assert sha(args.plan)==args.approved_plan_sha256,'Reviewed plan changed'
    plan=read(args.plan)
    for key in ['source','target','gvn','ocr']:assert sha(plan[key])==plan[key+'_sha256'],key+' changed'
    assert sha(__file__)==plan['controller_sha256'],'Controller changed after review'
    assert sha(ROOT/'include/gvn/commands.h')==plan['command_header_sha256']
    assert sha(ROOT/'tools/benchmark_navigation.py')==plan['native_helper_sha256']
    assert all(sha(p)==v for p,v in plan['protected_profile'].items()),'GVN user profile changed;regenerate plan'
    assert Path(plan['directory']).resolve().is_relative_to((ROOT/'.cache').resolve())
    configure();U.SetProcessDpiAwarenessContext(P(-4));controller=Controller(plan)
    try:controller.run()
    except Exception as error:controller.report['error']=str(error);raise
    finally:
        controller.report['user_gvn_profile_unchanged']=all(sha(p)==v for p,v in plan['protected_profile'].items())
        controller.cleanup()
if __name__=='__main__':main()
