#include "gvn/jump_preview.h"
#include <mutex>
#include <utility>

namespace gvn {
struct JumpPreview::Inbox {
    std::mutex mutex;
    std::optional<Loaded> value;
};
struct JumpPreview::Impl {
    std::mutex mutex;
    std::weak_ptr<Inbox> recipient;
    Decoder decoder;
    // Join before destroying the callback's mutex/recipient. The callback never
    // captures a dialog, HWND, or UI object.
    std::unique_ptr<Loader> worker;
    explicit Impl(Decoder decode) : decoder(std::move(decode)) {}
    void ensure_worker() {
        if (worker)
            return;
        worker = std::make_unique<Loader>(
            [this](Loaded result) {
                Mailbox inbox;
                {
                    std::lock_guard lock(mutex);
                    inbox = recipient.lock();
                }
                if (!inbox)
                    return;
                // Destroy any displaced image outside the mailbox lock.
                std::optional<Loaded> previous;
                {
                    std::lock_guard lock(inbox->mutex);
                    previous = std::move(inbox->value);
                    inbox->value = std::move(result);
                }
            },
            0, decoder);
    }
};
JumpPreview::JumpPreview(Decoder decode) : impl(std::make_unique<Impl>(std::move(decode))) {}
JumpPreview::~JumpPreview() = default;
JumpPreview::Mailbox JumpPreview::mailbox() {
    return std::make_shared<Inbox>();
}
uint64_t JumpPreview::request(const std::filesystem::path &path, LoadingOptions options, const Mailbox &box) {
    if (!box)
        throw std::invalid_argument("Preview request needs a live mailbox");
    impl->ensure_worker();
    {
        std::lock_guard lock(impl->mutex);
        impl->recipient = box;
    }
    return impl->worker->request(path, false, {}, false, false, options);
}
void JumpPreview::cancel(const Mailbox &box) {
    if (!box)
        return;
    bool current{};
    {
        std::lock_guard lock(impl->mutex);
        current = impl->recipient.lock() == box;
        if (current)
            impl->recipient.reset();
    }
    if (current && impl->worker)
        impl->worker->cancel(true);
    std::optional<Loaded> previous;
    {
        std::lock_guard lock(box->mutex);
        previous = std::move(box->value);
        box->value.reset();
    }
}
void JumpPreview::cancel() {
    Mailbox box;
    {
        std::lock_guard lock(impl->mutex);
        box = impl->recipient.lock();
        impl->recipient.reset();
    }
    if (impl->worker)
        impl->worker->cancel(true);
    if (box) {
        std::optional<Loaded> previous;
        std::lock_guard lock(box->mutex);
        previous = std::move(box->value);
        box->value.reset();
    }
}
std::optional<Loaded> JumpPreview::take(const Mailbox &box) {
    if (!box)
        return {};
    std::lock_guard lock(box->mutex);
    auto result = std::move(box->value);
    box->value.reset();
    return result;
}
bool JumpPreview::idle() const {
    return !impl->worker || impl->worker->stats().idle;
}
bool JumpPreview::started() const {
    return impl->worker != nullptr;
}
} // namespace gvn
