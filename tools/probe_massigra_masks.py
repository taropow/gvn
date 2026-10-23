"""Probe-owned official 0.45 mask controls and pixel results; private desktop only."""
from pathlib import Path
from ctypes import wintypes as w
import ctypes as c
import json,shutil,struct,sys,time,uuid,zipfile
from PIL import Image
import benchmark_navigation as native

root=Path(__file__).resolve().parents[1]
phase=sys.argv[1] if len(sys.argv)>1 else 'inspect'
assert phase in {'inspect','measure','palette','fl','fl-selection','mask-selection','remaining-inspect','remaining-measure','remaining-extra','meko-large','rainbow-dialog','cp-letters','cp-dialog'}
out=root/'.cache/mask-reference'/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
archive=root/'.cache/massigra-reference/MassiGra045.zip'
assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
reference=out/'reference'
with zipfile.ZipFile(archive) as package:
 for name in package.namelist():
  if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
   target=(reference/name).resolve();assert target.is_relative_to(reference.resolve())
   target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(package.read(name))
assert native.sha(reference/'MassiGra.exe')=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
U,K=native.U,native.K
for name,result,args in [
 ('IsWindow',w.BOOL,[native.P]),('IsWindowEnabled',w.BOOL,[native.P]),('GetParent',native.P,[native.P]),
 ('GetWindowRect',w.BOOL,[native.P,c.POINTER(w.RECT)]),
 ('GetMenuState',w.UINT,[native.P,w.UINT,w.UINT]),
 ('GetDlgCtrlID',c.c_int,[native.P]),('GetMenu',native.P,[native.P]),('GetSubMenu',native.P,[native.P,c.c_int]),
 ('GetMenuItemCount',c.c_int,[native.P]),('GetMenuItemID',w.UINT,[native.P,c.c_int]),
 ('GetMenuStringW',c.c_int,[native.P,w.UINT,w.LPWSTR,c.c_int,w.UINT])]:native.api(U,name,result,args)
for name,result,args in [('VirtualAllocEx',native.P,[native.P,native.P,native.UP,w.DWORD,w.DWORD]),
 ('VirtualFreeEx',w.BOOL,[native.P,native.P,native.UP,w.DWORD]),
 ('ReadProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P]),
 ('WriteProcessMemory',w.BOOL,[native.P,native.P,native.P,native.UP,native.P])]:native.api(K,name,result,args)
mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
station=U.CreateWindowStationW(None,0,0x37f,None);native.check(station)
flags=(w.DWORD*3)();needed=w.DWORD()
native.check(U.GetUserObjectInformationW(station,1,flags,c.sizeof(flags),c.byref(needed)));assert not flags[2]&1
station_name=c.create_unicode_buffer(512)
native.check(U.GetUserObjectInformationW(station,2,station_name,c.sizeof(station_name),c.byref(needed)))
native.check(U.SetProcessWindowStation(station))
desktop_name='GVN-Mask-'+uuid.uuid4().hex
native.desktop=U.CreateDesktopW(desktop_name,None,None,0,0x10000000,None);native.check(native.desktop);native.check(U.SetThreadDesktop(native.desktop))
asset=out/'source.png';original=Image.new('RGB',(29,19))
original.putdata([((x*31+y*11+20)%256,(x*13+y*29+40)%256,(x*17+y*19+60)%256) for y in range(19) for x in range(29)])
assert len(set(original.getdata()))==29*19
if phase == 'palette':
 paletted=Image.new('P',original.size)
 paletted.putpalette([value for i in range(256) for value in [(i*37)%256,(i*71)%256,(i*109)%256]])
 paletted.putdata([(x+y*3)%16 for y in range(19) for x in range(29)])
 paletted.save(asset,bits=4);original=paletted.convert('RGB')
else:original.save(asset)
child=native.Child(reference/'MassiGra.exe',[asset],station_name.value+'\\'+desktop_name)
report=dict(passed=False,reference_sha256=native.sha(reference/'MassiGra.exe'),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),controlled_pid=child.info.pid,
 scope='Own noninteractive official process/profile/synthetic opaque source/clipboard. Actual mask settings, menu commands and copied RGB. No ComputerUse/Sky/global input/user data/startup comparison.')

def descendants(parent):
 result=[]
 @native.Callback
 def add(hwnd,_):result.append(child.own(hwnd));return True
 U.EnumChildWindows(child.own(parent),add,0);return result

def text(control):
 value=c.create_unicode_buffer(4096);child.send(child.own(control),0xd,len(value),c.addressof(value));return value.value

def settings(main):
 child.post(main,0x111,40101)
 dialog=child.wait(lambda:next((h for h in child.windows() if '設定' in native.window_text(h) and U.GetDlgItem(h,1)),None))
 tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None))
 remote=K.VirtualAllocEx(child.info.process,None,1024,0x3000,4);native.check(remote)
 try:
  captions=[]
  for i in range(child.send(tab,0x1304)):
   value=c.create_string_buffer(struct.pack('<IIIIiii',1,0,0,remote+64,256,0,0))
   native.check(K.WriteProcessMemory(child.info.process,remote,value,28,None));native.check(child.send(tab,0x133c,i,remote))
   value=c.create_string_buffer(512);native.check(K.ReadProcessMemory(child.info.process,remote+64,value,512,None))
   captions.append(value.raw.decode('utf-16-le').split('\0')[0])
  report['settings_tabs']=captions;assert captions.count('マスク')==1
  index=captions.index('マスク');native.check(child.send(tab,0x130a,index,remote));rect=(c.c_int*4)()
  native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None))
  x,y=(rect[0]+rect[2])//2,(rect[1]+rect[3])//2;child.post(tab,0x201,1,x|y<<16);child.post(tab,0x202,0,x|y<<16)
 finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
 child.wait(lambda:child.send(tab,0x130b)==index)
 field=child.wait(lambda:next((h for h in descendants(dialog) if U.GetDlgCtrlID(h)==2013 and native.window_text(h,True)=='Button'),None))
 page=child.own(U.GetParent(field));child.wait(lambda:all(U.GetDlgItem(page,id) for id in [2021,2031,2012,2061]))
 child.ping(dialog);return dialog,page

def controls(page):
 result=[]
 for h in descendants(page):
  cls=native.window_text(h,True)
  if cls not in {'Button','Edit','ComboBox','Static','msctls_updown32'}:continue
  row=dict(id=U.GetDlgCtrlID(h),cls=cls,text=text(h),enabled=bool(U.IsWindowEnabled(h)),style_visible=bool(U.GetWindowLongW(h,-16)&0x10000000))
  if cls=='Button':row['checked']=child.send(h,0xf0)
  if cls=='ComboBox':
   row.update(selected=child.send(h,0x147),choices=[])
   for i in range(child.send(h,0x146)):
    buffer=c.create_unicode_buffer(512);child.send(h,0x148,i,c.addressof(buffer));row['choices'].append(buffer.value)
  if cls=='msctls_updown32':row['range16_raw']=child.send(h,0x466)
  result.append(row)
 return result

def menu_items(menu,path=()):
 result=[]
 for i in range(U.GetMenuItemCount(menu)):
  value=c.create_unicode_buffer(512);U.GetMenuStringW(menu,i,value,512,0x400)
  label=value.value;sub=U.GetSubMenu(menu,i)
  if sub:result.extend(menu_items(sub,(*path,label)))
  else:result.append(dict(command=U.GetMenuItemID(menu,i),label=label,path=list(path)))
 return result

def configure(main,options):
 dialog,page=settings(main)
 bits={**{2001+i:bool(options['negative']&(1<<i)) for i in range(3)},
       **{2004+i:bool(options['xor']&(1<<i)) for i in range(3)},
       **{2007+i:bool(options['q0']&(1<<i)) for i in range(6)},
       2013:options['fl_negative'],2014:options['remainder']}
 for id,value in bits.items():
  control=child.own(U.GetDlgItem(page,id))
  if bool(child.send(control,0xf0))!=value:child.send(control,0xf5)
  assert bool(child.send(control,0xf0))==value
 combo=child.own(U.GetDlgItem(page,2021));assert child.send(combo,0x14e,options['swap'])==options['swap']
 child.send(page,0x111,2021|(1<<16),combo)
 buffer=c.create_unicode_buffer(str(options['size']));child.send(child.own(U.GetDlgItem(page,2031)),0xc,0,c.addressof(buffer))
 child.post(child.own(U.GetDlgItem(dialog,1)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)

def copied(main,label,expected_size=None):
 sequence=U.GetClipboardSequenceNumber();child.post(main,0x111,40025)
 child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and (U.IsClipboardFormatAvailable(17) or U.IsClipboardFormatAvailable(8)))
 child.wait(lambda:bool(U.OpenClipboard(None)))
 try:
  format=17 if U.IsClipboardFormatAvailable(17) else 8;handle=U.GetClipboardData(format)
  pointer=K.GlobalLock(handle);native.check(pointer)
  try:data=c.string_at(pointer,K.GlobalSize(handle))
  finally:K.GlobalUnlock(handle)
 finally:U.CloseClipboard()
 header,width,height,planes,bits,compression=struct.unpack_from('<IiiHHI',data)
 assert header in (40,108,124) and planes==1 and bits in (1,2,4,8,24,32) and compression in (0,3)
 offset=header+(12 if header==40 and compression==3 else 0)+struct.unpack_from('<I',data,32)[0]*4
 stride=((width*bits+31)//32)*4
 details={}
 if bits <= 8:
  assert compression==0
  count=struct.unpack_from('<I',data,32)[0] or (1<<bits)
  palette=[tuple(reversed(data[header+i*4:header+i*4+3])) for i in range(count)]
  offset=header+count*4;indices=[]
  for y in range(abs(height)):
   row=abs(height)-1-y if height>0 else y
   for x in range(width):indices.append((data[offset+row*stride+x*bits//8]>>(8-bits-x*bits%8))&((1<<bits)-1))
  image=Image.new('RGB',(width,abs(height)));image.putdata([palette[i] for i in indices])
  details=dict(palette=palette,indices=indices)
 else:image=Image.frombytes('RGB',(width,abs(height)),data[offset:],'raw','BGR' if bits==24 else 'BGRX',stride,-1 if height>0 else 1)
 if expected_size is not False:assert image.size==(expected_size or original.size)
 target=out/'pixels'/(label+'.png');target.parent.mkdir(exist_ok=True);image.save(target)
 return image,dict(file=target.relative_to(out).as_posix(),sha256=native.sha(target),size=list(image.size),clipboard_format=format,bits=bits,**details)

def palette_cases(main):
 source,metadata=copied(main,'palette-source');assert source.tobytes()==original.tobytes()
 report['palette_source']=metadata;report['palette_cases']=[]
 for kind,command in [('negative',40643),('xor',40644),('swap',40645),('vertical',40641),('q0',40646)]:
  child.send(main,0x111,40002);child.send(main,0x111,command)
  image,metadata=copied(main,'palette-'+kind)
  report['palette_cases'].append(dict(kind=kind,command=command,result=metadata))

def measure(main):
 default=dict(size=8,swap=3,negative=7,xor=7,q0=7,remainder=False,fl_negative=True)
 report['cases']=[];source,metadata=copied(main,'source');assert source.tobytes()==original.tobytes()
 report['copied_original']=metadata
 commands={'vertical':40641,'horizontal':40642,'negative':40643,'xor':40644,'swap':40645,'q0':40646,'win':40647,'fl':40648,'image_negative':40045}
 def run(kind,overrides,repeat=False):
  options={**default,**overrides};configure(main,options);child.send(main,0x111,40002)
  child.send(main,0x111,commands[kind]);label=f"case-{len(report['cases']):04d}"
  image,metadata=copied(main,label)
  record=dict(label=label,kind=kind,command=commands[kind],options=options,result=metadata)
  report['cases'].append(record)
  if repeat:
   child.send(main,0x111,commands[kind]);again,metadata=copied(main,label+'-twice')
   record.update(twice=metadata,involutive=again.tobytes()==original.tobytes())
   assert record['involutive'],record
 for channel in range(8):
  run('negative',dict(negative=channel),True);run('xor',dict(xor=channel),True)
 for swap in range(5):run('swap',dict(swap=swap))
 for channel in [0,1]:run('image_negative',dict(negative=channel),True)
 for size in [2,3,5,8,16,999]:
  for remainder in [False,True]:
   for kind in ['vertical','horizontal']:run(kind,dict(size=size,remainder=remainder),True)
 for size in [3,5,8]:
  for remainder in [False,True]:
   for flags in range(64):run('q0',dict(size=size,remainder=remainder,q0=flags),True)
 for remainder in [False,True]:
  for flags in range(64):run('q0',dict(size=3,remainder=remainder,q0=flags,negative=1,xor=2,swap=4),True)
 run('win',{})
 for enabled in [False,True]:run('fl',dict(fl_negative=enabled),True)
 assert len(report['cases'])==562
 report['rgb_pixels_per_capture']=29*19

def fl_cases(main):
 global original
 inputs=out/'inputs';inputs.mkdir();shutil.copyfile(asset,out/'initial-source.png')
 shapes=[(1,1),(7,7),(7,9),(9,7),(8,8),(16,8),(8,16),(16,16),(24,8),(8,24),
         (24,16),(16,24),(24,24),(32,24),(24,32),(40,24),(24,40),(40,32),
         (32,40),(48,40),(40,48),(56,48),(48,56),(64,64),(72,56),(56,72),
         (17,19),(29,19),(49,43),(63,65),(81,73),(65,81)]
 report['fl_cases']=[]
 for indexed,sizes in [(False,shapes),(True,[(7,7),(8,8),(16,16),(29,19),(49,43),(64,64)])]:
  for n,(width,height) in enumerate(sizes):
   source=Image.new('P' if indexed else 'RGB',(width,height))
   if indexed:
    source.putpalette([value for i in range(256) for value in [(i*37)%256,(i*71)%256,(i*109)%256]])
    source.putdata([(x+y*3)%16 for y in range(height) for x in range(width)])
    source.save(asset,bits=4)
   else:
    source.putdata([(x,y,(x*17+y*31)%256) for y in range(height) for x in range(width)])
    source.save(asset)
   original=source.convert('RGB');name=('indexed' if indexed else 'rgb')+f'-{width}x{height}'
   path=inputs/(name+'.png');shutil.copyfile(asset,path)
   for negative in [False,True]:
    options=dict(size=3+n%3,swap=n%5,negative=n%8,xor=(n//3)%8,q0=n%64,remainder=bool(n&1),fl_negative=negative)
    configure(main,options);child.send(main,0x111,40002)
    label=name+('-invert' if negative else '-plain')
    before,metadata=copied(main,label+'-before');assert before.tobytes()==original.tobytes()
    child.send(main,0x111,40648);image,result=copied(main,label)
    child.send(main,0x111,40648);twice,repeated=copied(main,label+'-twice')
    row=dict(label=label,source=dict(file=path.relative_to(out).as_posix(),sha256=native.sha(path),size=[width,height],indexed=indexed),
             options=options,before=metadata,result=result,twice=repeated,involutive=twice.tobytes()==original.tobytes())
    report['fl_cases'].append(row);assert row['involutive'],row
 assert len(report['fl_cases'])==76

def reference_selection(main,rectangle=None):
 child.post(main,0x111,40023)
 dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770' and '選択' in native.window_text(h)),None))
 child.wait(lambda:all(U.GetDlgItem(dialog,id) for id in [2031,2032,2033,2034,2035,2036,1,2]));child.ping(dialog)
 if rectangle:
  for id,value in zip([2031,2032,2035,2036],rectangle):
   field=child.own(U.GetDlgItem(dialog,id));child.send(dialog,0x28,field,1)
   buffer=c.create_unicode_buffer(str(value));child.send(field,0xc,0,c.addressof(buffer));child.send(dialog,0x28,0,0)
 values={str(id):text(child.own(U.GetDlgItem(dialog,id))) for id in range(2031,2037)}
 child.post(child.own(U.GetDlgItem(dialog,1 if rectangle else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));return values

def fl_selection_cases(main):
 global original
 report['fl_selection_cases']=[];inputs=out/'inputs';inputs.mkdir()
 for indexed in [False,True]:
  source=Image.new('P' if indexed else 'RGB',(41,35))
  if indexed:
   source.putpalette([v for i in range(256) for v in [(i*37)%256,(i*71)%256,(i*109)%256]])
   source.putdata([(x+y*3)%16 for y in range(35) for x in range(41)]);source.save(asset,bits=4)
  else:source.putdata([(x,y,(x*17+y*31)%256) for y in range(35) for x in range(41)]);source.save(asset)
  original=source.convert('RGB');path=inputs/('indexed.png' if indexed else 'rgb.png');shutil.copyfile(asset,path)
  tasks=[('fl',40648,rect,negative) for rect in [(0,0,24,16),(3,2,29,19),(5,7,31,25),(7,1,7,7)] for negative in [False,True]]
  if phase=='mask-selection':tasks.extend((kind,command,(3,2,29,19),False) for kind,command in [('vertical',40641),('horizontal',40642),('negative',40643),('xor',40644),('swap',40645),('q0',40646),('win',40647)])
  for kind,command,rect,negative in tasks:
    label=f"selection-{len(report['fl_selection_cases']):02d}"
    options=dict(size=5,swap=4 if kind=='fl' else 0,negative=1,xor=2,q0=63,remainder=True,fl_negative=negative)
    configure(main,options)
    child.send(main,0x111,40002);selected=reference_selection(main,rect)
    region,before=copied(main,label+'-before',False)
    x,y,width,height=rect;expected=original.crop((x,y,x+width,y+height));assert region.tobytes()==expected.tobytes() and region.size==expected.size
    child.send(main,0x111,command);_,after=copied(main,label+'-after',False);after_selection=reference_selection(main)
    reference_selection(main,(0,0,41,35));result,metadata=copied(main,label)
    reference_selection(main,rect);child.send(main,0x111,command)
    reference_selection(main,(0,0,41,35));twice,repeated=copied(main,label+'-twice');assert twice.tobytes()==original.tobytes()
    reference_selection(main,rect);child.send(main,0x111,40021);_,undo_copy=copied(main,label+'-undo-selection',False);undo_selection=reference_selection(main)
    reference_selection(main,(0,0,41,35));undo,undo_full=copied(main,label+'-undo-full');assert undo.tobytes()==result.tobytes()
    report['fl_selection_cases'].append(dict(label=label,kind=kind,command=command,options=options,source=dict(file=path.relative_to(out).as_posix(),sha256=native.sha(path),size=[41,35],indexed=indexed),rectangle=list(rect),negative=negative,before_selection=selected,after_selection=after_selection,undo_selection=undo_selection,before=before,after=after,result=metadata,twice=repeated,undo_copy=undo_copy,undo_full=undo_full,involutive=True))
 assert len(report['fl_selection_cases'])==(30 if phase=='mask-selection' else 16)

def remaining_inspect(main):
 report['remaining_dialogs']=[]
 for kind,command in [('cp',40649),('rainbow',40652)]:
  child.send(main,0x111,40002)
  child.post(main,0x111,command)
  dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1) and U.GetDlgItem(h,2)),None))
  child.ping(dialog)
  rows=controls(dialog)
  for row in rows:
   if row['cls']=='Edit':row['limit']=child.send(child.own(U.GetDlgItem(dialog,row['id'])),0xd5)
  report['remaining_dialogs'].append(dict(kind=kind,command=command,title=native.window_text(dialog),controls=rows))
  child.post(child.own(U.GetDlgItem(dialog,2)),0xf5);child.wait(lambda:not U.IsWindow(dialog))
  image,metadata=copied(main,kind+'-cancel');assert image.tobytes()==original.tobytes()
  report['remaining_dialogs'][-1]['cancel']=metadata

def rainbow_dialog_contract(main):
 global child
 report['rainbow_dialog_contract']=[]
 def view_state():
  window=w.RECT();client=w.RECT();native.check(U.GetWindowRect(main,c.byref(window)));native.check(U.GetClientRect(main,c.byref(client)))
  status=[]
  for bar in descendants(main):
   if 'status' not in native.window_text(bar,True).casefold():continue
   count=child.send(bar,0x406);parts=[]
   assert 0<=count<=64
   remote=K.VirtualAllocEx(child.info.process,None,8192,0x3000,4);native.check(remote)
   try:
    for part in range(count):
     length=child.send(bar,0x40c,part)&0xffff;assert length<4096
     child.send(bar,0x40d,part,remote);value=c.create_string_buffer((length+1)*2)
     native.check(K.ReadProcessMemory(child.info.process,remote,value,len(value),None));parts.append(value.raw.decode('utf-16-le').split('\0')[0])
   finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
   status.append(dict(cls=native.window_text(bar,True),parts=parts))
  return dict(title=native.window_text(main),window=[window.left,window.top,window.right,window.bottom],client=[client.right,client.bottom],status=status)
 def opened(label):
  child.post(main,0x111,40652)
  dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and '虹色' in native.window_text(h) and U.GetDlgItem(h,2146)),None))
  child.ping(dialog);report['rainbow_dialog_contract'].append(dict(label=label,controls=controls(dialog)));return dialog
 def close(dialog,accept):
  child.post(child.own(U.GetDlgItem(dialog,1 if accept else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)
 def values(dialog,size,preview):
  child.send(child.own(U.GetDlgItem(dialog,2141+[1,2,4,8,16,32].index(size))),0xf5)
  check=child.own(U.GetDlgItem(dialog,2001))
  if bool(child.send(check,0xf0))!=preview:child.send(check,0xf5)
  child.ping(dialog)
 initial,initial_copy=copied(main,'rainbow-before');assert initial.tobytes()==original.tobytes()
 report['rainbow_view_contract']={'initial':view_state()}
 dialog=opened('first');values(dialog,2,False)
 report['rainbow_dialog_contract'].append(dict(label='changed-before-cancel',controls=controls(dialog)));close(dialog,False)
 image,metadata=copied(main,'rainbow-cancel-first');assert image.tobytes()==initial.tobytes();report['rainbow_cancel_first']=metadata
 dialog=opened('after-cancel');values(dialog,4,False);close(dialog,True)
 accepted,metadata=copied(main,'rainbow-accepted-four');report['rainbow_accepted_four']=metadata
 report['rainbow_view_contract']['accepted']=view_state()
 child.send(main,0x111,40021);child.ping(main)
 image,metadata=copied(main,'rainbow-view-undo');assert image.tobytes()==initial.tobytes()
 report['rainbow_view_contract']['undo']=view_state();report['rainbow_view_undo_copy']=metadata
 # Official Undo is one-way; apply the same mask again to restore the accepted test state.
 remaining_apply(main,'rainbow',size=4)
 image,metadata=copied(main,'rainbow-view-reapplied');assert image.tobytes()==accepted.tobytes()
 report['rainbow_view_contract']['reapplied']=view_state();report['rainbow_view_reapplied_copy']=metadata
 dialog=opened('after-accept');values(dialog,1,True);close(dialog,False)
 image,metadata=copied(main,'rainbow-cancel-second');assert image.tobytes()==accepted.tobytes();report['rainbow_cancel_second']=metadata
 dialog=opened('after-second-cancel');close(dialog,False)
 child.post(main,0x111,40649)
 cp=child.wait(lambda:next((h for h in child.windows() if 'CPマスク' in native.window_text(h) and U.GetDlgItem(h,2001)),None));child.ping(cp)
 report['cp_preview_after_rainbow_accept']=bool(child.send(child.own(U.GetDlgItem(cp,2001)),0xf0));close(cp,False)
 child.post(main,0x10)
 deadline=time.monotonic()+10
 while K.WaitForSingleObject(child.info.process,0)!=0:
  assert time.monotonic()<deadline,'Owned graceful close timed out'
  for window in child.windows():
   if window!=main and native.window_text(window,True)=='#32770' and U.GetDlgItem(window,7):child.post(child.own(U.GetDlgItem(window,7)),0xf5)
  time.sleep(.01)
 child.close();child=native.Child(reference/'MassiGra.exe',[asset],station_name.value+'\\'+desktop_name)
 report['restarted_pid']=child.info.pid
 main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:'source.png' in native.window_text(main));child.ping(main)
 dialog=opened('after-profile-restart');close(dialog,False)

def remaining_apply(main,kind,code='',size=8):
 commands={'cp':40649,'meko-forward':40650,'meko-reverse':40651,'rainbow':40652}
 if kind.startswith('meko'):
  child.send(main,0x111,commands[kind]);return
 child.post(main,0x111,commands[kind])
 dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and native.window_text(h,True)=='#32770' and U.GetDlgItem(h,1) and U.GetDlgItem(h,2)),None))
 control_id=2031 if kind=='cp' else 2141+[1,2,4,8,16,32].index(size)
 child.wait(lambda:U.GetDlgItem(dialog,control_id));child.ping(dialog)
 if kind=='cp':
  control=child.own(U.GetDlgItem(dialog,2031));value=c.create_unicode_buffer(code);child.send(control,0xc,0,c.addressof(value))
  assert text(control).casefold()==code.casefold()
 else:
  child.send(child.own(U.GetDlgItem(dialog,2141+[1,2,4,8,16,32].index(size))),0xf5)
 child.post(child.own(U.GetDlgItem(dialog,1)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)

def cp_dialog_contract(main):
 global child
 report['cp_dialog_contract']=[]
 def opened(label):
  child.post(main,0x111,40649)
  dialog=child.wait(lambda:next((h for h in child.windows() if h!=main and 'CPマスク' in native.window_text(h) and U.GetDlgItem(h,2031)),None))
  child.ping(dialog)
  report['cp_dialog_contract'].append(dict(label=label,controls=controls(dialog),code=text(child.own(U.GetDlgItem(dialog,2031))),preview=bool(child.send(child.own(U.GetDlgItem(dialog,2001)),0xf0))))
  return dialog
 def values(dialog,code,preview):
  field=child.own(U.GetDlgItem(dialog,2031));check=child.own(U.GetDlgItem(dialog,2001))
  if bool(child.send(check,0xf0))!=preview:child.send(check,0xf5)
  value=c.create_unicode_buffer(code);child.send(field,0xc,0,c.addressof(value));child.ping(dialog)
 def closed(dialog,accept):
  child.post(child.own(U.GetDlgItem(dialog,1 if accept else 2)),0xf5);child.wait(lambda:not U.IsWindow(dialog));child.ping(main)
 first=opened('first');report['cp_edit_limit']=child.send(child.own(U.GetDlgItem(first,2031)),0xd5)
 values(first,'ab',False);closed(first,False)
 image,metadata=copied(main,'cp-cancel-first');assert image.tobytes()==original.tobytes();report['cp_cancel_first']=metadata
 dialog=opened('after-cancel');values(dialog,'a',False);closed(dialog,True)
 _,report['cp-accepted-a']=copied(main,'cp-accepted-a')
 dialog=opened('after-accept');values(dialog,'b',True);closed(dialog,False)
 dialog=opened('after-second-cancel');closed(dialog,False)
 report['cp_invalid_inputs']=[]
 for code in ['a b','ab1','あ','abcdefghijklmnopq']:
  dialog=opened('invalid-'+str(len(report['cp_invalid_inputs'])));values(dialog,code,False)
  row=dict(input=code,text=text(child.own(U.GetDlgItem(dialog,2031))),ok_enabled=bool(U.IsWindowEnabled(child.own(U.GetDlgItem(dialog,1)))))
  if row['ok_enabled']:
   child.post(child.own(U.GetDlgItem(dialog,1)),0xf5)
   deadline=time.monotonic()+2;error=None
   while time.monotonic()<deadline and U.IsWindow(dialog):
    error=next((h for h in child.windows() if h not in {main,dialog} and U.GetDlgItem(h,1)),None)
    if error:break
    time.sleep(.005)
   row['accepted']=not bool(U.IsWindow(dialog))
   if error:
    row['error_title']=native.window_text(error);row['error_controls']=controls(error)
    child.post(child.own(U.GetDlgItem(error,1)),0xf5);child.wait(lambda:not U.IsWindow(error))
  report['cp_invalid_inputs'].append(row)
  if U.IsWindow(dialog):closed(dialog,False)
 child.send(main,0x111,40002);before,metadata=copied(main,'cp-empty-before')
 report['cp_empty_undo_before']=U.GetMenuState(U.GetMenu(main),40021,0)
 dialog=opened('before-empty');values(dialog,'',True);closed(dialog,True)
 image,metadata=copied(main,'cp-empty-after');assert image.tobytes()==before.tobytes()
 report['cp_empty_copy']=metadata;report['cp_empty_undo_after']=U.GetMenuState(U.GetMenu(main),40021,0)
 # Distinguish an empty-code no-op from replacing the one-step Undo buffer.
 child.send(main,0x111,40002)
 remaining_apply(main,'cp',code='A');one,report['cp_empty_history_first']=copied(main,'cp-empty-history-first')
 remaining_apply(main,'cp',code='B');two,report['cp_empty_history_second']=copied(main,'cp-empty-history-second')
 remaining_apply(main,'cp',code='');empty,report['cp_empty_history_empty']=copied(main,'cp-empty-history-empty')
 assert empty.tobytes()==two.tobytes()
 child.send(main,0x111,40021);undone,report['cp_empty_history_undo']=copied(main,'cp-empty-history-undo')
 report['cp_empty_replaces_undo']=undone.tobytes()==two.tobytes()
 report['cp_empty_preserves_prior_undo']=undone.tobytes()==one.tobytes()
 assert report['cp_empty_replaces_undo']!=report['cp_empty_preserves_prior_undo']
 dialog=opened('after-empty');values(dialog,'AB',False);closed(dialog,True)
 child.post(main,0x111,40652)
 rainbow=child.wait(lambda:next((h for h in child.windows() if h!=main and '虹色' in native.window_text(h) and U.GetDlgItem(h,2146)),None));child.ping(rainbow)
 report['rainbow_preview_after_cp_accept']=bool(child.send(child.own(U.GetDlgItem(rainbow,2001)),0xf0));closed(rainbow,False)
 child.post(main,0x10);deadline=time.monotonic()+10
 while K.WaitForSingleObject(child.info.process,0)!=0:
  assert time.monotonic()<deadline,'Owned graceful close timed out'
  for window in child.windows():
   if window!=main and U.GetDlgItem(window,7):child.post(child.own(U.GetDlgItem(window,7)),0xf5)
  time.sleep(.01)
 child.close();child=native.Child(reference/'MassiGra.exe',[asset],station_name.value+'\\'+desktop_name)
 report['restarted_pid']=child.info.pid
 main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None));child.wait(lambda:'source.png' in native.window_text(main));child.ping(main)
 dialog=opened('after-profile-restart');closed(dialog,False)

def remaining_measure(main,extra=False,large=False,letters=False):
 global original
 inputs=out/'inputs';inputs.mkdir();shutil.copyfile(asset,out/'initial-source.png');report['remaining_cases']=[]
 def run(kind,shape,code='',size=8,indexed=False,rectangle=None):
  global original
  width,height=shape;source=Image.new('P' if indexed else 'RGB',shape)
  if indexed:
   source.putpalette([v for i in range(256) for v in [(i*37)%256,(i*71)%256,(i*109)%256]])
   source.putdata([(x+y*3)%16 for y in range(height) for x in range(width)]);source.save(asset,bits=4)
  else:
   values=[(i&255,(i>>8)&255,(i>>16)&255) for i in range(width*height)] if max(shape)>256 else [(x,y,(x*17+y*31)%256) for y in range(height) for x in range(width)]
   source.putdata(values);source.save(asset)
  original=source.convert('RGB');label=f"remaining-{len(report['remaining_cases']):03d}"
  path=inputs/(label+'.png');shutil.copyfile(asset,path);child.send(main,0x111,40002)
  if rectangle:reference_selection(main,rectangle)
  before=copied(main,label+'-before',False)[1]
  remaining_apply(main,kind,code,size)
  after=copied(main,label+'-after',False)[1]
  after_selection=reference_selection(main) if rectangle else None
  if rectangle:reference_selection(main,(0,0,width,height))
  actual,result=copied(main,label)
  if rectangle:reference_selection(main,rectangle)
  inverse='meko-reverse' if kind=='meko-forward' else 'meko-forward' if kind=='meko-reverse' else kind
  remaining_apply(main,inverse,code,size)
  if rectangle:reference_selection(main,(0,0,width,height))
  restored,twice=copied(main,label+'-restored')
  report['remaining_cases'].append(dict(label=label,kind=kind,code=code,block_size=size,
    source=dict(file=path.relative_to(out).as_posix(),sha256=native.sha(path),size=list(shape),indexed=indexed),
    rectangle=rectangle,before=before,after=after,after_selection=after_selection,result=result,restored=twice,
    inverse_restores_original=restored.tobytes()==original.tobytes()))
 if letters:
  for code in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':run('cp',(136,152),code=code)
  for code in ['','a','b','ab','MassiGra','abcdefghijklmnop']:
   run('cp',(40,24),code=code)
  for code in ['','a','z','ab']:
   for shape in [(1,1),(7,7)]:run('cp',shape,code=code)
  report['remaining_summary']={'cp':len(report['remaining_cases'])}
  return
 if large:
  for kind in ['meko-forward','meko-reverse']:
   for shape in [(1520,1552)]:run(kind,shape)
  report['remaining_summary']={kind:sum(r['kind']==kind for r in report['remaining_cases']) for kind in ['meko-forward','meko-reverse']}
  return
 if extra:
  for kind in ['meko-forward','meko-reverse']:
   for shape in [(256,256),(512,512),(1024,1024)]:run(kind,shape)
   for shape in [(64,48),(16,16)]:run(kind,shape,indexed=True)
  for code in ['', 'a','ab','abcdefghijklmnop']:run('cp',(64,48),code=code,indexed=True)
  for shape in [(7,7),(8,8),(31,31),(32,32)]:
   for size in [8,32]:run('rainbow',shape,size=size,indexed=True)
  report['remaining_summary']={kind:sum(r['kind']==kind for r in report['remaining_cases']) for kind in ['cp','meko-forward','meko-reverse','rainbow']}
  return
 # Small, rectangular and non-multiple grids expose edge policy and local coordinates.
 for kind in ['meko-forward','meko-reverse']:
  for shape in [(1,1),(7,7),(15,15),(16,16),(32,16),(16,32),(32,32),(48,16),(16,48),(48,32),(32,48),(64,48),(48,64),(80,64),(64,80),(112,96),(17,19),(31,35),(49,43),(81,73)]:run(kind,shape)
  for shape in [(7,7),(16,16),(49,43),(81,73)]:run(kind,shape,indexed=True)
  for indexed in [False,True]:run(kind,(107,89),indexed=indexed,rectangle=(3,2,65,49))
 for code in ['', 'a','A','b','ab','ba','gvn','GVN','MassiGra','abcdefghijklmnop','zzzzzzzzzzzzzzzz']:
  for shape in [(8,8),(24,16),(29,19),(65,49),(81,73)]:run('cp',shape,code=code)
 for code in ['','a','ab','abcdefghijklmnop']:
  run('cp',(65,49),code=code,indexed=True)
  run('cp',(107,89),code=code,rectangle=(3,2,65,49))
 for size in [1,2,4,8,16,32]:
  for indexed in [False,True]:run('rainbow',(53,47),size=size,indexed=indexed)
  run('rainbow',(53,47),size=size,rectangle=(3,2,29,19))
 report['remaining_summary']={kind:sum(r['kind']==kind for r in report['remaining_cases']) for kind in ['cp','meko-forward','meko-reverse','rainbow']}

try:
 main=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
 child.wait(lambda:'source.png' in native.window_text(main));child.ping(main)
 if not U.GetMenu(child.own(main)):child.send(main,0x111,41008)
 report['menu']=menu_items(U.GetMenu(child.own(main)))
 dialog,page=settings(main);report['initial_controls']=controls(page)
 # Change every checkbox, the swap choice and glass size, then read a real page reset.
 for id in range(2001,2015):child.send(child.own(U.GetDlgItem(page,id)),0xf5)
 combo=child.own(U.GetDlgItem(page,2021));child.send(combo,0x14e,4);child.send(page,0x111,2021|(1<<16),combo)
 buffer=c.create_unicode_buffer('17');child.send(child.own(U.GetDlgItem(page,2031)),0xc,0,c.addressof(buffer))
 report['custom_controls']=controls(page)
 child.send(child.own(U.GetDlgItem(page,2061)),0xf5);report['reset_controls']=controls(page)
 child.post(child.own(U.GetDlgItem(dialog,2)),0xf5);child.wait(lambda:not U.IsWindow(dialog))
 sources=out/'sources';sources.mkdir();shutil.copyfile(__file__,sources/Path(__file__).name);shutil.copyfile(native.__file__,sources/Path(native.__file__).name)
 report['phase']=phase
 if phase=='measure':measure(main)
 if phase=='palette':palette_cases(main)
 if phase=='fl':fl_cases(main)
 if phase=='fl-selection':fl_selection_cases(main)
 if phase=='mask-selection':fl_selection_cases(main)
 if phase=='remaining-inspect':remaining_inspect(main)
 if phase=='remaining-measure':remaining_measure(main)
 if phase=='remaining-extra':remaining_measure(main,True)
 if phase=='meko-large':remaining_measure(main,large=True)
 if phase=='rainbow-dialog':rainbow_dialog_contract(main)
 if phase=='cp-letters':remaining_measure(main,letters=True)
 if phase=='cp-dialog':cp_dialog_contract(main)
 report['passed']=True
 print(json.dumps(dict(controls=report['initial_controls'],mask_menu=[v for v in report['menu'] if any('マスク' in p for p in v['path'])]),ensure_ascii=False),flush=True)
except BaseException as error:
 report['error']=repr(error)
 report['failure_windows']=[dict(title=native.window_text(h),cls=native.window_text(h,True),
   children=[dict(id=U.GetDlgCtrlID(v),title=native.window_text(v),cls=native.window_text(v,True)) for v in descendants(h)]) for h in child.windows()]
 raise
finally:
 child.close();assert native.sha(__file__)==report['tool_sha256']
 (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 K.ReleaseMutex(mutex);K.CloseHandle(mutex);print('Evidence: '+str(out),flush=True)
