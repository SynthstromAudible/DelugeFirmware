#pragma once

/// @file
/// @brief `ctx_of`: the menu's context over a spec's legacy song, bound as the app binds one.

#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/services/view_state.h"

#include "model/song/song.h"

#include <cstdint>

namespace deluge::ui::scenes::spec {

/// @brief `menu::context_for` over @p song's record, with the app's ports when the app plays @p song (it is
///        `currentSong`) and the app's device port alone otherwise -- the binding the app's own callers make.
/// @param song                     The spec's song, whose record the context names.
/// @param view                     What the menu opens over (`context_for`'s @p view).
/// @param source_index             `context_for`'s @p source_index.
/// @param performance_view         `context_for`'s @p performance_view.
/// @param session_or_arranger_root `context_for`'s @p session_or_arranger_root.
/// @param session_view_root        `context_for`'s @p session_view_root.
/// @param kit_global_fx            `context_for`'s @p kit_global_fx.
/// @return The context. It binds the app's ports when the app plays @p song; otherwise only the app's device port, as
///         the device's settings act on no song. The ports are valid for the program's life.
[[nodiscard]] inline menu::MenuContext ctx_of(const ::Song& song, const services::MenuView& view, int8_t source_index,
                                              bool performance_view, bool session_or_arranger_root,
                                              bool session_view_root, bool kit_global_fx) {
	// The device's settings act on no song, so a song the app does not play still binds the app's device port.
	static const bind::Ports kDeviceOnly{.device = deluge::gui::shell_host::host_ports().device};
	const bind::Ports* ports = &song == currentSong ? &deluge::gui::shell_host::host_ports() : &kDeviceOnly;
	return menu::context_for(song.project, ports, view, source_index, performance_view, session_or_arranger_root,
	                         session_view_root, kit_global_fx);
}

} // namespace deluge::ui::scenes::spec
