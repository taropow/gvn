    // Same input and native operation before/after optimization. No DWM present
    // latency or image-file decoding is included in these component timings.
    void daily_hotpaths_benchmark() {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.settings.confirm_unsaved = false;
        app.replace(selection_fixture(2048, 1536), false, true);
        const auto image = app.image;
        const auto dib = make_dibv5(*image, 0);
        const auto measure = [&](const char *name, int count, const auto &action) {
            action();
            std::array<double, 5> samples{};
            for (auto &sample : samples) {
                const auto start = std::chrono::steady_clock::now();
                for (int n = 0; n < count; ++n) action();
                sample = std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - start).count() / count;
            }
            std::cout << "DAILY_PERF {\"case\":\"" << name << "\",\"iterations\":" << count * 5 << ",\"samples_us\":[";
            for (size_t n = 0; n < samples.size(); ++n) std::cout << (n ? "," : "") << samples[n];
            std::cout << "]}\n";
        };
        measure("dib32_decode_2048x1536", 3, [&] { auto decoded = read_dib(dib); CHECK(decoded.width == image->width); });
        const auto memory = GlobalAlloc(GMEM_MOVEABLE, dib.size()); CHECK(memory);
        const auto data = GlobalLock(memory); CHECK(data);
        memcpy(data, dib.data(), dib.size()); GlobalUnlock(memory);
        CHECK(OpenClipboard(app.hwnd)); CHECK(EmptyClipboard());
        CHECK(SetClipboardData(CF_DIBV5, memory)); CHECK(CloseClipboard());
        measure("dib32_clipboard_paste_2048x1536", 3, [&] { auto pasted = paste_image(app.hwnd); CHECK(pasted.width == image->width); });
        CHECK(paste_image(app.hwnd).frames[0].pixels == image->frames[0].pixels);
        for (bool visible : {false, true}) {
            auto next = app.settings; next.menu = visible; app.apply_settings(next);
            measure(visible ? "document_menu_visible" : "document_menu_hidden", 10, [&] { app.refresh_menu(false); });
        }
        CHECK(app.image == image && !app.modified && notices.empty());
    }
    void daily_reused_menu_matches_rebuilt_state_and_navigation() {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        auto options = app.settings; options.menu = true; options.confirm_unsaved = false;
        app.apply_settings(options);
        const auto system = GetSystemMenu(app.hwnd,FALSE); CHECK(system);
        EnableMenuItem(system,SC_CLOSE,MF_BYCOMMAND | MF_GRAYED);
        const auto system_close = GetMenuState(system,SC_CLOSE,MF_BYCOMMAND) & MFS_DISABLED;
        app.message(WM_INITMENUPOPUP,reinterpret_cast<WPARAM>(system),MAKELPARAM(0,TRUE));
        CHECK((GetMenuState(system,SC_CLOSE,MF_BYCOMMAND) & MFS_DISABLED) == system_close);
        EnableMenuItem(system,SC_CLOSE,MF_BYCOMMAND | MF_ENABLED);
        const auto folder = temporary / L"daily-navigation";
        std::filesystem::create_directory(folder);
        for (const auto &name : {L"01.png", L"02.png"})
            std::filesystem::copy_file(fixture(L"alpha.png"), folder / name);
        std::filesystem::copy_file(fixture(L"disposal.gif"), folder / L"03.gif");
        const auto snapshot = [&](auto &&self, HMENU menu) -> std::wstring {
            std::wstring result;
            for (int n = 0; n < GetMenuItemCount(menu); ++n) {
                wchar_t label[4096]{}; MENUITEMINFOW item{sizeof(item)};
                item.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU;
                item.dwTypeData = label; item.cch = 4096;
                CHECK(GetMenuItemInfoW(menu,n,TRUE,&item));
                result += std::to_wstring(item.hSubMenu ? 0 : item.wID) + L":" + label + L":" +
                    std::to_wstring(item.fState & (MFS_DISABLED | MFS_CHECKED)) + L":" +
                    std::to_wstring(item.fType & (MFT_SEPARATOR | MFT_RADIOCHECK));
                if (item.hSubMenu) result += L"{" + self(self,item.hSubMenu) + L"}";
            }
            return result;
        };
        unsigned comparisons{};
        const auto compare = [&] {
            const auto expected = app.menu();
            const auto actual_state = snapshot(snapshot,app.main_menu);
            const auto expected_state = snapshot(snapshot,expected);
            ui_destroy_menu(expected);
            ++comparisons;
            if (actual_state != expected_state) {
                const auto mismatch = std::mismatch(actual_state.begin(),actual_state.end(),expected_state.begin(),expected_state.end());
                const auto at = size_t(mismatch.first - actual_state.begin());
                std::cout << "MENU_COMPARE " << comparisons << " mismatch at " << at << " actual="
                    << (at < actual_state.size() ? unsigned(actual_state[at]) : 0) << " expected="
                    << (at < expected_state.size() ? unsigned(expected_state[at]) : 0) << "\n";
            }
            CHECK(actual_state == expected_state);
        };
        open(folder / L"01.png"); compare();
        const auto stable = app.main_menu;
        command(Next); wait(); CHECK(app.main_menu == stable); compare();
        command(Next); wait(); CHECK(app.image->animated() && app.main_menu == stable); compare();
        command(StopAnimation); compare();
        command(Previous); wait(); CHECK(app.main_menu == stable); compare();
        for (UINT id : {ToggleEditor, SelectRectangle, SelectAll, Copy, RotateRight, Undo, Redo,
                        ClearSelection, Checker, Grid8, SortReverse}) {
            command(id); CHECK(app.main_menu == stable); compare();
        }
        app.selection = {0,0,1,1};
        app.message(WM_INITMENUPOPUP,reinterpret_cast<WPARAM>(GetSubMenu(app.main_menu,1)),0);
        compare();
        command(PasteSelection); CHECK(app.floating); compare();
        command(CancelSelection); CHECK(!app.floating); compare();
        options = app.settings; options.skip_count = 7;
        options.set_keys(Copy,{L"Ctrl+Alt+C"});
        options.history.insert(options.history.begin(),temporary.wstring());
        app.apply_settings(options); compare();
        wchar_t label[128]{};
        CHECK(GetMenuStringW(app.main_menu,SkipNext,label,128,MF_BYCOMMAND));
        CHECK(std::wstring(label).starts_with(L"7枚"));
        CHECK(GetMenuStringW(app.main_menu,Copy,label,128,MF_BYCOMMAND));
        CHECK(std::wstring(label).find(L"Ctrl+Alt+C") != std::wstring::npos);
        options.menu = false; app.apply_settings(options); CHECK(!GetMenu(app.hwnd));
        command(Previous); wait(); compare();
        command(ToggleMenu); CHECK(GetMenu(app.hwnd) == app.main_menu); compare();
        CHECK(notices.empty());
    }
