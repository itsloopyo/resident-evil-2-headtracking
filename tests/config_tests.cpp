// Config loading / validation tests.
//
// HeadTracking.ini is a user-editable boundary: values flow straight into the
// UDP bind port and the tracking-sensitivity pipeline. These tests pin the
// validation that keeps a bad edit (out-of-range port, absurd multiplier) from
// silently producing wrong-but-plausible behaviour - in particular the port
// range check that replaced an unchecked uint16_t truncation.

#include "core/config.h"
#include "core/logger.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool NearEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// Win32 GetPrivateProfile* (used by IniReader on Windows) resolves a relative
// path against the Windows directory, not the cwd. Production builds the path
// absolutely from the DLL location; the tests must do the same.
const std::string& TmpPath() {
    static const std::string path =
        std::filesystem::absolute("re2ht_config_test.ini").string();
    return path;
}

void WriteIni(const std::string& contents) {
    std::ofstream f(TmpPath(), std::ios::trunc);
    f << contents;
}

void RemoveTmp() {
    std::error_code ec;
    std::filesystem::remove(TmpPath(), ec);
}

}  // namespace

int RunConfigTests() {
    using RE2HT::Config;

    std::cout << "Config tests\n";

    // Out-of-range port that would wrap to a valid-but-wrong value under a raw
    // uint16_t cast (70000 & 0xFFFF == 4464) must fall back to the default.
    {
        WriteIni("[Network]\nUDPPort=70000\n");
        Config cfg;
        cfg.Load(TmpPath().c_str());
        Check(cfg.udpPort == RE2HT::DEFAULT_UDP_PORT, "out-of-range port falls back to default");
        RemoveTmp();
    }

    // Reserved low port rejected.
    {
        WriteIni("[Network]\nUDPPort=80\n");
        Config cfg;
        cfg.Load(TmpPath().c_str());
        Check(cfg.udpPort == RE2HT::DEFAULT_UDP_PORT, "reserved low port falls back to default");
        RemoveTmp();
    }

    // Valid port preserved.
    {
        WriteIni("[Network]\nUDPPort=5005\n");
        Config cfg;
        cfg.Load(TmpPath().c_str());
        Check(cfg.udpPort == 5005, "valid port preserved");
        RemoveTmp();
    }

    // Sensitivity multipliers are clamped to their documented ranges.
    {
        WriteIni("[Sensitivity]\nYawMultiplier=99\nPitchMultiplier=-5\nRollMultiplier=10\n");
        Config cfg;
        cfg.Load(TmpPath().c_str());
        Check(NearEqual(cfg.yawMultiplier, 5.0f), "yaw multiplier clamped to max 5.0");
        Check(NearEqual(cfg.pitchMultiplier, 0.1f), "pitch multiplier clamped to min 0.1");
        Check(NearEqual(cfg.rollMultiplier, 2.0f), "roll multiplier clamped to max 2.0");
        RemoveTmp();
    }

    // Position smoothing clamped to [0, 0.99].
    {
        WriteIni("[Position]\nSmoothing=5.0\n");
        Config cfg;
        cfg.Load(TmpPath().c_str());
        Check(NearEqual(cfg.positionSmoothing, 0.99f), "position smoothing clamped to 0.99");
        RemoveTmp();
    }

    // A missing file leaves defaults intact and reports failure.
    {
        RemoveTmp();
        Config cfg;
        bool ok = cfg.Load(TmpPath().c_str());
        Check(!ok, "missing file reports load failure");
        Check(cfg.udpPort == RE2HT::DEFAULT_UDP_PORT, "missing file keeps default port");
    }

    // Save then load round-trips a non-default value.
    {
        Config saved;
        saved.SetDefaults();
        saved.udpPort = 6006;
        saved.worldSpaceYaw = false;
        Check(saved.Save(TmpPath().c_str()), "save succeeds");

        Config loaded;
        Check(loaded.Load(TmpPath().c_str()), "reload succeeds");
        Check(loaded.udpPort == 6006, "round-trip preserves port");
        Check(loaded.worldSpaceYaw == false, "round-trip preserves worldSpaceYaw");
        RemoveTmp();
    }

    if (g_failures == 0) {
        std::cout << "Config tests: all passed\n";
    } else {
        std::cout << "Config tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
