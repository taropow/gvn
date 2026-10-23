#pragma once
#include "loader.h"
#include <memory>

namespace gvn {
// The UI owner keeps this service alive after closing a dialog and retires it
// once idle. Requests remain bounded to one decoder plus its latest pending job.
class JumpPreview {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    struct Inbox;
    using Mailbox = std::shared_ptr<Inbox>;
    using Decoder = std::function<Image(const std::filesystem::path &)>;
    explicit JumpPreview(Decoder = {});
    ~JumpPreview();
    Mailbox mailbox();
    uint64_t request(const std::filesystem::path &, LoadingOptions, const Mailbox &);
    void cancel(const Mailbox &);
    void cancel();
    std::optional<Loaded> take(const Mailbox &);
    bool idle() const;
    bool started() const;
};
} // namespace gvn
