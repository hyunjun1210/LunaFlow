#pragma once
#include "Core/Scene.h"
#include <array>
#include <optional>

namespace LunaFlow::Mocap {
constexpr double LostAfter = .5;
constexpr double RecordFps = 30;
constexpr double MaxSeconds = 120;
struct Landmark {
    XMFLOAT3 world{};
    float visibility=0, presence=0, u=0, v=0;
};
struct Frame {
    uint64_t sequence=0;
    double time=0;
    bool detected=false;
    std::array<Landmark,33> landmarks{};
};
struct Event {
    std::optional<Frame> frame;
    std::string state, message;
    double received=0;
};
// Versioned NDJSON over inherited anonymous pipes; never a listening socket.
Event ParseEvent(const std::string& line);
double Clock();
// Drops dense 30 FPS keys the evaluator can rebuild within the tolerance; returns the key count left.
size_t SimplifyTake(AnimationClip&, float rotationDegrees=1.f, float positionTolerance=1e-4f);
// Keeps [in,out] source seconds, re-timed to start at 0, with exact interpolated boundary keys.
void TrimTake(AnimationClip&, double in, double out);
size_t KeyCount(const AnimationClip&);
// Plain-language framing problem for the live guide (Good when the full body is usable).
enum class Framing { NoPerson, StepBack, MoveLeft, MoveRight, LowConfidence, Good };
Framing CheckFraming(const Frame&);

class Capture {
public:
    void Configure(const SkeletonData&);
    void BeginCalibration();
    void Consume(const Frame&, double received);
    void Tick(double now);
    void StartRecording(const std::string& name, double now);
    void StopRecording();
    std::optional<AnimationClip> TakeCompleted();
    const Pose& Preview() const { return pose_; }
    const std::string& Status() const { return status_; }
    const std::string& Mapping() const { return mapping_; }
    bool Calibrated() const { return calibrated_; }
    bool Calibrating() const { return calibrating_; }
    bool Tracking() const { return tracking_; }
    bool Recording() const { return recording_.has_value(); }
    double RecordedSeconds() const;
    int CalibrationSamples() const { return samples_; }
    float smoothingSeconds=.09f;
private:
    struct Link { int bone, from, to; XMFLOAT3 bindDirection{}; };
    bool Usable(const Frame&) const;
    void SampleCalibration(const Frame&);
    Pose Solve(const Frame&) const;
    void WriteSample(double time, const Pose&);
    void Finish(double duration, const Pose&);
    SkeletonData skeleton_;
    Pose bind_, pose_, previousPose_;
    std::vector<Link> links_;
    int hips_=-1, chest_=-1;
    Frame reference_, anchor_;
    std::array<XMFLOAT3,33> sums_{};
    XMFLOAT4X4 referenceBasis_{};
    bool calibrated_=false, calibrating_=false, tracking_=false;
    int samples_=0;
    uint64_t sequence_=0;
    double frameTime_=-1, goodTime_=-1, goodReceived_=-1, calibrationStart_=0;
    double recordStart_=0;
    size_t nextSample_=1;
    std::optional<AnimationClip> recording_, completed_;
    std::string status_, mapping_;
};
}
