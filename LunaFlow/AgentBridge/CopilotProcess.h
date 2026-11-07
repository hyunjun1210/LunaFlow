#pragma once
#include "Core/Serialization.h"
#include <windows.h>
#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace LunaFlow {
// Folder holding agent_service/copilot_client; each agent resolves its own settings file below it.
std::filesystem::path CopilotProjectRoot();
// One owned SDK process, with pipe IO entirely outside the rendering thread.
class CopilotProcess {
public:
    ~CopilotProcess(){Cancel();}
    void Submit(const std::filesystem::path& root,const Json& request,const std::filesystem::path& settings={});
    void Cancel();
    std::vector<Json> Poll();
    bool Busy() const {return busy_;}
private:
    void Publish(Json);
    std::mutex mutex_;
    std::deque<Json> events_;
    std::thread worker_;
    HANDLE process_=nullptr;
    std::atomic<bool> busy_{false},cancelled_{false};
};
}
