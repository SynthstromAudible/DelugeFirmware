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

#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/target.h"
#include "scenes/bind/write_sink.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/menu/bind_host.h"
#include "scenes/profile.h"

#include "ui/core/led_frame.h"
#include "ui/core/metrics.h"
#include "ui/oled/popup.h"

// `Deluge::factoryReset` (`deluge.cpp`). `deluge.h` is a ROOT-LEVEL app header, so it is named
// explicitly in this layer's include allow-list (`src/scenes/CMakeLists.txt`) the way
// `definitions_cxx.hpp` is, rather than being covered by one of the allowed subdirectories. It is included rather than
// re-declared locally, and its body is not re-implemented here (a notice plus the other four
// resets) -- that would fork a legacy composite.
#include "deluge-project/audio_input.h"
#include "deluge-project/setting_group.h"
#include "deluge.h"
#include "gui/l10n/strings.h"
#include "instruments/deluge/sound_state.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string_view>

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
	menu::host_settings_reset(); // what main reads live reaches the open guest too
	return false;                // `acceptCurrentOption` returns false -> the menu closes.
}

bool accept_reset_community_features(const menu::MenuContext&, uint8_t) {
	runtimeFeatureSettings.factoryReset();
	menu::host_settings_reset();
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
	menu::host_settings_reset();
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
	Usb,
	Master,
	Output,
	Track,
	Count,
};

/// `AudioInputSelector::getOptions()` (`:77-85`), in its own order. Nine fixed strings: there is
/// no `isCurrentOptionAvailable()` override on this class, so nothing here is dynamic.
constexpr uint16_t kAudioSourceOptions[] = {
    sid(String::STRING_FOR_DISABLED),     sid(String::STRING_FOR_LEFT_INPUT),     sid(String::STRING_FOR_RIGHT_INPUT),
    sid(String::STRING_FOR_STEREO_INPUT), sid(String::STRING_FOR_BALANCED_INPUT), sid(String::STRING_FOR_USB_INPUT),
    sid(String::STRING_FOR_MIX_PRE_FX),   sid(String::STRING_FOR_MIX_POST_FX),    sid(String::STRING_FOR_TRACK),
};
static_assert(static_cast<size_t>(AudioSourceOption::Track) == project::kInputChannelSpecificTrack
                  && project::kInputChannelWords[static_cast<size_t>(AudioSourceOption::Usb)] == "usb",
              "each option is the channel word at its own index");
static_assert(std::size(kAudioSourceOptions) == static_cast<size_t>(AudioSourceOption::Count),
              "the option list and the Value enum are the same list twice; a row added to one "
              "without the other silently shifts every channel mapping below");

/// @brief The audio track this menu edits -- legacy's `audioInputSelector.audioOutput`, which
///        `AudioSourceSelector::selectButtonPress` sets from `getCurrentOutput()`
///        (`gui/menu_item/audio_clip/audio_source_selector.h:28`); nullopt off an audio clip.
std::optional<project::TrackAddress> audio_track_of(const menu::MenuContext& ctx) {
	if (!ctx.is_audio_clip || !ctx.addresses.track || ctx.model == nullptr) {
		return std::nullopt;
	}
	return ctx.addresses.track;
}

constexpr bind::Target audio_input_row(std::string_view path) {
	return {.kind = bind::Target::Kind::MODEL, .path = path, .group = project::SettingGroupId::AUDIO_INPUT};
}

/// The audio track's AUDIO_INPUT row @p path, as the record holds it.
std::optional<serialization::Value> audio_input(const menu::MenuContext& ctx, std::string_view path) {
	const std::optional<project::TrackAddress> track = audio_track_of(ctx);
	if (!track) {
		return std::nullopt;
	}
	return project::read_setting(
	    *ctx.model, project::SettingAddress{.group = project::SettingGroupId::AUDIO_INPUT, .owner = *track}, path);
}

/// The input channel the audio track records, as an index of the record's channel words (0, none, where it has none).
int32_t input_channel_of(const menu::MenuContext& ctx) {
	const std::optional<serialization::Value> word = audio_input(ctx, "inputChannel");
	return word ? project::index_of_word(project::kInputChannelWords, word->str()).value_or(0) : 0;
}

/// The track the audio track records from, by id; nullopt for none.
std::optional<uint32_t> recording_from_of(const menu::MenuContext& ctx) {
	const std::optional<serialization::Value> id = audio_input(ctx, "outputRecordingFrom");
	if (!id) {
		return std::nullopt;
	}
	const int64_t value = std::strtoll(id->str().c_str(), nullptr, 10);
	return value >= 0 ? std::optional{static_cast<uint32_t>(value)} : std::nullopt;
}

/// `getRecordableOutputInSong` (`audio_input_selector.cpp:46-58`): the saved source, but only if it is still both
/// recordable and in the song.
std::optional<uint32_t> recordable_track_in_song(const menu::MenuContext& ctx, std::optional<uint32_t> selected) {
	const std::optional<project::TrackAddress> track = audio_track_of(ctx);
	if (!track || !selected || ctx.facts == nullptr) {
		return std::nullopt;
	}
	for (const bind::TrackFact& candidate : ctx.facts->tracks()) {
		if (candidate.output_id == *selected) {
			return bind::can_record_from(track->output_id, candidate) ? selected : std::nullopt;
		}
	}
	return std::nullopt;
}

/// `getFirstRecordableOutput` (`:61-69`).
std::optional<uint32_t> first_recordable_track(const menu::MenuContext& ctx) {
	const std::optional<project::TrackAddress> track = audio_track_of(ctx);
	if (!track || ctx.facts == nullptr) {
		return std::nullopt;
	}
	for (const bind::TrackFact& candidate : ctx.facts->tracks()) {
		if (bind::can_record_from(track->output_id, candidate)) {
			return candidate.output_id;
		}
	}
	return std::nullopt;
}

uint8_t initial_audio_source(const menu::MenuContext& ctx) {
	// `AudioInputSelector::setupAndCheckAvailability` (`:87-127`), switch arm for switch arm. Its
	// `default:` is `Value::OFF`, which also covers no audio track here. The channel words are the
	// options' own order, from none (OFF) to the specific track (TRACK).
	if (!audio_track_of(ctx)) {
		return static_cast<uint8_t>(AudioSourceOption::Off);
	}
	return static_cast<uint8_t>(input_channel_of(ctx));
}

void on_move_audio_source(const menu::MenuContext& ctx, Selection& selection) {
	// `AudioInputSelector::selectEncoderAction`'s tail (`:140-195`), after the base class has already
	// moved the cursor (the scene does that before calling this, exactly as legacy calls
	// `ContextMenu::selectEncoderAction` first).
	//
	// Not ported: the `if (currentUIMode != 0u) return;` guard at `:135-137`. `currentUIMode` is a
	// legacy UI global this layer may not read, and the shell's own input routing is what stands in
	// for it -- a scene below a modal overlay is not handed the turn at all.
	if (!audio_track_of(ctx) || ctx.sink == nullptr) {
		return;
	}
	const auto option = static_cast<AudioSourceOption>(selection.option);

	if (option == AudioSourceOption::Track) {
		// `:142-145` -- "Keep Track on the first visible row so the selected track name has room
		// below it", which is what stops the footer from landing on a drawn option row.
		selection.scroll = selection.option;
	}

	// The option is the channel's word: OFF is none. Leaving the specific track clears the recording-from state (the
	// input's Edit does), so the track that was being recorded is not left silently muted.
	const int32_t channel = option < AudioSourceOption::Count ? static_cast<int32_t>(option) : 0;
	(void)ctx.sink->write({.target = audio_input_row("inputChannel"), .where = ctx.addresses, .menu_value = channel});

	if (option == AudioSourceOption::Track) {
		// `:178-187` -- keep the chosen source if it is still usable, otherwise take the first one
		// that is; none is the "none available" answer.
		std::optional<uint32_t> record_from = recordable_track_in_song(ctx, recording_from_of(ctx));
		if (!record_from) {
			record_from = first_recordable_track(ctx);
		}
		(void)ctx.sink->write({.target = audio_input_row("outputRecordingFrom"),
		                       .where = ctx.addresses,
		                       .menu_value = record_from ? static_cast<int32_t>(*record_from) : -1});
	}

	// `:193` -- the song-wide default follows whatever was last picked here.
	ctx.sink->set_default_audio_input_channel(input_channel_of(ctx));
}

std::string_view footer_audio_source(const menu::MenuContext& ctx, std::span<char> scratch) {
	// `AudioInputSelector::renderOLED`'s tail (`:225-236`): nothing at all unless the channel is
	// SPECIFIC_OUTPUT, then the source's name, or the literal "No track" when there is none.
	if (!audio_track_of(ctx) || input_channel_of(ctx) != project::kInputChannelSpecificTrack) {
		return {};
	}
	const std::optional<uint32_t> record_from = recordable_track_in_song(ctx, recording_from_of(ctx));
	const std::optional<serialization::Value> name =
	    record_from ? project::read_setting(*ctx.model,
	                                        project::SettingAddress{.group = project::SettingGroupId::TRACK,
	                                                                .owner = project::TrackAddress{*record_from}},
	                                        "name")
	                : std::nullopt;
	if (!name) {
		// Legacy's own literal, not an l10n id: `audio_input_selector.cpp:231` hard-codes "No track", as
		// it does the two `popupTextTemporary` strings in `padAction`.
		return std::string_view{"No track"};
	}
	const size_t n = std::min(name->str().size(), scratch.size());
	std::copy_n(name->str().data(), n, scratch.data());
	return {scratch.data(), n};
}

/// `AudioInputSelector` has no `acceptCurrentOption()` override, so it takes the base's
/// `return false` (`gui/context_menu/context_menu.h:38`) -- a select press closes the menu, and the
/// value has already been committed by `on_move`.
bool accept_audio_source(const menu::MenuContext&, uint8_t) {
	return false;
}

constexpr uint16_t kSampleBrowserKitOptions[] = {sid(String::STRING_FOR_LOAD_ALL), sid(String::STRING_FOR_SLICE)};

constexpr uint16_t kSampleBrowserSynthOptions[] = {
    sid(String::STRING_FOR_MULTISAMPLES),
    sid(String::STRING_FOR_BASIC),
    sid(String::STRING_FOR_SINGLE_CYCLE),
    sid(String::STRING_FOR_WAVETABLE),
};

/// `Kit::acceptCurrentOption`: LOAD ALL imports the folder; SLICE opens the slicer on the file, over this menu. Like
/// `Synth`'s, it never closes the menu itself: the load it parks for the host closes it once it ends
/// (`BrowserScene::long_press_done`).
bool accept_sample_browser_kit(const menu::MenuContext&, uint8_t option) {
	browser::browser_scene().park_operation(option == 0 ? browser::BrowserChoice::Op::ImportKit
	                                                    : browser::BrowserChoice::Op::Slice);
	return true;
}

/// `Kit::isCurrentOptionAvailable`: LOAD ALL always, SLICE only on a file.
size_t sample_browser_kit_options_now(const menu::MenuContext&, std::span<uint8_t> out) {
	size_t n = 0;
	out[n++] = 0;
	if (!browser::browser_scene().current_is_folder() && n < out.size()) {
		out[n++] = 1;
	}
	return n;
}

/// `Synth::acceptCurrentOption` (`gui/context_menu/sample_browser/synth.cpp:95-120`): MULTISAMPLES imports the folder;
/// the other three claim the file with their own detection choices.
bool accept_sample_browser_synth(const menu::MenuContext&, uint8_t option) {
	using Op = browser::BrowserChoice::Op;
	using Args = browser::BrowserChoice::ClaimArgs;
	switch (option) {
	case 0:
		browser::browser_scene().park_operation(Op::ImportMultisamples);
		break;
	case 1:
		browser::browser_scene().park_operation(Op::Claim, Args{.pitch = 0, .single_cycle = 0, .wavetable = 0});
		break;
	case 2:
		browser::browser_scene().park_operation(Op::Claim, Args{.pitch = 2, .single_cycle = 2, .wavetable = 1});
		break;
	default:
		browser::browser_scene().park_operation(Op::Claim, Args{.pitch = 1, .single_cycle = 1, .wavetable = 2});
		break;
	}
	return true;
}

size_t sample_browser_synth_options_now(const menu::MenuContext& ctx, std::span<uint8_t> out) {
	return sample_browser_synth_options(ctx, browser::browser_scene().current_is_folder(), out);
}

/// `Kit::padAction` and `Synth::padAction` hand the pad to the browser beneath. Its keyboard reads the press itself
/// as it draws under this menu, so the menu keeps the pad from closing it and does nothing more.
Result sample_browser_pad(const UiEvent&, const UiContext&) {
	Result r;
	r.handled = true;
	return r;
}

/// The browser's gate (`SampleBrowser::buttonAction`, `sample_browser.cpp:466-469`): a claim or a folder import the
/// menu started is still running.
bool sample_load_busy(const menu::MenuContext& ctx) {
	return ctx.facts != nullptr && ctx.facts->sample_load_in_flight();
}

/// `DeleteFile::acceptCurrentOption`: the browser's file is deleted by the host, which removes it from the browser's
/// list once the card has. The menu closes at once.
bool accept_delete_file(const menu::MenuContext&, uint8_t) {
	browser::browser_scene().park_operation(browser::BrowserChoice::Op::DeleteFile);
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
    {sid(String::STRING_FOR_SAMPLES), kSampleBrowserKitOptions, accept_sample_browser_kit, nullptr, nullptr, nullptr,
     sample_browser_kit_options_now, sample_browser_pad, sample_load_busy},
    {sid(String::STRING_FOR_LOAD_FILES), kSampleBrowserSynthOptions, accept_sample_browser_synth, nullptr, nullptr,
     nullptr, sample_browser_synth_options_now, sample_browser_pad, sample_load_busy},
    {.title = sid(String::STRING_FOR_DELETE_QMARK),
     .options = kConfirmOptions,
     .accept = accept_delete_file,
     .for_saving = true},
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

size_t sample_browser_synth_options(const menu::MenuContext& ctx, bool is_folder, std::span<uint8_t> out) {
	const deluge::instrument::SoundState* state =
	    ctx.facts != nullptr && ctx.addresses.slot ? ctx.facts->sound_state(*ctx.addresses.slot) : nullptr;
	const bool ringmod = state != nullptr && state->synth_mode == SynthMode::RINGMOD;
	const bool available[] = {!ringmod, !is_folder && !ringmod, !is_folder, !is_folder};
	size_t n = 0;
	for (uint8_t i = 0; i < std::size(available) && n < out.size(); i++) {
		if (available[i]) {
			out[n++] = i;
		}
	}
	return n;
}

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
	sel_ = Selection{};
	refresh_available();
	if (available_count_ == 0) {
		return false;
	}
	if (d.initial_option != nullptr) {
		const uint8_t count = available_count_;
		const uint8_t wanted = d.initial_option(ctx_);
		// Clamped, not trusted: a hook that answers past the end would put the cursor on a row
		// `option_window` never draws and `accept` would then act on it.
		sel_.option = wanted < count ? wanted : static_cast<uint8_t>(count - 1);
		sel_.scroll = sel_.option;
	}
	scroll_ = deluge::ui::ScrollText{};
	footer_scroll_ = deluge::ui::ScrollText{};
	last_render_ms_ = 0;
	blink_restart_ = true;
	return true;
}

void ContextMenuScene::on_exit() {
	// Same fail-closed reasoning as `RenameScene::on_exit`: `open()` is the only writer of `ctx_`
	// and this scene is a singleton that outlives every visit, so a re-entry that forgot its
	// `open()` must not hand the accept hooks the previous song's pointers: the sample browser's menus read it.
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
	refresh_available();
	const size_t n = available_count_;
	for (size_t i = 0; i < n; i++) {
		options[i] = deluge_profile().text_or_digits(d.options[available_[i]], option_scratch[i]);
	}

	// `AudioInputSelector::renderOLED` calls the base and then draws one more line
	// (`audio_input_selector.cpp:223-250`); `option_window` takes it as a parameter so the drawing
	// stays in the toolkit and this scene keeps its no-frame-primitives rule.
	std::array<char, kFooterScratch> footer_scratch{};
	const std::string_view footer =
	    d.footer != nullptr ? d.footer(ctx_, std::span{footer_scratch}) : std::string_view{};

	// Drawn on the whole panel: legacy's window starts at row 4 of the 48-row panel, above the root frame's
	// `visible_top`.
	deluge::ui::oled::OledFrame pf = deluge::ui::oled::panel_frame(f);
	deluge::ui::oled::option_window(pf, deluge_profile().text_or_digits(d.title, heading_scratch),
	                                std::span<const std::string_view>{options.data(), n}, sel_.option, sel_.scroll,
	                                scroll_, /*allow_motion=*/true, footer, &footer_scroll_);
}

void ContextMenuScene::leds(deluge::ui::LedFrame& f) {
	// `ContextMenu::focusRegained`: `indicator_leds::blinkLed(IndicatorLED::BACK)`; a saving menu
	// (`ContextMenuForSaving::focusRegained`) also blinks SAVE and turns LOAD off.
	//
	// The period is `ui::kFlashTime` (`ui/core/metrics.h`), imported rather than re-spelled as a
	// literal 110: `indicator_leds::blinkLed` runs at exactly that constant
	// (`hid/led/indicator_leds.cpp:108`, `definitions_cxx.hpp:159`), and `rename_scene.cpp`
	// imports it the same way.
	// `blinkLed`'s default blink on the phase every normal blink shares, restarted at the open
	// (`context_menu.cpp:54`).
	if (blink_restart_) {
		blink_restart_ = false;
		f.restart(deluge::ui::BlinkRate::Normal);
	}
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back),
	            deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal));
	if (descriptor(id_).for_saving) {
		(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Save),
		            deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal));
		(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Load),
		            deluge::ui::LedState{deluge::ui::LedState::Kind::Off, 0, 0});
	}
}

Result ContextMenuScene::handle(const UiEvent& event, const UiContext& ctx) {
	const Descriptor& desc = descriptor(id_);
	const bool button_or_pad = event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton
	                           || event.kind == UiEvent::Kind::Pad;
	if (button_or_pad && desc.busy != nullptr && desc.busy(ctx_)) {
		Result r;
		r.handled = true;
		return r;
	}
	refresh_available();
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
			// answer `SAVE`/`LOAD` instead; both are file-browser menus the port does not have,
			// so `Descriptor` carries no `accept_button` field yet. Adding one means one extra
			// comparison here, nothing more.
			return accept_current_option();
		}
		break;

	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::EncoderRole::Select) {
			// `ContextMenu::selectEncoderAction` -> `renderUIsForOled()`. `option_window` is
			// render-only (it claims nothing), so the turn arrives here, before the tick's render.
			const Selection before = sel_;
			const Descriptor& d = descriptor(id_);
			sel_ = step(sel_, event.delta, available_count_);
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
		if (desc.pad != nullptr) {
			return desc.pad(event, ctx);
		}
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

	// The MIDI kinds (the transport in `shell/scene.h`) are declined here for the reason
	// every scene declines them: claiming one means answering `learnable_at(pad)`,
	// which is the timeline scenes' work. Declining lets `Router::run_chain` carry the
	// message on to `services::MidiLearn`, which is where legacy's `view.` fallback puts it.
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
	case UiEvent::Kind::MidiProgramChange:
	case UiEvent::Kind::MidiPitchBend:
	case UiEvent::Kind::AllReleased:
		break;
	}
	return Result::unhandled();
}

Result ContextMenuScene::accept_current_option() {
	Result r;
	r.handled = true;
	if (available_count_ == 0) {
		return r;
	}
	if (descriptor(id_).accept(ctx_, available_[sel_.option])) {
		// "If returns false, will cause UI to exit" -- so a true keeps the menu open: the sample browser's menus,
		// whose load closes them once it ends.
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}
	return close();
}

void ContextMenuScene::refresh_available() {
	const Descriptor& d = descriptor(id_);
	if (d.options_now != nullptr) {
		available_count_ = static_cast<uint8_t>(std::min(d.options_now(ctx_, available_), d.options.size()));
	}
	else {
		available_count_ = static_cast<uint8_t>(std::min(d.options.size(), kMaxOptions));
		for (uint8_t i = 0; i < available_count_; i++) {
			available_[i] = i;
		}
	}
	if (available_count_ != 0 && sel_.option >= available_count_) {
		sel_.option = static_cast<uint8_t>(available_count_ - 1);
	}
	sel_.scroll = std::min(sel_.scroll, sel_.option);
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
