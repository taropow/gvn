"""Profile owned hidden GVN processes on a private desktop; no MassiGra comparison."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time
import uuid
import benchmark_navigation as native

ROOT = Path(__file__).resolve().parents[1]
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
api = native.api
psapi = c.WinDLL('psapi', use_last_error=True)


class Memory(c.Structure):
    _fields_ = [('cb', w.DWORD), ('faults', w.DWORD)] + [(name, c.c_size_t) for name in
        ['peak_working_set', 'working_set', 'peak_paged', 'paged', 'peak_nonpaged', 'nonpaged',
         'commit', 'peak_commit', 'private']]


api(psapi, 'GetProcessMemoryInfo', w.BOOL, [native.P, c.POINTER(Memory), w.DWORD])
api(native.K, 'GetExitCodeProcess', w.BOOL, [native.P, c.POINTER(w.DWORD)])


class SystemMemory(c.Structure):
    _fields_ = [('length', w.DWORD), ('load', w.DWORD)] + [(name, c.c_uint64) for name in
        ['total_physical', 'available_physical', 'total_pagefile', 'available_pagefile',
         'total_virtual', 'available_virtual', 'available_extended']]


api(native.K, 'GlobalMemoryStatusEx', w.BOOL, [c.POINTER(SystemMemory)])


def sample_memory(child):
    data = Memory()
    data.cb = c.sizeof(data)
    if not psapi.GetProcessMemoryInfo(child.info.process, c.byref(data), data.cb):
        if native.K.WaitForSingleObject(child.info.process, 0) == 0:
            return None
        native.check(False)
    return {name: getattr(data, name) for name in ['working_set', 'peak_working_set', 'private', 'peak_commit']}


def station():
    handle = native.U.CreateWindowStationW(None, 0, 0x37f, None)
    native.check(handle)
    flags = (w.DWORD*3)(); needed = w.DWORD()
    native.check(native.U.GetUserObjectInformationW(handle, 1, flags, c.sizeof(flags), c.byref(needed)))
    assert not flags[2] & 1
    name = c.create_unicode_buffer(512)
    native.check(native.U.GetUserObjectInformationW(handle, 2, name, c.sizeof(name), c.byref(needed)))
    native.check(native.U.SetProcessWindowStation(handle))
    desktop_name = 'GVN-Performance-'+uuid.uuid4().hex
    native.desktop = native.U.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
    native.check(native.desktop)
    native.check(native.U.SetThreadDesktop(native.desktop))
    return name.value+'\\'+desktop_name


def cohort(exe, asset, image, instances, directory, desktop, budget):
    system = SystemMemory(); system.length = c.sizeof(system)
    native.check(native.K.GlobalMemoryStatusEx(c.byref(system)))
    estimate = asset['rgba_bytes']*instances*2+64*1024*1024*instances
    assert system.available_physical >= estimate+512*1024*1024, 'Insufficient currently available physical memory for this owned cohort'
    directory.mkdir()
    children = []; maxima = []; sampled_aggregate = 0; samples = 0; timeline = []
    start = time.perf_counter()
    try:
        for index in range(instances):
            child = native.Child(exe, ['--profile', image, directory/f'{index}.json'], desktop)
            children.append(child)
            maxima.append(dict(working_set=0, peak_working_set=0, private=0, peak_commit=0))
        deadline = time.perf_counter()+60
        while True:
            live = False; aggregate = 0; private_values = []; commit_values = []
            for index, child in enumerate(children):
                if native.K.WaitForSingleObject(child.info.process, 0) == 0:
                    private_values.append(None); commit_values.append(None)
                    continue
                live = True
                memory = sample_memory(child)
                if memory:
                    aggregate += memory['private']
                    private_values.append(memory['private']); commit_values.append(memory['peak_commit'])
                    for name, value in memory.items():
                        maxima[index][name] = max(maxima[index][name], value)
                else:
                    private_values.append(None); commit_values.append(None)
            timeline.append(dict(ms=(time.perf_counter()-start)*1000,
                                 private_bytes=private_values, observed_os_peak_commit_bytes=commit_values))
            sampled_aggregate = max(sampled_aggregate, aggregate)
            samples += 1
            assert aggregate <= budget, 'Owned cohort exceeded its private-memory safety budget'
            if not live:
                break
            assert time.perf_counter() < deadline, 'Owned profile process did not finish'
            time.sleep(.005)
        results = []
        for index, child in enumerate(children):
            code = w.DWORD()
            native.check(native.K.GetExitCodeProcess(child.info.process, c.byref(code)))
            assert code.value == 0, ('Profile process failed', asset['name'], code.value)
            report = json.loads((directory/f'{index}.json').read_text(encoding='utf-8'))
            assert report['rendered'] and not report['error'] and report['first_draw_succeeded']
            assert (report['width'], report['height'], report['frames']) == (asset['width'], asset['height'], asset['frames'])
            assert report['display_ready_ms'] >= report['ready_ms'] and report['first_draw_ms'] > 0
            assert sum(report['draw_phases_ms'].values()) <= report['first_draw_ms']+.2
            assert report['cache_idle'] and report['cache_bytes'] <= max(report['cache_limit'], asset['rgba_bytes']+8*1024*1024)
            assert maxima[index]['peak_commit'] >= report['private_bytes'] and maxima[index]['peak_working_set'] >= report['working_set']
            results.append(dict(pid=child.info.pid, graceful_exit=True, exit_code=code.value,
                                report=report, os_peak_commit_bytes=maxima[index]['peak_commit'],
                                os_peak_working_set_bytes=maxima[index]['peak_working_set'],
                                sampled_max_private_bytes=maxima[index]['private']))
        data = dict(asset=asset['name'], instances=instances, cohort_wall_seconds=time.perf_counter()-start,
                    sampled_aggregate_max_private_bytes=sampled_aggregate, memory_samples=samples,
                    sum_retained_private_bytes=sum(result['report']['private_bytes'] for result in results), processes=results,
                    memory_timeline=timeline, available_physical_before_bytes=system.available_physical)
        (directory/'cohort.json').write_text(json.dumps(data, indent=2)+'\n', encoding='utf-8')
        return data
    finally:
        # Every handle belongs to this cohort; a failed/timed-out run may terminate only these children.
        for child in children:
            child.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('assets', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'.cache/performance')
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--asset', action='append')
    parser.add_argument('--max-private-mib', type=int, default=2048)
    args = parser.parse_args()
    assert 1 <= args.samples <= 5 and 512 <= args.max_private_mib <= 4096
    exe = args.executable.resolve(); assets = args.assets.resolve()
    manifest = json.loads((assets/'manifest.json').read_text(encoding='utf-8'))
    selected = [asset for asset in manifest['assets'] if not args.asset or asset['name'] in args.asset]
    assert selected
    if args.asset:
        assert set(args.asset) == {asset['name'] for asset in selected}
    frozen = {path: sha(path) for path in [exe, Path(__file__), Path(native.__file__), assets/'manifest.json']}
    for asset in selected:
        image = (assets/asset['path']).resolve()
        assert image.is_relative_to(assets) and sha(image) == asset['sha256']
        frozen[image] = asset['sha256']
    output = args.output.resolve()/('run-'+uuid.uuid4().hex)
    output.mkdir(parents=True)
    desktop = station()
    runs = []; summary = dict(passed=False, created_at=datetime.now(timezone.utc).isoformat(),
        executable_sha256=sha(exe), tool_sha256=sha(__file__), helper_sha256=sha(native.__file__),
        private_station=True, station_visible=False, actual_user_clipboard_used=False,
        source_images_synthetic=True, memory_interval_ms=5, aggregate_private_safety_budget_mib=args.max_private_mib,
        method='Fresh hidden --profile processes on a private desktop; each asset is alone in its folder. '
               'ready_ms ends at adoption and display_ready_ms at first Direct2D EndDraw; both exclude process creation/OLE/DWM presentation. '
               'Profile uses its fixed hidden1080x760 window and default Linear, not the normal startup rectangle. '
               'First draw is timed internally; profiles hold two seconds before reporting. OS cache is not cleared. '
               'Peak commit/working set are OS lifetime high-water counters observed while each child is alive. Aggregate private maximum is sampled every5ms, '
               'not an exact simultaneous lifetime peak. Retained private sums are separate per-process snapshots. '
               'Cohort wall time includes the two-second hold and is not image-display latency. '
               'Synthetic inputs and hidden rendering do not establish real-photo or physical animation smoothness.',
        memory_definition_source='https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex',
        asset_manifest=manifest, runs=runs)
    try:
        for sample in range(args.samples):
            order = selected[sample % len(selected):]+selected[:sample % len(selected)]
            for asset in order:
                counts = asset['instances'] if sample % 2 == 0 else list(reversed(asset['instances']))
                for count in counts:
                    data = cohort(exe, asset, assets/asset['path'], count,
                                  output/f'{sample}-{asset["name"]}-{count}', desktop,
                                  args.max_private_mib*1024*1024)
                    data['sample'] = sample; runs.append(data)
                    print('PASS', asset['name'], 'instances', count, 'sample', sample,
                          'draw_ms', [p['report']['first_draw_ms'] for p in data['processes']],
                          'peak_commit_MiB', [round(p['os_peak_commit_bytes']/1048576, 2) for p in data['processes']], flush=True)
        assert all(sha(path) == digest for path, digest in frozen.items())
        summary['passed'] = True
        summary['aggregates'] = []
        for asset in selected:
            for count in asset['instances']:
                matching = [run for run in runs if run['asset'] == asset['name'] and run['instances'] == count]
                assert len(matching) == args.samples
                processes = [process for run in matching for process in run['processes']]
                summary['aggregates'].append(dict(asset=asset['name'], instances=count,
                    display_ready_ms=[p['report']['display_ready_ms'] for p in processes],
                    first_draw_ms=[p['report']['first_draw_ms'] for p in processes],
                    peak_commit_bytes=[p['os_peak_commit_bytes'] for p in processes],
                    retained_private_sum_bytes=[run['sum_retained_private_bytes'] for run in matching],
                    sampled_aggregate_max_private_bytes=[run['sampled_aggregate_max_private_bytes'] for run in matching]))
    finally:
        summary['source_and_input_guard_passed'] = all(sha(path) == digest for path, digest in frozen.items())
        (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
        print('Evidence:', output, flush=True)
    assert summary['passed'] and summary['source_and_input_guard_passed']


if __name__ == '__main__':
    main()
