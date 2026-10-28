#include "gvn/loader.h"
#include "gvn/archive.h"
#include "gvn/edit.h"
#include "gvn/view_rotation.h"
#include <utility>

namespace gvn {
bool ImageCache::protected_key(const std::wstring &key) const {
    return (!current.empty() && key == current) ||
           std::find(protected_warm.begin(), protected_warm.end(), key) != protected_warm.end();
}
void ImageCache::trim() {
    const bool stored =
        std::any_of(entries.begin(), entries.end(), [&](const auto &e) { return e.key == current; });
    const size_t external = foreground_bytes() - (stored ? current_bytes : 0);
    const size_t allowed = maximum > external ? maximum - external : 0;
    while (used > allowed) {
        const auto victim = std::find_if(entries.rbegin(), entries.rend(),
                                         [&](const auto &e) { return !protected_key(e.key); });
        if (victim == entries.rend())
            break;
        used -= victim->image->bytes();
        entries.erase(std::next(victim).base());
    }
}
void ImageCache::clear() {
    entries.clear();
    used = current_bytes = visible_bytes = 0;
    current.clear();
    protected_warm.clear();
}
void ImageCache::set_limit(size_t limit) {
    maximum = limit;
    protected_warm.clear();
    if (foreground_bytes() > maximum) {
        entries.clear();
        used = 0;
    } else
        trim();
}
void ImageCache::activate(std::wstring key, std::shared_ptr<Image> image) {
    current = key;
    current_bytes = image->bytes();
    visible_bytes = 0;
    protected_warm.clear();
    if (current_bytes > maximum) {
        entries.clear();
        used = 0;
        return;
    }
    put(std::move(key), std::move(image));
}
size_t ImageCache::prefetch_limit() const {
    size_t protected_bytes = foreground_bytes();
    for (const auto &entry : entries)
        if (entry.key != current &&
            std::find(protected_warm.begin(), protected_warm.end(), entry.key) != protected_warm.end())
            protected_bytes += entry.image->bytes();
    return maximum > protected_bytes ? maximum - protected_bytes : 0;
}
void ImageCache::begin_prefetch(size_t visible) {
    visible_bytes = visible;
    protected_warm.clear();
    if (foreground_bytes() > maximum) {
        entries.clear();
        used = 0;
    } else
        trim();
}
void ImageCache::protect_prefetch(const std::wstring &key) {
    if (key != current &&
        std::find(protected_warm.begin(), protected_warm.end(), key) == protected_warm.end())
        protected_warm.push_back(key);
}
bool ImageCache::put_prefetch(std::wstring key, std::shared_ptr<Image> image) {
    if (!image || image->bytes() > prefetch_limit())
        return false;
    protect_prefetch(key);
    put(std::move(key), std::move(image));
    return true;
}
std::shared_ptr<Image> ImageCache::get(const std::wstring &key) {
    for (auto it = entries.begin(); it != entries.end(); ++it)
        if (it->key == key) {
            auto result = it->image;
            entries.splice(entries.begin(), entries, it);
            return result;
        }
    return {};
}
void ImageCache::put(std::wstring key, std::shared_ptr<Image> image) {
    for (auto it = entries.begin(); it != entries.end(); ++it)
        if (it->key == key) {
            used -= it->image->bytes();
            entries.erase(it);
            break;
        }
    if (!image || image->bytes() > maximum)
        return;
    used += image->bytes();
    entries.push_front({std::move(key), std::move(image)});
    trim();
}
static std::wstring file_key(const std::filesystem::path &path) {
    const auto file = source_file(path);
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error)
        throw std::runtime_error("Image file unavailable");
    const auto time = std::filesystem::last_write_time(file, error);
    if (error)
        throw std::runtime_error("Cannot read image timestamp");
    return std::filesystem::absolute(path).lexically_normal().wstring() + L"|" + std::to_wstring(size) +
           L"|" + std::to_wstring(time.time_since_epoch().count());
}
Loader::Loader(std::function<void(Loaded)> receive, size_t limit,
               std::function<Image(const std::filesystem::path &)> decode_image, FolderFinder find,
               InterruptibleDecoder controlled)
    : requested_limit(limit), cache(limit), callback(std::move(receive)), decoder(std::move(decode_image)),
      interruptible_decoder(std::move(controlled)),folder_finder(std::move(find)) {
    observed.cache_limit = limit;
    thread = std::jthread([this](std::stop_token stop) { run(stop); });
}
Loader::~Loader() {
    thread.request_stop();
    ready.notify_all();
    if (thread.joinable())
        thread.join();
}
uint64_t Loader::request(std::filesystem::path path, bool force, std::filesystem::path second, bool rtl,
                         bool match_height, LoadingOptions options) {
    return request(std::move(path), force, std::move(second), rtl, ComicOptions{match_height ? 3 : 4},
                   options);
}
uint64_t Loader::request(std::filesystem::path path, bool force, std::filesystem::path second, bool rtl,
                         ComicOptions comic, LoadingOptions options) {
    std::lock_guard guard(mutex);
    const uint64_t id = ++latest;
    pending = Request{id, std::move(path), force, std::move(second), rtl, comic, options};
    warm.clear();
    ready.notify_all();
    return id;
}
uint64_t Loader::request_folder(FolderQuery query) {
    std::lock_guard guard(mutex);
    const uint64_t id = ++latest;
    Request request{id, query.directory, false};
    request.folder_query = std::move(query);
    pending = std::move(request);
    warm.clear();
    ready.notify_all();
    return id;
}
void Loader::cancel(bool release_cache) {
    std::lock_guard guard(mutex);
    ++latest;
    pending.reset();
    warm.clear();
    if (release_cache) {
        clear_requested = maintenance = true;
        ready.notify_all();
    }
}
uint64_t Loader::request_open(OpenQuery query, OpenLister lister) {
    std::lock_guard guard(mutex);
    const uint64_t id = ++latest;
    Request request{id, query.path, false};
    request.open_query = std::move(query);
    request.open_lister = std::move(lister);
    pending = std::move(request);
    warm.clear();
    ready.notify_all();
    return id;
}
void Loader::configure(size_t limit, bool enable_prefetch) {
    std::lock_guard guard(mutex);
    requested_limit = limit;
    prefetch_enabled = enable_prefetch;
    if (!enable_prefetch || !limit)
        warm.clear();
    maintenance = true;
    ready.notify_all();
}
LoaderStats Loader::stats() const {
    std::lock_guard guard(mutex);
    auto result = observed;
    result.idle = !active && !pending && warm.empty() && !maintenance;
    return result;
}
bool Loader::wait_idle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex);
    return ready.wait_for(lock, timeout, [&] { return !active && !pending && warm.empty() && !maintenance; });
}
void Loader::prefetch(std::vector<std::filesystem::path> paths, LoadingOptions options,
                      size_t visible_bytes) {
    std::lock_guard guard(mutex);
    warm = prefetch_enabled && requested_limit ? std::move(paths) : std::vector<std::filesystem::path>{};
    warm_options = options;
    warm_visible_bytes = visible_bytes;
    ++warm_epoch;
    ready.notify_all();
}
void Loader::run(std::stop_token stop) {
    LoaderStats tally;
    uint64_t applied_warm_epoch{};
    while (!stop.stop_requested()) {
        Request request{};
        bool preloading = false, has_request = false, update_policy{}, clear{};
        size_t limit{}, visible{};
        uint64_t batch{};
        {
            std::unique_lock lock(mutex);
            ready.wait(lock, stop, [&] { return pending.has_value() || !warm.empty() || maintenance; });
            if (stop.stop_requested())
                break;
            active = true;
            update_policy = std::exchange(maintenance, false);
            clear = std::exchange(clear_requested, false);
            limit = requested_limit;
            if (pending) {
                has_request = true;
                request = std::move(*pending);
                pending.reset();
            } else if (!warm.empty()) {
                has_request = true;
                preloading = true;
                batch = warm_epoch;
                visible = warm_visible_bytes;
                request = {latest.load(), warm.front(), false};
                request.options = warm_options;
                warm.erase(warm.begin());
            }
        }
        if (clear)
            cache.clear();
        if (update_policy)
            cache.set_limit(limit);
        if (preloading && batch != applied_warm_epoch) {
            applied_warm_epoch = batch;
            cache.begin_prefetch(visible);
        }
        Loaded result;
        result.sequence = request.sequence;
        result.path = request.path;
        if (has_request)
            try {
                if (request.open_query) {
                    const std::function<bool()> cancelled = [&] {
                        return stop.stop_requested() || result.sequence != latest.load();
                    };
                    result.open_match = prepare_open(*request.open_query, cancelled, request.open_lister);
                    result.path = result.open_match->path;
                } else if (request.folder_query) {
                    const std::function<bool()> cancelled = [&] {
                        return stop.stop_requested() || result.sequence != latest.load();
                    };
                    result.folder_match = folder_finder ? folder_finder(*request.folder_query, cancelled)
                                                        : find_folder(*request.folder_query, cancelled);
                    result.path = result.folder_match->directory;
                } else {
                    const CancelCheck cancelled=[&] {
                        if(stop.stop_requested())return true;
                        if(request.sequence==latest.load())return false;
                        if(!preloading)return true;
                        std::lock_guard guard(mutex);
                        // A foreground request for this very image promotes the work.
                        return !(pending&&!pending->folder_query&&!pending->open_query&&!pending->force&&
                            pending->second.empty()&&pending->path==request.path&&
                            pending->options.flags()==request.options.flags()&&pending->options.background==request.options.background);
                    };
                    check_cancelled(cancelled);
                    const auto first_key = file_key(request.path),
                               second_key = request.second.empty() ? L"" : file_key(request.second);
                    // Single images have the same cache identity during prefetch and display.
                    // Reading direction and height matching affect composed pairs only.
                    auto key = request.second.empty()
                                   ? first_key
                                   : first_key + L"|pair:" + second_key + std::to_wstring(request.rtl) +
                                         request.comic.key();
                    key += L"|load:" + std::to_wstring(request.options.flags()) + L":" +
                           std::to_wstring(request.options.background);
                    const auto decode_path = [&](const auto &path) {
                        const size_t budget = preloading ? cache.prefetch_limit() : kDecodeLimit;
                        auto options = request.options;
                        if (!request.second.empty() && !request.comic.rotate_before)
                            options.view_rotation = 0;
                        if (interruptible_decoder||decoder) {
                            auto image = interruptible_decoder?interruptible_decoder(path,cancelled):decoder(path);
                            check_cancelled(cancelled);
                            apply_view_rotation(image, options.view_rotation);
                            return image;
                        }
                        return load_source(path, options, budget, preloading ? budget : kSourceLimit,cancelled);
                    };
                    if (!request.force)
                        result.image = cache.get(key);
                    if (result.image)
                        ++tally.cache_hits;
                    if (!result.image) {
                        if (preloading) {
                            ++tally.prefetch_loads;
                            if (!cache.prefetch_limit())
                                throw std::runtime_error("No remaining prefetch budget");
                        } else
                            ++tally.foreground_loads;
                        result.image = std::make_shared<Image>(decode_path(request.path));
                        if (preloading && result.image->bytes() > cache.prefetch_limit())
                            throw std::runtime_error("Prefetch image exceeds remaining budget");
                        if (!request.second.empty() && !result.image->animated() &&
                            (!request.comic.single_wide || result.image->width <= result.image->height)) {
                            auto second = decode_path(request.second);
                            if (!second.animated() &&
                                (!request.comic.single_wide || second.width <= second.height)) {
                                auto combined =
                                    compose_comic_pages(*result.image, second, request.rtl, request.comic);
                                combined.format = L"見開き";
                                combined.path.clear();
                                result.image = std::make_shared<Image>(std::move(combined));
                            }
                        }
                        if (!request.second.empty() && !request.comic.rotate_before)
                            apply_view_rotation(*result.image, request.options.view_rotation);
                        if (file_key(request.path) != first_key ||
                            (!request.second.empty() && file_key(request.second) != second_key))
                            throw std::runtime_error("Image changed during decoding; reload it");
                    }
                    bool useful = result.sequence == latest.load();
                    if (preloading && !useful) {
                        std::lock_guard guard(mutex);
                        useful = pending && !pending->folder_query && !pending->open_query &&
                                 !pending->force && pending->second.empty() &&
                                 pending->path == request.path &&
                                 pending->options.flags() == request.options.flags() &&
                                 pending->options.background == request.options.background;
                    }
                    if (!stop.stop_requested() && useful) {
                        if (preloading) {
                            if (cache.get(key))
                                cache.protect_prefetch(key);
                            else if (!cache.put_prefetch(key, result.image))
                                ++tally.prefetch_skips;
                        } else
                            cache.activate(key, result.image);
                    } else
                        ++tally.discarded;
                }
            } catch (const OperationCancelled &) {
                ++tally.cancelled;++tally.discarded;result.image.reset();
            } catch (const std::exception &error) {
                result.error = error.what();
                result.image.reset();
                if (preloading)
                    ++tally.prefetch_skips;
            }
        if (has_request && !preloading && !stop.stop_requested() && result.sequence == latest.load()) {
            try {
                callback(std::move(result));
            } catch (...) { /* UI callback failures must not terminate the worker. */
            }
        }
        {
            std::lock_guard guard(mutex);
            tally.cache_bytes = cache.bytes();
            tally.cache_limit = cache.limit();
            tally.foreground_bytes = cache.foreground_bytes();
            observed = tally;
            active = false;
        }
        ready.notify_all();
    }
}
} // namespace gvn
