#pragma once

namespace RE2HT {

// on_pre_gui_draw_element callback for marker/crosshair compensation.
// Returns true to keep drawing the element, false to hide.
bool OnPreGuiDrawElement(void* element, void* context);

} // namespace RE2HT
