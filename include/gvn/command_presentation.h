#pragma once
#include "settings.h"
namespace gvn {
inline std::wstring command_caption(const Settings &settings,unsigned id,const std::wstring &name={},bool menu=false){
 auto label=name.empty()?std::wstring(command_name(id)):name;label=label.substr(0,label.find(L'\t'));
 const auto key=settings.first_key(id);if(!key.empty())label+=menu?L"\t"+key:L" ("+key+L")";return label;
}
struct CommandContext {bool image{},pending{},busy{},file{},selection{},undo{},redo{},files{};};
inline bool common_command_enabled(unsigned id,const CommandContext &s){
 switch(id){case Save:case SaveAs:case Copy:case ArmDrag:return s.image&&!s.pending;case Info:case ShowPrompt:case ZoomIn:case ZoomOut:case FitImage:case Actual:return s.image;case Fullscreen:return true;
 case Undo:return s.pending||s.undo;case Redo:return s.redo&&!s.pending;case FileDelete:return s.file&&!s.pending;case Previous:case Next:return s.files;default:return true;}
}
}
