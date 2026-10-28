#pragma once
#include "transfer.h"
#include "selection_region.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace gvn {
struct ClipboardCopyRequest {
    std::shared_ptr<const Image> image;size_t frame{};SelectionRegion selection;ImageMetadata metadata;
};
PreparedClipboardImage prepare_clipboard_copy(const ClipboardCopyRequest &,const CancelCheck &cancel={});
struct ClipboardCopyResult {
    uint64_t sequence{};PreparedClipboardImage prepared;SIZE size{};std::string error;
};
class ClipboardCopyWorker {
public:
    using Encoder=std::function<PreparedClipboardImage(const ClipboardCopyRequest &,const CancelCheck &)>;
private:
    struct Job {ClipboardCopyRequest request;uint64_t sequence;};
    std::mutex mutex;std::condition_variable_any ready;
    std::optional<Job> pending;std::optional<ClipboardCopyResult> result;
    std::atomic<uint64_t> epoch{};bool working{};
    std::function<void()> notify;Encoder encoder;std::jthread thread;
    void run(std::stop_token);
public:
    explicit ClipboardCopyWorker(std::function<void()> notify={},Encoder encoder={});
    ~ClipboardCopyWorker();
    uint64_t request(ClipboardCopyRequest);
    void cancel();
    std::optional<ClipboardCopyResult> take();
    bool wait_idle(std::chrono::milliseconds);
};
}
