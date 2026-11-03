#pragma once

/// @file
/// @brief The menu's MIDI learn: learn mode, the LEARN button's arm and LED, and delivering a learned message to the
///        focused item -- `SoundEditor`'s learn members.

#include "deluge-project/mapping_table.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_state.h"
#include "shell/scene.h"
#include "ui/core/led_frame.h"

#include <cstdint>
#include <optional>

class MIDICable;

namespace deluge::scenes::menu {

/// @brief Learn mode and the learn delivery for the menu. Its state belongs to one visit of the scene.
class MenuLearn {
public:
	/// @brief The param a learned knob binds to on the focused node -- `MenuScene::learn_target`.
	std::optional<deluge::project::OwnerParamTarget> target(const MenuContext& ctx, const MenuState& st) const;
	/// @brief `getCurrentMenuItem()->allowsLearnMode()`: on a page, its selected child's (`Submenu::allowsLearnMode`,
	///        `submenu.cpp:233-238`), which a child page never allows.
	bool allows_mode(const MenuContext& ctx, const MenuState& st) const;
	/// @brief Whether the focused item wants the LEARN LED blinking.
	bool wants_blink(const MenuState& st) const;

	/// @brief Deliver a note-on to the focused item; true if it used it.
	bool note_on(const MenuState& st, ::MIDICable& cable, int32_t channel, int32_t note) const;
	/// @brief Deliver a CC to the focused item; true if it used it.
	bool cc(const MenuState& st, ::MIDICable& cable, int32_t channel, int32_t cc, int32_t value) const;
	/// @brief Deliver a program change to the focused item; true if it used it.
	bool program_change(const MenuState& st, ::MIDICable& cable, int32_t channel, int32_t program) const;
	/// @brief Ask the focused item to forget what it learned, and drop a param's learned knob.
	void unlearn(const MenuContext& ctx, const MenuState& st) const;

	/// @brief `SoundEditor::buttonAction`'s LEARN arm (`sound_editor.cpp:718-745`), observed only: the event goes on.
	/// @param ctx   The menu context.
	/// @param st    The menu state.
	/// @param event The event; anything but LEARN is ignored.
	/// @param ui    The input context: Shift and the held pads.
	/// @return True when the release restarts the shared normal blink (`blinkLed(LEARN)`, `:736-738`).
	bool observe(const MenuContext& ctx, const MenuState& st, const deluge::ui::shell::UiEvent& event,
	             const deluge::ui::shell::UiContext& ui);

	/// @brief Declare the LEARN LED: learn mode's fast blink, else @p shared_blink while the item wants it.
	void leds(const MenuState& st, deluge::ui::LedFrame& f, deluge::ui::LedState shared_blink);

	/// @brief Legacy's `UI_MODE_MIDI_LEARN`: entered on a LEARN press with Shift up, no other UI mode on, and an item
	///        that allows learn; left on LEARN's release (`sound_editor.cpp:716-745`). Select's release does nothing
	///        in it.
	bool mode() const { return mode_; }

private:
	bool mode_ = false;
	/// Whether the next `leds()` restarts the shared fast blink, as learn mode's `blinkLed(LEARN, 255, 1)` does.
	bool restart_ = false;
};

} // namespace deluge::scenes::menu
