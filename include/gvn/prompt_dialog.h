#pragma once
#include "prompt_metadata.h"
#include <windows.h>
namespace gvn {
namespace prompt_control {constexpr int Status=100,Source=101,Tree=102,Heading=103,Value=104,Notice=105,Copy=106,Positive=107,Negative=108;}
class PromptDialog {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    explicit PromptDialog(PromptDocument);
    ~PromptDialog();
    void show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    bool ready() const;
    const PromptDocument &document() const;
    void select(size_t);
    size_t selected() const;
};
void show_prompt(HWND,const PromptDocument &);
}
