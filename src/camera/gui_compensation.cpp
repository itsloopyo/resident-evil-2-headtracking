#include "pch.h"
#include "gui_compensation.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/plugin_mod.h>

#include <reframework/API.hpp>
#include <cstring>

namespace RE2HT {

namespace ref = cameraunlock::reframework;

// RE2's GUI tree is flat compared with Requiem's: both the world-anchored
// markers and the crosshair/HUD arrive as top-level elements, and the same
// centre delta on their root View is right for all of them.
static bool IsCompensatedGuiElement(const char* goName) {
    return strcmp(goName, "GUI_FloatIcon") == 0
        || strcmp(goName, "GUI_Purpose") == 0
        || strcmp(goName, "GUI_Reticle") == 0
        || strcmp(goName, "GUI_RemainingBullet") == 0;
}

// Shift a GUI element's View to the head-tracked screen position of the clean
// aim point, so world-anchored markers and the HUD/crosshair stay locked to
// where the game is actually aiming while the head turns the view.
static void ApplyGuiViewCenterOffset(reframework::API::ManagedObject* mo) {
    const auto& projection = ref::GetFrameProjection();

    float fx = 0.f, fy = 0.f;
    if (!ref::GetMarkerFocalLengths(fx, fy)) return;

    float deltaX = -projection.aimTanRight * fx;
    float deltaY =  projection.aimTanUp * fy;

    if (!ref::ShiftElementView(mo, deltaX, deltaY)) return;

    static bool s_diagOnce = false;
    if (!s_diagOnce) {
        s_diagOnce = true;
        ref::LogInfo("GUI center offset applied: fov=%.1f tanR=%.4f tanU=%.4f dX=%.1f dY=%.1f",
            projection.fovDegrees, projection.aimTanRight, projection.aimTanUp, deltaX, deltaY);
    }
}

bool OnPreGuiDrawElement(void* element, void* context) {
    (void)context;
    if (!ref::PluginMod::Instance().IsEnabled()) return true;

    // In this REFramework SDK version, 'element' is the GUI ManagedObject directly.
    if (!element) return true;

    auto* mo = reinterpret_cast<reframework::API::ManagedObject*>(element);

    char goName[128] = {};
    if (!ref::ReadGuiElementName(mo, goName, sizeof(goName))) return true;
    ref::LogGuiElementNameOnce(goName);

    if (ref::GetFrameProjection().aimValid && IsCompensatedGuiElement(goName)) {
        if (!IsInGameplay()) return true;
        ApplyGuiViewCenterOffset(mo);
    }

    return true;
}

} // namespace RE2HT
