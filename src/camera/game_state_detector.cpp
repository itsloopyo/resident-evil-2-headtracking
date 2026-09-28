#include "pch.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>

#include <reframework/API.hpp>

namespace RE2HT {

namespace ref = cameraunlock::reframework;

// RE2 (app.ropeway) game-state signals, confirmed at runtime:
//   PlayerManager.get_CurrentPlayer()          null  => menu / loading
//   PlayerManager.get_CurrentPlayerCondition() then .get_IsEvent() => cutscene
//   GUIMaster.get_IsOpenPause()                true  => pause / inventory
//
// Named outright rather than probed. The generic manager probing the RE7/RE8/
// Requiem detectors use binds whichever candidate resolves first, which is the
// right trade only where nothing has been confirmed; here these three have,
// and swapping them for a probe would trade a verified binding for a guess.
static constexpr const char* kPlayerManager = "app.ropeway.PlayerManager";
static constexpr const char* kPlayerCondition = "app.ropeway.survivor.player.PlayerCondition";
static constexpr const char* kGuiMaster = "app.ropeway.gui.GUIMaster";
// The aim camera's own flag, read for the lean while aiming, not for the gate.
// RE3's offline.camera.CameraSystem carries get_IsHoldWeaponCamera (its TDB
// dump); RE3 is RE2's code under another root namespace, so the name is taken
// from there and checked at runtime by the "Aim state" log line.
static constexpr const char* kCameraSystem = "app.ropeway.camera.CameraSystem";

static struct {
    reframework::API::Method* getCurrentPlayer = nullptr;
    reframework::API::Method* getCurrentPlayerCondition = nullptr;
    reframework::API::Method* getIsEvent = nullptr;
    reframework::API::Method* getIsOpenPause = nullptr;
    bool available = false;
    reframework::API::Method* getIsHoldWeaponCamera = nullptr;
} g_checks;

static void Discover() {
    auto tdb = reframework::API::get()->tdb();

    auto pmType = tdb->find_type(kPlayerManager);
    if (pmType) {
        g_checks.getCurrentPlayer = pmType->find_method("get_CurrentPlayer");
        g_checks.getCurrentPlayerCondition = pmType->find_method("get_CurrentPlayerCondition");
    }
    auto condType = tdb->find_type(kPlayerCondition);
    if (condType) g_checks.getIsEvent = condType->find_method("get_IsEvent");
    auto guiType = tdb->find_type(kGuiMaster);
    if (guiType) g_checks.getIsOpenPause = guiType->find_method("get_IsOpenPause");

    g_checks.available =
        g_checks.getCurrentPlayer && g_checks.getCurrentPlayerCondition &&
        g_checks.getIsEvent && g_checks.getIsOpenPause;

    ref::LogInfo("Game state detection %s: player=%p, condition=%p, isEvent=%p, pause=%p",
        g_checks.available ? "ready" : "unavailable",
        g_checks.getCurrentPlayer, g_checks.getCurrentPlayerCondition,
        g_checks.getIsEvent, g_checks.getIsOpenPause);

    auto cameraSystemType = tdb->find_type(kCameraSystem);
    if (cameraSystemType) g_checks.getIsHoldWeaponCamera = cameraSystemType->find_method("get_IsHoldWeaponCamera");
    ref::LogInfo("Aim state %s: %s.get_IsHoldWeaponCamera=%p",
                 g_checks.getIsHoldWeaponCamera ? "ready" : "unavailable, the lean is never eased while aiming",
                 kCameraSystem, g_checks.getIsHoldWeaponCamera);
}

// The three managed calls, guarded together. A probe that faults reports no
// suppression rather than a state: the game is running, this detector is not,
// and dropping tracking on the detector's own failure would be the worse of the
// two errors.
static const char* SuppressReason(const reframework::API* api) {
    __try {
        auto pmgr = api->get_managed_singleton(kPlayerManager);
        if (!pmgr) return "no PlayerManager";

        if (!ref::CallMethod(g_checks.getCurrentPlayer, pmgr)) {
            return "no player (menu/loading)";
        }

        auto condition = ref::CallMethod(g_checks.getCurrentPlayerCondition, pmgr);
        if (condition && ref::CallMethodBool(g_checks.getIsEvent, condition)) {
            return "cutscene";
        }

        auto gui = api->get_managed_singleton(kGuiMaster);
        if (gui && ref::CallMethodBool(g_checks.getIsOpenPause, gui)) {
            return "paused";
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

static bool Check(void* primaryCamera, bool diag, const char** reason) {
    (void)primaryCamera;
    (void)diag;
    if (!g_checks.available) return true;

    const char* suppress = SuppressReason(reframework::API::get().get());
    if (!suppress) return true;
    *reason = suppress;
    return false;
}

static ref::GameplayGate g_gate{&Discover, &Check};

ref::GameplayGate* GameplayGateInstance() { return &g_gate; }

bool IsInGameplay() { return g_gate.IsInGameplay(); }

static bool g_aimFaultLogged = false;

// Called only past the gate, whose first refresh ran Discover.
bool IsAiming() {
    if (!g_checks.getIsHoldWeaponCamera) return false;
    __try {
        auto system = reframework::API::get()->get_managed_singleton(kCameraSystem);
        if (!system) return false;
        return ref::CallMethodBool(g_checks.getIsHoldWeaponCamera, system);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (!g_aimFaultLogged) {
            g_aimFaultLogged = true;
            ref::LogWarning("Aim state: %s.get_IsHoldWeaponCamera faulted; the sights read as down on every "
                            "frame it does (logged once)",
                            kCameraSystem);
        }
    }
    return false;
}

} // namespace RE2HT
