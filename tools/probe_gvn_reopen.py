"""Verify persisted startup images in owned GVN processes on a private desktop."""
from pathlib import Path
import argparse,ctypes as c,json,shutil,uuid,configparser,re,zipfile
from ctypes import wintypes as w
import benchmark_navigation as native

parser=argparse.ArgumentParser()
parser.add_argument('executable',type=Path)
parser.add_argument('--output',type=Path,default=Path('.cache/reopen-contract'))
args=parser.parse_args();root=Path(__file__).resolve().parents[1]
output=args.output.resolve()/('run-'+uuid.uuid4().hex);output.mkdir(parents=True)
exe=args.executable.resolve();U,K=native.U,native.K
native.api(K,'GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)])
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)))
assert not flags[2]&1
name=c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station,2,name,c.sizeof(name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Reopen-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None)
native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
desktop=name.value+'\\'+desktop_name
children=[]
report=dict(executable=str(exe),executable_sha256=native.sha(exe),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),private_station=True,no_user_clipboard_or_settings=True,scope='Owned GVN startup/closing persistence, no startup speed comparison.',cases=[],passed=False)

def launch(config,path=None):
    child=native.Child(exe,['--settings',config,*([path] if path else [])],desktop)
    children.append(child);return child
def main_window(child):
    return next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None)
def loaded(child,filename):
    window=child.wait(lambda:main_window(child))
    child.wait(lambda:filename in native.window_text(window),timeout=8)
    return window
def config_file(name,path,reopen=True,multiple=True):
    config=output/(name+'.ini')
    config.write_text('[Viewer]\nallow_multiple='+str(int(multiple))+'\nreopen_last='+str(int(reopen))+'\nlast_file='+str(path)+'\ndisplay_mode=1\nconfirm_unsaved=0\nprefetch_enabled=0\n',encoding='utf-16')
    return config
def passed(name,**details):
    report['cases'].append(dict(name=name,passed=True,**details));print('PASS '+name,flush=True)
def exited(child):
    assert K.WaitForSingleObject(child.info.process,10000)==0,'Owned child did not exit'
    code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)))
    assert code.value==0,code.value
def close_viewer(child,window):
    child.post(window,0x10);exited(child)
    children.remove(child);child.close()
def saved(config):
    ini=configparser.ConfigParser(interpolation=None);ini.read(config,encoding='utf-16')
    return ini['Viewer']
def archive_name(config):
    data=saved(config)
    if 'last_archive_entry_parts' not in data:return data.get('last_archive_entry','')
    count=int(data['last_archive_entry_parts']);assert 0<=count<=16
    return bytes.fromhex(''.join(data['last_archive_entry_'+str(i)] for i in range(count))).decode('utf-16-be')
def command(name):
    text=(root/'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {',1)[1].split('};',1)[0]
    value=0
    for item in text.split(','):
        match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
        if not match:continue
        key,number=match.groups()
        if number:value=int(number)
        if key==name:return value
        value+=1
    raise ValueError(name)

try:
    assets=output/'assets';assets.mkdir()
    first=assets/'01.png';second=assets/'日本語 空白 02.bmp'
    shutil.copyfile(root/'tests/fixtures/alpha.png',first)
    shutil.copyfile(root/'tests/fixtures/rgb.bmp',second)
    config=config_file('reopen',second)
    child=launch(config)
    case=dict(name='reopen_last_file_on_empty_launch',passed=False);report['cases'].append(case)
    window=loaded(child,second.name)
    case.update(passed=True,title=native.window_text(window))
    print('PASS '+case['name'],flush=True)

    explicit_config=config_file('explicit',second)
    explicit=launch(explicit_config,first);loaded(explicit,first.name)
    passed('explicit_cli_image_takes_priority_over_saved_image')

    missing_config=config_file('missing',assets/'deleted.png')
    missing=launch(missing_config);loaded(missing,first.name)
    passed('missing_previous_file_falls_back_to_folder_first')

    # Every required format follows the same persisted-startup worker path.
    for fixture in ['psd-16-3.psd','disposal.gif','blend.apng']:
        target=assets/fixture;shutil.copyfile(root/'tests/fixtures'/fixture,target)
        source_config=config_file('format-'+target.suffix[1:],target)
        source=launch(source_config);loaded(source,target.name)
        passed('reopen_required_format_'+target.suffix[1:])

    persist_config=config_file('persist',first)
    persist=launch(persist_config,second);persist_window=loaded(persist,second.name)
    close_viewer(persist,persist_window)
    assert saved(persist_config)['last_file']==str(second)
    persist=launch(persist_config);persist_window=loaded(persist,second.name)
    passed('graceful_close_saves_displayed_file_and_next_process_reopens_it')

    # Editing for viewing must not write the original when unsaved confirmation is OFF.
    original_hash=native.sha(second)
    persist.post(persist_window,0x111,command('Negative'));persist.ping(persist_window)
    close_viewer(persist,persist_window)
    assert saved(persist_config)['last_file']==str(second) and native.sha(second)==original_hash
    persist=launch(persist_config);persist_window=loaded(persist,second.name)
    passed('unsaved_edits_do_not_overwrite_source_and_reopen_original')

    persist.post(persist_window,0x111,command('CloseImage'));persist.ping(persist_window)
    close_viewer(persist,persist_window)
    assert saved(persist_config)['last_file']==''
    passed('closing_image_clears_persisted_startup_file')

    off_config=config_file('disabled',second,reopen=False)
    off=launch(off_config);off_window=off.wait(lambda:main_window(off))
    off.ping(off_window)
    assert second.name not in native.window_text(off_window)
    # Direct OFF behavior is also asserted against App.loading/image in the C++ contract.
    close_viewer(off,off_window)
    assert saved(off_config)['last_file']==''
    passed('disabled_setting_keeps_empty_launch_and_close_empty')

    empty_folder=output/'empty';empty_folder.mkdir()
    empty_config=config_file('empty',empty_folder/'gone.png')
    empty=launch(empty_config);empty_window=empty.wait(lambda:main_window(empty))
    empty.ping(empty_window)
    close_viewer(empty,empty_window)
    assert saved(empty_config)['last_file']==''
    passed('empty_previous_folder_remains_blank_without_error_dialog')

    reuse_config=config_file('reuse',first,multiple=False)
    reuse=launch(reuse_config);reuse_window=loaded(reuse,first.name)
    second_request=launch(reuse_config,second);exited(second_request)
    reuse.wait(lambda:second.name in native.window_text(reuse_window))
    empty_request=launch(reuse_config);exited(empty_request)
    assert second.name in native.window_text(reuse_window) and main_window(empty_request) is None
    passed('empty_single_instance_reuse_does_not_reopen_saved_file_over_current')

    book=assets/'book.zip';shutil.copyfile(root/'tests/fixtures/images.zip',book)
    with zipfile.ZipFile(book) as archive:
        entries=[(info,archive.read(info)) for info in archive.infolist()]
    supported=[(i,info.filename) for i,(info,data) in enumerate(entries) if Path(info.filename).suffix.lower() in {'.png','.jpg','.bmp','.gif','.apng','.psd'}]
    index,entry=supported[-1]
    book_config=config_file('book',str(book)+'|'+str(index))
    reader=launch(book_config);reader_window=loaded(reader,entry)
    close_viewer(reader,reader_window)
    assert archive_name(book_config)==entry
    with zipfile.ZipFile(book,'w') as archive:
        for info,data in reversed(entries):archive.writestr(info,data)
    reader=launch(book_config);reader_window=loaded(reader,entry)
    close_viewer(reader,reader_window)
    passed('zip_member_name_survives_changed_central_directory_index')

    report['passed']=True
except BaseException as error:
    report['error']=repr(error);raise
finally:
    for child in reversed(children):child.close()
    (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    K.ReleaseMutex(mutex);K.CloseHandle(mutex)
    print(output)
