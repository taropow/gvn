"""Read selection-grid rounding from only an owned reference child/private desktop."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import json,shutil,sys,uuid,zipfile
from PIL import Image
import benchmark_navigation as native
root=Path(__file__).resolve().parents[1];read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
cp=root/'.cache/goal-continuation-0.40.0.json';state=read(cp);assert not state['source_freeze'] and not state['live_other_sessions']
protected=read(root/'.cache/current-distribution-protection-0.40.json');assert all(native.sha(root/n)==v for n,v in protected.items())
output=root/'.cache/selection-units-reference'/('run-'+uuid.uuid4().hex);output.mkdir(parents=True);reference=output/'reference'
archive=root/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
with zipfile.ZipFile(archive) as package:
    for name in package.namelist():
        if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
            target=(reference/name).resolve();assert target.is_relative_to(reference.resolve());target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
assert native.sha(reference/'MassiGra.exe')=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
asset=output/'source.png';Image.new('RGB',(96,80),(13,79,141)).save(asset)
U,K=native.U,native.K;G=c.WinDLL('gdi32',use_last_error=True)
for name,result,args in [('GetMenu',native.P,[native.P]),('GetSubMenu',native.P,[native.P,c.c_int]),('GetMenuState',w.UINT,[native.P,w.UINT,w.UINT]),('IsWindow',w.BOOL,[native.P]),('ScreenToClient',w.BOOL,[native.P,c.POINTER(w.POINT)]),('ChildWindowFromPointEx',native.P,[native.P,w.POINT,w.UINT])]:native.api(U,name,result,args)
for name,result,args in [('CreateCompatibleDC',native.P,[native.P]),('CreateDIBSection',native.P,[native.P,native.P,w.UINT,c.POINTER(native.P),native.P,w.DWORD]),('SelectObject',native.P,[native.P,native.P]),('DeleteDC',w.BOOL,[native.P]),('DeleteObject',w.BOOL,[native.P])]:native.api(G,name,result,args)
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in [0,0x80]
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station);flags=(w.DWORD*3)();needed=w.DWORD();native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
name=c.create_unicode_buffer(512);native.check(U.GetUserObjectInformationW(station,2,name,c.sizeof(name),c.byref(needed)));native.check(U.SetProcessWindowStation(station))
desktop='GVN-SelectionUnits-'+uuid.uuid4().hex;native.desktop=U.CreateDesktopW(desktop,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
sources={p.relative_to(root).as_posix():native.sha(p) for p in [Path(__file__),Path(native.__file__)]}
report=dict(passed=False,noninteractive=True,user_clipboard_or_windows_used=False,reference_sha256=native.sha(reference/'MassiGra.exe'),sources=sources,cases=[])
state.update(source_freeze=True,live_other_sessions=['selection-units-reference']);write(cp,state)
child=None
try:
    child=native.Child(reference/'MassiGra.exe',[asset],name.value+'\\'+desktop);report['controlled_pid']=child.info.pid
    main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:asset.name in native.window_text(main));child.ping(main)
    def text(h):
        b=c.create_unicode_buffer(2048);child.send(child.own(h),0xd,len(b),c.addressof(b));return b.value
    def dialog(command,title):
        child.post(main,0x111,command)
        h=child.wait(lambda:next((h for h in child.windows() if h!=main and title in native.window_text(h)),None))
        child.wait(lambda:all(U.GetDlgItem(h,n) for n in [2031,2032,2033,2034,1,2]));child.ping(h);return h
    def close(h,accept):
        child.post(h,0x111,1 if accept else 2);child.wait(lambda:not U.IsWindow(h))
    h=dialog(40407,'選択範囲の単位');report['defaults']=[text(U.GetDlgItem(h,n)) for n in range(2031,2035)]
    report['spin_range16']=[child.send(child.own(U.GetDlgItem(h,n)),0x466) for n in range(2161,2165)];close(h,False)
    descendants=[]
    @native.Callback
    def enumerate_child(h,_):
        child.own(h);r=w.RECT();U.GetWindowRect(h,c.byref(r));descendants.append((h,native.window_text(h,True),[r.left,r.top,r.right,r.bottom]));return True
    U.EnumChildWindows(main,enumerate_child,0);report['descendants']=[dict(cls=cls,rect=rect) for h,cls,rect in descendants]
    # The VCL reference window does not implement PrintWindow. Read its image-pane
    # geometry at explicit100% and use an even client size so centering is integral.
    if not U.GetMenu(main):child.send(main,0x111,41008)
    menu=U.GetMenu(main);assert menu;edit_menu=U.GetSubMenu(menu,1);assert edit_menu
    child.send(main,0x111,41026);child.send(main,0x111,40086)
    target=next(h for h,cls,rect in descendants if cls=='TF811202_MassiGra_MaCl')
    r=w.RECT();native.check(U.GetWindowRect(main,c.byref(r)))
    pane=w.RECT();native.check(U.GetClientRect(target,c.byref(pane)))
    native.check(U.SetWindowPos(main,None,0,0,r.right-r.left+(pane.right%2),r.bottom-r.top+(pane.bottom%2),6))
    child.ping(main);native.check(U.GetClientRect(target,c.byref(pane)));assert pane.right%2==0 and pane.bottom%2==0
    image_origin=w.POINT((pane.right-96)//2,(pane.bottom-80)//2)
    report['target_class']=native.window_text(target,True);report['origin']=[image_origin.x,image_origin.y];report['pane_size']=[pane.right,pane.bottom]
    child.send(main,0x111,40022)
    tasks=[('free',[1,1,1,1],[13,11],[30,27]),('preset8',[8,8,8,8],[13,11],[30,27]),('custom',[8,4,6,5],[13,11],[30,27]),('reverse',[8,4,6,5],[30,27],[13,11]),('short',[8,4,6,5],[13,11],[14,12]),('edge',[8,4,6,5],[90,74],[110,100]),('oversized',[1,1,128,100],[5,5],[30,27]),('lastpixel',[8,4,6,5],[95,79],[96,80]),('click',[1,1,1,1],[13,11],[13,11])]
    if len(sys.argv)>1:tasks=[task for task in tasks if task[0] in sys.argv[1:]]
    for label,units,start,end in tasks:
        h=dialog(40407,'選択範囲の単位')
        for n,value in zip(range(2031,2035),units):
            b=c.create_unicode_buffer(str(value));child.send(child.own(U.GetDlgItem(h,n)),0xc,0,c.addressof(b))
        close(h,True)
        def lp(p):return ((image_origin.x+p[0])&0xffff)|((image_origin.y+p[1])&0xffff)<<16
        child.send(target,0x201,1,lp(start));child.send(target,0x200,1,lp(end));child.send(target,0x202,0,lp(end));child.ping(main)
        child.send(main,0x117,edit_menu,1);crop_state=U.GetMenuState(edit_menu,40027,0);assert crop_state!=0xffffffff
        h=dialog(40023,'選択範囲');child.wait(lambda:U.GetDlgItem(h,2036));values=[int(text(U.GetDlgItem(h,n))) for n in range(2031,2037)];close(h,False)
        report['cases'].append(dict(name=label,units=units,start=start,end=end,selection=values,crop_enabled=not bool(crop_state&3)));print(label,values,not bool(crop_state&3),flush=True)
    report['passed']=True
finally:
    if child:child.close()
    report['source_guard']=all(native.sha(root/n)==v for n,v in sources.items());report['distribution_and_profile_guard']=all(native.sha(root/n)==v for n,v in protected.items());write(output/'result.json',report)
    state.update(source_freeze=False,live_other_sessions=[]);write(cp,state);K.ReleaseMutex(mutex);K.CloseHandle(mutex);print(output,flush=True)
assert report['passed'] and report['source_guard'] and report['distribution_and_profile_guard']
