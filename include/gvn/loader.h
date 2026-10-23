#pragma once
#include "codecs.h"
#include "folder_navigation.h"
#include "open_source.h"
#include "comic.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <thread>

namespace gvn {
class ImageCache {
    struct Entry {
        std::wstring key;
        std::shared_ptr<Image> image;
    };
    std::list<Entry> entries;
    size_t maximum, used{};
    std::wstring current;
    size_t current_bytes{}, visible_bytes{};
    std::vector<std::wstring> protected_warm;
    bool protected_key(const std::wstring &key) const;
    void trim();

  public:
    explicit ImageCache(size_t limit = kDecodeLimit) : maximum(limit) {}
    std::shared_ptr<Image> get(const std::wstring &key);
    void put(std::wstring key, std::shared_ptr<Image> image);
    void activate(std::wstring key, std::shared_ptr<Image> image);
    void begin_prefetch(size_t visible = 0);
    void protect_prefetch(const std::wstring &key);
    bool put_prefetch(std::wstring key, std::shared_ptr<Image> image);
    size_t prefetch_limit() const;
    void clear();
    void set_limit(size_t limit);
    size_t limit() const {
        return maximum;
    }
    size_t foreground_bytes() const {
        return std::max(current_bytes, visible_bytes);
    }
    size_t bytes() const {
        return used;
    }
};
constexpr size_t kDefaultCacheLimit = 64ull * 1024 * 1024;
struct LoaderStats {
    size_t cache_bytes{}, cache_limit{}, foreground_bytes{};
    uint64_t foreground_loads{}, prefetch_loads{}, cache_hits{}, prefetch_skips{}, discarded{};
    bool idle{};
};
struct Loaded {
    uint64_t sequence{};
    std::filesystem::path path;
    std::shared_ptr<Image> image;
    std::string error;
    std::optional<FolderMatch> folder_match;
    std::optional<OpenMatch> open_match;
};
class Loader {
  public:
    using FolderFinder = std::function<FolderMatch(const FolderQuery &, const std::function<bool()> &)>;

  private:
    struct Request {
        uint64_t sequence;
        std::filesystem::path path;
        bool force;
        std::filesystem::path second;
        bool rtl{};
        ComicOptions comic;
        LoadingOptions options;
        std::optional<FolderQuery> folder_query;
        std::optional<OpenQuery> open_query;
        OpenLister open_lister;
    };
    mutable std::mutex mutex;
    std::condition_variable_any ready;
    std::optional<Request> pending;
    std::vector<std::filesystem::path> warm;
    LoadingOptions warm_options;
    size_t requested_limit;
    bool prefetch_enabled{true}, maintenance{}, clear_requested{}, active{};
    uint64_t warm_epoch{};
    size_t warm_visible_bytes{};
    LoaderStats observed;
    std::atomic<uint64_t> latest{};
    ImageCache cache;
    std::function<void(Loaded)> callback;
    std::function<Image(const std::filesystem::path &)> decoder;
    FolderFinder folder_finder;
    std::jthread thread;
    void run(std::stop_token stop);

  public:
    explicit Loader(std::function<void(Loaded)> callback, size_t cache_limit = kDefaultCacheLimit,
                    std::function<Image(const std::filesystem::path &)> decoder = {},
                    FolderFinder folder_finder = {});
    ~Loader();
    uint64_t request(std::filesystem::path path, bool force = false, std::filesystem::path second = {},
                     bool rtl = false, bool match_height = false, LoadingOptions options = {});
    uint64_t request(std::filesystem::path path, bool force, std::filesystem::path second, bool rtl,
                     ComicOptions comic, LoadingOptions options = {});
    // Reuses the image worker; discovery never decodes pixels or creates an additional idle thread.
    uint64_t request_folder(FolderQuery);
    uint64_t request_open(OpenQuery, OpenLister = {});
    void cancel(bool release_cache = false);
    void configure(size_t cache_limit, bool enable_prefetch);
    LoaderStats stats() const;
    bool wait_idle(std::chrono::milliseconds timeout);
    void prefetch(std::vector<std::filesystem::path> paths, LoadingOptions options = {},
                  size_t visible_bytes = 0);
    uint64_t sequence() const {
        return latest.load();
    }
};
} // namespace gvn
