#include "scenes/menu/note_bridge.h"

namespace deluge::scenes::menu {

namespace {

/// @brief The installed bridge, or null. A single pointer, set once at start-up by the
///        application; no heap, no ownership -- mirrors `bindings.cpp`'s `g_feedback_hook`.
const NoteBridge* g_note_bridge = nullptr;

} // namespace

void set_note_bridge(const NoteBridge* bridge) {
	g_note_bridge = bridge;
}

const NoteBridge* note_bridge() {
	return g_note_bridge;
}

} // namespace deluge::scenes::menu
