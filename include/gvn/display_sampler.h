#pragma once
#include "resample.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace gvn {
// Estimate separable Lanczos work in the visible area, including source rows.
bool background_display_sample(const Image &, const View &, int method);
struct DisplayRequest {
    std::shared_ptr<const Image> image;
    size_t frame{};View view;int method{DisplayLanczos3};
    bool same(const DisplayRequest &other) const {
        return image==other.image&&frame==other.frame&&method==other.method&&
            view.width==other.view.width&&view.height==other.view.height&&view.scale==other.view.scale&&
            view.left()==other.view.left()&&view.top()==other.view.top();
    }
};
struct DisplaySample {
    DisplayRequest request;DisplayTile tile;int method{};uint64_t sequence{};
    bool failed{};
};
// One lazily constructed CPU worker; no Direct2D objects or UI calls are used here.
class DisplaySampler {
    struct Job {DisplayRequest request;uint64_t sequence;};
    mutable std::mutex mutex;
    std::condition_variable_any ready;
    std::optional<Job> pending;
    std::optional<DisplaySample> result;
    std::optional<DisplayRequest> latest;
    std::atomic<uint64_t> epoch{};
    bool working{},notified{};
    std::function<void()> notify;
    void run(std::stop_token);
    void publish(DisplaySample,std::stop_token);
    std::jthread thread;
  public:
    explicit DisplaySampler(std::function<void()> callback={});
    ~DisplaySampler();
    DisplaySampler(const DisplaySampler &)=delete;
    uint64_t request(DisplayRequest);
    void cancel();
    std::optional<DisplaySample> take();
    bool matches(const DisplayRequest &) const;
    bool idle() const;
    bool wait_idle(std::chrono::milliseconds);
};
} // namespace gvn
