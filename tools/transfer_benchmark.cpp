#include "gvn/transfer.h"
#include "gvn/edit.h"
#include "../tests/test_desktop.h"
#include <chrono>
#include <iostream>
#include <psapi.h>
int main(int argc,char **argv) {
    const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 3;
    const auto desktop=CreateDesktopW(L"GVN-TransferBenchmark",nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop)||FAILED(OleInitialize(nullptr)))return 4;
    try {
        const auto owner=CreateWindowExW(0,L"STATIC",L"Owned clipboard",WS_POPUP,0,0,16,16,nullptr,nullptr,nullptr,nullptr);
        if(!owner)return 5;
        auto image=gvn::new_image(argc>1?std::stoul(argv[1]):2048,argc>2?std::stoul(argv[2]):1536);
        uint32_t seed=83719;
        for(auto &p:image.frames[0].pixels){seed=seed*1664525+1013904223;p={uint8_t(seed),uint8_t(seed>>8),uint8_t(seed>>16),uint8_t(seed>>24)};}
        PROCESS_MEMORY_COUNTERS_EX before{},after{};before.cb=after.cb=sizeof(before);
        GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&before),sizeof(before));
        const auto start=std::chrono::steady_clock::now();gvn::copy_image(owner,image,0,gvn::SelectionOrigin{13,-17,image.width,image.height});
        const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&after),sizeof(after));
        const auto roundtrip=gvn::paste_selection_clipboard(owner);
        if(roundtrip.image.frames[0].pixels!=image.frames[0].pixels||!roundtrip.origin||roundtrip.origin->x!=13||roundtrip.origin->y!=-17)return 5;
        std::cout<<"{\"copy_ms\":"<<milliseconds<<",\"incremental_peak_commit\":"<<(after.PeakPagefileUsage-before.PeakPagefileUsage)
                 <<",\"rgba_equal\":true,\"origin_equal\":true,\"width\":"<<image.width<<",\"height\":"<<image.height<<"}\n";
        DestroyWindow(owner);OleUninitialize();return 0;
    }catch(const std::exception &e){std::cerr<<e.what();return 1;}
}
