    // Native size messages and real D2D draws. A private desktop does not
    // measure physical DWM presentation or a user's pointer input latency.
    void resize_interaction_benchmark() {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        SetWindowLongPtrW(app.hwnd, GWL_STYLE, GetWindowLongPtrW(app.hwnd, GWL_STYLE) | WS_CLIPCHILDREN);
        app.settings.confirm_unsaved = false;
        app.settings.window_toolbar_min = false;
        app.settings.display_down = DisplayLanczos3;
        const auto measure = [&](const char *name, bool draw, bool vertical) {
            CHECK(SUCCEEDED(app.render_scene()));
            app.message(WM_ENTERSIZEMOVE, 0, 0);
            std::array<double, 3> samples{};
            NativeUiProbe probe{app.hwnd, app.toolbar.hwnd, app.editor_tools.hwnd, app.status};
            probe.reset();
            const auto before_builds = app.renderer.sample_builds;
            for (auto &sample : samples) {
                const auto start = std::chrono::steady_clock::now();
                for (int n = 0; n < 20; ++n) {
                    CHECK(SetWindowPos(app.hwnd, nullptr, 0, 0, vertical ? 1000 : 980 + n,
                        vertical ? 680 + n : 700, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
                    if (draw) CHECK(SUCCEEDED(app.render_scene()));
                }
                sample = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count() / 20;
            }
            std::cout << "RESIZE_PERF {\"case\":\"" << name << "\",\"samples_ms\":[";
            for (size_t n = 0; n < samples.size(); ++n) std::cout << (n ? "," : "") << samples[n];
            std::cout << "],\"toolbar_row_layouts\":" << probe.rows
                << ",\"sample_builds\":" << app.renderer.sample_builds - before_builds
                << ",\"source_bytes\":" << app.renderer.source_bytes() << "}\n";
            app.message(WM_EXITSIZEMOVE, 0, 0);
            CHECK(SUCCEEDED(app.render_scene()));
        };
        measure("empty_horizontal_ui", false, false);
        app.replace(selection_fixture(2048, 1536), false, true);
        const auto source = app.image;
        command(FitImage);
        measure("viewer_horizontal_ui", false, false);
        measure("viewer_horizontal_draw", true, false);
        measure("viewer_vertical_draw", true, true);
        command(ToggleEditor);
        measure("editor_horizontal_draw", true, false);
        CHECK(app.image == source && !app.modified && notices.empty());
    }

    void resize_coalesces_geometry_keeps_alpha_and_restores_selected_quality() {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.settings.confirm_unsaved = false;
        app.settings.window_toolbar_min = false;
        app.settings.display_down = app.settings.display_up = DisplayLanczos3;
        app.replace(selection_fixture(1600, 1200), false, true);
        command(FitImage);
        const auto image = app.image;
        const auto pixels = image->frames[0].pixels;
        CHECK(SUCCEEDED(app.render_scene()) && app.renderer.sample_current(image.get(), 0, app.view));
        const auto sample_count = app.renderer.sample_builds;
        app.message(WM_ENTERSIZEMOVE, 0, 0);
        for (int n = 0; n < 12; ++n)
            CHECK(SetWindowPos(app.hwnd, nullptr, 0, 0, 920 + n * 7, 680 + n * 4,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        CHECK(app.resize_layout_pending);
        CHECK(SUCCEEDED(app.render_scene()) && !app.resize_layout_pending);
        CHECK(app.renderer.source_bytes() == 0 && app.renderer.sample_builds == sample_count);
        RECT client{}; CHECK(GetClientRect(app.hwnd, &client));
        CHECK(app.view.width <= client.right && app.view.height <= client.bottom);
        CHECK(app.renderer.filter(app.view) == DisplayLinear);
        app.message(WM_EXITSIZEMOVE, 0, 0);
        CHECK(SUCCEEDED(app.render_scene()));
        CHECK(app.renderer.filter(app.view) == DisplayLanczos3);
        CHECK(app.renderer.sample_current(image.get(), 0, app.view));
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == pixels);

        // A manual zoom can expose pixels outside the previous viewport tile.
        // Such a tile must take the complete-source path instead of blank edges.
        command(Actual);
        app.view.scale = 1.3; app.view.manual = true; app.view.centered = false;
        app.view.pan_x = app.view.pan_y = 0; app.update();
        CHECK(SUCCEEDED(app.render_scene()));
        app.message(WM_ENTERSIZEMOVE, 0, 0);
        CHECK(SetWindowPos(app.hwnd, nullptr, 0, 0, 1300, 900, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        CHECK(SUCCEEDED(app.render_scene()) && app.renderer.source_bytes() == image->bytes());
        app.message(WM_CANCELMODE, 0, 0);
        CHECK(!app.renderer.resizing && !app.resize_layout_pending && SUCCEEDED(app.render_scene()));
        CHECK(app.renderer.sample_current(image.get(), 0, app.view));
        CHECK(app.image == image && image->frames[0].pixels == pixels && !app.modified && notices.empty());
    }

    void resize_toolbar_cached_geometry_matches_forced_native_relayout() {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.settings.window_toolbar_min = false;
        const auto snapshot = [&](HWND bar) {
            std::vector<std::array<LONG, 6>> result;
            for (int n = 0; n < SendMessageW(bar, TB_BUTTONCOUNT, 0, 0); ++n) {
                TBBUTTON item{}; RECT bounds{};
                CHECK(SendMessageW(bar, TB_GETBUTTON, n, reinterpret_cast<LPARAM>(&item)));
                if (!(item.fsState & TBSTATE_HIDDEN))
                    CHECK(SendMessageW(bar, TB_GETITEMRECT, n, reinterpret_cast<LPARAM>(&bounds)));
                result.push_back({bounds.left, bounds.top, bounds.right, bounds.bottom,
                    LONG(item.idCommand), LONG(item.fsState)});
            }
            return result;
        };
        for (bool overflow : {true, false}) for (bool large : {false, true}) {
            app.toolbar.use_overflow(overflow); app.toolbar.configure(false, false, large);
            for (UINT dpi : {96u, 144u}) for (int width : {1300, 1299, 980, 981, 520, 360, 361, 800, 1300}) {
                const auto height = app.toolbar.measure_height(width, dpi);
                const auto geometry = snapshot(app.toolbar.hwnd);
                const auto hidden = app.toolbar.hidden_commands();
                app.toolbar.use_overflow(!overflow); app.toolbar.use_overflow(overflow);
                CHECK(app.toolbar.measure_height(width, dpi) == height);
                CHECK(snapshot(app.toolbar.hwnd) == geometry && app.toolbar.hidden_commands() == hidden);
            }
        }
        CHECK(notices.empty());
    }
