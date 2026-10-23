"""Owned GVN processes on a private station; verifies routing, never benchmarks startup."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c
import argparse, json, shutil, uuid
import benchmark_navigation as native

parser=argparse.ArgumentParser()
parser.add_argument('executable',type=Path)
parser.add_argument('--output',type=Path,default=Path('.cache/instance-contract'))
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
output=args.output.resolve()/('run-'+uuid.uuid4().hex)
output.mkdir(parents=True)
exe=args.executable.resolve()
U,K=native.U,native.K
native.api(U,'IsIconic',w.BOOL,[native.P])
native.api(K,'GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)])
native.api(K,'SuspendThread',w.DWORD,[native.P])
native.api(K,'ResumeThread',w.DWORD,[native.P])
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard')
native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None)
native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)))
assert not flags[2]&1
name=c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station,2,name,c.sizeof(name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Instance-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None)
native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
desktop=name.value+'\\'+desktop_name
children=[]
report=dict(executable=str(exe),executable_sha256=native.sha(exe),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),private_station=True,no_user_clipboard_or_settings=True,scope='Owned real GVN process launches and file handoff; no startup timing comparison.',cases=[],passed=False)

def launch(config,path=None):
    child=native.Child(exe,['--settings',config,*([path] if path else [])],desktop)
    children.append(child)
    return child

def main_window(child):
    return next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None)

def wait_exit_or_window(child):
    import time
    end=time.monotonic()+10
    while time.monotonic()<end:
        if K.WaitForSingleObject(child.info.process,0)==0:return None
        window=main_window(child)
        if window:return window
        time.sleep(.005)
    raise TimeoutError('Owned secondary neither exited nor created a window: '+repr([(native.window_text(h),native.window_text(h,True)) for h in child.windows()]))

def success_exit(child):
    assert K.WaitForSingleObject(child.info.process,10000)==0,'Owned secondary did not exit'
    code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)))
    assert code.value==0,code.value

def config_file(name,multiple=False,activate=False,display=0):
    path=output/(name+'.ini')
    path.write_text('[Viewer]\nallow_multiple='+str(int(multiple))+'\nactivate_reused='+str(int(activate))+'\nactivate_dropped=0\ndisplay_mode='+str(display)+'\nconfirm_unsaved=0\nprefetch_enabled=0\n',encoding='utf-16')
    return path

def loaded(child,name):
    window=child.wait(lambda:main_window(child))
    child.wait(lambda:name in native.window_text(window))
    return window

def forwarded(child):
    assert wait_exit_or_window(child) is None,'Unexpected secondary viewer window'
    success_exit(child)

def passed(name,**details):
    report['cases'].append(dict(name=name,passed=True,**details))
    print('PASS '+name,flush=True)

def minimize(child,window):
    child.post(window,0x112,0xf020)
    child.wait(lambda:U.IsIconic(window))

try:
    assets=output/'assets';assets.mkdir()
    first_path=assets/'01.png';second_path=assets/'02.bmp'
    shutil.copyfile(root/'tests/fixtures/alpha.png',first_path)
    shutil.copyfile(root/'tests/fixtures/rgb.bmp',second_path)
    config=output/'settings.ini'
    config.write_text('[Viewer]\nallow_multiple=0\nactivate_reused=0\nactivate_dropped=0\ndisplay_mode=0\nconfirm_unsaved=0\nprefetch_enabled=0\n',encoding='utf-16')
    first=launch(config,first_path)
    window=first.wait(lambda:main_window(first))
    first.wait(lambda:'01.png' in native.window_text(window))
    second=launch(config,second_path)
    extra=wait_exit_or_window(second)
    case=dict(name='single_instance_reuses_existing_window',primary_pid=first.info.pid,secondary_pid=second.info.pid,secondary_window_created=bool(extra),passed=False)
    report['cases'].append(case)
    assert extra is None,'Multiple-instance OFF created a secondary window'
    code=w.DWORD()
    native.check(K.GetExitCodeProcess(second.info.process,c.byref(code)))
    assert code.value==0,code.value
    first.wait(lambda:'02.bmp' in native.window_text(window))
    case['passed']=True
    print('PASS '+case['name'],flush=True)

    forwarded(launch(config))
    assert '02.bmp' in native.window_text(window)
    passed('empty_relaunch_keeps_existing_image')

    minimize(first,window)
    # CLI path is relative to sender's different CWD; include non-ASCII and spaces.
    unicode_path=assets/'日本語 空白 😀.png'
    shutil.copyfile(first_path,unicode_path)
    import os
    forwarded(launch(config,Path(os.path.relpath(unicode_path,exe.parent))))
    first.wait(lambda:unicode_path.name in native.window_text(window))
    assert U.IsIconic(window),'Activation OFF restored minimized receiver'
    forwarded(launch(config))
    assert U.IsIconic(window),'Empty relaunch activated receiver despite OFF'
    passed('activation_off_preserves_minimized_with_unicode_relative_path_and_empty_request')

    activate_config=config_file('activate',activate=True)
    active=launch(activate_config,first_path);active_window=loaded(active,'01.png')
    minimize(active,active_window)
    forwarded(launch(activate_config,second_path))
    active.wait(lambda:'02.bmp' in native.window_text(active_window) and not U.IsIconic(active_window))
    minimize(active,active_window)
    forwarded(launch(activate_config))
    active.wait(lambda:not U.IsIconic(active_window))
    assert '02.bmp' in native.window_text(active_window)
    passed('activation_on_restores_receiver_for_image_and_empty_relaunch')

    multiple_config=config_file('multiple',multiple=True)
    multiple_first=launch(multiple_config,first_path);one=loaded(multiple_first,'01.png')
    multiple_second=launch(multiple_config,second_path);two=loaded(multiple_second,'02.bmp')
    multiple_empty=launch(multiple_config);blank=multiple_empty.wait(lambda:main_window(multiple_empty))
    assert len({one,two,blank})==3 and '01.png' in native.window_text(one)
    passed('multiple_on_creates_independent_image_and_empty_windows',windows=3)

    isolated_config=config_file('isolated')
    isolated=launch(isolated_config,first_path);isolated_window=loaded(isolated,'01.png')
    assert isolated_window not in {window,active_window,one,two,blank}
    assert unicode_path.name in native.window_text(window)
    passed('different_profile_creates_independent_viewer')

    changed_config=config_file('changed-policy',multiple=True)
    changed=launch(changed_config,first_path);changed_window=loaded(changed,'01.png')
    config_file('changed-policy',multiple=False)
    forwarded(launch(changed_config,second_path))
    changed.wait(lambda:'02.bmp' in native.window_text(changed_window))
    passed('previously_multiple_window_can_receive_after_policy_saved_off')

    concurrent_config=config_file('concurrent')
    concurrent=[launch(concurrent_config,first_path if index%2 else second_path) for index in range(6)]
    outcomes=[wait_exit_or_window(child) for child in concurrent]
    survivors=[(child,h) for child,h in zip(concurrent,outcomes) if h]
    assert len(survivors)==1,('Concurrent launch made duplicate windows',outcomes)
    for child,h in zip(concurrent,outcomes):
        if not h:success_exit(child)
    survivor,survivor_window=survivors[0]
    survivor.wait(lambda:any(name in native.window_text(survivor_window) for name in ('01.png','02.bmp')))
    passed('six_concurrent_single_instance_launches_create_one_viewer',launches=6,viewer_windows=1)

    # Suspend only the exact main-thread handle created for this private child.
    # Resume before dismissing the second child's own error dialog, even on assertion failure.
    first.ping(window)
    assert K.SuspendThread(first.info.thread)!=0xffffffff
    try:
        busy=launch(config,second_path)
        dialog=busy.wait(lambda:next((h for h in busy.windows() if native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1)),None),timeout=10)
        assert main_window(busy) is None,'Unresponsive receiver caused duplicate viewer'
        report['busy_dialog']=dict(title=native.window_text(dialog),button=native.window_text(U.GetDlgItem(dialog,1)))
    finally:
        assert K.ResumeThread(first.info.thread)!=0xffffffff
    busy.post(U.GetDlgItem(dialog,1),0xf5)
    assert K.WaitForSingleObject(busy.info.process,10000)==0
    native.check(K.GetExitCodeProcess(busy.info.process,c.byref(code)))
    assert code.value==2,code.value
    assert unicode_path.name in native.window_text(window)
    passed('unresponsive_owned_receiver_reports_busy_without_duplicate_or_lost_current_image',exit_code=code.value)
    report['passed']=True
except BaseException as error:
    report['error']=repr(error)
    raise
finally:
    for child in reversed(children):child.close()
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    print(output)
