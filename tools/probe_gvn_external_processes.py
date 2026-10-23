"""Normal GVN -> owned native receiver, private station/profile/clipboard only."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,json,os,re,shutil,struct,subprocess,sys,time,uuid
from PIL import Image
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
import benchmark_navigation as native
parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('receiver',type=Path);parser.add_argument('--output',type=Path,default=root/'.cache/external-processes');args=parser.parse_args()
out=args.output.resolve()/('run-'+uuid.uuid4().hex);out.mkdir(parents=True);(out/'sources').mkdir()
for path in [Path(__file__),Path(native.__file__)]:shutil.copyfile(path,out/'sources'/path.name)
viewer=out/'viewer 日本語';viewer.mkdir();exe=viewer/'GVN.exe';shutil.copyfile(args.executable,exe)
receiver=viewer/'受信 app'/'receiver 日本語.exe';receiver.parent.mkdir();shutil.copyfile(args.receiver,receiver)
receipts=out/'receipts';receipts.mkdir();token=uuid.uuid4().hex
work=out/'images 日本語 space';work.mkdir();asset=work/'original.alpha image.png'
original=Image.new('RGBA',(3,2));original.putdata([(7,11,13,0),(31,59,61,1),(67,71,73,127),(79,83,89,128),(97,101,103,254),(107,109,113,255)]);original.save(asset)
initial_cwd=out/'separate initial cwd';initial_cwd.mkdir()
ids={};number=0
for entry in (root/'include/gvn/commands.h').read_text().split('enum Command {',1)[1].split('};',1)[0].split(','):
 match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',entry)
 if not match:continue
 key,explicit=match.groups()
 if explicit:number=int(explicit)
 ids[key]=number;number+=1
assert ids['ExternalBase']==1030
slots=[dict(name='No send',send=0,show=0),dict(name='File',send=1,show=0),dict(name='Folder minimized',send=2,show=1),dict(name='Clipboard maximized',send=3,show=2),dict(name='Explicit args',send=1,show=0,command='"'+str(receiver)+'" /probe "($f)" "($d)" ""'),dict(name='Relative',send=1,show=0,command=str(receiver.relative_to(viewer))),dict(name='Missing no quit',send=1,show=0,quit=1,command=str(viewer/'missing owned.exe')),dict(name='Rendered legacy',send=4,show=0),dict(name='Quit successful',send=0,show=0,quit=1)]
def settings(path):
 lines=['[Viewer]','allow_multiple=1','reopen_last=0','prefetch_enabled=0','animation_autoplay=0','confirm_unsaved=0','[ExternalOptions]','Version=1']
 for index,slot in enumerate(slots,1):
  values=dict(present=1,name='"'+slot['name']+'"',command='"'+slot.get('command',str(receiver))+'"',verb='""',send=slot['send'],show=slot['show'],quit=slot.get('quit',0),separator=0,archive_member=0,archive_folder=0)
  lines.extend(str(index)+'.'+key+'='+str(value) for key,value in values.items())
 path.write_text('\n'.join(lines)+'\n',encoding='utf-16')
U,K=native.U,native.K
native.api(U,'IsWindow',w.BOOL,[native.P]);native.api(U,'EmptyClipboard',w.BOOL,[]);native.api(U,'SetClipboardData',native.P,[w.UINT,native.P]);native.api(U,'RegisterClipboardFormatW',w.UINT,[w.LPCWSTR])
native.api(K,'GlobalAlloc',native.P,[w.UINT,native.UP]);native.api(K,'GlobalFree',native.P,[native.P]);native.api(K,'GetExitCodeProcess',w.BOOL,[native.P,c.POINTER(w.DWORD)])
native.api(K,'GetTempPathW',w.DWORD,[w.DWORD,w.LPWSTR])
mutex=K.CreateMutexW(None,False,'Local'+chr(92)+'GVN.NoninteractiveClipboard');native.check(mutex);assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station);flags=(w.DWORD*3)();needed=w.DWORD();native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512);native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)));native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-External-'+uuid.uuid4().hex;native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
report=dict(passed=False,scope='Actual normal GVN and owned native receiver. Private station/profile/clipboard, generated RGBA, exact native argv and show states, source identity and successful/failed quit. No real CSP/editor or association claim.',executable_sha256=native.sha(exe),receiver_sha256=native.sha(receiver),asset_sha256=native.sha(asset),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),cases=[],parents=[])
children=[];parent=None;window=None
class Viewer(native.Child):
 def __init__(self,profile,has_image):
  self.info=native.Process();startup=native.Startup();startup.cb=c.sizeof(startup);startup.desktop=station_name.value+chr(92)+desktop_name;startup.flags=1;startup.show=1
  arguments=[str(exe),'--settings',str(profile)]+([str(asset)] if has_image else [])
  line=c.create_unicode_buffer(subprocess.list2cmdline(arguments));self.started=time.perf_counter()
  keys=['GVN_OWNED_RECEIVER_OUTPUT','GVN_OWNED_RECEIVER_TOKEN'];old={key:os.environ.get(key) for key in keys}
  os.environ[keys[0]]=str(receipts);os.environ[keys[1]]=token
  try:native.check(K.CreateProcessW(str(exe),line,None,None,False,0,None,str(initial_cwd),c.byref(startup),c.byref(self.info)))
  finally:
   for key,value in old.items():
    if value is None:os.environ.pop(key,None)
    else:os.environ[key]=value
def bounded(predicate,timeout=15):
 end=time.perf_counter()+timeout
 while time.perf_counter()<end:
  value=predicate()
  if value:return value
  time.sleep(.003)
 raise TimeoutError('Owned external-process operation timed out')
def main(child):return next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None)
def marker(label):
 bounded(lambda:U.OpenClipboard(None))
 try:
  native.check(U.EmptyClipboard());payload=(label+chr(0)).encode('utf-16le');handle=K.GlobalAlloc(0x42,len(payload));native.check(handle);pointer=K.GlobalLock(handle);native.check(pointer);c.memmove(pointer,payload,len(payload));K.GlobalUnlock(handle)
  if not U.SetClipboardData(13,handle):K.GlobalFree(handle);native.check(False)
 finally:U.CloseClipboard()
 return U.GetClipboardSequenceNumber()
def clipboard_bytes(fmt):
 bounded(lambda:U.OpenClipboard(None))
 try:
  handle=U.GetClipboardData(fmt);native.check(handle);pointer=K.GlobalLock(handle);native.check(pointer)
  try:return c.string_at(pointer,K.GlobalSize(handle))
  finally:K.GlobalUnlock(handle)
 finally:U.CloseClipboard()
def verify_rgba(receipt,expected):
 formats=receipt['clipboard_formats'];png=next(item for item in formats if item['name']=='PNG');dib=next(item for item in formats if item['format']==17)
 decoded=Image.open(receipts/png['file']).convert('RGBA');payload=(receipts/dib['file']).read_bytes();header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',payload)
 assert header==124 and bits==32 and planes==1 and compression==3
 raw=Image.frombytes('RGBA',(width,abs(height)),payload[header:],'raw','BGRA',width*4,-1 if height>0 else 1)
 assert decoded.size==raw.size==expected.size and decoded.tobytes()==raw.tobytes()==expected.tobytes()
def command(index,label,expected,has_image,quits=False,rendered=None,slot_override=None):
 before=set(receipts.glob('*.json'));sequence=marker('owned '+label);parent.post(window,0x111,ids['ExternalBase']+index)
 paths=bounded(lambda:[p for p in receipts.glob('*.json') if p not in before]);assert len(paths)==1
 record=json.loads(paths[0].read_text(encoding='utf-8'));slot=slots[index] if slot_override is None else slot_override
 assert record['passed'] and record['token']==token and record['parent_pid']==parent.info.pid and Path(record['module']).resolve()==receiver.resolve()
 assert record['station']==station_name.value and record['desktop']==desktop_name and not record['station_visible']
 assert Path(record['cwd']).resolve()==viewer.resolve()
 assert record['iconic']==(slot['show']==1) and record['zoomed']==(slot['show']==2)
 assert (record['clipboard_sequence']!=sequence)==(slot['send']==3 and has_image)
 arguments=record['argv'][1:]
 if expected is not None:assert arguments==expected,(label,arguments,expected)
 if slot['send']==3 and has_image:verify_rgba(record,original)
 if rendered is not None:
  assert len(arguments)==1 and Path(arguments[0]).resolve()!=asset.resolve();decoded=Image.open(arguments[0]).convert('RGBA');assert decoded.size==rendered.size and decoded.tobytes()==rendered.tobytes()
  shutil.copyfile(arguments[0],out/(label+'-received.png'))
  temporary=c.create_unicode_buffer(32768);native.check(K.GetTempPathW(len(temporary),temporary))
  exported=Path(arguments[0]).resolve();owned_prefix=(Path(temporary.value)/'GVN'/'drag').resolve()
  assert exported.is_relative_to(owned_prefix) and re.fullmatch(r'GVN-\{[0-9A-Fa-f-]{36}\}\.png',exported.name)
  exported.unlink();report.setdefault('owned_temporary_exports_removed',[]).append(str(exported))
 if quits:assert K.WaitForSingleObject(parent.info.process,10000)==0
 else:assert K.WaitForSingleObject(parent.info.process,0)==258;parent.ping(window)
 case=dict(label=label,slot=index+1,parent_pid=parent.info.pid,receiver_pid=record['pid'],arguments=arguments,receipt=str(paths[0].relative_to(out)),clipboard_changed=record['clipboard_sequence']!=sequence,passed=True);report['cases'].append(case);print(json.dumps(case,ensure_ascii=True),flush=True)
def failure(label):
 before=set(receipts.glob('*.json'));sequence=marker('failure '+label);parent.post(window,0x111,ids['ExternalBase']+6)
 dialog=bounded(lambda:next((h for h in parent.windows() if native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1)),None))
 parent.post(U.GetDlgItem(dialog,1),0xf5);bounded(lambda:not U.IsWindow(dialog));parent.ping(window)
 assert K.WaitForSingleObject(parent.info.process,0)==258 and set(receipts.glob('*.json'))==before and U.GetClipboardSequenceNumber()==sequence
 report['cases'].append(dict(label=label,slot=7,parent_pid=parent.info.pid,owned_error_dismissed=True,parent_remains=True,clipboard_unchanged=True,passed=True));print('PASS '+label,flush=True)
def external_dialog():
 parent.post(window,0x111,ids['ConfigureExternal'])
 dialog=parent.wait(lambda:next((h for h in parent.windows() if native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1015)),None))
 page=parent.own(U.GetDlgItem(dialog,1015));parent.wait(lambda:U.GetDlgItem(page,420) and parent.send(U.GetDlgItem(page,420),0x146)==9)
 native.check(U.SetWindowPos(parent.own(dialog),None,0,0,1000,760,0x16));return dialog,page
def external_control(page,cid):return parent.own(U.GetDlgItem(page,cid))
def external_text(page,cid):
 buffer=c.create_unicode_buffer(32768);parent.send(external_control(page,cid),0xd,len(buffer),c.addressof(buffer));return buffer.value
def external_select(page,cid,value):
 control=external_control(page,cid);assert parent.send(control,0x14e,value)==value
 parent.send(page,0x111,cid|(1<<16),control)
def external_fields(page):
 return dict(name=external_text(page,460),command=external_text(page,421),verb=external_text(page,463),send=parent.send(external_control(page,461),0x147),show=parent.send(external_control(page,462),0x147),quit=parent.send(external_control(page,464),0xf0),separator=parent.send(external_control(page,465),0xf0),archive_member=parent.send(external_control(page,466),0xf0),archive_folder=parent.send(external_control(page,467),0xf0))
def external_set_fields(dialog,page,values):
 for cid,key in [(460,'name'),(421,'command'),(463,'verb')]:
  control=external_control(page,cid);parent.send(dialog,0x28,control,1);text=c.create_unicode_buffer(values[key]);parent.send(control,0xc,0,c.addressof(text))
 for cid,key in [(461,'send'),(462,'show')]:external_select(page,cid,values[key])
 for cid,key in [(464,'quit'),(465,'separator'),(466,'archive_member'),(467,'archive_folder')]:
  control=external_control(page,cid)
  if bool(parent.send(control,0xf0))!=bool(values[key]):parent.send(control,0xf5)
 assert external_fields(page)==values
def external_close(dialog,accept):
 parent.post(U.GetDlgItem(dialog,1 if accept else 2),0xf5);parent.wait(lambda:not U.IsWindow(dialog));parent.ping(window)
def start(profile,has_image):
 global parent,window
 parent=Viewer(profile,has_image);children.append(parent);report['parents'].append(parent.info.pid)
 window=parent.wait(lambda:main(parent));parent.wait(lambda:asset.name in native.window_text(window) if has_image else native.window_text(window)=='GVN Image Viewer');parent.ping(window)
def close_viewer():
 parent.post(window,0x10);assert K.WaitForSingleObject(parent.info.process,10000)==0
 parent.close();parent.info.process=None
try:
 for has_image in [True,False]:
  profile=out/('image.ini' if has_image else 'empty.ini');settings(profile);start(profile,has_image)
  source=str(asset) if has_image else '';folder=str(asset.parent) if has_image else str(viewer)
  for index in range(6):
   expected=[] if index in (0,3) else ([source] if index in (1,5) and source else [folder] if index==2 else ['/probe',source,folder,''] if index==4 else [])
   command(index,('image-' if has_image else 'empty-')+str(index+1),expected,has_image)
  failure(('image' if has_image else 'empty')+'-missing-does-not-quit')
  if has_image:
   parent.send(window,0x111,ids['RotateRight']);expected_image=original.transpose(Image.Transpose.ROTATE_270)
   command(1,'edited-source-original-path',[str(asset)],True)
   command(7,'edited-rendered-alpha',None,True,rendered=expected_image)
  command(8,('image' if has_image else 'empty')+'-quit-success',[],has_image,quits=True)
  parent.close();parent.info.process=None
 profile=out/'gui-saved-restarted.ini';settings(profile);start(profile,True)
 first=dict(name='画面から保存 & Alpha',command=str(receiver),verb='',send=3,show=1,quit=0,separator=1,archive_member=1,archive_folder=1)
 second=dict(name='画面から保存 File',command=str(receiver),verb='open',send=1,show=2,quit=0,separator=0,archive_member=0,archive_folder=0)
 dialog,page=external_dialog();external_select(page,420,0);external_set_fields(dialog,page,first)
 external_select(page,420,1);external_set_fields(dialog,page,second);external_select(page,420,0);assert external_fields(page)==first
 report['ui_saved_fields']=[external_fields(page),second];external_close(dialog,True)
 command(0,'gui-before-restart-alpha',[],True,slot_override=first)
 command(1,'gui-before-restart-file',[str(asset)],True,slot_override=second)
 close_viewer();start(profile,True)
 dialog,page=external_dialog();loaded=[]
 for index,expected in [(0,first),(1,second)]:external_select(page,420,index);loaded.append(external_fields(page));assert loaded[-1]==expected
 report['ui_restarted_fields']=loaded;report['same_profile_native_ui_restart_verified']=True;external_close(dialog,False)
 command(0,'gui-after-restart-alpha',[],True,slot_override=first)
 command(1,'gui-after-restart-file',[str(asset)],True,slot_override=second)
 close_viewer()
 assert native.sha(asset)==report['asset_sha256'];report['passed']=True
except BaseException as error:
 report['error']=type(error).__name__+': '+str(error)
 if parent and parent.info.process:report['owned_windows_at_failure']=[dict(cls=native.window_text(h,True),title=native.window_text(h)) for h in parent.windows()]
 raise
finally:
 for child in reversed(children):
  if child.info.process:child.close();child.info.process=None
 assert native.sha(__file__)==report['tool_sha256']
 (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Evidence: '+str(out),flush=True)
 K.ReleaseMutex(mutex);K.CloseHandle(mutex)
