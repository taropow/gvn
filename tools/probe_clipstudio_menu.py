"""Read-only identification of CSP's standard menu commands. Never opens a menu or clipboard."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,hashlib,json,re

P,UP,SP=c.c_void_p,c.c_size_t,c.c_ssize_t
U=c.WinDLL('user32',use_last_error=True);K=c.WinDLL('kernel32',use_last_error=True)
def api(lib,name,result,args):
    f=getattr(lib,name);f.restype=result;f.argtypes=args;return f
api(K,'OpenProcess',P,[w.DWORD,w.BOOL,w.DWORD]);api(K,'CloseHandle',w.BOOL,[P])
api(K,'QueryFullProcessImageNameW',w.BOOL,[P,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)])
api(U,'EnumWindows',w.BOOL,[P,SP]);api(U,'GetWindowThreadProcessId',w.DWORD,[P,c.POINTER(w.DWORD)])
api(U,'GetClassNameW',c.c_int,[P,w.LPWSTR,c.c_int]);api(U,'GetWindowTextW',c.c_int,[P,w.LPWSTR,c.c_int])
api(U,'GetWindowLongPtrW',SP,[P,c.c_int]);api(U,'GetWindowRect',w.BOOL,[P,c.POINTER(w.RECT)])
api(U,'GetMenu',P,[P]);api(U,'GetMenuItemCount',c.c_int,[P]);api(U,'GetSubMenu',P,[P,c.c_int])
api(U,'GetMenuItemID',w.UINT,[P,c.c_int]);api(U,'GetMenuState',w.UINT,[P,w.UINT,w.UINT])
api(U,'GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT])
Callback=c.WINFUNCTYPE(w.BOOL,P,SP)
def label(menu,index):
    buf=c.create_unicode_buffer(512);U.GetMenuStringW(menu,index,buf,len(buf),0x400);return buf.value
def normalize(value):
    value=value.split('\t')[0]
    return re.sub(r'\(&.\)|[&.\u2026\s\u3000]','',value).casefold()
ROOTS={'ファイル':'file','file':'file','編集':'edit','edit':'edit','選択範囲':'selection','select':'selection','selection':'selection'}
WANTED={'file':{'開く':'open','open':'open','閉じる':'close','close':'close','クリップボードから新規作成':'new_from_clipboard','newfromclipboard':'new_from_clipboard'},
        'edit':{'コピー':'copy','copy':'copy','貼り付け':'paste','paste':'paste'},
        'selection':{'すべてを選択':'select_all','全選択':'select_all','selectall':'select_all'}}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--identity',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    expected=json.loads(args.identity.read_text(encoding='utf-8-sig'));pid=expected['pid']
    handle=K.OpenProcess(0x1000,False,pid)
    if not handle:raise c.WinError(c.get_last_error())
    try:
        buf=c.create_unicode_buffer(32768);count=w.DWORD(len(buf))
        if not K.QueryFullProcessImageNameW(handle,0,buf,c.byref(count)):raise c.WinError(c.get_last_error())
        path=Path(buf.value);assert str(path).casefold()==expected['path'].casefold()
        digest=hashlib.sha256(path.read_bytes()).hexdigest();assert digest==expected['executable_sha256']
    finally:K.CloseHandle(handle)
    result=dict(read_only=True,pid=pid,executable_sha256=digest,clipboard_opened=False,menus_opened=False,window_commands_sent=0,document_titles_logged=False,windows=[],commands={})
    @Callback
    def visit(window,_):
        actual=w.DWORD();U.GetWindowThreadProcessId(window,c.byref(actual))
        if actual.value!=pid or not U.GetWindowLongPtrW(window,-16)&0x10000000:return True
        cls=c.create_unicode_buffer(256);U.GetClassNameW(window,cls,len(cls));menu=U.GetMenu(window)
        rc=w.RECT();U.GetWindowRect(window,c.byref(rc));row=dict(hwnd=int(window),class_name=cls.value,has_standard_menu=bool(menu),size=[rc.right-rc.left,rc.bottom-rc.top])
        result['windows'].append(row)
        if not menu:return True
        row['recognized_menu_roots']=[]
        for index in range(max(0,U.GetMenuItemCount(menu))):
            kind=ROOTS.get(normalize(label(menu,index)))
            if not kind:continue
            row['recognized_menu_roots'].append(kind);sub=U.GetSubMenu(menu,index)
            if not sub:continue
            # Shallow static entries only: never traverse recent-file/document/history lists.
            for i in range(max(0,U.GetMenuItemCount(sub))):
                name=WANTED[kind].get(normalize(label(sub,i)))
                if not name:continue
                command=U.GetMenuItemID(sub,i)
                if command not in [0,0xffffffff]:
                    result['commands'][name]=dict(command=command,hwnd=int(window),currently_enabled=not bool(U.GetMenuState(sub,i,0x400)&3))
        return True
    if not U.EnumWindows(visit,0):raise c.WinError(c.get_last_error())
    result['standard_menu_commands_ready']=all(n in result['commands'] for n in ['open','close','copy','paste'])
    result['discovery_completed']=True
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False),flush=True)
if __name__=='__main__':main()
