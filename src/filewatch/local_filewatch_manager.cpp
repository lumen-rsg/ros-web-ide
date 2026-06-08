#include "filewatch/local_filewatch_manager.hpp"

#include <algorithm>
#include <filesystem>

#ifdef MACOS
#include <CoreServices/CoreServices.h>
#endif

#ifdef LINUX
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/select.h>
#endif

namespace rosweb::filewatch {

// --- Constructor / Destructor ---

LocalFileWatchManager::LocalFileWatchManager() {
    running_ = true;

#ifdef LINUX
    if (::pipe(shutdown_pipe_) < 0) {
        shutdown_pipe_[0] = -1;
        shutdown_pipe_[1] = -1;
    }
    inotify_fd_ = ::inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ >= 0) {
        event_thread_ = std::thread(&LocalFileWatchManager::event_thread_linux, this);
    }
#endif

#ifdef MACOS
    event_thread_ = std::thread(&LocalFileWatchManager::event_thread_macos, this);
#endif
}

LocalFileWatchManager::~LocalFileWatchManager() {
    shutdown();
}

// --- watch / unwatch ---

auto LocalFileWatchManager::watch(const std::string& watch_id,
                                   const std::string& path,
                                   bool recursive)
    -> std::expected<void, errors::ErrorCode> {
    if (!std::filesystem::exists(path)) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

    std::lock_guard lock(watches_mutex_);

    // Remove existing watch with same ID if present
    auto existing = watches_.find(watch_id);
    if (existing != watches_.end()) {
#ifdef LINUX
        for (int wd : existing->second.wds) {
            if (wd >= 0 && inotify_fd_ >= 0) {
                ::inotify_rm_watch(inotify_fd_, wd);
                wd_map_.erase(wd);
            }
        }
#endif
#ifdef MACOS
        if (existing->second.stream) {
            FSEventStreamStop(existing->second.stream);
            FSEventStreamInvalidate(existing->second.stream);
            FSEventStreamRelease(existing->second.stream);
        }
#endif
    }

    WatchEntry entry;
    entry.watch_id = watch_id;
    entry.path = path;
    entry.recursive = recursive;

    // Store entry first so add_recursive_watches can find it
    watches_.emplace(watch_id, std::move(entry));

#ifdef LINUX
    if (inotify_fd_ >= 0) {
        if (recursive) {
            // Walk directory tree and add watches for all subdirectories
            add_recursive_watches(watch_id, path);
        } else {
            uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
            int wd = ::inotify_add_watch(inotify_fd_, path.c_str(), mask);
            if (wd < 0) {
                watches_.erase(watch_id);
                return std::unexpected(errors::ErrorCode::FS_PERMISSION_DENIED);
            }
            watches_[watch_id].wds.insert(wd);
            wd_map_[wd] = {watch_id, path};
        }
    }
#endif

#ifdef MACOS
    // Create FSEventStream for this watch (inherently recursive)
    CFStringRef cf_path = CFStringCreateWithCString(kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
    CFArrayRef paths_to_watch = CFArrayCreate(kCFAllocatorDefault, (const void**)&cf_path, 1, nullptr);

    FSEventStreamContext context = {0, this, nullptr, nullptr, nullptr};
    FSEventStreamRef stream = FSEventStreamCreate(
        kCFAllocatorDefault,
        &LocalFileWatchManager::fs_event_callback,
        &context,
        paths_to_watch,
        kFSEventStreamEventIdSinceNow,
        0.1,
        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);

    CFRelease(paths_to_watch);
    CFRelease(cf_path);

    if (stream) {
        FSEventStreamScheduleWithRunLoop(stream, run_loop_, kCFRunLoopDefaultMode);
        FSEventStreamStart(stream);
        watches_[watch_id].stream = stream;
    }
#endif

    return {};
}

auto LocalFileWatchManager::unwatch(const std::string& watch_id)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(watches_mutex_);
    auto it = watches_.find(watch_id);
    if (it == watches_.end()) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

#ifdef LINUX
    for (int wd : it->second.wds) {
        if (wd >= 0 && inotify_fd_ >= 0) {
            ::inotify_rm_watch(inotify_fd_, wd);
            wd_map_.erase(wd);
        }
    }
#endif
#ifdef MACOS
    if (it->second.stream) {
        FSEventStreamStop(it->second.stream);
        FSEventStreamInvalidate(it->second.stream);
        FSEventStreamRelease(it->second.stream);
    }
#endif

    watches_.erase(it);
    return {};
}

// --- Listener management ---

auto LocalFileWatchManager::add_listener(std::shared_ptr<IFileWatchListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    listeners_.push_back(std::move(listener));
}

auto LocalFileWatchManager::remove_listener(std::shared_ptr<IFileWatchListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
    }
}

// --- Notification ---

void LocalFileWatchManager::notify_file_changed(const std::string& watch_id,
                                                  const std::string& path,
                                                  models::FileChangeKind kind,
                                                  const std::optional<std::string>& old_path) {
    std::vector<std::shared_ptr<IFileWatchListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_file_changed(watch_id, path, kind, old_path);
    }
}

// --- Shutdown ---

auto LocalFileWatchManager::shutdown() -> void {
    if (!running_.exchange(false)) return;

#ifdef LINUX
    if (shutdown_pipe_[1] >= 0) {
        char c = 0;
        ssize_t ignored [[maybe_unused]] = ::write(shutdown_pipe_[1], &c, 1);
    }
#endif

#ifdef MACOS
    if (run_loop_) {
        CFRunLoopStop(run_loop_);
    }
#endif

    if (event_thread_.joinable()) {
        event_thread_.join();
    }

    {
        std::lock_guard lock(watches_mutex_);
        for (auto& [id, entry] : watches_) {
#ifdef LINUX
            for (int wd : entry.wds) {
                if (wd >= 0 && inotify_fd_ >= 0) {
                    ::inotify_rm_watch(inotify_fd_, wd);
                }
            }
#endif
#ifdef MACOS
            if (entry.stream) {
                FSEventStreamStop(entry.stream);
                FSEventStreamInvalidate(entry.stream);
                FSEventStreamRelease(entry.stream);
            }
#endif
        }
        watches_.clear();
#ifdef LINUX
        wd_map_.clear();
#endif
    }

#ifdef LINUX
    if (inotify_fd_ >= 0) ::close(inotify_fd_);
    if (shutdown_pipe_[0] >= 0) ::close(shutdown_pipe_[0]);
    if (shutdown_pipe_[1] >= 0) ::close(shutdown_pipe_[1]);
#endif
}

// --- Platform-specific event threads ---

#ifdef MACOS
void LocalFileWatchManager::event_thread_macos() {
    run_loop_ = CFRunLoopGetCurrent();
    // Keep the run loop going until shutdown
    while (running_) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
    }
}

void LocalFileWatchManager::fs_event_callback(
    ConstFSEventStreamRef /*stream*/,
    void* client_callback_info,
    size_t num_events,
    void* event_paths,
    const FSEventStreamEventFlags event_flags[],
    const FSEventStreamEventId /*event_ids*/[]) {
    auto* self = static_cast<LocalFileWatchManager*>(client_callback_info);
    if (!self || !self->running_) return;

    auto** paths = static_cast<CFStringRef*>(event_paths);

    for (size_t i = 0; i < num_events; ++i) {
        char path_buf[1024];
        CFStringGetCString(paths[i], path_buf, sizeof(path_buf), kCFStringEncodingUTF8);
        std::string path(path_buf);

        models::FileChangeKind kind = models::FileChangeKind::modified;
        auto flags = event_flags[i];
        if (flags & kFSEventStreamEventFlagItemCreated) {
            kind = models::FileChangeKind::created;
        } else if (flags & kFSEventStreamEventFlagItemRemoved) {
            kind = models::FileChangeKind::deleted;
        } else if (flags & kFSEventStreamEventFlagItemRenamed) {
            kind = models::FileChangeKind::renamed;
        }

        // Find which watch this path belongs to
        std::lock_guard lock(self->watches_mutex_);
        for (auto& [wid, entry] : self->watches_) {
            if (path.starts_with(entry.path)) {
                self->notify_file_changed(wid, path, kind);
                break;
            }
        }
    }
}
#endif

#ifdef LINUX
auto LocalFileWatchManager::add_recursive_watches(const std::string& watch_id,
                                                    const std::string& root_path) -> void {
    // Must be called under watches_mutex_
    namespace fs = std::filesystem;
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;

    // We need a temporary way to accumulate wds; they'll be stored in the WatchEntry later.
    // For now, add to wd_map_ directly. The caller will store in entry.wds.

    auto add_single = [&](const std::string& dir) {
        int wd = ::inotify_add_watch(inotify_fd_, dir.c_str(), mask);
        if (wd >= 0) {
            // wd_map_ is populated; entry.wds is populated by caller
            wd_map_[wd] = {watch_id, dir};
            // Find or create the watch entry and add the wd
            auto it = watches_.find(watch_id);
            if (it != watches_.end()) {
                it->second.wds.insert(wd);
            }
        }
    };

    add_single(root_path);

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
            root_path, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        if (it->is_directory()) {
            add_single(it->path().string());
        }
        it.increment(ec);
    }
}

void LocalFileWatchManager::event_thread_linux() {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event* event;

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(inotify_fd_, &read_fds);
        FD_SET(shutdown_pipe_[0], &read_fds);
        int max_fd = std::max(inotify_fd_, shutdown_pipe_[0]) + 1;

        struct timeval tv{};
        tv.tv_usec = 100000;  // 100ms

        int ret = ::select(max_fd, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EBADF) break;
            continue;
        }

        if (FD_ISSET(shutdown_pipe_[0], &read_fds)) break;

        if (ret > 0 && FD_ISSET(inotify_fd_, &read_fds)) {
            ssize_t len = ::read(inotify_fd_, buf, sizeof(buf));
            if (len <= 0) continue;

            for (char* ptr = buf; ptr < buf + len;
                 ptr += sizeof(struct inotify_event) + event->len) {
                event = reinterpret_cast<const struct inotify_event*>(ptr);

                if (event->len == 0) continue;

                std::string filename(event->name);

                std::lock_guard lock(watches_mutex_);

                // Look up watch info via wd_map_
                auto map_it = wd_map_.find(event->wd);
                if (map_it == wd_map_.end()) continue;

                const auto& [wid, dir_path] = map_it->second;
                auto full_path = dir_path + "/" + filename;

                models::FileChangeKind kind = models::FileChangeKind::modified;
                if (event->mask & IN_CREATE) kind = models::FileChangeKind::created;
                else if (event->mask & IN_DELETE) kind = models::FileChangeKind::deleted;
                else if (event->mask & IN_MOVED_FROM) kind = models::FileChangeKind::renamed;
                else if (event->mask & IN_MODIFY) kind = models::FileChangeKind::modified;

                // If a new directory was created in a recursive watch, add a watch for it
                if ((event->mask & (IN_CREATE | IN_MOVED_TO)) && (event->mask & IN_ISDIR)) {
                    auto watch_it = watches_.find(wid);
                    if (watch_it != watches_.end() && watch_it->second.recursive) {
                        // Add watch for the new subdirectory
                        uint32_t sub_mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
                        int new_wd = ::inotify_add_watch(inotify_fd_, full_path.c_str(), sub_mask);
                        if (new_wd >= 0) {
                            watch_it->second.wds.insert(new_wd);
                            wd_map_[new_wd] = {wid, full_path};
                        }
                    }
                }

                notify_file_changed(wid, full_path, kind);
            }
        }
    }
}
#endif

}  // namespace rosweb::filewatch
