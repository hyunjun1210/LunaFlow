#pragma once
#include "Mocap.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace LunaFlow::Mocap {
class TrackerProcess {
public:
    ~TrackerProcess();
    void Start(const std::filesystem::path& python,const std::filesystem::path& script,
        const std::filesystem::path& model,int camera);
    void RequestStop() { stop_=true; }
    bool Busy() const { return busy_; }
    std::vector<Event> Poll();
private:
    void Publish(Event);
    std::atomic<bool> stop_{false},busy_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::deque<Event> events_;
};
// Runs Setup.ps1 hidden with its output captured, so first-time install happens inside the panel.
class SetupProcess {
public:
    ~SetupProcess();
    void Start(const std::filesystem::path& script);
    void Cancel() { cancel_=true; }
    bool Busy() const { return busy_; }
    bool Started() const { return started_; }
    // -1 while running or never started.
    int ExitCode() const { return exitCode_; }
    std::vector<std::string> Log();
private:
    void Append(std::string);
    std::atomic<bool> busy_{false},cancel_{false};
    std::atomic<int> exitCode_{-1};
    bool started_=false;
    std::thread worker_;
    std::mutex mutex_;
    std::deque<std::string> log_;
};
struct EditorState {
    TrackerProcess tracker;
    SetupProcess setup;
    Capture capture;
    bool show=false,active=false,stopping=false;
    int camera=0;
    char folder[1024]{};
    // Empty uses the localized default ("Webcam take N").
    char takeName[128]{};
    std::string processStatus;
    // One-button flow modeled on Unity's Animation record button.
    bool cameraReady=false,startAfterSetup=false,simplify=true,userStopped=false;
    int countdown=3,autoStop=0,takes=0;
    float simplifyDegrees=1.f;
    double countdownEnd=-1;
    std::optional<Frame> lastFrame;
    // Review keeps the camera alive: the take loops in the viewport until kept, retaken or discarded.
    int review=-1,reviewPreviousClip=0,endReason=0;
    float trimIn=0,trimOut=0;
    size_t rawKeys=0,keptKeys=0;
    bool reviewWasDirty=false,scrubbing=false;
    const void* reviewUndo=nullptr;
};
}
