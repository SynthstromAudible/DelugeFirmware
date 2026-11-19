#pragma once

/// @file
/// @brief Cases every implementation of a port must pass, the fake and the host's alike. Each takes the port under
///        test and returns whether it holds; a spec states the case and expects true.

#include "scenes/bind/ports/ports.h"

namespace deluge::scenes::bind::contract {

/// @brief A port answers the same thing twice when nothing changed between the calls.
[[nodiscard]] inline bool ui_facts_are_stable(const UiFacts& ui) {
	return ui.auditioning() == ui.auditioning() && ui.popup_showing() == ui.popup_showing()
	       && ui.flash_cursor() == ui.flash_cursor() && ui.performance_editing_mode() == ui.performance_editing_mode()
	       && ui.dx7_engine_enabled() == ui.dx7_engine_enabled();
}

/// @brief The voice count is never negative.
[[nodiscard]] inline bool voice_count_is_a_count(const AudioFacts& audio) {
	return audio.live_voice_count() >= 0;
}

/// @brief The flash cursor is one of its three values.
[[nodiscard]] inline bool flash_cursor_is_named(const UiFacts& ui) {
	const auto c = ui.flash_cursor();
	return c == FlashCursor::Fast || c == FlashCursor::Off || c == FlashCursor::Slow;
}

} // namespace deluge::scenes::bind::contract
