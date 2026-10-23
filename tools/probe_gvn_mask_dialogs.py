"""Normal GVN CP/Rainbow controls, copied RGBA and private-profile graceful restarts."""
from pathlib import Path
from ctypes import wintypes as w
import argparse, configparser, ctypes as c, json, re, shutil, struct, time, uuid
from PIL import Image
import benchmark_navigation as native

parser=argparse.ArgumentParser()
parser.add_argument('executable',type=Path)
parser.add_argument('--output',type=Path,default=Path('.cache/mask-dialog-processes'))
args=parser.parse_args();root=Path(__file__).resolve().parents[1]
out=args.output.resolve()/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
exe=args.executable.resolve();U,K=native.U,native.K
for name,result,arguments in [('IsWindow',w.BOOL,[native.P]),('IsWindowEnabled',w.BOOL,[native.P]),('GetWindowLongW',c.c_long,[native.P,c.c_int])]:native.api(U,name,result,arguments)
native.api(K,'GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)])

def commands():
    text=(root/'include/gvn/commands.h').read_text().split('enum Command {',1)[1].split('};',1)[0]
    result={};value=0
    for item in text.split(','):
        match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
        if match:
            name,number=match.groups()
            if number:value=int(number)
            result[name]=value;value+=1
    assert result['RainbowMask']==299 and result['CPMask']==300
    return result

ids=commands();fixtures=root/'tests/fixtures'
inputs=[fixtures/'cp-reference/png/full-a-source.png',fixtures/'cp-reference/png/full-a-expected.png',fixtures/'cp-reference/png/full-ab-expected.png',fixtures/'rainbow-reference/png/full-1-source.png']
inputs.extend(fixtures/f'rainbow-reference/png/full-{size}-expected.png' for size in [1,2,4,8,16,32])
input_hashes={str(p):native.sha(p) for p in inputs}
assets={}
for name,source in [('cp',inputs[0]),('rainbow',inputs[3])]:
    target=out/('日本語 '+name+' Alpha.png');shutil.copyfile(source,target);assets[name]=target
(out/'sources').mkdir()
for source in [Path(__file__),Path(native.__file__)]:shutil.copyfile(source,out/'sources'/source.name)

mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Mask-Process-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
desktop=station_name.value+'\\'+desktop_name
children=[];child=None;window=None
report=dict(passed=False,scope='Normal GVN executable, actual CP/Rainbow dialogs, all six Rainbow sizes, original/edited/Undo copied RGBA and settings after graceful process restarts. Own station/desktop/clipboard, profiles and synthetic images only; no global input/ComputerUse/Sky/startup-speed comparison.',executable_sha256=native.sha(exe),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),input_hashes=input_hashes,processes=[],cases=[])

def case(name,**details):
    report['cases'].append(dict(name=name,passed=True,**details));print('PASS '+name,flush=True)

def copied(expected,label):
    sequence=U.GetClipboardSequenceNumber();deadline=time.monotonic()+10
    while U.GetClipboardSequenceNumber()==sequence or not U.IsClipboardFormatAvailable(17):
        assert time.monotonic()<deadline,'Owned GVN did not publish DIBV5'
        child.post(window,0x111,ids['Copy']);time.sleep(.03)
    child.wait(lambda:bool(U.OpenClipboard(None)))
    try:
        handle=U.GetClipboardData(17);pointer=K.GlobalLock(handle);native.check(pointer)
        try:data=c.string_at(pointer,K.GlobalSize(handle))
        finally:K.GlobalUnlock(handle)
    finally:U.CloseClipboard()
    header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data)
    reference=Image.open(expected).convert('RGBA')
    assert (header,width,abs(height),planes,bits,compression)==(124,reference.width,reference.height,1,32,3)
    actual=Image.frombytes('RGBA',reference.size,data[124:],'raw','BGRA',width*4,-1 if height>0 else 1)
    assert actual.tobytes()==reference.tobytes(),label+' RGBA differs'
    actual.save(out/(label+'.png'));case(label,rgba_pixels=width*abs(height),reference_sha256=native.sha(expected))

def start(profile,kind):
    global child,window
    asset=assets[kind];child=native.Child(exe,['--settings',profile,asset],desktop);children.append(child)
    report['processes'].append(dict(pid=child.info.pid,asset=asset.name,profile=profile.name,graceful_exit=False))
    window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None))
    child.wait(lambda:asset.name in native.window_text(window));child.ping(window)
    copied(asset,'startup-'+str(len(report['processes'])))

def close():
    child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0,'Owned GVN did not close'
    code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0
    report['processes'][-1].update(graceful_exit=True,exit_code=code.value);child.close();children.remove(child)

def text(control):
    value=c.create_unicode_buffer(4096);child.send(child.own(control),0xd,len(value),c.addressof(value));return value.value

def opened(kind):
    child.post(window,0x111,ids['CPMask' if kind=='cp' else 'RainbowMask'])
    title='CPマスク' if kind=='cp' else '虹色マスク';key=240 if kind=='cp' else 215
    dialog=child.wait(lambda:next((h for h in child.windows() if native.window_text(h)==title and U.GetDlgItem(h,key) and U.GetDlgItem(h,1) and U.GetWindowLongW(h,-16)&0x10000000),None))
    child.wait(lambda:U.IsWindowEnabled(child.own(U.GetDlgItem(dialog,1))));child.ping(dialog);return dialog

def button(dialog,key):child.post(child.own(U.GetDlgItem(dialog,key)),0xf5)

def finished(dialog,accept):
    if accept:child.wait(lambda:U.IsWindowEnabled(child.own(U.GetDlgItem(dialog,1))))
    button(dialog,1 if accept else 2);child.wait(lambda:not U.IsWindow(dialog));child.ping(window)

def preview(dialog,key,value):
    control=child.own(U.GetDlgItem(dialog,key))
    if bool(child.send(control,0xf0))!=value:child.send(control,0xf5)
    assert bool(child.send(control,0xf0))==value

def cp_state(dialog):return text(U.GetDlgItem(dialog,240)),bool(child.send(child.own(U.GetDlgItem(dialog,241)),0xf0))

def cp_set(dialog,value,enabled):
    control=child.own(U.GetDlgItem(dialog,240));buffer=c.create_unicode_buffer(value);child.send(control,0xc,0,c.addressof(buffer))
    expected=''.join(ch for ch in value if 'A'<=ch<='Z' or 'a'<=ch<='z')[:16]
    assert text(control)==expected;preview(dialog,241,enabled)

def rainbow_state(dialog):
    choices=[size for i,size in enumerate([1,2,4,8,16,32]) if child.send(child.own(U.GetDlgItem(dialog,210+i)),0xf0)]
    assert len(choices)==1;return choices[0],bool(child.send(child.own(U.GetDlgItem(dialog,220)),0xf0))

def rainbow_set(dialog,size,enabled):
    child.send(child.own(U.GetDlgItem(dialog,210+[1,2,4,8,16,32].index(size))),0xf5);preview(dialog,220,enabled)
    assert rainbow_state(dialog)==(size,enabled)

def saved(profile):
    ini=configparser.ConfigParser(interpolation=None);ini.read(profile,encoding='utf-16');return dict(ini['Viewer'])

try:
    profile=out/'専用 settings.ini';assert not profile.exists();start(profile,'cp')
    dialog=opened('cp');assert cp_state(dialog)==('',True);cp_set(dialog,'a b1',False);finished(dialog,False)
    dialog=opened('cp');assert cp_state(dialog)==('',True);finished(dialog,False);case('cp_fresh_defaults_and_cancel_preserve_options')
    copied(assets['cp'],'cp-cancel-source')
    dialog=opened('cp');cp_set(dialog,'a b1',False);finished(dialog,True)
    copied(inputs[2],'cp-accepted-ab');dialog=opened('cp');assert cp_state(dialog)==('AB',False);cp_set(dialog,'a',True);finished(dialog,False)
    dialog=opened('cp');assert cp_state(dialog)==('AB',False);finished(dialog,False);case('cp_filter_uppercase_commit_and_later_cancel')
    close();settings=saved(profile);assert settings['cp_code']=='AB' and settings['cp_preview']=='0'
    start(profile,'rainbow');dialog=opened('cp');assert cp_state(dialog)==('AB',False);finished(dialog,False);case('cp_code_and_preview_survive_graceful_restart')
    dialog=opened('rainbow');assert rainbow_state(dialog)==(8,True);finished(dialog,False);case('rainbow_defaults_independent_of_cp_preview')
    for i,size in enumerate([1,2,4,8,16,32]):
        dialog=opened('rainbow');rainbow_set(dialog,size,i%2==0);finished(dialog,True)
        copied(fixtures/f'rainbow-reference/png/full-{size}-expected.png','rainbow-'+str(size))
        child.send(window,0x111,ids['Undo']);copied(assets['rainbow'],'rainbow-'+str(size)+'-undo')
    dialog=opened('rainbow');assert rainbow_state(dialog)==(32,False);rainbow_set(dialog,1,True);finished(dialog,False)
    dialog=opened('rainbow');assert rainbow_state(dialog)==(32,False);finished(dialog,False);case('rainbow_cancel_preserves_last_accepted_size_and_preview')
    close();settings=saved(profile);assert settings['rainbow_size']=='32' and settings['rainbow_preview']=='0' and settings['cp_code']=='AB' and settings['cp_preview']=='0'
    start(profile,'cp');dialog=opened('rainbow');assert rainbow_state(dialog)==(32,False);finished(dialog,False)
    dialog=opened('cp');assert cp_state(dialog)==('AB',False);cp_set(dialog,'a',True);finished(dialog,True);case('both_mask_dialogs_survive_restart_without_crossing_options')
    copied(inputs[1],'cp-a-before-empty');dialog=opened('cp');cp_set(dialog,'',False);finished(dialog,True);copied(inputs[1],'cp-empty-keeps-a')
    child.send(window,0x111,ids['Undo']);copied(assets['cp'],'cp-empty-keeps-previous-undo');case('empty_code_preserves_edited_pixels_and_prior_undo')
    close();start(profile,'cp');dialog=opened('cp');assert cp_state(dialog)==('',False);finished(dialog,False)
    dialog=opened('rainbow');assert rainbow_state(dialog)==(32,False);finished(dialog,False);case('empty_code_and_independent_previews_survive_restart');close()
    legacy=out/'legacy.ini';legacy.write_text('[Viewer]\ncp_code=a b1あ\ncp_preview=0\nrainbow_size=3\nrainbow_preview=1\nglass_size=1\n',encoding='utf-16')
    start(legacy,'cp');dialog=opened('cp');assert cp_state(dialog)==('AB',False);finished(dialog,False)
    dialog=opened('rainbow');assert rainbow_state(dialog)==(8,True);finished(dialog,False);case('legacy_ini_filters_cp_bounds_rainbow_and_keeps_independent_flags');close()
    settings=saved(legacy);assert settings['glass_size']=='1' and settings['cp_code']=='AB' and settings['rainbow_size']=='8'
    assert all(native.sha(p)==digest for p,digest in input_hashes.items());report['passed']=True
except BaseException as error:
    report['error']=type(error).__name__+': '+str(error)
    if child and child in children:report['owned_windows_at_failure']=[dict(cls=native.window_text(h,True),title=native.window_text(h)) for h in child.windows()]
    raise
finally:
    for item in reversed(children):item.close()
    report['guards_passed']=native.sha(exe)==report['executable_sha256'] and native.sha(__file__)==report['tool_sha256'] and native.sha(native.__file__)==report['helper_sha256'] and all(native.sha(p)==h for p,h in input_hashes.items())
    (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Evidence: '+str(out),flush=True);K.ReleaseMutex(mutex);K.CloseHandle(mutex);assert report['guards_passed']
