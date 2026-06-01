#include "pch.h"
#include "game_state_detector.h"
#include "ref_utils.h"
#include "core/logger.h"

#include <reframework/API.hpp>

namespace RE2HT {

// RE2 (app.ropeway) game-state signals, confirmed at runtime:
//   PlayerManager.get_CurrentPlayer()          null  => menu / loading
//   PlayerManager.get_CurrentPlayerCondition() then .get_IsEvent() => cutscene
//   GUIMaster.get_IsOpenPause()                true  => pause / inventory
static constexpr const char* kPlayerManager = "app.ropeway.PlayerManager";
static constexpr const char* kPlayerCondition = "app.ropeway.survivor.player.PlayerCondition";
static constexpr const char* kGuiMaster = "app.ropeway.gui.GUIMaster";

static struct {
    bool inGameplay = false;
    uint64_t lastCheckTime = 0;
    static constexpr uint64_t CHECK_INTERVAL_MS = 100;

    bool typesInitialized = false;

    // Tier 1: camera existence
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;

    // Tier 2: gameplay-vs-not signals
    reframework::API::Method* getCurrentPlayer = nullptr;
    reframework::API::Method* getCurrentPlayerCondition = nullptr;
    reframework::API::Method* getIsEvent = nullptr;
    reframework::API::Method* getIsOpenPause = nullptr;
    bool stateMethodsAvailable = false;

    bool wasInGameplay = false;
    bool pendingRecenter = false;
} g_state;

void RefreshGameState() {
    uint64_t now = GetTickCount64();
    if (now - g_state.lastCheckTime < g_state.CHECK_INTERVAL_MS) return;
    g_state.lastCheckTime = now;

    const auto& api = reframework::API::get();
    if (!api) {
        g_state.inGameplay = false;
        return;
    }

    if (!g_state.typesInitialized) {
        g_state.typesInitialized = true;
        auto tdb = api->tdb();

        auto smType = tdb->find_type("via.SceneManager");
        if (smType) g_state.getMainView = smType->find_method("get_MainView");
        auto svType = tdb->find_type("via.SceneView");
        if (svType) g_state.getPrimaryCamera = svType->find_method("get_PrimaryCamera");

        auto pmType = tdb->find_type(kPlayerManager);
        if (pmType) {
            g_state.getCurrentPlayer = pmType->find_method("get_CurrentPlayer");
            g_state.getCurrentPlayerCondition = pmType->find_method("get_CurrentPlayerCondition");
        }
        auto condType = tdb->find_type(kPlayerCondition);
        if (condType) g_state.getIsEvent = condType->find_method("get_IsEvent");
        auto guiType = tdb->find_type(kGuiMaster);
        if (guiType) g_state.getIsOpenPause = guiType->find_method("get_IsOpenPause");

        g_state.stateMethodsAvailable =
            g_state.getCurrentPlayer && g_state.getCurrentPlayerCondition &&
            g_state.getIsEvent && g_state.getIsOpenPause;

        if (g_state.stateMethodsAvailable) {
            Logger::Instance().Info("Game state detection ready: player=%p, condition=%p, isEvent=%p, pause=%p",
                g_state.getCurrentPlayer, g_state.getCurrentPlayerCondition,
                g_state.getIsEvent, g_state.getIsOpenPause);
        } else {
            Logger::Instance().Info("Game state detection unavailable: player=%p, condition=%p, isEvent=%p, pause=%p",
                g_state.getCurrentPlayer, g_state.getCurrentPlayerCondition,
                g_state.getIsEvent, g_state.getIsOpenPause);
        }
    }

    bool newState = false;
    const char* suppressReason = nullptr;

    do {
        // Tier 1: camera must exist
        if (!g_state.getMainView || !g_state.getPrimaryCamera) break;

        auto sceneManager = api->get_native_singleton("via.SceneManager");
        if (!sceneManager) break;
        auto mainView = g_state.getMainView->call<void*>(api->get_vm_context(), sceneManager);
        if (!mainView) break;
        auto camera = g_state.getPrimaryCamera->call<void*>(api->get_vm_context(), mainView);
        if (!camera) break;

        // Tier 2: suppress outside active gameplay
        if (g_state.stateMethodsAvailable) {
            __try {
                auto pmgr = api->get_managed_singleton(kPlayerManager);
                if (!pmgr) { suppressReason = "no PlayerManager"; __leave; }

                if (!InvokePtr(g_state.getCurrentPlayer, pmgr)) {
                    suppressReason = "no player (menu/loading)";
                    __leave;
                }

                auto condition = InvokePtr(g_state.getCurrentPlayerCondition, pmgr);
                if (condition && InvokeBool(g_state.getIsEvent, condition)) {
                    suppressReason = "cutscene";
                    __leave;
                }

                auto gui = api->get_managed_singleton(kGuiMaster);
                if (gui && InvokeBool(g_state.getIsOpenPause, gui)) {
                    suppressReason = "paused";
                    __leave;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                suppressReason = nullptr;  // probe failed; do not suppress on error
            }

            if (suppressReason) break;
        }

        newState = true;
    } while (false);

    g_state.inGameplay = newState;

    if (g_state.inGameplay && !g_state.wasInGameplay) {
        g_state.pendingRecenter = true;
        Logger::Instance().Info("Game state: entered gameplay - pending recenter");
    } else if (!g_state.inGameplay && g_state.wasInGameplay) {
        Logger::Instance().Info("Game state: left gameplay (%s)",
            suppressReason ? suppressReason : "no camera");
    }
    g_state.wasInGameplay = g_state.inGameplay;
}

bool IsInGameplay() {
    RefreshGameState();
    return g_state.inGameplay;
}

bool ShouldRecenter() {
    if (g_state.pendingRecenter) {
        g_state.pendingRecenter = false;
        return true;
    }
    return false;
}

} // namespace RE2HT
