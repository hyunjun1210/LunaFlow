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
// settings.public.json (sign-in-free demo agent) wins over settings.local.json (Microsoft sign-in).
std::filesystem::path AgentSettingsFile(const std::filesystem::path& folder);
// True when the agent connects without sign-in: "Authentication":"None" or a public TokenEndpoint.
bool AgentSignInFree(const std::filesystem::path& settings);
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
