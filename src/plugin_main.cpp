#include "pch.h"

#include <reframework/API.hpp>

#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"
#include "core/config.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/plugin_bootstrap.h>

namespace ref = cameraunlock::reframework;

namespace {

// RE2's game code lives under app.ropeway.* (the game's internal codename).
// Fast-path candidates; the hooker's parent-chain walk discovers the real
// controller dynamically and logs the component tree if none of these match.
const char* const kControllerTypeCandidates[] = {
    "app.ropeway.camera.CameraSystem",
    "app.ropeway.PlayerCameraController",
};

// Assumed range to the point being aimed at, in metres.
//
// Fixed, not measured. The reticle marks where the clean aim lands in the
// head-turned view, and that projection needs a range because the drawn frame
// comes from the leaned eye: the correction is the rotation term plus a
// parallax term of lean/range. Held constant, the parallax is exact at this
// range and drifts with the lean either side of it, crossing zero here.
//
// Requiem measures the range instead, with a physics cast whose collision-layer
// allow-list was derived from captures of that title. Doing the same here needs
// the same captures - the layers that stop a bullet are per-game, and a wrong
// one collapses the range onto a trigger volume the player is standing in,
// which oversizes the correction rather than removing it. Until those captures
// exist for this game, a constant that is right at conversational-to-room range
// beats a measurement that can be wrong by an order of magnitude.
constexpr float kAimDistanceMeters = 50.0f;

const ref::PluginBootstrapDescriptor kPlugin = [] {
    ref::PluginBootstrapDescriptor d;
    d.logTag = "RE2HT";
    d.mod.displayName = RE2HT::RE2HT_PLUGIN_NAME;
    d.mod.version = RE2HT::RE2HT_VERSION;
    d.mod.config = RE2HT::kConfigSchema;
    d.camera.controllerCandidateTypes = kControllerTypeCandidates;
    d.camera.controllerCandidateCount =
        static_cast<int>(std::size(kControllerTypeCandidates));
    d.camera.aimDistanceMeters = kAimDistanceMeters;
    d.camera.gate = RE2HT::GameplayGateInstance();
    d.camera.onInit = []() { ref::InitGuiMethods(); };
    d.preGuiDrawElement = &RE2HT::OnPreGuiDrawElement;
    return d;
}();

} // namespace

// --- REFramework plugin exports ---

extern "C" __declspec(dllexport)
void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

extern "C" __declspec(dllexport)
bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    if (!param) return false;
    return ref::InitializePlugin(param, kPlugin);
}
