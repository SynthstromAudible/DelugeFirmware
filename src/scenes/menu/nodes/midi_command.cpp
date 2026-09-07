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

/// @brief The first of `drawPixelsForOled`'s three text rows, in @p f's frame-local coordinates.
///
/// Two corrections stacked, both recorded in `ui/core/metrics.h` rather than as literals here:
///
///   * `kMidiCommandRowY48` / `kMidiCommandRowY64` -- legacy's literal canvas row 20 minus the
///     panel's own `visible_top`, which the frame's origin already carries. It is a *pair* because
///     `visible_top` is 5 on the 48-row panel and 0 on a 64-row one, so a single constant is wrong
///     on one of them by five rows. Selected from `Style::menu_base_y`, the profile value
///     `ui::oled::output_name` already picks its own 48/64 pair with (`ui/oled/misc.cpp:57`) --
///     never from the framebuffer's height, so a mismatched (Style, Geometry) pair cannot disagree.
///   * `kApple7Height7YAdjust` -- legacy draws these rows at `kTextSizeYUpdated` (7), which selects
///     the Apple face's height-7 variant; `FontId::Apple7` is the height-9 one, which draws the
///     same glyphs one row lower. Laying the rect one row high cancels that.
///
/// Together the ink lands on legacy's own canvas rows 20 / 29 / 38, on both panels.
int32_t command_row0_y(const deluge::ui::oled::OledFrame& f) {
	const bool tall = f.style.menu_base_y == deluge::ui::kMenuBaseY64;
	return (tall ? deluge::ui::kMidiCommandRowY64 : deluge::ui::kMidiCommandRowY48) + deluge::ui::kApple7Height7YAdjust;
}

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
	const int32_t row0_y = command_row0_y(f);
	const int32_t row_h = f.style.row_height;
	const int32_t spacing = deluge::ui::oled::spacing_x_for(f.style.row_font);
	const auto row = [&](int32_t i, int32_t x) {
		return deluge::gfx::oled::Rect{x, row0_y + i * row_h, f.bounds().w - x, row_h};
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
		//
		// The two MPE-zone arms are live, not defensive: `PlaybackHandler::noteMessageReceived`
		// (`playback/playback_handler.cpp:2966-2968`) resolves the channel through
		// `MIDIPort::channelToZone` (`io/midi/midi_device.cpp:236-238`) *before* dispatching to
		// `noteOnReceivedForMidiLearn`, so a note-on learn from an MPE-configured input port really
		// does store `MIDI_CHANNEL_MPE_LOWER_ZONE`/`UPPER_ZONE` here. (The CC path is unaffected --
		// `midiCCReceived` is handed the raw channel.)
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
	// `MenuItem::selectButtonPress`'s `return nullptr` means to `SoundEditor` (`sound_editor.cpp:395-428`).
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
	// `SoundEditor::midiCCReceived` returns true for it (`sound_editor.cpp:1600-1608`) -- so this
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
	// (`sound_editor.cpp:1588-1594` returns true either way).
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
