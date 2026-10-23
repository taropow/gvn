#include "gvn/context_menu.h"
#include <algorithm>
namespace gvn {
const std::vector<CommandInfo> &context_menu_choices(){
    static const auto choices=[] {
        std::vector<CommandInfo> out{{0,L"区切り線"},{ContextRotate,L"回転・反転（サブメニュー）"},
            {ContextExternal,L"編集ソフトで開く（サブメニュー）"},{ContextFile,L"ファイル操作（サブメニュー）"}};
        for(const auto &c:command_catalog())if(c.id!=PopupContext&&c.id!=ConfigureContextMenu&&
            !(c.id>=PopupAll&&c.id<=PopupExternal))out.push_back(c);
        return out;
    }();return choices;
}
const wchar_t *context_item_name(unsigned id){for(const auto &c:context_menu_choices())if(c.id==id)return c.name.c_str();return L"不明な操作";}
std::vector<unsigned> default_context_items(){return {Copy,Paste,PasteSelection,SaveAs,0,FitImage,Actual,ContextRotate,ToggleEditor,0,ContextExternal,ShowPrompt,ContextFile};}
std::vector<unsigned> validated_context_items(std::span<const unsigned> values){
    std::vector<unsigned> out;
    for(auto id:values){
        if(out.size()==64)break;
        if(std::any_of(context_menu_choices().begin(),context_menu_choices().end(),[&](const auto &c){return c.id==id;})&&
            (!id||std::find(out.begin(),out.end(),id)==out.end()))out.push_back(id);
    }return out;
}
}
