"""Observe width/height/either fit with actual native zoom dialogs, on a private desktop."""
from pathlib import Path
from ctypes import wintypes as w
import argparse,ctypes as c,json,re,zipfile
from PIL import Image
import benchmark_navigation as native
from probe_gvn_performance import station
ROOT=Path(__file__).resolve().parents[1];U,K,P=native.U,native.K,native.P
for name,result,args in [('GetMenu',P,[P]),('GetSubMenu',P,[P,c.c_int]),('GetMenuItemCount',c.c_int,[P]),('GetMenuItemID',w.UINT,[P,c.c_int]),('GetMenuStringW',c.c_int,[P,w.UINT,w.LPWSTR,c.c_int,w.UINT]),('IsWindow',w.BOOL,[P]),('GetDpiForWindow',w.UINT,[P])]:native.api(U,name,result,args)
native.api(K,'GetExitCodeProcess',w.BOOL,[P,c.POINTER(w.DWORD)])
def main():
    parser=argparse.ArgumentParser();parser.add_argument('gvn',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'.cache') and not out.exists();out.mkdir(parents=True)
    archive=ROOT/'.cache/massigra-reference/MassiGra045.zip';assert native.sha(archive)=='76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25'
    reference=out/'reference'
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if name=='MassiGra.exe' or name.startswith('data/') and not name.endswith('/'):
                path=(reference/name).resolve();assert path.is_relative_to(reference.resolve());path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(package.read(name))
    exe=reference/'MassiGra.exe';assert native.sha(exe)=='c23b73887a3a98833f9318e7e3321a6a5b9825ad442b88e8bd3b0333ba28a3e5'
    commands={};value=0
    for item in (ROOT/'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {',1)[1].split('};',1)[0].split(','):
        match=re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*',item)
        if match:
            if match[2]:value=int(match[2])
            commands[match[1]]=value;value+=1
    desktop=station();child=None;report=dict(passed=False,tool_sha256=native.sha(__file__),reference_sha256=native.sha(exe),gvn_sha256=native.sha(args.gvn),private_station=True,user_desktop_clipboard_or_artwork_used=False,global_input_used=False,processes=[])
    def text(hwnd):
        buffer=c.create_unicode_buffer(2048);child.send(child.own(hwnd),0xd,len(buffer),c.addressof(buffer));return buffer.value
    def rect(hwnd):
        result=(c.c_long*4)();native.check(U.GetWindowRect(child.own(hwnd),result));return list(result)
    def menu_items(menu):
        result=[]
        for index in range(U.GetMenuItemCount(menu)):
            label=c.create_unicode_buffer(1024);U.GetMenuStringW(menu,index,label,len(label),0x400);result.append((U.GetMenuItemID(menu,index),re.sub(r'[\s&]','',label.value.split('\t')[0]),int(menu)))
            sub=U.GetSubMenu(menu,index)
            if sub:result+=menu_items(sub)
        return result
    def numeric(window,kind):
        before=text(window);bounds=rect(window);cid=2031 if kind=='reference' else 101;child.post(window,0x111,40084 if kind=='reference' else commands['ZoomNumber'])
        dialog=child.wait(lambda:next((h for h in child.windows() if h!=window and U.GetDlgItem(h,cid) and U.GetDlgItem(h,1) and ('ズーム' in text(h) or '表示倍率' in text(h))),None))
        field=child.own(U.GetDlgItem(dialog,cid));child.wait(lambda:bool(re.fullmatch(r'[0-9]+(?:\.[0-9]+)?',text(field))))
        percent=float(text(field));assert 0<percent<=5000
        if kind=='reference':child.send(dialog,0x10)
        else:child.post(child.own(U.GetDlgItem(dialog,2)),0xf5)
        child.wait(lambda:not U.IsWindow(dialog));child.ping(window)
        assert text(window)==before and rect(window)==bounds,'Reading cancelled zoom dialog changed fit or window'
        return percent
    try:
        assets=out/'assets';assets.mkdir()
        for label,size in [('large-wide',(1920,480)),('large-tall',(480,1920)),('square',(1024,1024)),('small-wide',(160,40)),('small-tall',(40,160))]:
            source=assets/(label+'.png');Image.new('RGB',size,(41,77,113)).save(source);source_hash=native.sha(source)
            for kind,program in [('reference',exe),('gvn',args.gvn.resolve())]:
                if kind=='gvn':
                    config=out/(label+'.ini');config.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\nmenu=1\ncaption=1\nstartup_state=0\nrestore_fullscreen=0\ndisplay_mode=1\n',encoding='utf-16');arguments=['--settings',config,source];cls='GVN.ImageViewer'
                else:arguments=[source];cls='TF811202_MassiGra_Main'
                child=native.Child(program,arguments,desktop);window=child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)==cls),None));child.wait(lambda:source.name in text(window));child.ping(window)
                native.check(U.SetWindowPos(child.own(window),None,0,0,640,480,0x14));child.ping(window)
                if kind=='reference':
                    if not U.GetMenu(window):child.send(window,0x111,41008)
                    items=menu_items(U.GetMenu(window))
                    def one(predicate,pool=None):
                        matches=[n for n,title,parent in (items if pool is None else pool) if predicate(title)];assert len(matches)==1,matches;return matches[0]
                    either=[row for row in items if '幅' in row[1] and '高さ' in row[1] and 'どちらか' in row[1]];assert len(either)==1
                    group=[row for row in items if row[2]==either[0][2]]
                    fit={'width':one(lambda title:'幅に合わせる' in title and '高さ' not in title,group),'height':one(lambda title:'高さに合わせる' in title and '幅' not in title,group),'either':either[0][0]}
                    report.setdefault('reference_mode_labels',[]).append([dict(command=n,label=title) for n,title,parent in items if 'ウィンドウ' in title and '合わせる' in title])
                    mode=one(lambda title:re.sub(r'^\(\d+\)','',title).startswith('画像をウィンドウに合わせる'));child.send(window,0x111,mode)
                else:fit={'width':commands['FitWidth'],'height':commands['FitHeight'],'either':commands['FitCover']}
                row=dict(kind=kind,pid=child.info.pid,label=label,image_dimensions=size,source_sha256=source_hash,dpi=U.GetDpiForWindow(window),commands=fit,window_bounds=rect(window),steps=[])
                for action in ['width','height','either']:
                    child.send(window,0x111,fit[action]);child.ping(window);row['steps'].append(dict(action=action,percent=numeric(window,kind),caption=text(window),window_bounds=rect(window)))
                dominant='width' if row['steps'][0]['percent']>=row['steps'][1]['percent'] else 'height'
                for action in [dominant,'either']:
                    child.send(window,0x111,fit[action]);child.ping(window);row['steps'].append(dict(action=action,percent=numeric(window,kind),caption=text(window),window_bounds=rect(window)))
                row.update(dominant=dominant,stable_dominant_percent=row['steps'][-2]['percent'],stable_either_percent=row['steps'][-1]['percent']);row['matches_max_with_same_scrollbar_state']=abs(row['stable_dominant_percent']-row['stable_either_percent'])<=.01
                child.post(window,0x10);assert K.WaitForSingleObject(child.info.process,10000)==0;code=w.DWORD();native.check(K.GetExitCodeProcess(child.info.process,c.byref(code)));assert code.value==0 and native.sha(source)==source_hash
                row.update(normal_close=True,exit_code=0);report['processes'].append(row);child.close();child=None;print(kind,label,row['stable_either_percent'],'max matches',row['matches_max_with_same_scrollbar_state'],flush=True)
        assert len(report['processes'])==10;report.update(passed=True,reference_max_model_matches=all(row['matches_max_with_same_scrollbar_state'] for row in report['processes'] if row['kind']=='reference'),gvn_max_model_matches=all(row['matches_max_with_same_scrollbar_state'] for row in report['processes'] if row['kind']=='gvn'))
    except BaseException as error:
        report['error']=type(error).__name__+': '+str(error)
        if child:report['failed_owned_pid']=child.info.pid
        raise
    finally:
        if child:child.close()
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Evidence: '+str(out),flush=True)
if __name__=='__main__':main()
