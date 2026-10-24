    void paste_status_erase_stays_themed_during_document_updates() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        struct EraseProbe {
            HWND status;HDC dc{CreateCompatibleDC(nullptr)};HBITMAP bitmap{};HGDIOBJ old{};
            size_t checks{};bool mismatch{};
            static LRESULT CALLBACK message(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
                const auto result=DefSubclassProc(h,m,w,l);
                if(m==SB_SETPARTS||m==SB_SETTEXTW||m==WM_SIZE){
                    auto &self=*reinterpret_cast<EraseProbe *>(data);
                    RECT area{0,0,128,64};FillRect(self.dc,&area,ui_brush(RGB(255,0,255)));
                    const auto erased=SendMessageW(h,WM_ERASEBKGND,reinterpret_cast<WPARAM>(self.dc),0);
                    ++self.checks;
                    if(!erased||GetPixel(self.dc,10,5)!=ui_palette().surface)self.mismatch=true;
                }
                return result;
            }
            explicit EraseProbe(HWND h):status(h){
                BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),128,-64,1,32,BI_RGB};void *pixels{};
                bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);CHECK(dc&&bitmap&&pixels);
                old=SelectObject(dc,bitmap);CHECK(SetWindowSubclass(status,message,903,reinterpret_cast<DWORD_PTR>(this)));
            }
            ~EraseProbe(){RemoveWindowSubclass(status,message,903);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
        } probe(app.status);
        for(int theme:{2,1}){
            auto settings=app.settings;settings.ui_theme=theme;settings.paste_new=true;app.apply_settings(settings);
            command(CloseImage);const auto before=probe.checks;
            auto input=selection_fixture(80,60);const auto pixels=input.frames[0].pixels;
            copy_image(app.hwnd,input,0);command(Paste);
            CHECK(app.image&&app.image->frames[0].pixels==pixels&&status(0)==L"80 × 60  RGBA");
            CHECK(app.toast.text().starts_with(L"貼り付けました"));
            copy_image(app.hwnd,input,0);command(NewPaste);
            CHECK(app.image->frames[0].pixels==pixels&&probe.checks>before&&!probe.mismatch);
            CHECK(IsWindowEnabled(app.editor_chrome.control(editor_id::Name)));
            command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==pixels);
        }
        CHECK(notices.empty());
    }
