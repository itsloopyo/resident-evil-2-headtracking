#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE2HT {

using Config = cameraunlock::reframework::PluginConfig;

// The game's name as cameraunlock-core's data/games.json spells it, written at
// the top of CameraUnlock.ini.
inline constexpr const char* kGameName = "Resident Evil 2";

// RE2's schema. positionSensitivity is the lean's scale at the camera boundary,
// not a setting: RE Engine's native head-bob range is narrow enough that 1:1
// reads as no movement at typical tracker range, so every build has shipped
// 2.0. CameraUnlock.ini has no row for it, and PluginMod applies it from
// SetDefaults. The [Position] Invert keys are still named here because the
// legacy import reads them out of an old HeadTracking.ini to report a changed
// one as dropped.
//
// canonicalConfig: settings live in reframework\plugins\CameraUnlock.ini, and
// HeadTracking.ini, the file every earlier build read, is imported once while
// CameraUnlock.ini is absent and never written.
//
// trueFreeLook: RE2 has an aim state (IsAiming, game_state_detector.cpp), so it
// ships sights locked and true free look ([Position] TrueFreeLook,
// [Hotkeys] TrueFreeLookKey).
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE2 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ false,
    /*positionSensitivity*/ 2.0f,
    /*modId*/ "re2",
    /*canonicalConfig*/ true,
    /*trueFreeLook*/ true,
};

} // namespace RE2HT
