"""Capture actual desktop pixels of owned GVN startup windows, without input injection.

An owned black backing window prevents unrelated desktop content being recorded.
Unlike PrintWindow/WM_PRINTCLIENT and private-desktop tests, BitBlt reads the
interactive desktop's composed pixels. This samples presentation, not scanout.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import subprocess
import time
import uuid
from PIL import Image
import benchmark_navigation as native

U, K, P = native.U, native.K, native.P
G = c.WinDLL('gdi32', use_last_error=True)
D = c.WinDLL('dwmapi', use_last_error=True)
for lib, name, result, arguments in [
    (U, 'GetProcessWindowStation', P, []), (U, 'GetThreadDesktop', P, [w.DWORD]),
    (U, 'OpenInputDesktop', P, [w.DWORD,w.BOOL,w.DWORD]), (U,'CloseDesktop',w.BOOL,[P]),
    (U, 'GetForegroundWindow', P, []), (U, 'IsWindowVisible',w.BOOL,[P]),
    (U, 'WindowFromPoint',P,[w.POINT]), (U,'GetAncestor',P,[P,w.UINT]),
    (U, 'GetDC',P,[P]), (U,'ReleaseDC',c.c_int,[P,P]),
    (U,'CreateWindowExW',P,[w.DWORD,w.LPCWSTR,w.LPCWSTR,w.DWORD,c.c_int,c.c_int,c.c_int,c.c_int,P,P,P,P]),
    (U,'DestroyWindow',w.BOOL,[P]), (U,'UpdateWindow',w.BOOL,[P]),
    (U,'SystemParametersInfoW',w.BOOL,[w.UINT,w.UINT,P,w.UINT]),
    (U,'SetProcessDpiAwarenessContext',w.BOOL,[P]),
    (G,'CreateCompatibleDC',P,[P]), (G,'DeleteDC',w.BOOL,[P]),
    (G,'SelectObject',P,[P,P]), (G,'DeleteObject',w.BOOL,[P]),
    (G,'CreateDIBSection',P,[P,P,w.UINT,c.POINTER(P),P,w.DWORD]),
    (G,'BitBlt',w.BOOL,[P,c.c_int,c.c_int,c.c_int,c.c_int,P,c.c_int,c.c_int,w.DWORD]),
    (D,'DwmIsCompositionEnabled',c.c_long,[c.POINTER(w.BOOL)]),
    (D,'DwmFlush',c.c_long,[]), (K,'GetCurrentThreadId',w.DWORD,[]),
    (D,'DwmGetWindowAttribute',c.c_long,[P,w.DWORD,P,w.DWORD]),
    (K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])]:
    native.api(lib,name,result,arguments)


def object_name(handle):
    name=c.create_unicode_buffer(512);needed=w.DWORD()
    native.check(U.GetUserObjectInformationW(handle,2,name,c.sizeof(name),c.byref(needed)))
    return name.value


class Surface:
    def __init__(self,width,height):
        self.width,self.height=width,height
        self.screen=U.GetDC(None);native.check(self.screen)
        self.dc=G.CreateCompatibleDC(self.screen);native.check(self.dc)
        header=(w.DWORD*10)(40,width,(-height)&0xffffffff,1|(32<<16),0,0,0,0,0,0)
        self.pixels=P();self.bitmap=G.CreateDIBSection(self.dc,header,0,c.byref(self.pixels),None,0)
        native.check(self.bitmap);self.old=G.SelectObject(self.dc,self.bitmap)
    def capture(self,x,y):
        native.check(G.BitBlt(self.dc,0,0,self.width,self.height,self.screen,x,y,0x40cc0020))
        return c.string_at(self.pixels,self.width*self.height*4)
    def close(self):
        G.SelectObject(self.dc,self.old);G.DeleteObject(self.bitmap);G.DeleteDC(self.dc);U.ReleaseDC(None,self.screen)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--repeat',type=int,default=1)
    args=parser.parse_args();assert 1<=args.repeat<=3
    exe=args.executable.resolve();output=args.output.resolve();output.mkdir()
    U.SetProcessDpiAwarenessContext(P(-4))
    flags=(w.DWORD*3)();needed=w.DWORD()
    native.check(U.GetUserObjectInformationW(U.GetProcessWindowStation(),1,flags,c.sizeof(flags),c.byref(needed)))
    assert flags[2]&1,'No interactive station: presentation test cannot run'
    desktop=U.GetThreadDesktop(K.GetCurrentThreadId());input_desktop=U.OpenInputDesktop(0,False,1)
    native.check(input_desktop)
    try:assert object_name(desktop)==object_name(input_desktop),'Not on the input desktop'
    finally:U.CloseDesktop(input_desktop)
    composed=w.BOOL();assert D.DwmIsCompositionEnabled(c.byref(composed))>=0 and composed.value
    native.desktop=desktop
    work=w.RECT();native.check(U.SystemParametersInfoW(0x30,0,c.byref(work),0))
    x,y=work.left+40,work.top+40
    width,height=min(720,work.right-work.left-100),min(500,work.bottom-work.top-100)
    assert width>=480 and height>=360
    foreground=U.GetForegroundWindow();clipboard_sequence=U.GetClipboardSequenceNumber()
    # STATIC black rectangle, owned by this process, with no activation or input.
    backing=U.CreateWindowExW(0x08000080,'STATIC','GVN startup capture backing',0x90000004,
        x-10,y-10,width+20,height+20,None,None,None,None)
    native.check(backing);U.UpdateWindow(backing);D.DwmFlush()
    report={'passed':False,'interactive_desktop':True,'dwm_composition':True,
        'method':'Screen DC BitBlt of owned client over owned black backing; no PrintWindow, WM_PRINTCLIENT, input injection or user clipboard. Samples composed desktop frames, not physical scanout.',
        'executable_sha256':native.sha(exe),'tool_sha256':native.sha(__file__),'runs':[]}
    children=[]
    try:
        for repeat in range(args.repeat):
            for mode in ['saved','custom']:
                name=f'{repeat}-{mode}';folder=output/name;folder.mkdir();config=folder/'settings.ini'
                config.write_text('[Viewer]\nallow_multiple=1\nreopen_last=0\nconfirm_unsaved=0\n'
                    'topmost=1\nmenu=0\nwindow_center=0\nstartup_state=0\nrestore_maximized=0\n'
                    f'startup_position={1 if mode=="saved" else 2}\nlast_window_valid=1\nlast_state=0\n'
                    f'last_x={x}\nlast_y={y}\nlast_width={width}\nlast_height={height}\n'
                    f'window_x={x}\nwindow_y={y}\nwindow_width={width}\nwindow_height={height}\n'
                    '[Interface]\nui_theme=2\n',encoding='utf-16')
                info=native.Process();startup=native.Startup();startup.cb=c.sizeof(startup)
                startup.flags=1;startup.show=1 # Actual normal startup, without injected input.
                line=c.create_unicode_buffer(subprocess.list2cmdline([str(exe),'--settings',str(config)]))
                start=time.perf_counter();native.check(K.CreateProcessW(str(exe),line,None,None,False,0,None,str(exe.parent),c.byref(startup),c.byref(info)))
                child=object.__new__(native.Child);child.info=info;child.started=start;children.append(child)
                window=None;deadline=start+10;frames=[];first_visible=None;surface=None;owners=[];cloaked_samples=0
                try:
                    while time.perf_counter()<deadline:
                        if not window:
                            window=next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None)
                        if window and U.IsWindowVisible(window):
                            cloaked=w.DWORD()
                            native.check(D.DwmGetWindowAttribute(window,14,c.byref(cloaked),c.sizeof(cloaked))>=0)
                            if cloaked.value:
                                cloaked_samples+=1;time.sleep(.001);continue
                            bounds=w.RECT();point=w.POINT();native.check(U.GetClientRect(child.own(window),c.byref(bounds)))
                            native.check(U.ClientToScreen(window,c.byref(point)))
                            # Save the entire owned client; analyze its canvas separately from the controls.
                            cw,ch=bounds.right,bounds.bottom
                            sx,sy=point.x,point.y
                            assert cw>0 and ch>0 and x-10<=sx and y-10<=sy and sx+cw<=x+width+10 and sy+ch<=y+height+10
                            screen_owners=[U.GetAncestor(U.WindowFromPoint(w.POINT(px,py)),2) for px,py in
                                [(sx+2,sy+2),(sx+cw-3,sy+2),(sx+2,sy+ch-3),(sx+cw-3,sy+ch-3),(sx+cw//2,sy+ch//2)]]
                            assert all(owner in (window,backing) for owner in screen_owners),'Foreign window obscured the capture region'
                            screen_owner=screen_owners[-1]
                            owners.append('viewer' if screen_owner==window else 'backing')
                            if surface is None:surface=Surface(cw,ch)
                            if first_visible is None:first_visible=time.perf_counter()
                            frames.append(((time.perf_counter()-start)*1000,surface.capture(sx,sy)))
                            if time.perf_counter()-first_visible>=.65 or len(frames)>=160:break
                        else:assert K.WaitForSingleObject(info.process,0)!=0,'Owned viewer exited before showing'
                        time.sleep(.003)
                    assert frames,'No visible startup frames'
                finally:
                    if surface:surface.close()
                    if window:
                        child.post(window,0x10)
                        assert K.WaitForSingleObject(info.process,10000)==0,'Owned test viewer failed to close'
                        code=w.DWORD();native.check(K.GetExitCodeProcess(info.process,c.byref(code)));assert code.value==0
                analyzed=[];images=[]
                for index,(ms,raw) in enumerate(frames):
                    image=Image.frombytes('RGB',(cw,ch),raw,'raw','BGRX');images.append(image)
                    canvas=image.crop(((cw-400)//2,80,(cw+400)//2,min(ch-20,300)))
                    pixels=list(canvas.resize((40,22),Image.Resampling.NEAREST).getdata())
                    light=sum(min(pixel)>=225 for pixel in pixels)/len(pixels)
                    checker=sum(max(pixel)-min(pixel)<=3 and 20<=max(pixel)<=65 for pixel in pixels)/len(pixels)
                    analyzed.append({'ms':ms,'light_fraction':light,'checker_fraction':checker})
                    image.save(folder/f'{index:03}.png')
                images[0].save(folder/'startup.gif',save_all=True,append_images=images[1:],duration=[max(10,round(analyzed[min(i+1,len(analyzed)-1)]['ms']-a['ms'])) for i,a in enumerate(analyzed)],loop=0)
                white=[i for i,a in enumerate(analyzed) if a['light_fraction']>.90]
                run={'name':name,'pid':info.pid,'frames':analyzed,'white_frames':white,
                    'cloaked_samples':cloaked_samples,
                    'screen_owners':owners,'last_checker_valid':analyzed[-1]['checker_fraction']>.95,
                    'first_visible_ms':analyzed[0]['ms'],'first_checker_ms':next((a['ms'] for a in analyzed if a['checker_fraction']>.95),None),'passed':not white and analyzed[-1]['checker_fraction']>.95}
                report['runs'].append(run);print(json.dumps({k:v for k,v in run.items() if k!='frames'}),flush=True)
        assert clipboard_sequence==U.GetClipboardSequenceNumber(),'User clipboard changed during probe'
        report.update(passed=all(run['passed'] for run in report['runs']),clipboard_unchanged=True,
            foreground_unchanged=foreground==U.GetForegroundWindow())
    finally:
        for child in children:
            if K.WaitForSingleObject(child.info.process,0)!=0:
                K.TerminateProcess(child.info.process,1);K.WaitForSingleObject(child.info.process,5000)
            K.CloseHandle(child.info.thread);K.CloseHandle(child.info.process)
        U.DestroyWindow(backing)
        (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
