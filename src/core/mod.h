#pragma once

#include "config.h"
#include <atomic>
#include <mutex>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/processing/tracking_processor.h>
#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>

namespace RE2HT {

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void CycleTrackingMode();
    void ToggleReticle();
    void ToggleYawMode();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);

    float GetLastDeltaTime() const { return m_lastDeltaTime; }

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw; }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();

    // Performs the recenter while m_pipelineMutex is already held. The public
    // Recenter() acquires the lock and delegates here; the render-thread
    // auto-recenter path (inside GetProcessedRotation) calls it directly
    // because it already holds the lock.
    void RecenterLocked();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    // Hotkeys fire on the HotkeyPoller's background thread, so Recenter() and
    // CycleTrackingMode() can mutate the tracking pipeline while the render
    // thread is inside GetProcessedRotation()/GetPositionOffset(). The pipeline
    // objects (processors, interpolators) and their timing/cache state are not
    // thread-safe, so all access is serialized through this lock.
    std::mutex m_pipelineMutex;

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::PoseInterpolator m_poseInterpolator;
    cameraunlock::TrackingProcessor m_processor;
    int64_t m_lastReceiveTimestamp = 0;

    cameraunlock::PositionProcessor m_positionProcessor;
    cameraunlock::PositionInterpolator m_positionInterpolator;
    // Read on the render thread, toggled on the hotkey thread.
    std::atomic<bool> m_positionEnabled{true};
    std::atomic<bool> m_rotationEnabled{true};
    std::atomic<bool> m_reticleEnabled{true};
    std::atomic<bool> m_worldSpaceYaw{false};

    uint64_t m_lastProcessTime = 0;
    float m_lastDeltaTime = DELTA_TIME_DEFAULT;

    float m_cachedYaw = 0.0f;
    float m_cachedPitch = 0.0f;
    float m_cachedRoll = 0.0f;
    bool m_cachedValid = false;
    bool m_hasCentered = false;
    int m_stabilizationFrames = 0;

    // Previous raw values for new-sample detection (data change, not just packet arrival)
    float m_lastRawYaw = 0.0f;
    float m_lastRawPitch = 0.0f;
    float m_lastRawRoll = 0.0f;
};

} // namespace RE2HT
