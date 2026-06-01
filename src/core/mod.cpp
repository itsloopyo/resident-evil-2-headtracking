#include "pch.h"
#include "mod.h"
#include "logger.h"

namespace RE2HT {

static uint64_t GetTimeMicros() {
    static const double freqReciprocal = []() {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return 1000000.0 / static_cast<double>(freq.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart * freqReciprocal);
}

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) {
        Logger::Instance().Warning("Mod already initialized");
        return true;
    }

    Logger::Instance().Info("RE2 Head Tracking v%s initializing...", RE2HT_VERSION);

    if (!LoadConfig()) {
        Logger::Instance().Warning("Using default configuration");
    }

    // Initialize TrackingProcessor
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_processor.SetSensitivity(sensitivity);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    // Initialize position processor
    m_positionEnabled = m_config.positionEnabled;
    m_reticleEnabled = m_config.reticleEnabled;
    m_worldSpaceYaw = m_config.worldSpaceYaw;

    cameraunlock::PositionSettings posSettings(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ, m_config.positionLimitZBack,
        m_config.positionSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ
    );
    m_positionProcessor.SetSettings(posSettings);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            m_positionEnabled ? "6DOF" : "3DOF",
                            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z);

    // Wire the receiver's bind-failure / retry messages to our logger before
    // Start. The callback runs on the background retry thread; Logger forwards
    // to REFramework's thread-safe log functions.
    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("%s", msg.c_str());
    });

    // Start returns false when the port is held by another process (e.g. another
    // head-tracker). The receiver keeps a background retry thread alive and
    // rebinds once the port frees up, so init must not abort: the camera hooks
    // and Update loop have to stay running for tracking to resume on its own.
    if (m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);
    }

    if (m_config.autoEnable) {
        m_enabled.store(true);
        Logger::Instance().Info("Head tracking auto-enabled");
    }

    m_initialized.store(true);
    Logger::Instance().Info("Initialization complete");
    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) return;

    Logger::Instance().Info("Shutting down...");
    m_udpReceiver.Stop();
    m_initialized.store(false);
    Logger::Instance().Info("Shutdown complete");
}

bool Mod::LoadConfig() {
    // Config file is in the same directory as the DLL (reframework/plugins/)
    HMODULE hModule = nullptr;
    char dllPath[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&Mod::Instance, &hModule)) {
        GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    }

    std::string configPath;
    if (dllPath[0] != '\0') {
        configPath = dllPath;
        auto lastSlash = configPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            configPath = configPath.substr(0, lastSlash + 1);
        }
    }
    configPath += "HeadTracking.ini";

    if (!m_config.Load(configPath.c_str())) {
        m_config.SetDefaults();
        m_config.Save(configPath.c_str());
        return false;
    }
    return true;
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        Logger::Instance().Info("Head tracking %s", enabled ? "enabled" : "disabled");
    }
}

void Mod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void Mod::Recenter() {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);
    RecenterLocked();
}

void Mod::RecenterLocked() {
    m_udpReceiver.Recenter();
    m_processor.Reset();
    m_poseInterpolator.Reset();
    m_lastProcessTime = 0;

    float px, py, pz;
    if (m_udpReceiver.GetPosition(px, py, pz)) {
        cameraunlock::PositionData posCenter(px, py, pz);
        m_positionProcessor.SetCenter(posCenter);
    }
    m_positionInterpolator.Reset();

    Logger::Instance().Info("View recentered");
}

void Mod::ToggleReticle() {
    m_reticleEnabled = !m_reticleEnabled;
    Logger::Instance().Info("Reticle %s", m_reticleEnabled ? "enabled" : "disabled");
}

void Mod::CycleTrackingMode() {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);
    // Three-state cycle: normal -> rotation-only -> position-only -> normal.
    if (m_rotationEnabled && m_positionEnabled) {
        m_positionEnabled = false;
    } else if (m_rotationEnabled && !m_positionEnabled) {
        m_rotationEnabled = false;
        m_positionEnabled = true;
    } else {
        m_rotationEnabled = true;
        m_positionEnabled = true;
    }

    if (!m_positionEnabled) {
        m_positionProcessor.Reset();
        m_positionInterpolator.Reset();
    }

    const char* mode = (m_rotationEnabled && m_positionEnabled) ? "normal (rotation + position)"
                     : (m_rotationEnabled)                       ? "rotation only"
                                                                 : "position only";
    Logger::Instance().Info("Tracking mode: %s", mode);
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);

    if (!m_rotationEnabled) {
        yaw = pitch = roll = 0.0f;
        return true;
    }

    uint64_t now = GetTimeMicros();
    if (m_lastProcessTime > 0 && (now - m_lastProcessTime) < CACHE_VALIDITY_US) {
        yaw = m_cachedYaw;
        pitch = m_cachedPitch;
        roll = m_cachedRoll;
        return m_cachedValid;
    }

    float rawYaw, rawPitch, rawRoll;
    if (!m_udpReceiver.GetRotation(rawYaw, rawPitch, rawRoll)) {
        m_lastProcessTime = now;
        m_cachedValid = false;
        return false;
    }

    // Wait for stabilization before auto-recentering (skip noisy initial frames)
    if (!m_hasCentered) {
        m_stabilizationFrames++;
        if (m_stabilizationFrames >= STABILIZATION_FRAME_COUNT) {
            m_hasCentered = true;
            RecenterLocked();
            Logger::Instance().Info("Auto-recentered after %d frames", m_stabilizationFrames);
        }
        // Still process data below so smoothing settles
    }

    float deltaTime = DELTA_TIME_DEFAULT;
    if (m_lastProcessTime > 0) {
        deltaTime = (now - m_lastProcessTime) / 1000000.0f;
        if (deltaTime > DELTA_TIME_MAX) deltaTime = DELTA_TIME_MAX;
        if (deltaTime < DELTA_TIME_MIN) deltaTime = DELTA_TIME_MIN;
    }
    m_lastProcessTime = now;
    m_lastDeltaTime = deltaTime;

    int64_t receiveTs = m_udpReceiver.GetLastReceiveTimestamp();
    bool isNewPacket = (receiveTs != m_lastReceiveTimestamp);
    m_lastReceiveTimestamp = receiveTs;

    bool isNewSample = isNewPacket &&
        (rawYaw != m_lastRawYaw || rawPitch != m_lastRawPitch || rawRoll != m_lastRawRoll);
    if (isNewPacket) {
        m_lastRawYaw = rawYaw;
        m_lastRawPitch = rawPitch;
        m_lastRawRoll = rawRoll;
    }

    cameraunlock::InterpolatedPose interpolated = m_poseInterpolator.Update(
        rawYaw, rawPitch, rawRoll, isNewSample, deltaTime);

    cameraunlock::TrackingPose processed = m_processor.Process(
        interpolated.yaw, interpolated.pitch, interpolated.roll, deltaTime);

    yaw = processed.yaw;
    pitch = processed.pitch;
    roll = processed.roll;

    m_cachedYaw = yaw;
    m_cachedPitch = pitch;
    m_cachedRoll = roll;
    m_cachedValid = true;

    return true;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);

    if (!m_positionEnabled) {
        x = y = z = 0.0f;
        return false;
    }

    float rawX, rawY, rawZ;
    if (!m_udpReceiver.GetPosition(rawX, rawY, rawZ)) {
        x = y = z = 0.0f;
        return false;
    }

    float deltaTime = m_lastDeltaTime;
    cameraunlock::PositionData rawPos(rawX, rawY, rawZ);
    cameraunlock::PositionData interpolatedPos = m_positionInterpolator.Update(rawPos, deltaTime);

    cameraunlock::math::Quat4 headRotQ = cameraunlock::math::Quat4::FromYawPitchRoll(
        m_cachedYaw * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedPitch * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedRoll * static_cast<float>(cameraunlock::math::kDegToRad));

    cameraunlock::math::Vec3 offset = m_positionProcessor.Process(interpolatedPos, headRotQ, deltaTime);

    x = offset.x;
    y = offset.y;
    z = offset.z;
    return true;
}

void Mod::ToggleYawMode() {
    m_worldSpaceYaw = !m_worldSpaceYaw;
    Logger::Instance().Info("Yaw mode: %s", m_worldSpaceYaw ? "world-space (horizon-locked)" : "camera-local");
}

} // namespace RE2HT
