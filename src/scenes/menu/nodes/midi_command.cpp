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
/// @brief `MidiCommandItem`'s members. See `nodes/midi_command.h`.

#include "scenes/menu/nodes/midi_command.h"

#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/profile.h"

#include "scenes/bind/ports/learn_sink.h"
#include "scenes/bind/ports/midi_devices.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"

#include "shell/profile.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/oled/scroll_text.h"
#include "ui/widgets/oled/text.h"

#include "project/device_settings.h"
#include "project/midi_channels.h"
#include "project/midi_command.h"

#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;
using deluge::project::kFirstCcChannel;
using deluge::project::kFirstProgramChangeChannel;

/// @brief The node currently on screen, so this item can draw its own title.
///
/// A `CustomItem` is handed `MenuState`, not its own `Node` -- `nodes/settings.cpp`'s `focused_of` /
/// `draw_title_for` pair, and `nodes/edit_name.cpp`'s inline copy of the same three lines.
void draw_title_for(deluge::ui::widgets::oled::OledFrame& f, const MenuState& state) {
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::widgets::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
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
/// Two corrections stacked, both recorded in `ui/widgets/core/metrics.h` rather than as literals here:
///
///   * `kMidiCommandRowY48` / `kMidiCommandRowY64` -- legacy's literal canvas row 20 minus the
///     panel's own `visible_top`, which the frame's origin already carries. It is a *pair* because
///     `visible_top` is 5 on the 48-row panel and 0 on a 64-row one, so a single constant is wrong
///     on one of them by five rows. Selected from `Style::menu_base_y`, the profile value
///     `ui::widgets::oled::output_name` already picks its own 48/64 pair with (`output_name` in
///     `ui/widgets/oled/misc.cpp`) -- never from the framebuffer's height, so a mismatched (Style, Geometry) pair
///     cannot disagree.
///   * `kApple7Height7YAdjust` -- legacy draws these rows at `kTextSizeYUpdated` (7), which selects
///     the Apple face's height-7 variant; `FontId::Apple7` is the height-9 one, which draws the
///     same glyphs one row lower. Laying the rect one row high cancels that.
///
/// Together the ink lands on legacy's own canvas rows 20 / 29 / 38, on both panels.
int32_t command_row0_y(const deluge::ui::widgets::oled::OledFrame& f) {
	const bool tall = f.style.menu_base_y == deluge::ui::widgets::core::kMenuBaseY64;
	return (tall ? deluge::ui::widgets::core::kMidiCommandRowY64 : deluge::ui::widgets::core::kMidiCommandRowY48)
	       + deluge::ui::widgets::core::kApple7Height7YAdjust;
}

} // namespace

void MidiCommandItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	// A context with no MIDI devices port has no binding to show: the command reads as unassigned.
	std::optional<bind::LearnedBinding> command =
	    ctx.ports.midi != nullptr ? ctx.ports.midi->global_command(command_) : std::nullopt;
	if (command && !command->channel_or_zone) {
		command.reset(); // a command is learned only with its channel, as `LearnedMIDI::containsSomething` has it
	}
	const int32_t row0_y = command_row0_y(f);
	const int32_t row_h = f.style.row_height;
	const int32_t spacing = deluge::ui::widgets::oled::spacing_x_for(f.style.row_font);
	const auto row = [&](int32_t i, int32_t x) {
		return deluge::ui::gfx::oled::Rect{x, row0_y + i * row_h, f.bounds().w - x, row_h};
	};
	const deluge::ui::widgets::oled::TextStyle ts{.font = f.style.row_font};
	deluge::ui::shell::TextScratch scratch;

	if (!command) {
		// `Command::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`).
		deluge::ui::widgets::oled::text(f, row(0, 0), text_of(String::STRING_FOR_COMMAND_UNASSIGNED, scratch), ts);
	}
	else {
		// Row 0: the device, side-scrolling. `Command::drawPixelsForOled`
		// (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`) -- the display name of the device the binding
		// names, or "ANY MIDI DEVICE" for a binding with none, drawn and then handed to `OLED::setupSideScroller`
		// across the full panel width. `scroll_text` is this layer's side scroller and does both in one call; the
		// scroll state is the menu's own per-depth `ScrollText` (`MenuState::scroll`), which nothing else on this
		// screen uses.
		std::string_view device = text_of(String::STRING_FOR_ANY_MIDI_DEVICE, scratch);
		if (command->device) {
			device = ctx.ports.midi->name(*command->device);
		}
		deluge::ui::widgets::oled::scroll_text(f, row(0, 0), device, f.style.row_font,
		                                       state.scroll[state.depth < kMenuDepth ? state.depth : 0]);

		// Row 1: the channel or MPE zone. `Command::drawPixelsForOled`
		// (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`). The "great CC channel hack" is that
		// `channel_or_zone` carries the message type in its high range (`kFirstCcChannel`, `kFirstProgramChangeChannel`
		// -- `project/midi_command.h`), so the printed channel is the stored value minus whichever band it falls
		// in, plus one for 1-based display.
		//
		// The two MPE-zone arms are live, not defensive: `PlaybackHandler::noteMessageReceived`
		// (`playback/playback_handler.cpp`) resolves the channel through
		// `MIDIPort::channelToZone` (`io/midi/midi_device.cpp`) *before* dispatching to
		// `noteOnReceivedForMidiLearn`, so a note-on learn from an MPE-configured input port really
		// does store `MIDI_CHANNEL_MPE_LOWER_ZONE`/`UPPER_ZONE` here. A CC's channel is resolved the same way.
		deluge::ui::shell::TextScratch channel_scratch;
		std::string_view channel_text;
		const int32_t channel = *command->channel_or_zone;
		if (channel == project::kMidiChannelLowerZone) {
			channel_text = text_of(String::STRING_FOR_MPE_LOWER_ZONE, channel_scratch);
		}
		else if (channel == project::kMidiChannelUpperZone) {
			channel_text = text_of(String::STRING_FOR_MPE_UPPER_ZONE, channel_scratch);
		}
		else {
			channel_text = text_of(String::STRING_FOR_CHANNEL, channel_scratch);
			int32_t channel_mod = 0;
			if (channel >= kFirstProgramChangeChannel) {
				channel_mod = kFirstProgramChangeChannel;
			}
			else if (channel >= kFirstCcChannel) {
				channel_mod = kFirstCcChannel;
			}
			char buf[12];
			deluge::ui::widgets::oled::text(f, row(1, spacing * 8), format_int(channel + 1 - channel_mod, buf), ts);
		}
		deluge::ui::widgets::oled::text(f, row(1, 0), channel_text, ts);

		// Row 2: which kind of message, and its number. `Command::drawPixelsForOled`
		// (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`). "Note"/"CC"/"PC" are bare literals in legacy too
		// -- they are not in the string table.
		std::string_view kind = "Note";
		if (channel >= kFirstProgramChangeChannel) {
			kind = "PC";
		}
		else if (channel >= kFirstCcChannel) {
			kind = "CC";
		}
		deluge::ui::widgets::oled::text(f, row(2, 0), kind, ts);
		char buf[12];
		deluge::ui::widgets::oled::text(f, row(2, spacing * 5),
		                                format_int(command->note_or_cc.value_or(project::MidiCommand::kNone), buf), ts);
	}

	// A leaf's select press leaves the item -- `FirmwareVersionItem::render`'s own arm, and what
	// `MenuItem::selectButtonPress`'s `return nullptr` means to `SoundEditor`
	// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`). `Kind::Custom`'s dispatch draws nothing else, so this item
	// claims the press itself.
	if (f.select_pressed()) {
		state.tick.render_resp.entered = true;
	}
}

deluge::ui::shell::Result MidiCommandItem::handle(const deluge::ui::shell::UiEvent& event,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& ctx, MenuState& /*state*/) const {
	// `Command::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`): *any* select-encoder
	// motion unlearns the command, in either direction -- the item has no value to walk, so legacy spends the encoder
	// on "forget this". `owned_input` leaves the turn unclaimed so that it arrives here, and taking it keeps the scene
	// from also acting on it. `midi::Command` has no `buttonAction`/`padAction` override, and MIDI learn reaches the
	// item through `MenuLearn::offer`, which calls the learn members below.
	using deluge::ui::shell::UiEvent;
	if (event.kind != UiEvent::Kind::Encoder || event.encoder != deluge::ui::widgets::core::EncoderRole::Select
	    || event.delta == 0) {
		return {};
	}
	if (ctx.ports.learn != nullptr) {
		ctx.ports.learn->learn_global(command_, std::nullopt);
	}
	deluge::base::notify::dirty().raise(deluge::base::notify::Dirty::Oled);
	return {.handled = true};
}

bool MidiCommandItem::learn_note_on(const MenuContext& ctx, deluge::midi::DeviceId device, int32_t channel,
                                    int32_t note) const {
	// `Command::learnNoteOn` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`), minus its display arm:
	// `MenuLearn` pops or redraws (`nodes/midi_command.h`'s note). A device the MIDI devices port does not know learns
	// nothing.
	if (ctx.ports.learn == nullptr || ctx.ports.midi == nullptr || !ctx.ports.midi->settings(device)) {
		return false;
	}
	ctx.ports.learn->learn_global(command_, bind::LearnedBinding{.device = device,
	                                                             .channel_or_zone = static_cast<uint8_t>(channel),
	                                                             .note_or_cc = static_cast<uint8_t>(note)});
	deluge::base::notify::dirty().raise(deluge::base::notify::Dirty::Oled);
	return true;
}

bool MidiCommandItem::learn_cc(const MenuContext& ctx, deluge::midi::DeviceId device, int32_t channel, int32_t cc,
                               int32_t value) const {
	// `Command::learnCC` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`): a zero-valued CC is the release
	// half of a momentary controller and binds nothing; the menu still uses the message, as `SoundEditor` does.
	if (value != 0) {
		return learn_note_on(ctx, device, channel + kFirstCcChannel, cc);
	}
	return false;
}

bool MidiCommandItem::learn_program_change(const MenuContext& ctx, deluge::midi::DeviceId device, int32_t channel,
                                           int32_t program) const {
	// `Command::learnProgramChange` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`). FILL is the one command
	// a program change cannot drive -- it is a momentary hold, and a PC has no off half -- so legacy refuses it with a
	// popup; the menu still uses the message, as `SoundEditor::pcReceivedForMidiLearn` does.
	if (command_ == deluge::project::GlobalCommand::FILL) {
		deluge::base::notify::notice_string(sid(String::STRING_FOR_CANT_LEARN_PC));
		return false;
	}
	return learn_note_on(ctx, device, channel + kFirstProgramChangeChannel, program);
}

bool MidiCommandItem::unlearn(const MenuContext& ctx) const {
	// `Command::unlearnAction`.
	if (ctx.ports.learn == nullptr) {
		return false;
	}
	ctx.ports.learn->learn_global(command_, std::nullopt);
	deluge::base::notify::dirty().raise(deluge::base::notify::Dirty::Oled);
	return true;
}

} // namespace deluge::scenes::menu::nodes
