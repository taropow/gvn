"""Read official0.45 captions, or replay its parent mode in actual owned GVN.

The --drive-root check creates one uniquely named synthetic PNG in the explicit
workspace volume root and removes only that unchanged file in finally. UNC
checks use an existing localhost administrative share; no share is created.
"""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,io,json,re,shutil,sys,uuid,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station

U,K,P=native.U,native.K,native.P
for name,result,arguments in [('GetParent',P,[P]),('GetDlgCtrlID',c.c_int,[P]),('IsWindow',w.BOOL,[P])]:
    native.api(U,name,result,arguments)
for name,result,arguments in [('VirtualAllocEx',P,[P,P,native.UP,w.DWORD,w.DWORD]),
    ('VirtualFreeEx',w.BOOL,[P,P,native.UP,w.DWORD]),('ReadProcessMemory',w.BOOL,[P,P,P,native.UP,P])]:
    native.api(K,name,result,arguments)
net=c.WinDLL('netapi32',use_last_error=True)
native.api(net,'NetShareGetInfo',w.DWORD,[w.LPCWSTR,w.LPCWSTR,w.DWORD,c.POINTER(P)])
native.api(net,'NetApiBufferFree',w.DWORD,[P])
native.api(U,'RegisterClipboardFormatW',w.UINT,[w.LPCWSTR])

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=Path('.cache/caption-path-reference'))
    parser.add_argument('--drive-root',action='store_true')
    parser.add_argument('--gvn',type=Path);parser.add_argument('--expectations',type=Path);args=parser.parse_args()
    assert bool(args.gvn)==bool(args.expectations)
    root=Path(__file__).resolve().parents[1];out=args.output.resolve()/('run-'+uuid.uuid4().hex);out.mkdir(parents=True)
    (out/'sources').mkdir()
    for p in [Path(__file__),Path(native.__file__),root/'tools/probe_gvn_performance.py']:shutil.copyfile(p,out/'sources'/p.name)
    archive=root/'.cache/massigra-reference/MassiGra045.zip'
    assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference=out/'reference'
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                target=(reference/name).resolve();assert target.is_relative_to(reference.resolve())
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(name))
    exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    asset=out/'owned images/親フォルダ/子フォルダ/many.parts 日本語.png';asset.parent.mkdir(parents=True)
    image=Image.new('RGBA',(3,2));image.putdata([(7,11,13,0),(31,59,61,1),(67,71,73,127),(79,83,89,128),(97,101,103,254),(107,109,113,255)]);image.save(asset)
    report=dict(passed=False,reference_sha256=native.sha(exe),tool_sha256=native.sha(__file__),helper_sha256=native.sha(native.__file__),
        asset_sha256=native.sha(asset),private_station=True,startup_timing_measured=False,user_windows_or_clipboard_used=False,shares_created=False,cases=[])
    if args.gvn:
        gvn=args.gvn.resolve();assert gvn.is_file()
        expected_report=json.loads(args.expectations.read_text(encoding='utf-8-sig'))
        assert expected_report['passed'] and expected_report['reference_sha256']==report['reference_sha256']
        expected_rows={row['label']:row for row in expected_report['cases']}
        shutil.copyfile(args.expectations,out/'reference-expectations.json')
        report.update(target='GVN',gvn_sha256=native.sha(gvn),expectations_sha256=native.sha(args.expectations),
                      scope='Four actual normalGVN processes;parent-folder caption mode compared with owned official0.45 observations;root/UNC files decoded and recopied with allRGBA. No injected caption source or edited return value.')
        ids={};value=0
        for row in (root/'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {',1)[1].split('};',1)[0].split(','):
            match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',row)
            if match:
                if match[2]:value=int(match[2])
                ids[match[1]]=value;value+=1
    root_asset=None;root_created=False;child=None;mutex=None
    try:
        paths=[('nested',asset)]
        if args.drive_root:
            drive_root=Path(root.anchor).resolve();assert drive_root==Path('G:/').resolve()
            root_asset=drive_root/('gvn-caption-proof-'+uuid.uuid4().hex+'.parts 日本語.png')
            assert root_asset.resolve().parent==drive_root and not root_asset.exists()
            shutil.copyfile(asset,root_asset);root_created=True
            assert native.sha(root_asset)==report['asset_sha256']
            paths.append(('drive-root',root_asset));report['temporary_drive_root_file']=str(root_asset)
        share_buffer=P();share_status=net.NetShareGetInfo(None,root.drive[0]+'$',1,c.byref(share_buffer))
        if share_buffer:net.NetApiBufferFree(share_buffer)
        report['localhost_share_query_status']=share_status
        if share_status==0:
            for label,path in list(paths):
                unc=Path('\\\\localhost\\'+root.drive[0]+'$\\'+str(path.relative_to(root.anchor)))
                if unc.is_file() and native.sha(unc)==report['asset_sha256']:
                    paths.append(('unc-'+label,unc))
        report['unc_available']=any(label.startswith('unc-') for label,_ in paths)
        mutex=K.CreateMutexW(None,False,'Local\\GVN.NoninteractiveClipboard');native.check(mutex)
        assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
        desktop=station()
        for label,path in paths:
            if args.gvn:
                recorded=expected_rows[label]
                expected=recorded['modes'][1]['filename'].replace(recorded['path'],str(path)).replace(Path(recorded['path']).name,path.name)
                config=out/(label+'.ini');config.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\nreopen_last=0\ntitle_mode=2\n',encoding='utf-16')
                child=native.Child(gvn,['--settings',config,path],desktop)
                window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None))
                child.wait(lambda:'%' in native.window_text(window) and path.name in native.window_text(window))
                title=native.window_text(window);actual=title.split(' <',1)[0]
                row=dict(label=label,path=str(path),pid=child.info.pid,title=title,expected=expected,filename=actual,matches_reference=actual==expected)
                report['cases'].append(row);assert row['matches_reference'],row
                sequence=U.GetClipboardSequenceNumber();child.post(window,0x111,ids['Copy'])
                png=U.RegisterClipboardFormatW('PNG')
                child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence and U.IsClipboardFormatAvailable(png))
                child.wait(lambda:bool(U.OpenClipboard(None)))
                try:
                    handle=U.GetClipboardData(png);pointer=K.GlobalLock(handle);native.check(pointer)
                    try:data=c.string_at(pointer,K.GlobalSize(handle))
                    finally:K.GlobalUnlock(handle)
                finally:U.CloseClipboard()
                with Image.open(io.BytesIO(data)) as copied:
                    assert copied.size==image.size and copied.convert('RGBA').tobytes()==image.tobytes()
                row['all_rgba_equal']=True
                child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0
                row['graceful_close']=True;child.close();child=None
                continue
            child=native.Child(exe,[path],desktop)
            main_window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='TF811202_MassiGra_Main'),None))
            child.wait(lambda:path.name in native.window_text(main_window))
            row=dict(label=label,path=str(path),pid=child.info.pid,modes=[]);report['cases'].append(row)
            def descendants(parent):
                result=[]
                @native.Callback
                def visit(window,_):result.append(child.own(window));return True
                U.EnumChildWindows(child.own(parent),visit,0);return result
            def choices(window):
                if child.send(window,0x146)!=3:return []
                result=[]
                for i in range(3):
                    text=c.create_unicode_buffer(512);child.send(window,0x148,i,c.addressof(text));result.append(text.value)
                return result
            for mode in range(3):
                child.post(main_window,0x111,40101)
                dialog=child.wait(lambda:next((h for h in child.windows() if h!=main_window and '設定' in native.window_text(h)),None))
                tab=child.wait(lambda:next((h for h in descendants(dialog) if native.window_text(h,True)=='SysTabControl32'),None))
                remote=K.VirtualAllocEx(child.info.process,None,16,0x3000,4);native.check(remote)
                try:
                    native.check(child.send(tab,0x130a,6,remote));rect=(c.c_int*4)()
                    native.check(K.ReadProcessMemory(child.info.process,remote,rect,16,None))
                    point=((rect[0]+rect[2])//2)|(((rect[1]+rect[3])//2)<<16)
                    child.post(tab,0x201,1,point);child.post(tab,0x202,0,point)
                finally:K.VirtualFreeEx(child.info.process,remote,0,0x8000)
                child.wait(lambda:child.send(tab,0x130b)==6)
                combo=child.wait(lambda:next((h for h in descendants(dialog) if 'combo' in native.window_text(h,True).lower() and choices(h)==['フルパス','フォルダ名以降','ファイル名']),None))
                assert child.send(combo,0x14e,mode)==mode
                child.send(U.GetParent(combo),0x111,U.GetDlgCtrlID(combo)|(1<<16),combo)
                child.post(dialog,0x111,1);child.wait(lambda:not U.IsWindow(dialog));child.ping(main_window)
                title=native.window_text(main_window);assert path.name in title
                row['modes'].append(dict(mode=mode,title=title,filename=re.sub(r' <[^>]*>$','',title)))
            child.post(main_window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0
            row['graceful_close']=True;child.close();child=None
        assert native.sha(exe)==report['reference_sha256']
        if args.gvn:
            assert {row['label'] for row in report['cases']}==set(expected_rows)
            assert native.sha(gvn)==report['gvn_sha256']
        report['passed']=True
    except Exception as error:
        report['error']=repr(error);raise
    finally:
        if child:child.close()
        if root_created and root_asset.exists():
            assert root_asset.resolve().parent==Path('G:/').resolve() and root_asset.name.startswith('gvn-caption-proof-')
            assert native.sha(root_asset)==report['asset_sha256'],'Owned temporary root asset changed;retained'
            root_asset.unlink();report['temporary_drive_root_file_removed']=not root_asset.exists()
        assert native.sha(__file__)==report['tool_sha256']
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Evidence: '+str(out),flush=True)
        if mutex:K.ReleaseMutex(mutex);K.CloseHandle(mutex)

if __name__=='__main__':main()
