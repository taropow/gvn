#pragma once
#include "selection_units.h"
#include <windows.h>
#include <optional>
namespace gvn {
namespace selection_control {enum {StartX=600,StartY,SizeX,SizeY,Quick=620,Error=650,Spin=660};}
class SelectionUnitsDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    explicit SelectionUnitsDialog(SelectionUnits);
    ~SelectionUnitsDialog();
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    bool ready() const;
    std::optional<SelectionUnits> result() const;
    std::optional<SelectionUnits> run(HWND);
};
std::optional<SelectionUnits> choose_selection_units(HWND,SelectionUnits);
}
