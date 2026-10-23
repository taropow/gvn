    void fill_context(int row=0){
        const auto list=find(cid::ContextList);if(!list)return;
        SendMessageW(list,LB_RESETCONTENT,0,0);
        for(auto id:draft.context_menu_items){const auto label=id?context_item_name(id):L"──────── 区切り線 ────────";
            SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));}
        if(!draft.context_menu_items.empty())SendMessageW(list,LB_SETCURSEL,std::clamp(row,0,int(draft.context_menu_items.size())-1),0);
    }
    void initialize_context(){
        const auto page=pages[size_t(Page::ContextMenu)];
        make(page,L"STATIC",L"カスタムでは一覧の項目だけを表示します。状態別の操作と「すべてのメニュー」は自動で追加します。",SS_LEFT,cid::ContextHelp);
        make(page,L"LISTBOX",L"右クリックメニューの並び順",WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,cid::ContextList);
        const auto choice=make(page,WC_COMBOBOXW,L"追加する操作",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,cid::ContextChoice);
        for(const auto &c:context_menu_choices())SendMessageW(choice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(c.name.c_str()));
        SendMessageW(choice,CB_SETCURSEL,0,0);
        for(auto [id,title]:{std::pair{cid::ContextAdd,L"追加"},{cid::ContextRemove,L"非表示にする"},{cid::ContextUp,L"上へ"},{cid::ContextDown,L"下へ"},{cid::ContextSeparatorButton,L"区切り線を追加"}})
            make(page,L"BUTTON",title,WS_TABSTOP,id);
        fill_context();
    }
    bool context_action(int id){
        if(id<cid::ContextAdd||id>cid::ContextSeparatorButton)return false;
        if(SendMessageW(find(cid::ContextMode),CB_GETCURSEL,0,0)!=2)return true;
        auto &items=draft.context_menu_items;int row=int(SendMessageW(find(cid::ContextList),LB_GETCURSEL,0,0));
        if(id==cid::ContextRemove){if(row<0||size_t(row)>=items.size())return true;items.erase(items.begin()+row);}
        else if(id==cid::ContextUp||id==cid::ContextDown){
            const int next=row+(id==cid::ContextUp?-1:1);if(row<0||next<0||size_t(next)>=items.size())return true;
            std::swap(items[row],items[next]);row=next;
        }else{
            if(items.size()>=64)return true;
            const int choice=int(SendMessageW(find(cid::ContextChoice),CB_GETCURSEL,0,0));
            if(id!=cid::ContextSeparatorButton&&(choice<0||size_t(choice)>=context_menu_choices().size()))return true;
            const auto item=id==cid::ContextSeparatorButton?0:context_menu_choices()[choice].id;
            if(item){const auto found=std::find(items.begin(),items.end(),item);if(found!=items.end()){SendMessageW(find(cid::ContextList),LB_SETCURSEL,found-items.begin(),0);return true;}}
            row=std::clamp(row+1,0,int(items.size()));items.insert(items.begin()+row,item);
        }
        fill_context(row);changed();return true;
    }
