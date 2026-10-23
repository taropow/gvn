"""Compare retained memory while navigating only owned hidden native GVN processes."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import time

user = C.WinDLL('user32', use_last_error=True)
psapi = C.WinDLL('psapi', use_last_error=True)
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [CALLBACK, W.LPARAM]
user.EnumWindows.restype = W.BOOL
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user.GetWindowThreadProcessId.restype = W.DWORD
user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.GetWindowTextW.restype = C.c_int
user.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
user.SendMessageTimeoutW.restype = C.c_ssize_t

class Memory(C.Structure):
    _fields_ = [('cb', W.DWORD), ('faults', W.DWORD)] + [(key, C.c_size_t) for key in
        ['peak_working_set', 'working_set', 'peak_paged', 'paged', 'peak_nonpaged', 'nonpaged',
         'pagefile', 'peak_pagefile', 'private']]

psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(Memory), W.DWORD]
psapi.GetProcessMemoryInfo.restype = W.BOOL
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def owner(hwnd):
    pid = W.DWORD()
    user.GetWindowThreadProcessId(hwnd, C.byref(pid))
    return pid.value

def find_window(child):
    found = []
    @CALLBACK
    def visit(hwnd, unused):
        if owner(hwnd) == child.pid:
            name = C.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if name.value == 'GVN.ImageViewer':
                found.append(hwnd)
        return True
    assert user.EnumWindows(visit, 0)
    assert len(found) <= 1, 'Ambiguous owned GVN window'
    return found[0] if found else None

def message(child, hwnd, command=0):
    assert child.poll() is None and owner(hwnd) == child.pid, 'Owned window changed'
    result = C.c_size_t()
    assert user.SendMessageTimeoutW(hwnd, 0x111 if command else 0, command, 0, 3, 10000,
                                    C.byref(result)), 'Owned UI timed out'

def wait_image(child, filename, hwnd=None):
    deadline = time.perf_counter() + 15
    while time.perf_counter() < deadline:
        assert child.poll() is None, 'Viewer exited before expected image'
        hwnd = hwnd or find_window(child)
        if hwnd:
            assert owner(hwnd) == child.pid
            title = C.create_unicode_buffer(1024)
            user.GetWindowTextW(hwnd, title, len(title))
            if filename in title.value:
                # Observe the adopted title, then require the owned UI to answer a WM_NULL barrier.
                message(child, hwnd)
                return hwnd
        time.sleep(.005)
    raise AssertionError('Expected owned image title did not arrive')

def memory(child):
    result = Memory()
    result.cb = C.sizeof(result)
    assert psapi.GetProcessMemoryInfo(int(child._handle), C.byref(result), result.cb)
    return {key: getattr(result, key) for key in ['working_set', 'peak_working_set', 'private']}

def main():
    parser = argparse.ArgumentParser()
    for field in ['baseline', 'candidate', 'assets', 'output']:
        parser.add_argument(field, type=Path)
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--settle-ms', type=int, default=150)
    args = parser.parse_args()
    assert 1 <= args.samples <= 10 and 0 <= args.settle_ms <= 1000
    root = Path(__file__).resolve().parents[1]
    header = root/'include/gvn/commands.h'
    manifest = json.loads((root/'docs/verification/0.10.3-source-manifest.json').read_text())
    assert manifest['include/gvn/commands.h'] == sha(header), 'Baseline command IDs differ'
    prefix = header.read_text().split('enum Command {', 1)[1].split('Next,', 1)[0]
    assert re.findall(r'\b(Open|Reload|Exit|Previous)\b', prefix) == ['Open', 'Reload', 'Exit', 'Previous']
    assert re.search(r'Open\s*=\s*100\s*,', prefix)
    next_command = 104
    paths = {role: getattr(args, role).resolve() for role in ['baseline', 'candidate']}
    images = sorted(p.resolve() for p in args.assets.iterdir() if p.suffix.lower() in {'.jpg', '.jpeg', '.png'})
    assert 2 <= len(images) <= 100
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    frozen = {str(p): sha(p) for p in [*paths.values(), *images, header, Path(__file__)]}
    runs = []
    for sample in range(args.samples):
        for role in (['baseline', 'candidate'] if sample % 2 == 0 else ['candidate', 'baseline']):
            report = output/f'{sample}-{role}-final.json'
            assert not report.exists(), 'Use a fresh output directory'
            started = time.perf_counter()
            child = subprocess.Popen([str(paths[role]), '--profile', str(images[0]), str(report)])
            try:
                hwnd = wait_image(child, images[0].name)
                startup = (time.perf_counter() - started) * 1000
                stages = []
                for index, image in enumerate(images):
                    if index:
                        step = time.perf_counter()
                        message(child, hwnd, next_command)
                        wait_image(child, image.name, hwnd)
                        latency = (time.perf_counter() - step) * 1000
                    else:
                        latency = startup
                    time.sleep(args.settle_ms/1000)
                    stages.append(dict(index=index, image=image.name, navigation_ms=latency, **memory(child)))
                assert child.wait(timeout=10) == 0
                final = json.loads(report.read_text())
                assert final['rendered'] and not final['error']
                if 'cache_idle' in final:
                    assert final['cache_idle'] and final['cache_bytes'] <= final['cache_limit']
                run = dict(sample=sample, role=role, launch_to_title_and_barrier_ms=startup,
                           stages=stages, last_image_report=final)
                runs.append(run)
                (output/f'{sample}-{role}-stages.json').write_text(json.dumps(run, indent=2)+'\n', encoding='utf-8')
                print(role, sample, 'final_private_MiB', round(final['private_bytes']/1048576, 2),
                      'peak_WS_MiB', round(stages[-1]['peak_working_set']/1048576, 2), flush=True)
            finally:
                if child.poll() is None:
                    child.kill()  # Only this Popen handle; never an existing viewer.
                    child.wait()
    assert all(sha(Path(path)) == digest for path, digest in frozen.items()), 'Measured inputs changed'
    aggregate = {}
    for role in paths:
        matching = [r for r in runs if r['role'] == role]
        aggregate[role] = dict(
            final_private_MiB=[r['last_image_report']['private_bytes']/1048576 for r in matching],
            final_working_set_MiB=[r['last_image_report']['working_set']/1048576 for r in matching],
            peak_working_set_MiB=[r['stages'][-1]['peak_working_set']/1048576 for r in matching],
            navigation_ms_median=statistics.median(s['navigation_ms'] for r in matching for s in r['stages'][1:]))
    summary = dict(verified_at=datetime.now(timezone.utc).isoformat(), frozen_sha256=frozen,
                   next_command=next_command, settle_ms=args.settle_ms, aggregate=aggregate, runs=runs,
                   method='Serial counterbalanced owned hidden --profile processes; only PID-checked GVN HWND messages. '
                   'Each unique filename is observed in the owned title and the UI answers WM_NULL; then a fixed settling delay. '
                   'Stages sample native process memory; final memory after two idle seconds. Peak WS is the OS process '
                   'counter, including transient allocations. Source images and OS file cache remain warm. '
                   'launch_to_title_and_barrier_ms is an observation proxy including Popen/process/OLE/constructors; it does not prove physical presentation or exact EndDraw completion '
                   'and includes polling/barrier overhead. Final internal ready/display fields refer to the last image, '
                   'not initial startup. No existing viewer, clipboard, settings or MassiGra is operated.')
    (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(aggregate))

if __name__ == '__main__':
    main()
