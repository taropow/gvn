"""Drive actual owned GVN popup menus on a private station, without input injection.

GetMenuBarInfo identifies real popup HMENUs. Native menu control messages move
the highlight and enter/cancel TrackPopupMenu; no WM_COMMAND substitutes a
selection. Other commands are used only to read back pixels or close the child.
The MN_* protocol is internal to Windows and deliberately confined to this
owned test driver. Its effects are checked rather than assumed. This does not
test physical keyboard/input-queue behavior on an interactive desktop.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import io
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import time
import uuid
from PIL import Image
import benchmark_navigation as native

U, K, P = native.U, native.K, native.P


class MenuBarInfo(c.Structure):
    _fields_ = [('size', w.DWORD), ('rect', w.RECT), ('menu', P), ('window', P), ('focus', w.DWORD)]


class GuiThreadInfo(c.Structure):
    _fields_ = [('size', w.DWORD), ('flags', w.DWORD)]+[(name, P) for name in
        ['active', 'focus', 'capture', 'menu_owner', 'move_size', 'caret']]+[('caret_rect', w.RECT)]


for name, result, arguments in [
    ('GetMenuBarInfo', w.BOOL, [P, w.LONG, w.LONG, c.POINTER(MenuBarInfo)]),
    ('GetGUIThreadInfo', w.BOOL, [w.DWORD, c.POINTER(GuiThreadInfo)]),
    ('GetMenu', P, [P]), ('GetSubMenu', P, [P, c.c_int]),
    ('GetMenuItemCount', c.c_int, [P]), ('GetMenuItemID', w.UINT, [P, c.c_int]),
    ('GetMenuState', w.UINT, [P, w.UINT, w.UINT]),
    ('GetMenuStringW', c.c_int, [P, w.UINT, w.LPWSTR, c.c_int, w.UINT]),
    ('SetForegroundWindow', w.BOOL, [P]),
    ('IsWindowVisible', w.BOOL, [P]), ('RegisterClipboardFormatW', w.UINT, [w.LPCWSTR])]:
    native.api(U, name, result, arguments)


def command_ids(root):
    result, value = {}, 0
    for row in (root/'include/gvn/commands.h').read_text(encoding='utf-8').split('enum Command {', 1)[1].split('};', 1)[0].split(','):
        match = re.fullmatch(r'\s*(\w+)(?:\s*=\s*(\d+))?\s*', row)
        if match:
            name, number = match.groups()
            if number:
                value = int(number)
            result[name] = value
            value += 1
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path, default=Path('.cache/popup-processes'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()/('run-'+uuid.uuid4().hex)
    out.mkdir(parents=True)
    (out/'sources').mkdir()
    for source in [Path(__file__), Path(native.__file__)]:
        shutil.copyfile(source, out/'sources'/source.name)
    ids = command_ids(root)
    exe = args.executable.resolve()
    profile = out/'settings 日本語.ini'
    asset = out/'透過 source.png'
    original = Image.new('RGBA', (3, 2))
    original.putdata([(7,11,13,0), (31,59,61,1), (67,71,73,127),
                      (79,83,89,128), (97,101,103,254), (107,109,113,255)])
    original.save(asset)
    # Right/middle/X buttons with Ctrl/Shift: all thirteen assignable popup roots.
    routes = [('PopupAll',1,0), ('PopupFile',2,0), ('PopupEdit',3,0), ('PopupImage',4,0),
              ('PopupList',1,1), ('PopupView',2,1), ('PopupAnimation',3,1), ('PopupOptions',4,1),
              ('PopupSort',1,2), ('PopupRGB',2,2), ('PopupMask',3,2), ('PopupDestinations',4,2),
              ('PopupExternal',1,3)]
    bindings = '\n'.join(f'{i}={gesture} 0 {modifiers} {ids[action]}' for i, (action,gesture,modifiers) in enumerate(routes))
    profile.write_text('[Viewer]\nallow_multiple=1\nconfirm_unsaved=0\nprefetch_enabled=0\nreopen_last=0\n'
                       '[MouseBindings]\nVersion=1\nDefaultsVersion=2\nCount=13\n'+bindings+'\n', encoding='utf-16')
    report = dict(passed=False, scope='Normal GVN process;13 configured mouse popup roots;actual native menu control messages for selection/cancel and highlight. Private station, profile and clipboard. No ComputerUse, global input, physical keyboard, external receiver or performance claim.',
                  executable_sha256=native.sha(exe), tool_sha256=native.sha(__file__), helper_sha256=native.sha(native.__file__),
                  asset_sha256=native.sha(asset), cases=[], native_menu_messages=0, selected_commands=[],
                  internal_menu_protocol=True, windows_version=sys.getwindowsversion()[:5])
    mutex = K.CreateMutexW(None, False, 'Local\\GVN.NoninteractiveClipboard')
    native.check(mutex)
    assert K.WaitForSingleObject(mutex,30000) in (0,0x80)
    from probe_gvn_performance import station
    desktop_name = station()
    child = None
    try:
        child = native.Child(exe, ['--settings',profile,asset], desktop_name)
        report['pid'] = child.info.pid
        window = child.wait(lambda:next((h for h in child.windows() if native.window_text(h,True)=='GVN.ImageViewer'),None))
        child.wait(lambda:'%' in native.window_text(window))
        thread = U.GetWindowThreadProcessId(child.own(window), None)
        # Posting button messages does not perform Windows' usual mouse activation.
        # This HWND belongs to the owned child on the noninteractive station.
        report['owned_foreground_activation'] = bool(U.SetForegroundWindow(child.own(window)))

        def menu_state():
            state = GuiThreadInfo(); state.size = c.sizeof(state)
            native.check(U.GetGUIThreadInfo(thread,c.byref(state)))
            return state

        def popups():
            menus = []
            for handle in child.windows():
                # IsWindowVisible also reflects this station being noninteractive.
                # As in autopilot.cpp's locally_visible, inspect the owned top-level
                # window's actual WS_VISIBLE bit, then require native menu-mode state.
                if native.window_text(handle,True)=='#32768' and U.GetWindowLongW(handle,-16)&0x10000000:
                    info = MenuBarInfo(); info.size = c.sizeof(info)
                    native.check(U.GetMenuBarInfo(child.own(handle),-4,0,c.byref(info)))
                    assert info.menu and U.GetMenuItemCount(info.menu)>0
                    menus.append((handle,info.menu))
            return menus

        def labels(menu):
            result=[]
            for index in range(U.GetMenuItemCount(menu)):
                label = c.create_unicode_buffer(512)
                U.GetMenuStringW(menu,index,label,len(label),0x400)
                result.append(dict(position=index,label=label.value,command=U.GetMenuItemID(menu,index),
                                   submenu=bool(U.GetSubMenu(menu,index)),state=U.GetMenuState(menu,index,0x400)))
            return result

        def menu_message(menu,message,index=0):
            handle=next((h for h,m in popups() if m==menu),None)
            assert handle, 'Requested menu is not an actual open owned popup'
            child.post(handle,message,index)
            report['native_menu_messages']+=1

        def wait_closed():
            child.wait(lambda:not (menu_state().flags&0x14) and not popups())
            child.ping(window)

        def cancel():
            menus=popups()
            assert len(menus)==1
            menu_message(menus[0][1],0x1e6) # MN_CANCELMENUS
            wait_closed()

        def path_to(menu,command):
            for index in range(U.GetMenuItemCount(menu)):
                if U.GetMenuItemID(menu,index)==command:
                    return [(menu,index)]
                sub=U.GetSubMenu(menu,index)
                if sub:
                    path=path_to(sub,command)
                    if path:
                        return [(menu,index)]+path
            return None

        def select(menu,name):
            path = path_to(menu,ids[name]); assert path, name
            trace=[]
            for depth,(current,index) in enumerate(path):
                available=[i for i in range(U.GetMenuItemCount(current))
                           if not U.GetMenuState(current,i,0x400)&(0x800|3)]
                assert index in available, (name,depth,index,labels(current))
                menu_message(current,0x1e5,index) # MN_SELECTITEM
                child.wait(lambda:U.GetMenuState(current,index,0x400)&0x80)
                assert U.GetMenuState(current,index,0x400)&0x80
                trace.append(dict(depth=depth,position=index,highlighted=True))
                if depth+1<len(path):
                    following=path[depth+1][0];menu_message(current,0x1e3) # MN_OPENHIERARCHY
                    child.wait(lambda:any(m==following for _,m in popups()))
            menu_message(current,0x1ed,index) # MN_BUTTONDOWN
            menu_message(current,0x1ef,index) # MN_BUTTONUP
            wait_closed()
            report['selected_commands'].append(name)
            return trace

        def png_clipboard(copy=True):
            if copy:
                sequence=U.GetClipboardSequenceNumber()
                child.post(window,0x111,ids['Copy'])
                child.wait(lambda:U.GetClipboardSequenceNumber()!=sequence)
            png=U.RegisterClipboardFormatW('PNG')
            child.wait(lambda:U.IsClipboardFormatAvailable(png))
            child.wait(lambda:bool(U.OpenClipboard(None)))
            try:
                handle=U.GetClipboardData(png);pointer=K.GlobalLock(handle);native.check(pointer)
                try:data=c.string_at(pointer,K.GlobalSize(handle))
                finally:K.GlobalUnlock(handle)
            finally:U.CloseClipboard()
            with Image.open(io.BytesIO(data)) as image:
                return image.convert('RGBA').copy()

        def open_popup(gesture,modifiers):
            flags=(8 if modifiers&1 else 0)|(4 if modifiers&2 else 0)
            down,up,button,xbutton={1:(0x204,0x205,2,0),2:(0x207,0x208,16,0),
                                  3:(0x20b,0x20c,32,1),4:(0x20b,0x20c,64,2)}[gesture]
            point=(150<<16)|120
            child.post(window,down,flags|button|(xbutton<<16),point)
            child.post(window,up,flags|(xbutton<<16),point)
            handles=child.wait(popups)
            state=menu_state()
            assert state.flags&4 and state.flags&0x10 and state.menu_owner==window
            assert len(handles)==1, handles
            return handles[0][1]

        def status_visible():
            found=[]
            @native.Callback
            def visit(handle,_):
                child.own(handle)
                if native.window_text(handle,True)=='msctls_statusbar32':
                    found.append(bool(U.GetWindowLongW(handle,-16)&0x10000000))
                return True
            U.EnumChildWindows(window,visit,0)
            assert len(found)==1, found
            return found[0]

        expected=original
        assert png_clipboard().tobytes()==original.tobytes()
        for action,gesture,modifiers in routes:
            sequence=U.GetClipboardSequenceNumber()
            # The same actual button messages used by the viewer's configured pointer dispatcher.
            menu=open_popup(gesture,modifiers)
            row=dict(action=action,gesture=gesture,modifiers=modifiers,menu_items=labels(menu),
                     native_menu_mode=True,owned_window=True)
            report['cases'].append(row)
            if action=='PopupAll':
                row['selection']=select(menu,'Copy')
                actual=png_clipboard(False)
                assert actual.size==expected.size and actual.tobytes()==expected.tobytes()
                row['all_rgba_equal']=True
            elif action in ['PopupImage','PopupRGB']:
                row['selection']=select(menu,'SwapRG')
                expected=Image.merge('RGBA',(expected.getchannel('G'),expected.getchannel('R'),expected.getchannel('B'),expected.getchannel('A')))
                actual=png_clipboard()
                assert actual.size==expected.size and actual.tobytes()==expected.tobytes()
                row['edited_rgba_equal']=True
            elif action=='PopupView':
                previous_status=bool(U.GetMenuState(menu,ids['ToggleStatus'],0)&8)
                assert status_visible()==previous_status
                row['selection']=select(menu,'ToggleStatus')
                child.wait(lambda:status_visible()!=previous_status)
                reopened=open_popup(gesture,modifiers)
                assert bool(U.GetMenuState(reopened,ids['ToggleStatus'],0)&8)!=previous_status
                cancel()
                row['status_setting_toggled']=True
                row['reopened_check_matches_actual_bar']=True
            elif action=='PopupSort':
                previous_reverse=bool(U.GetMenuState(menu,ids['SortReverse'],0)&8)
                row['selection']=select(menu,'SortReverse')
                reopened=open_popup(gesture,modifiers)
                assert bool(U.GetMenuState(reopened,ids['SortReverse'],0)&8)!=previous_reverse
                cancel()
                row['sort_setting_toggled']=True
                row['reopened_check_matches']=True
            else:
                cancel()
                assert U.GetClipboardSequenceNumber()==sequence
                row['cancel_preserved_clipboard']=True
            row['menu_loop_closed']=True
            row['passed']=True
        assert expected.tobytes()==original.tobytes()
        assert png_clipboard().tobytes()==original.tobytes()
        assert native.sha(asset)==report['asset_sha256'] and native.sha(exe)==report['executable_sha256']
        child.post(window,0x10)
        assert K.WaitForSingleObject(child.info.process,10000)==0, 'Owned GVN did not close gracefully'
        report.update(passed=True,graceful_close=True,source_file_unchanged=True)
    except Exception as error:
        report['error']=repr(error)
        if child:
            report['remaining_windows']=[dict(title=native.window_text(h),window_class=native.window_text(h,True),
                style=U.GetWindowLongW(h,-16)&0xffffffff,visible=bool(U.IsWindowVisible(h))) for h in child.windows()]
            if 'menu_state' in locals():
                state=menu_state()
                report['remaining_gui']=dict(flags=state.flags,active=state.active,menu_owner=state.menu_owner,capture=state.capture)
        raise
    finally:
        if child:
            child.close()
        assert native.sha(__file__)==report['tool_sha256']
        (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Evidence: '+str(out),flush=True)
        K.ReleaseMutex(mutex);K.CloseHandle(mutex)


if __name__=='__main__':
    main()
