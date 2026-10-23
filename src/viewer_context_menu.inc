    HMENU contextual_root{},contextual_all{};
    std::vector<HMENU> contextual_handles;
    bool contextual_all_ready{};
    bool contextual_menu(HMENU menu) const {return std::find(contextual_handles.begin(),contextual_handles.end(),menu)!=contextual_handles.end();}
    HMENU context_child(){const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create context menu");contextual_handles.push_back(menu);return menu;}
    bool context_enabled(UINT id) const {
        const auto state=command_context();
        if(!menu_command_enabled(id,state))return false;
        switch(id){
        case CommitSelection:case CancelSelection:return floating||cropping;
        case Copy:case Save:case SaveAs:case Cut:case ToolCrop:case ToolPen:case ToolText:case ToggleEditor:
        case RotateRight:case RotateLeft:case Rotate180:case RotateAngle:case FlipH:case FlipV:
            return image&&!state.pending&&!loading;
        case Paste:case PasteSelection:case NewPaste:return !state.pending&&!loading;
        case ClearSelection:case InvertSelection:case Crop:case DeleteSelection:case SelectMove:
            return image&&!selection.empty()&&!state.pending&&!loading;
        case FileCopy:case FileMove:case RenameFile:case CopyPath:case FileDelete:case FileDeleteDirect:
            return state.file&&!state.pending&&!loading;
        case External:return image&&!state.pending&&!loading;
        case Play:case StopAnimation:case FramePrevious:case FrameNext:case Loop:case FirstFrame:case LastFrame:
            return image&&image->animated()&&!state.pending&&!loading;
        default:
            if(id>=ExternalBase&&id<ExternalBase+9)return image&&!state.pending&&!loading;
            return true;
        }
    }
    std::wstring context_label(UINT id) const {
        switch(id){case Copy:return selection.empty()?L"画像をコピー":L"選択範囲をコピー";
        case PasteSelection:return L"選択範囲で貼り付け";case FitImage:return L"全体を表示";case Actual:return L"100%で表示";
        case ToggleEditor:return editor_mode?L"画像編集モードを終了":L"画像編集モード";
        case CommitSelection:return cropping?L"トリミングを適用":L"自由変形を適用";
        case CancelSelection:return L"キャンセル";case Crop:return L"選択範囲でトリミング";
        case DeleteSelection:return L"選択範囲の画素を削除";
        case FileDelete:return settings.recycle_deleted?L"元のファイルをごみ箱へ…":L"元のファイルを削除…";
        case FileDeleteDirect:return L"元のファイルを完全に削除…";
        case Play:return player.playing?L"一時停止":L"再生";
        case FramePrevious:return L"前のコマ";case FrameNext:return L"次のコマ";case Loop:return L"ループ再生";
        case ConfigureContextMenu:return L"このメニューをカスタマイズ…";
        default:return command_name(id);}
    }
    void context_add(HMENU menu,UINT id,const std::wstring &name={}){
        const auto label=command_caption(settings,id,name.empty()?context_label(id):name,true);
        AppendMenuW(menu,MF_STRING|(context_enabled(id)?0:MF_GRAYED)|(checked(id)?MF_CHECKED:0),id,label.c_str());
    }
    static void context_separator(HMENU menu){const auto count=GetMenuItemCount(menu);if(count>0&&!(GetMenuState(menu,count-1,MF_BYPOSITION)&MF_SEPARATOR))separator(menu);}
    static std::wstring menu_literal(std::wstring value){std::wstring out;for(auto c:value){if(c==L'&')out+=L'&';out+=c==L'\t'||c==L'\n'||c==L'\r'?L' ':c;}return out;}
    void context_entry(HMENU menu,UINT id){
        if(id==ContextRotate){const auto child=context_child();for(UINT c:{RotateRight,RotateLeft,Rotate180,RotateAngle,FlipH,FlipV})context_add(child,c);submenu(menu,child,L"回転・反転");}
        else if(id==ContextExternal){const auto child=context_child();context_add(child,External,L"アプリを選んで開く…");
            for(size_t n=0;n<9;++n){const auto entry=settings.external_application(n);if(!entry.name.empty()){context_add(child,ExternalBase+UINT(n),menu_literal(entry.name));if(entry.separator)context_separator(child);}}
            context_separator(child);context_add(child,ConfigureExternal,L"編集ソフトを登録…");submenu(menu,child,L"編集ソフトで開く");}
        else if(id==ContextFile){const auto child=context_child();context_add(child,CopyPath,L"元のファイルのパスをコピー");
            for(UINT c:{RenameFile,FileCopy,FileMove})context_add(child,c);context_separator(child);context_add(child,FileDelete);submenu(menu,child,L"ファイル操作");}
        else context_add(menu,id);
    }
    HMENU build_context_menu(){
        contextual_handles.clear();contextual_all_ready=false;contextual_all=nullptr;
        const auto menu=contextual_root=context_child();std::vector<UINT> shown;
        const auto add_once=[&](UINT id){if(std::find(shown.begin(),shown.end(),id)==shown.end()){context_entry(menu,id);shown.push_back(id);}};
        if(floating||cropping){add_once(CommitSelection);add_once(CancelSelection);context_separator(menu);}
        if(!image&&!floating){for(UINT id:{Open,Paste,NewImage})add_once(id);}
        else{
            const auto entries=settings.context_menu_mode==2?validated_context_items(settings.context_menu_items):default_context_items();
            for(auto id:entries){if(!id)context_separator(menu);else add_once(id);}
            if(!floating&&!cropping&&!selection.empty()){
                context_separator(menu);for(UINT id:{Cut,ClearSelection,InvertSelection,Crop,DeleteSelection})add_once(id);
            }
            if(image&&image->animated()){
                context_separator(menu);for(UINT id:{Play,FramePrevious,FrameNext,Loop})add_once(id);
            }
        }
        context_separator(menu);context_add(menu,ConfigureContextMenu);
        contextual_all=context_child();AppendMenuW(contextual_all,MF_STRING|MF_GRAYED,0,L"すべてのメニュー");
        submenu(menu,contextual_all,L"すべてのメニュー");ui_menu_icons(menu);ui_theme_menu(menu);return menu;
    }
    void clone_context_items(HMENU target,HMENU source){
        for(int n=0;n<GetMenuItemCount(source);++n){wchar_t text[2048]{};MENUITEMINFOW item{sizeof(item)};
            item.fMask=MIIM_FTYPE|MIIM_STATE|MIIM_ID|MIIM_SUBMENU|MIIM_STRING;item.dwTypeData=text;item.cch=2048;
            if(!GetMenuItemInfoW(source,n,TRUE,&item))throw std::runtime_error("Cannot read full menu");
            item.fType&=~MFT_OWNERDRAW;
            if(item.hSubMenu){const auto child=context_child();clone_context_items(child,item.hSubMenu);item.hSubMenu=child;}
            if(!InsertMenuItemW(target,UINT(-1),TRUE,&item))throw std::runtime_error("Cannot copy full menu");
        }
    }
    void context_init(HMENU menu){
        if(menu==contextual_all&&!contextual_all_ready){
            DeleteMenu(menu,0,MF_BYPOSITION);clone_context_items(menu,main_menu);contextual_all_ready=true;
        }
        for(int n=0;n<GetMenuItemCount(menu);++n){const auto id=GetMenuItemID(menu,n);if(id==UINT(-1)||!id)continue;
            EnableMenuItem(menu,id,MF_BYCOMMAND|(context_enabled(id)?MF_ENABLED:MF_GRAYED));
            CheckMenuItem(menu,id,MF_BYCOMMAND|(checked(id)?MF_CHECKED:MF_UNCHECKED));
        }
    }
    void context_popup(POINT point){
        if(settings.context_menu_mode==1){popup_menu(PopupAll,point);return;}
        if(contextual_root)return;
        UINT chosen{};
        {struct Cleanup{App &app;~Cleanup(){ui_destroy_menu(app.contextual_root);app.contextual_root=app.contextual_all=nullptr;app.contextual_handles.clear();}} cleanup{*this};
            chosen=dialogs.popup(hwnd,build_context_menu(),point);
        }
        if(chosen&&context_enabled(chosen))command(chosen);
    }
