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
/// @brief The `Descriptor` table and the scene. See `context_menu_scene.h`.

#include "scenes/context_menu/context_menu_scene.h"

#include "scenes/profile.h"

#include "ui/core/led_frame.h"
#include "ui/core/metrics.h"
#include "ui/oled/popup.h"

// `Deluge::factoryReset` (`deluge.cpp:222-228`). `deluge.h` is a ROOT-LEVEL app header, so it is named
// explicitly in this layer's include allow-list (`src/scenes/CMakeLists.txt`) the way
// `definitions_cxx.hpp` is, rather than being covered by one of the allowed subdirectories. Fix
// round 1, Minor 2: included rather than re-declared locally, and re-implementing its body here (a
// notice plus the other four resets) was rejected -- that would fork a legacy composite.
#include "deluge.h"
#include "gui/l10n/strings.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <array>
#include <string_view>

// `Song::setupDefault`-adjacent song-wide default the audio-source picker writes on every turn
// (`gui/context_menu/audio_input_selector.cpp:26`, `:193`; defined at `model/song/song.cpp:4896`).
// Declared bare at the global scope where it is defined -- legacy's own spelling, and the precedent
// `menu/nodes/settings.cpp` sets for `PadLEDs::flashCursor` and `jcong`.
extern AudioInputChannel defaultAudioOutputInputChannel;

namespace deluge::scenes::context_menu {

using deluge::l10n::String;
using deluge::ui::kFlashTime;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::TextScratch;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// The five descriptors. Each is one `ResetSettingsAction` arm of
// `ResetSettings::acceptCurrentOption` (`gui/context_menu/reset_settings.cpp:43-63`), with the
// heading and the option list that class's `getTitle()` / `getOptions()` return for all five.

/// `ResetSettings::getOptions()`: a single `STRING_FOR_OK`. **Not a NO/YES pair** -- legacy's
/// "are you sure" confirmation is one option you either press select on or back out of.
constexpr uint16_t kConfirmOptions[] = {sid(String::STRING_FOR_OK)};

bool accept_reset_flash(const menu::MenuContext&, uint8_t) {
	FlashStorage::factoryReset();
	return false; // `acceptCurrentOption` returns false -> the menu closes.
}

bool accept_reset_community_features(const menu::MenuContext&, uint8_t) {
	runtimeFeatureSettings.factoryReset();
	return false;
}

bool accept_reset_midi_follow(const menu::MenuContext&, uint8_t) {
	midiFollow.factoryReset();
	return false;
}

bool accept_reset_midi_devices(const menu::MenuContext&, uint8_t) {
	MIDIDeviceManager::factoryReset();
	return false;
}

bool accept_reset_all(const menu::MenuContext&, uint8_t) {
	Deluge::factoryReset();
	return false;
}

// ---------------------------------------------------------------------------------------------
// The sixth descriptor: `AudioInputSelector` (`gui/context_menu/audio_input_selector.{h,cpp}`).
//
// This is the one that made `Descriptor` grow. Legacy's class is not a confirmation dialog: it is a
// *live* picker whose every turn writes the model, whose cursor opens on the current setting, and
// which draws a line of its own under the options. All three of those are hooks below.

/// `AudioInputSelector::Value` (`audio_input_selector.cpp:30-40`) -- the option *order*, which is
/// what makes the cursor a value. Declared here rather than reusing `AudioInputChannel`, because
/// the two are deliberately different orderings and legacy keeps its own enum for exactly that.
enum class AudioSourceOption : uint8_t {
	Off = 0,
	Left,
	Right,
	Stereo,
	Balanced,
	Master,
	Output,
	Track,
	Count,
};

/// `AudioInputSelector::getOptions()` (`:77-85`), in its own order. Eight fixed strings: there is
/// no `isCurrentOptionAvailable()` override on this class, so nothing here is dynamic.
constexpr uint16_t kAudioSourceOptions[] = {
    sid(String::STRING_FOR_DISABLED),     sid(String::STRING_FOR_LEFT_INPUT),     sid(String::STRING_FOR_RIGHT_INPUT),
    sid(String::STRING_FOR_STEREO_INPUT), sid(String::STRING_FOR_BALANCED_INPUT), sid(String::STRING_FOR_MIX_PRE_FX),
    sid(String::STRING_FOR_MIX_POST_FX),  sid(String::STRING_FOR_TRACK),
};
static_assert(std::size(kAudioSourceOptions) == static_cast<size_t>(AudioSourceOption::Count),
              "the option list and the Value enum are the same list twice; a row added to one "
              "without the other silently shifts every channel mapping below");

/// @brief The `AudioOutput` this menu edits -- legacy's `audioInputSelector.audioOutput`, which
///        `AudioSourceSelector::selectButtonPress` sets from `getCurrentOutput()`
///        (`gui/menu_item/audio_clip/audio_source_selector.h:28`).
///
/// `MenuContext::output` is that same output; the cast is guarded by `is_audio_clip`, which is what
/// `context_for` resolved from the clip's type -- legacy's own cast is unguarded.
AudioOutput* audio_output_of(const menu::MenuContext& ctx) {
	if (!ctx.is_audio_clip || ctx.output == nullptr) {
		return nullptr;
	}
	return static_cast<AudioOutput*>(ctx.output);
}

/// `getRecordableOutputInSong` (`audio_input_selector.cpp:46-58`): the saved source, but only if it
/// is still both recordable and in the song. Transcribed -- it is a file-local static there.
Output* recordable_output_in_song(const menu::MenuContext& ctx, AudioOutput* audio_output, Output* selected) {
	if (audio_output == nullptr || selected == nullptr || !audio_output->canRecordFrom(selected)
	    || ctx.song == nullptr) {
		return nullptr;
	}
	for (Output* output = ctx.song->firstOutput; output != nullptr; output = output->next) {
		if (output == selected) {
			return selected;
		}
	}
	return nullptr;
}

/// `getFirstRecordableOutput` (`:61-69`).
Output* first_recordable_output(const menu::MenuContext& ctx, AudioOutput* audio_output) {
	if (audio_output == nullptr || ctx.song == nullptr) {
		return nullptr;
	}
	for (Output* output = ctx.song->firstOutput; output != nullptr; output = output->next) {
		if (audio_output->canRecordFrom(output)) {
			return output;
		}
	}
	return nullptr;
}

uint8_t initial_audio_source(const menu::MenuContext& ctx) {
	// `AudioInputSelector::setupAndCheckAvailability` (`:87-127`), switch arm for switch arm. Its
	// `default:` is `Value::OFF`, which also covers a null output here.
	const AudioOutput* audio_output = audio_output_of(ctx);
	if (audio_output == nullptr) {
		return static_cast<uint8_t>(AudioSourceOption::Off);
	}
	switch (audio_output->inputChannel) {
	case AudioInputChannel::LEFT:
		return static_cast<uint8_t>(AudioSourceOption::Left);
	case AudioInputChannel::RIGHT:
		return static_cast<uint8_t>(AudioSourceOption::Right);
	case AudioInputChannel::STEREO:
		return static_cast<uint8_t>(AudioSourceOption::Stereo);
	case AudioInputChannel::BALANCED:
		return static_cast<uint8_t>(AudioSourceOption::Balanced);
	case AudioInputChannel::MIX:
		return static_cast<uint8_t>(AudioSourceOption::Master);
	case AudioInputChannel::OUTPUT:
		return static_cast<uint8_t>(AudioSourceOption::Output);
	case AudioInputChannel::SPECIFIC_OUTPUT:
		return static_cast<uint8_t>(AudioSourceOption::Track);
	default:
		return static_cast<uint8_t>(AudioSourceOption::Off);
	}
}

void on_move_audio_source(const menu::MenuContext& ctx, Selection& selection) {
	// `AudioInputSelector::selectEncoderAction`'s tail (`:140-195`), after the base class has already
	// moved the cursor (the scene does that before calling this, exactly as legacy calls
	// `ContextMenu::selectEncoderAction` first).
	//
	// Not ported: the `if (currentUIMode != 0u) return;` guard at `:135-137`. `currentUIMode` is a
	// legacy UI global this layer may not read, and the shell's own input routing is what stands in
	// for it -- a scene below a modal overlay is not handed the turn at all.
	AudioOutput* audio_output = audio_output_of(ctx);
	if (audio_output == nullptr) {
		return;
	}
	const auto option = static_cast<AudioSourceOption>(selection.option);

	if (option == AudioSourceOption::Track) {
		// `:142-145` -- "Keep Track on the first visible row so the selected track name has room
		// below it", which is what stops the footer from landing on a drawn option row.
		selection.scroll = selection.option;
	}

	// `:149-151` -- leaving SPECIFIC_OUTPUT clears the recording-from state, so the track that was
	// being recorded is not left silently muted.
	if (audio_output->inputChannel == AudioInputChannel::SPECIFIC_OUTPUT && option != AudioSourceOption::Track) {
		audio_output->clearRecordingFrom();
	}

	switch (option) {
	case AudioSourceOption::Left:
		audio_output->inputChannel = AudioInputChannel::LEFT;
		break;
	case AudioSourceOption::Right:
		audio_output->inputChannel = AudioInputChannel::RIGHT;
		break;
	case AudioSourceOption::Stereo:
		audio_output->inputChannel = AudioInputChannel::STEREO;
		break;
	case AudioSourceOption::Balanced:
		audio_output->inputChannel = AudioInputChannel::BALANCED;
		break;
	case AudioSourceOption::Master:
		audio_output->inputChannel = AudioInputChannel::MIX;
		break;
	case AudioSourceOption::Output:
		audio_output->inputChannel = AudioInputChannel::OUTPUT;
		break;
	case AudioSourceOption::Track: {
		// `:178-187` -- keep the chosen source if it is still usable, otherwise take the first one
		// that is; `setOutputRecordingFrom(nullptr)` is the "none available" answer.
		audio_output->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		Output* record_from = recordable_output_in_song(ctx, audio_output, audio_output->getOutputRecordingFrom());
		if (record_from == nullptr) {
			record_from = first_recordable_output(ctx, audio_output);
		}
		audio_output->setOutputRecordingFrom(record_from);
		break;
	}
	case AudioSourceOption::Off:
	case AudioSourceOption::Count:
	default:
		audio_output->inputChannel = AudioInputChannel::NONE;
		break;
	}

	// `:193` -- the song-wide default follows whatever was last picked here.
	defaultAudioOutputInputChannel = audio_output->inputChannel;
}

std::string_view footer_audio_source(const menu::MenuContext& ctx, std::span<char> /*scratch*/) {
	// `AudioInputSelector::renderOLED`'s tail (`:225-236`): nothing at all unless the channel is
	// SPECIFIC_OUTPUT, then the source's name, or the literal "No track" when there is none. Both
	// strings outlive the render -- `Output::name` is a model member and the literal is static -- so
	// the scratch is unused, which is why the parameter is unnamed.
	const AudioOutput* audio_output = audio_output_of(ctx);
	if (audio_output == nullptr || audio_output->inputChannel != AudioInputChannel::SPECIFIC_OUTPUT) {
		return {};
	}
	Output* record_from = recordable_output_in_song(ctx, const_cast<AudioOutput*>(audio_output),
	                                                const_cast<AudioOutput*>(audio_output)->getOutputRecordingFrom());
	// Legacy's own literal, not an l10n id: `audio_input_selector.cpp:231` hard-codes "No track", as
	// it does the two `popupTextTemporary` strings in `padAction`.
	return record_from != nullptr ? std::string_view{record_from->name.c_str()} : std::string_view{"No track"};
}

/// `AudioInputSelector` has no `acceptCurrentOption()` override, so it takes the base's
/// `return false` (`gui/context_menu/context_menu.h:38`) -- a select press closes the menu, and the
/// value has already been committed by `on_move`.
bool accept_audio_source(const menu::MenuContext&, uint8_t) {
	return false;
}

constexpr std::array<Descriptor, static_cast<size_t>(DescriptorId::Count)> kDescriptors{{
    // All five resets share `ResetSettings`'s own heading and option list; only the action differs,
    // which is exactly what `ResetSettingsAction` selects in legacy. None of them uses the three
    // hooks the sixth needed, so all five still carry three fields.
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_flash},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_community_features},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_midi_follow},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_midi_devices},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_all},
    {sid(String::STRING_FOR_AUDIO_SOURCE), kAudioSourceOptions, accept_audio_source, initial_audio_source,
     on_move_audio_source, footer_audio_source},
}};

// Fix round 1, Minor 1. `render_oled` materialises `min(options.size(), kMaxOptions)` views while
// `step()` walks the descriptor's FULL count, so a descriptor with more options than the render
// arrays hold would let the cursor reach a row that is never drawn -- an invisible selection, and
// then an accept on it. The ceiling exists because this layer allocates nothing; this is the check
// that the ceiling is big enough for the table it guards.
static_assert(
    [] {
	    for (const Descriptor& d : kDescriptors) {
		    if (d.options.size() > kMaxOptions) {
			    return false;
		    }
	    }
	    return true;
    }(),
    "a descriptor carries more options than kMaxOptions: raise kMaxOptions, or render_oled will "
    "silently truncate a list step() can still walk");

} // namespace

const Descriptor& descriptor(DescriptorId id) {
	const size_t i = static_cast<size_t>(id);
	return kDescriptors[i < kDescriptors.size() ? i : 0];
}

Selection step(Selection s, int32_t offset, uint8_t count) {
	// `ContextMenu::selectEncoderAction`, statement for statement. The `do/while
	// (!isCurrentOptionAvailable())` loop collapses to one pass, because unavailable options are
	// not in the list at all (see `Descriptor::options`).
	if (count == 0) {
		return s;
	}
	const bool was_on_scroll_pos = s.option == s.scroll;
	const int32_t old_option = s.option;
	const int32_t next = old_option + offset;
	if (next >= count || next < 0) {
		// "currentOption = oldCurrentOption; return;" -- a turn off either end changes nothing at
		// all, not even the scroll, and does NOT clamp to the last option.
		return s;
	}

	Selection out{static_cast<uint8_t>(next), s.scroll};
	if (next < out.scroll) {
		out.scroll = static_cast<uint8_t>(next);
	}
	else if (offset >= 0 && !was_on_scroll_pos) {
		// The cursor had already left the top row, so the window follows it downwards and the
		// cursor stays on the *bottom* of the two rows. This is what `ui::oled::list`'s
		// centre-the-cursor rule would not do, and why this scene does not use that widget.
		out.scroll = static_cast<uint8_t>(old_option);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------

void ContextMenuScene::open(const menu::MenuContext& ctx) {
	ctx_ = ctx;
}

bool ContextMenuScene::on_enter(const SceneId& id) {
	if (id.arg >= static_cast<uint16_t>(DescriptorId::Count)) {
		return false;
	}
	id_ = static_cast<DescriptorId>(id.arg);

	// `ContextMenu::setupAndCheckAvailability`: walk to the first available option, seed
	// `scrollPos` from it, and refuse to open when there is none. With availability expressed by
	// compaction (see `Descriptor::options`) the walk is "option 0, if there is one" -- unless the
	// descriptor replaces it outright, which is what `initial_option` is
	// (`AudioInputSelector::setupAndCheckAvailability`, `audio_input_selector.cpp:123-125`, sets
	// `currentOption` *and* `scrollPos` from the current channel).
	const Descriptor& d = descriptor(id_);
	if (d.options.empty()) {
		return false;
	}
	sel_ = Selection{};
	if (d.initial_option != nullptr) {
		const auto count = static_cast<uint8_t>(d.options.size());
		const uint8_t wanted = d.initial_option(ctx_);
		// Clamped, not trusted: a hook that answers past the end would put the cursor on a row
		// `option_window` never draws and `accept` would then act on it.
		sel_.option = wanted < count ? wanted : static_cast<uint8_t>(count - 1);
		sel_.scroll = sel_.option;
	}
	scroll_ = deluge::ui::ScrollText{};
	last_render_ms_ = 0;
	return true;
}

void ContextMenuScene::on_exit() {
	// Same fail-closed reasoning as `RenameScene::on_exit`: `open()` is the only writer of `ctx_`
	// and this scene is a singleton that outlives every visit, so a re-entry that forgot its
	// `open()` must not hand the accept hook the previous song's pointers. None of the five
	// shipped hooks reads the context, but the next descriptor to land will.
	ctx_ = menu::MenuContext{};
}

void ContextMenuScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	(void)role;
	last_render_ms_ = f.now_ms();

	const Descriptor& d = descriptor(id_);

	// The l10n ids resolved into views that stay alive for the call. `text_or_digits` only writes
	// its scratch when the profile carries no text hook, but the views must outlive the call
	// either way, so the scratches are locals here rather than temporaries.
	TextScratch heading_scratch;
	std::array<TextScratch, kMaxOptions> option_scratch{};
	std::array<std::string_view, kMaxOptions> options{};
	const size_t n = std::min(d.options.size(), kMaxOptions);
	for (size_t i = 0; i < n; i++) {
		options[i] = deluge_profile().text_or_digits(d.options[i], option_scratch[i]);
	}

	// `AudioInputSelector::renderOLED` calls the base and then draws one more line
	// (`audio_input_selector.cpp:222-237`); `option_window` takes it as a parameter so the drawing
	// stays in the toolkit and this scene keeps its no-frame-primitives rule.
	std::array<char, kFooterScratch> footer_scratch{};
	const std::string_view footer =
	    d.footer != nullptr ? d.footer(ctx_, std::span{footer_scratch}) : std::string_view{};

	deluge::ui::oled::option_window(f, deluge_profile().text_or_digits(d.title, heading_scratch),
	                                std::span<const std::string_view>{options.data(), n}, sel_.option, sel_.scroll,
	                                scroll_, /*allow_motion=*/true, footer);
}

void ContextMenuScene::leds(deluge::ui::LedFrame& f) {
	// `ContextMenu::focusRegained`: `indicator_leds::blinkLed(IndicatorLED::BACK)`, and nothing
	// else. The saving/loading variants also blink SAVE/LOAD and clear the other of the pair, but
	// no shipped descriptor is one of those.
	//
	// The period is `ui::kFlashTime` (`ui/core/metrics.h`), imported rather than re-spelled as a
	// literal 110: `indicator_leds::blinkLed` runs at exactly that constant
	// (`hid/led/indicator_leds.cpp:108`, `definitions_cxx.hpp:159`), and `rename_scene.cpp:44`
	// already imports it this way. (Fix round 1, Minor 3 -- this was a third hand-written copy.)
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back),
	            deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, static_cast<uint16_t>(kFlashTime), 0});
}

Result ContextMenuScene::handle(const UiEvent& event, const UiContext& ctx) {
	(void)ctx;
	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// `ContextMenu::buttonAction`. Both arms are `on`-only; the `isUIModeWithinRange` gate and
		// the `inCardRoutine` retry are shell business (`Action::Kind::LockInput`), not the
		// scene's.
		if (!event.on) {
			break;
		}
		if (event.button == ButtonId::Back) {
			return close();
		}
		if (event.button == ButtonId::SelectEnc) {
			// `getAcceptButton()` is `SELECT_ENC` for the base class, and the `b == SELECT_ENC` arm
			// runs before it, so on every shipped descriptor the two are the same button and the
			// third arm of that if/else is dead. `ContextMenuForSaving`/`ContextMenuForLoading`
			// answer `SAVE`/`LOAD` instead; both are file-browser menus this plan does not port,
			// so `Descriptor` carries no `accept_button` field yet. Adding one means one extra
			// comparison here, nothing more.
			return accept_current_option();
		}
		break;

	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::EncoderRole::Select) {
			// `ContextMenu::selectEncoderAction` -> `renderUIsForOled()`. `option_window` is
			// render-only (it claims nothing), so the turn arrives here rather than being consumed
			// during the render pass the way `list` consumes it for `MenuScene`.
			const Selection before = sel_;
			const Descriptor& d = descriptor(id_);
			sel_ = step(sel_, event.delta, static_cast<uint8_t>(d.options.size()));
			if (d.on_move != nullptr) {
				// **Unconditionally, even when `step` refused to move.**
				// `AudioInputSelector::selectEncoderAction` (`audio_input_selector.cpp:134-196`) calls
				// the base and then runs its whole commit block whatever the base did, so a turn off
				// the end of the list re-commits the option already selected. Harmless (it writes the
				// same channel back) but it is legacy's shape, and guarding it here would be a
				// behaviour change dressed up as an optimisation.
				d.on_move(ctx_, sel_);
			}
			Result r;
			r.handled = true;
			if (sel_ != before) {
				(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			}
			return r;
		}
		break;

	case UiEvent::Kind::Pad:
		// `ContextMenu::padAction`: a press closes, and every pad event is `DEALT_WITH` -- so a
		// release is swallowed rather than falling through to the view underneath.
		if (event.on) {
			return close();
		}
		{
			Result r;
			r.handled = true;
			return r;
		}

	// The two MIDI kinds (F1 task 10's transport, `shell/scene.h`) are declined here for the reason
	// every scene ported so far declines them: claiming one means answering `learnable_at(pad)`,
	// which is the timeline scenes' work (E5/E6). Declining lets `Router::run_chain` carry the
	// message on to `services::MidiLearn`, which is where legacy's `view.` fallback puts it.
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
	case UiEvent::Kind::AllReleased:
		break;
	}
	return Result::unhandled();
}

Result ContextMenuScene::accept_current_option() {
	Result r;
	r.handled = true;
	if (descriptor(id_).accept(ctx_, sel_.option)) {
		// "If returns false, will cause UI to exit" -- so a true keeps the menu open. No shipped
		// descriptor does, but the convention is legacy's and is what a multi-step menu would use.
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}
	return close();
}

Result ContextMenuScene::close() {
	// `ContextMenu::close()`. `Remove` naming this scene's own kind, not `Pop`: the accept hooks
	// raise notices (`FlashStorage::factoryReset` and friends all `notify::notice(...)`), so an
	// overlay can legitimately be pushed above this scene between the decision and the router
	// applying it -- and `Pop` would take that down too (`shell/scene.h`'s note on
	// `Action::Kind::Pop`).
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kContextMenuSceneKind});
	return r;
}

std::optional<uint32_t> ContextMenuScene::next_timer() const {
	// The one thing on screen that moves without input is the focused row's side scroller, and it
	// moves only while its label is too wide for the box (`scroll_text`, which freezes and resets
	// otherwise). `ScrollText` records both widths, so "is it scrolling" is answerable here without
	// re-measuring anything.
	//
	// No shipped descriptor reaches it: "OK" is 18px in a 78px box. Kept anyway, because the
	// alternative is a scene whose first wide-labelled descriptor silently stops animating.
	if (scroll_.text_w_last <= scroll_.rect_w_last) {
		return std::nullopt;
	}
	const int32_t due = static_cast<int32_t>(scroll_.next_step_ms - last_render_ms_);
	return static_cast<uint32_t>(due > 0 ? due : 0);
}

ContextMenuScene& context_menu_scene() {
	static ContextMenuScene scene;
	return scene;
}

} // namespace deluge::scenes::context_menu
