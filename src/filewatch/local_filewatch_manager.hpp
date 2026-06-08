#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "filewatch/i_filewatch_manager.hpp"

#ifdef MACOS
#include <CoreServices/CoreServices.h>
#endif

#ifdef LINUX
#include <sys/inotify.h>
#endif

namespace rosweb::filewatch {

class LocalFileWatchManager : public IFileWatchManager {
public:
    LocalFileWatchManager();
    ~LocalFileWatchManager() override;

    LocalFileWatchManager(const LocalFileWatchManager&) = delete;
    auto operator=(const LocalFileWatchManager&) -> LocalFileWatchManager& = delete;

    auto watch(const std::string& watch_id,
                const std::string& path,
                bool recursive)
        -> std::expected<void, errors::ErrorCode> override;

    auto unwatch(const std::string& watch_id)
        -> std::expected<void, errors::ErrorCode> override;

    auto add_listener(std::shared_ptr<IFileWatchListener> listener) -> void override;
    auto remove_listener(std::shared_ptr<IFileWatchListener> listener) -> void override;

    auto shutdown() -> void override;

private:
    struct WatchEntry {
        std::string watch_id;
        std::string path;
        bool recursive;
#ifdef MACOS
        FSEventStreamRef stream;
#endif
#ifdef LINUX
        std::unordered_set<int> wds;  // all watch descriptors (root + subdirs)
#endif
    };

    void notify_file_changed(const std::string& watch_id,
                              const std::string& path,
                              models::FileChangeKind kind,
                              const std::optional<std::string>& old_path = std::nullopt);

#ifdef MACOS
    void event_thread_macos();
    static void fs_event_callback(ConstFSEventStreamRef stream,
                                   void* client_callback_info,
                                   size_t num_events,
                                   void* event_paths,
                                   const FSEventStreamEventFlags event_flags[],
                                   const FSEventStreamEventId event_ids[]);
#endif

#ifdef LINUX
    void event_thread_linux();
    auto add_recursive_watches(const std::string& watch_id, const std::string& root_path) -> void;
#endif

    std::atomic<bool> running_{false};
    mutable std::mutex watches_mutex_;
    std::unordered_map<std::string, WatchEntry> watches_;  // watch_id -> entry

    mutable std::mutex listeners_mutex_;
    std::vector<std::shared_ptr<IFileWatchListener>> listeners_;

    std::thread event_thread_;

#ifdef LINUX
    int inotify_fd_ = -1;
    int shutdown_pipe_[2] = {-1, -1};
    // Reverse map: wd -> (watch_id, dir_path)
    std::unordered_map<int, std::pair<std::string, std::string>> wd_map_;
#endif

#ifdef MACOS
    CFRunLoopRef run_loop_ = nullptr;
#endif
};

}  // namespace rosweb::filewatch
