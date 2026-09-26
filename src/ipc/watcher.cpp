// IPC file watcher (inotify on Linux)
#include <jyotish/types.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>

namespace jyotish::ipc {

class FileWatcher {
public:
    explicit FileWatcher(const std::string& dir) : dir_(dir), running_(false) {}
    
    void start() {
        running_ = true;
        thread_ = std::thread(&FileWatcher::watch, this);
    }
    
    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }
    
    ~FileWatcher() { stop(); }

private:
    void watch() {
        // TODO: implement inotify/kqueue/ReadDirectoryChangesW
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::string dir_;
    std::atomic<bool> running_;
    std::thread thread_;
};

} // namespace jyotish::ipc