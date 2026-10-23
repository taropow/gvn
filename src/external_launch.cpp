#include "gvn/external.h"
#include <shellapi.h>
#include <system_error>

namespace gvn {
bool launch_external_application(HWND owner, const ExternalLaunch &request) {
    if (request.file.empty()) return false;
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    // Finish handing the request to the Shell before a configured viewer exit; do not wait for the app to idle.
    execute.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.hwnd = owner;
    execute.lpVerb = request.verb.empty() ? nullptr : request.verb.c_str();
    execute.lpFile = request.file.c_str();
    execute.lpParameters = request.parameters.empty() ? nullptr : request.parameters.c_str();
    execute.lpDirectory = request.directory.c_str();
    execute.nShow = request.show;
    if (!ShellExecuteExW(&execute))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot launch external application");
    return true;
}
} // namespace gvn
