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
        if (existing->second.wd >= 0 && inotify_fd_ >= 0) {
            ::inotify_rm_watch(inotify_fd_, existing->second.wd);
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

#ifdef LINUX
    if (inotify_fd_ >= 0) {
        uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
        if (recursive) mask |= IN_ISDIR;
        entry.wd = ::inotify_add_watch(inotify_fd_, path.c_str(), mask);
        if (entry.wd < 0) {
            return std::unexpected(errors::ErrorCode::FS_PERMISSION_DENIED);
        }
    }
#endif

#ifdef MACOS
    // Create FSEventStream for this watch
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
        entry.stream = stream;
    }
#endif

    watches_.emplace(watch_id, std::move(entry));
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
    if (it->second.wd >= 0 && inotify_fd_ >= 0) {
        ::inotify_rm_watch(inotify_fd_, it->second.wd);
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
            if (entry.wd >= 0 && inotify_fd_ >= 0) {
                ::inotify_rm_watch(inotify_fd_, entry.wd);
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

                // Find which watch this wd belongs to
                std::lock_guard lock(watches_mutex_);
                for (auto& [wid, entry] : watches_) {
                    if (entry.wd == event->wd) {
                        auto full_path = entry.path + "/" + filename;

                        models::FileChangeKind kind = models::FileChangeKind::modified;
                        if (event->mask & IN_CREATE) kind = models::FileChangeKind::created;
                        else if (event->mask & IN_DELETE) kind = models::FileChangeKind::deleted;
                        else if (event->mask & IN_MOVED_FROM) kind = models::FileChangeKind::renamed;
                        else if (event->mask & IN_MODIFY) kind = models::FileChangeKind::modified;

                        notify_file_changed(wid, full_path, kind);
                        break;
                    }
                }
            }
        }
    }
}
#endif

}  // namespace rosweb::filewatch
