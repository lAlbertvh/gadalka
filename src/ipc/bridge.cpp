// IPC Bridge - file watcher for OpenMW communication
#include <jyotish/types.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <iostream>

namespace jyotish::ipc {

void run_bridge(const std::string& ipc_dir) {
    // TODO: implement file watching with inotify/kqueue
    // Watch ipc_dir/request.json, process, write response.json
    std::cout << "IPC Bridge watching: " << ipc_dir << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace jyotish::ipc

int main(int argc, char* argv[]) {
    std::string ipc_dir = "/tmp/jyotish-ipc";
    if (argc > 1) ipc_dir = argv[1];
    
    std::filesystem::create_directories(ipc_dir);
    jyotish::ipc::run_bridge(ipc_dir);
    return 0;
}