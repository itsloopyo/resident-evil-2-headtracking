#pragma once

namespace cameraunlock::reframework { class GameplayGate; }

namespace RE2HT {

// The gate the camera pipeline consults before writing the camera.
cameraunlock::reframework::GameplayGate* GameplayGateInstance();

// True while the player is in active gameplay (not paused, in a menu, loading,
// or in a cutscene).
bool IsInGameplay();

} // namespace RE2HT
