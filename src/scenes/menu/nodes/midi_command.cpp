/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief `MidiCommandItem`'s members. See `midi_command.h`.

#include "scenes/menu/nodes/midi_command.h"

#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/profile.h"

#include "notify/dirty_set.h"
#include "notify/notice_queue.h"

#include "shell/profile.h"
#include "ui/core/metrics.h"
#include "ui/oled/scroll_text.h"
#include "ui/oled/text.h"

#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"

#include <charconv>
#include <cstddef>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

/// @brief The slot `command` binds, as a live reference into the MIDI engine.
///
/// `util::to_underlying(commandNumber)` in every one of legacy's five members
/// (`gui/menu_item/midi/command.cpp:35,89,97,113-115`), once.
LearnedMIDI& slot_for(GlobalMIDICommand command) {
	return midiEngine.globalMIDICommands[static_cast<size_t>(command)];
}

/// @brief The node currently on screen, so this item can draw its own title.
///
/// A `CustomItem` is handed `MenuState`, not its own `Node` -- `settings.cpp`'s `focused_of` /
/// `draw_title_for` pair, and `edit_name.cpp`'s inline copy of the same three lines.
void draw_title_for(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

/// @brief `l10n::get(id)` through the profile's own table.
std::string_view text_of(String id, deluge::ui::shell::TextScratch& scratch) {
	return deluge_profile().text_or_digits(sid(id), scratch);
}

/// @brief Legacy's `intToString(v, buffer, 1)`, into a caller-owned buffer.
std::string_view format_int(int32_t v, char (&buf)[12]) {
	const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf), v);
	return std::string_view{buf, static_cast<size_t>(r.ptr - buf)};
}

/// @brief The first of `drawPixelsForOled`'s three text rows, in frame-local coordinates.
///
/// Legacy starts at a literal canvas `yPixel = 20` (`command.cpp:37`) and steps by `kTextSpacingY`.
/// The frame's origin already carries `OLED_MAIN_TOPMOST_PIXEL` (5, `board_config.h`) -- the same
/// "+ visible_top" subtraction `kReadoutValueY` documents in `ui/core/metrics.h` -- which would put
/// the rect at 20 - 5 = 15.
///
/// **Fourteen, not fifteen**, because of the Apple face's two legacy heights. Legacy draws these
/// rows with `kTextSizeYUpdated` = 7 (`command.cpp:38-87`), which `font_for_text_height` resolves to
/// `kApple7`; this toolkit's `FontId::Apple7` is the *height-9* variant, `kApple7Up`, which draws
/// the identical glyphs exactly one row lower (`gfx/oled/font.h`'s `y_adjust`, and the note on
/// `text_height_of` in `ui/core/metrics.h`). Laying the rect one row higher cancels that, so the ink
/// lands on legacy's own canvas rows 20 / 29 / 38 rather than one below them.
constexpr int32_t kCommandRow0Y = 14;

} // namespace

void MidiCommandItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	draw_title_for(f, state);

	// `Command::selectEncoderAction` (`command.cpp:88-91`): *any* select-encoder motion clears the
	// binding, in either direction -- the item has no value to walk, so legacy spends the encoder
	// on "forget this". `f.turn()` is this port's detent count, and reading it claims it
	// (`ui/core/frame_context.h`, rule 2), which is what keeps the shell from also acting on it.
	if (f.turn() != 0) {
		slot_for(command_).clear();
	}

	LearnedMIDI& command = slot_for(command_);
	const int32_t row_h = f.style.row_height;
	const int32_t spacing = deluge::ui::oled::spacing_x_for(f.style.row_font);
	const auto row = [&](int32_t i, int32_t x) {
		return deluge::gfx::oled::Rect{x, kCommandRow0Y + i * row_h, f.bounds().w - x, row_h};
	};
	const deluge::ui::oled::TextStyle ts{.font = f.style.row_font};
	deluge::ui::shell::TextScratch scratch;

	if (!command.containsSomething()) {
		// `command.cpp:38-41`.
		deluge::ui::oled::text(f, row(0, 0), text_of(String::STRING_FOR_COMMAND_UNASSIGNED, scratch), ts);
	}
	else {
		// Row 0: the device, side-scrolling. `command.cpp:43-51` -- the display name of the cable
		// the binding names, or "ANY MIDI DEVICE" for a binding with no cable, drawn and then
		// handed to `OLED::setupSideScroller` across the full panel width. `scroll_text` is this
		// layer's side scroller and does both in one call; the scroll state is the menu's own
		// per-depth `ScrollText` (`MenuState::scroll`), which nothing else on this screen uses.
		std::string_view device = text_of(String::STRING_FOR_ANY_MIDI_DEVICE, scratch);
		if (command.cable != nullptr) {
			device = std::string_view{command.cable->getDisplayName()};
		}
		deluge::ui::oled::scroll_text(f, row(0, 0), device, f.style.row_font,
		                              state.scroll[state.depth < kMenuDepth ? state.depth : 0]);

		// Row 1: the channel or MPE zone. `command.cpp:53-71`. The "great CC channel hack" is that
		// `channelOrZone` carries the message type in its high range (`IS_A_CC`, `IS_A_PC` --
		// `definitions_cxx.hpp:946-947`), so the printed channel is the stored value minus whichever
		// band it falls in, plus one for 1-based display.
		deluge::ui::shell::TextScratch channel_scratch;
		std::string_view channel_text;
		if (command.channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			channel_text = text_of(String::STRING_FOR_MPE_LOWER_ZONE, channel_scratch);
		}
		else if (command.channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			channel_text = text_of(String::STRING_FOR_MPE_UPPER_ZONE, channel_scratch);
		}
		else {
			channel_text = text_of(String::STRING_FOR_CHANNEL, channel_scratch);
			int32_t channel_mod = 0;
			if (command.channelOrZone >= IS_A_PC) {
				channel_mod = IS_A_PC;
			}
			else if (command.channelOrZone >= IS_A_CC) {
				channel_mod = IS_A_CC;
			}
			char buf[12];
			deluge::ui::oled::text(f, row(1, spacing * 8), format_int(command.channelOrZone + 1 - channel_mod, buf),
			                       ts);
		}
		deluge::ui::oled::text(f, row(1, 0), channel_text, ts);

		// Row 2: which kind of message, and its number. `command.cpp:73-88`. "Note"/"CC"/"PC" are
		// bare literals in legacy too -- they are not in the string table.
		std::string_view kind = "Note";
		if (command.channelOrZone >= IS_A_PC) {
			kind = "PC";
		}
		else if (command.channelOrZone >= IS_A_CC) {
			kind = "CC";
		}
		deluge::ui::oled::text(f, row(2, 0), kind, ts);
		char buf[12];
		deluge::ui::oled::text(f, row(2, spacing * 5), format_int(command.noteOrCC, buf), ts);
	}

	// A leaf's select press leaves the item -- `FirmwareVersionItem::render`'s own arm, and what
	// `MenuItem::selectButtonPress`'s `return nullptr` means to `SoundEditor` (`sound_editor.cpp:357-390`).
	// `Kind::Custom`'s dispatch draws nothing else, so this item claims the press itself.
	if (f.select_pressed()) {
		state.render_resp.entered = true;
	}
}

deluge::ui::shell::Result MidiCommandItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// `midi::Command` has no `buttonAction`/`padAction` override, and learn does not arrive as a
	// `UiEvent`: it comes through `MenuScene::learn_note_on` and friends, which the wiring plan will
	// call from the MIDI engine's own delivery path. Nothing to consume here.
	return {};
}

bool MidiCommandItem::learn_note_on(::MIDICable& cable, int32_t channel, int32_t note) const {
	// `Command::learnNoteOn` (`command.cpp:113-124`), minus its display arm: see `midi_command.h`'s
	// note on why only the `renderUIsForOled()` branch is reachable here.
	LearnedMIDI& command = slot_for(command_);
	command.cable = &cable;
	command.channelOrZone = static_cast<uint8_t>(channel);
	command.noteOrCC = static_cast<uint8_t>(note);
	deluge::notify::dirty().raise(deluge::notify::Dirty::Oled);
	return true;
}

bool MidiCommandItem::learn_cc(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value) const {
	// `Command::learnCC` (`command.cpp:126-130`): a zero-valued CC is the release half of a
	// momentary controller and binds nothing. The message is still *used* -- legacy's
	// `SoundEditor::midiCCReceived` returns true for it (`sound_editor.cpp:1562-1570`) -- so this
	// says so rather than letting the caller route the same CC on somewhere else.
	if (value != 0) {
		return learn_note_on(cable, channel + IS_A_CC, cc);
	}
	return true;
}

bool MidiCommandItem::learn_program_change(::MIDICable& cable, int32_t channel, int32_t program) const {
	// `Command::learnProgramChange` (`command.cpp:105-111`). FILL is the one command a program
	// change cannot drive -- it is a momentary hold, and a PC has no off half -- so legacy refuses
	// it with a popup, which `SoundEditor::pcReceivedForMidiLearn` still reports as a message used
	// (`sound_editor.cpp:1550-1556` returns true either way).
	if (command_ == GlobalMIDICommand::FILL) {
		deluge::notify::notice_string(sid(String::STRING_FOR_CANT_LEARN_PC));
		return true;
	}
	return learn_note_on(cable, channel + IS_A_PC, program);
}

void MidiCommandItem::unlearn() const {
	slot_for(command_).clear(); // `Command::unlearnAction` (`command.cpp:96-104`)
	deluge::notify::dirty().raise(deluge::notify::Dirty::Oled);
}

} // namespace deluge::scenes::menu::nodes
