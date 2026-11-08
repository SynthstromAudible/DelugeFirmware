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
/// @brief The `Descriptor` table and the scene. See `browser_scene.h`.

#include "scenes/browser/browser_scene.h"

#include "notify/notice_queue.h"
#include "ui/core/blink.h"
#include "ui/core/metrics.h"
#include "ui/grid/favourites.h"
#include "ui/grid/rows.h"
#include "ui/oled/misc.h"
#include "ui/oled/scroll_text.h"
#include "ui/oled/text.h"

#include "gfx/oled/icons.h"

#include "gui/l10n/strings.h"

#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "instruments/deluge/sound_state.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/bind_host.h"
#include "scenes/recorder/recorder_scene.h"
#include "scenes/services/waveform_paint.h"
#include "scenes/slicing/slicer_scene.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>

namespace deluge::scenes::browser {

using deluge::l10n::String;
using deluge::storage::FileCatalogue;
using deluge::ui::Highlighting;
using deluge::ui::kFlashTime;
using deluge::ui::kTextSpacingX;
using deluge::ui::kTextSpacingY;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using gfx::oled::Rect;

namespace {

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// Small local helpers. `util/` is outside this layer's include allowance
// (`check_layer_rules.py`'s `scenes-includes-outside`), so the handful of things the legacy
// browser borrows from it are spelled out here rather than included -- the same call
// `rename_scene.cpp` and `listing.cpp` both already made.

/// @brief `charCaseEqual` (`util/functions.cpp`): equal ignoring ASCII letter case.
bool char_case_equal(char a, char b) {
	const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a + 32) : a;
	const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b + 32) : b;
	return la == lb;
}

bool is_ascii_letter(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// @brief `memcasecmp` == 0 over the first @p n characters (`util/functions.cpp`).
/// @param a First string.
/// @param b Second string.
/// @param n How many characters to compare.
/// @return True when @p a is at least @p n long and matches @p b's first @p n, ignoring case.
bool case_prefix_equal(std::string_view a, std::string_view b, size_t n) {
	if (a.size() < n || b.size() < n) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		if (!char_case_equal(a[i], b[i])) {
			return false;
		}
	}
	return true;
}

/// @brief `deluge::string::caselessEquals` (`util/string.h`), re-spelled locally as `listing.cpp`
///        already does for the same reason.
bool caseless_equals(std::string_view a, std::string_view b) {
	return a.size() == b.size() && case_prefix_equal(a, b, a.size());
}

/// @brief What `Browser::renderOLED` draws of a row's name.
///
/// Legacy takes `strrchr(displayName, finalChar)` with `finalChar = isFolder ? 0 : '.'`, and falls
/// back to `finalChar = 0` when there is no dot at all (`browser.cpp:981-989`) -- so a folder shows
/// its whole name, a file shows everything before its **last** dot, and an extensionless file shows
/// its whole name too.
/// @param name     The entry's `FileItem::filename`.
/// @param is_folder Whether the entry is a directory.
/// @return The part of @p name legacy inks.
std::string_view row_label(std::string_view name, bool is_folder) {
	if (is_folder) {
		return name;
	}
	const size_t dot = name.rfind('.');
	return dot == std::string_view::npos ? name : name.substr(0, dot);
}

// ---------------------------------------------------------------------------------------------
// The three descriptors. Each block is one legacy `Browser` subclass, virtual for virtual.

/// @brief `SampleBrowser::allowedFileExtensionsAudio` (`sample_browser.cpp:86`).
char const* kAudioExtensions[] = {"WAV", "AIFF", "AIF", nullptr};

// --- Sample: `SampleBrowser` (`gui/ui/browser/sample_browser.cpp`) ---

/// @brief The file `SoundEditor::getCurrentAudioFileHolder` holds, as the
///        record names it: an audio clip's own, or the selected range's.
/// @param ctx What the browser was opened over.
/// @return The path (empty for a holder with no file), or nullopt when neither arm resolves.
std::optional<std::string> sample_path(const menu::MenuContext& ctx) {
	if (ctx.model == nullptr) {
		return std::nullopt;
	}
	if (ctx.is_audio_clip) {
		if (!ctx.addresses.clip) {
			return std::nullopt;
		}
		const auto& clips = ctx.model->audio_clip_structure.by_id;
		const auto found = clips.find(ctx.addresses.clip->clip_id);
		return found != clips.end() ? std::optional{found->second.file_path} : std::nullopt;
	}
	if (ctx.facts == nullptr || !ctx.addresses.slot || ctx.range_index < 0 || ctx.source_index < 0
	    || ctx.source_index >= kNumSources) {
		return std::nullopt;
	}
	const deluge::instrument::SoundState* state = ctx.facts->sound_state(*ctx.addresses.slot);
	if (state == nullptr) {
		return std::nullopt;
	}
	const auto& ranges = state->sources[ctx.source_index].ranges;
	return static_cast<size_t>(ctx.range_index) < ranges.size()
	           ? std::optional{ranges[static_cast<size_t>(ctx.range_index)].file_path}
	           : std::nullopt;
}

bool sample_can(const menu::MenuContext& ctx) {
	// `SampleBrowser::opened` has no refusal of its own: it walks straight into
	// `soundEditor.getCurrentAudioFileHolder()->filePath` (in `SampleBrowser::opened`). Declining to
	// open is the survivable spelling of that dereference, exactly as `rename::Target::can` is for
	// `RenameDrumUI`'s `FREEZE_WITH_ERROR`.
	return sample_path(ctx).has_value();
}

void sample_start_at(const menu::MenuContext& ctx, std::string& dir, std::string& name) {
	// `SampleBrowser::opened` (`sample_browser.cpp:136-171`): take the holder's path; one that is blank or outside
	// SAMPLES/ gives way to the file last previewed or loaded (`SampleBrowser::lastFilePathLoaded`, which the host
	// answers), and only when that is blank too to the default folder. Then dissect it into a directory and the
	// filename to select.
	const std::optional<std::string> held = sample_path(ctx);
	std::string last;
	std::string_view path = held ? std::string_view{*held} : std::string_view{};
	if (path.empty() || !case_prefix_equal(path, "SAMPLES/", 8)) {
		last = menu::host_last_sample_path();
		path = last;
		if (path.empty()) {
			return; // dir stays empty -> `Descriptor::default_dir`
		}
	}
	const size_t slash = path.rfind('/');
	if (slash == std::string_view::npos) {
		name = path;
	}
	else {
		name = path.substr(slash + 1);
		dir = path.substr(0, slash);
	}
}

/// The way a listing came into its folder, as `arrivedInNewFolder`'s direction: into a folder 1, up one -1, else 0.
int8_t entry_direction(ListingAction action) {
	switch (action) {
	case ListingAction::IntoFolder:
		return 1;
	case ListingAction::UpLevel:
		return -1;
	default:
		return 0;
	}
}

uint16_t sample_folder_refusal(std::string_view dir, std::string_view name) {
	// "Don't allow user to go into TEMP clips folder" (`sample_browser.cpp:346-351`).
	if (caseless_equals(name, "TEMP") && caseless_equals(dir, "SAMPLES/CLIPS")) {
		return sid(String::STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED);
	}
	return 0;
}

AcceptOutcome sample_accept(const menu::MenuContext& ctx, std::string_view path, Error& error) {
	(void)ctx;
	(void)path;
	(void)error;
	// See `Descriptor::accept`: `SampleBrowser::enterKeyPress`'s file arm is `claimCurrentFile()`, which names
	// `soundEditor`, `AudioEngine`, `slicer`, `PadLEDs` and `instrumentClipView` and loads with the browser up under
	// the working animation. The scene parks the path and waits; the application's dispatcher runs the claim against
	// the legacy object, and the claim's completion answers `deferred_load_done`.
	return AcceptOutcome::DeferAndWait;
}

// --- MIDI device definition: `LoadMidiDeviceDefinitionUI`
//     (`gui/ui/load/load_midi_device_definition_ui.cpp`) ---

/// @brief `MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER` (`load_midi_device_definition_ui.cpp`).
constexpr std::string_view kMidiDeviceDir = "MIDI_DEVICES/DEFINITION";

/// @brief The MIDI track's remembered device definition file, as its record holds it, or nullopt off a MIDI track.
std::optional<std::string> midi_device_file(const menu::MenuContext& ctx) {
	if (ctx.addresses.instrument != deluge::plugin::SettingContext::Instrument::MIDI || !ctx.addresses.track
	    || ctx.model == nullptr) {
		return std::nullopt;
	}
	const std::optional<deluge::serialization::Value> file = deluge::project::read_setting(
	    *ctx.model, {.group = deluge::project::SettingGroupId::INSTRUMENT_DEVICE, .owner = *ctx.addresses.track},
	    "deviceDefinitionFileName");
	return file ? file->str() : std::string{};
}

bool midi_device_can(const menu::MenuContext& ctx) {
	// `LoadMidiDeviceDefinitionUI::opened` (`:45-47`) refuses unless the current output is a MIDI
	// one. Its other conjunct, `getRootUI()->toClipMinder()`, is a UI read this layer may not make
	// (`scenes-no-root-ui`); the menu tree only reaches this item from a clip minder anyway, since
	// `midi_device_linked` hangs off the MIDI/CV root, so the surviving test is the one that
	// discriminates.
	return midi_device_file(ctx).has_value();
}

void midi_device_start_at(const menu::MenuContext& ctx, std::string& dir, std::string& name) {
	// `setupForLoadingMidiDeviceDefinition` (`:83-124`): with no remembered file, open on the
	// default folder; otherwise split the remembered path and select that file, extension included.
	const std::optional<std::string> file = midi_device_file(ctx);
	if (!file || file->empty()) {
		return; // dir stays empty -> `Descriptor::default_dir`
	}
	const std::string_view path = *file;
	const size_t slash = path.rfind('/');
	if (slash == std::string_view::npos) {
		return;
	}
	dir = path.substr(0, slash);
	name = path.substr(slash + 1);
	// Legacy appends ".XML" unconditionally (`load_midi_device_definition_ui.cpp:122-124`) to a name that already
	// carries it, so the search never matches and the browser opens on the folder's first entry. Kept as legacy has
	// it.
	name += ".XML";
}

AcceptOutcome midi_device_accept(const menu::MenuContext& ctx, std::string_view path, Error& error) {
	(void)path;
	// `LoadMidiDeviceDefinitionUI::enterKeyPress`'s file arm requests the definition's load from the
	// instrument load (`gui/ui/load/instrument_load.h`), which this layer may not name, and the browser
	// stays open, refusing input, until `definitionLoaded` closes it: the menu it returns to then draws
	// the definition linked. The scene parks the path, `gui::ui::dispatchShellChoices` requests the
	// load, and the load's completion answers `deferred_load_done`.
	if (!midi_device_file(ctx)) {
		error = Error::FILE_NOT_FOUND;
		return AcceptOutcome::Stay;
	}
	return AcceptOutcome::DeferAndWait;
}

// --- DX7 `.syx` banks: `DxSyxBrowser` (`gui/ui/browser/dx_browser.cpp`) ---

char const* kSyxExtensions[] = {"SYX", nullptr};

bool dx_syx_can(const menu::MenuContext& ctx) {
	// `DxSyxBrowser::opened` refuses nothing of its own: only "DX7 source" lists the row that opens it. A bank loads
	// into the sound the menu edits, so there has to be one.
	return ctx.addresses.slot.has_value();
}

void dx_syx_start_at(const menu::MenuContext& /*ctx*/, std::string& dir, std::string& name) {
	// `DxSyxBrowser::opened`: always `DX7`, on its first entry (`fileIndexSelected = 0`; no remembered name).
	dir = "DX7";
	name.clear();
}

AcceptOutcome dx_syx_accept(const menu::MenuContext& /*ctx*/, std::string_view /*path*/, Error& /*error*/) {
	// `DxSyxBrowser::enterKeyPress`'s file arm reads the bank through `dxCartridge`, a `gui/` object this layer may
	// not name, and closes only once the read has parsed: the host reads it and answers `deferred_load_done`.
	return AcceptOutcome::DeferAndWait;
}

const std::array<Descriptor, static_cast<size_t>(BrowserId::Count)> kDescriptors{{
    // `SampleBrowser::SampleBrowser` (`sample_browser.cpp:88-95`) plus `SampleBrowser::opened`
    // (`:110-113`): the wave icon, "Audio files", the audio extensions, note names interpreted, and
    // folders allowed to share a name with a file.
    {
        .title = "Audio files",
        .extensions = kAudioExtensions,
        .default_dir = "SAMPLES",
        .file_icon = &gfx::oled::icons::browserWave,
        .file_icon_pt2 = nullptr,
        .interpret_note_names = true,
        .allow_folders_sharing_name_with_file = true,
        .can = sample_can,
        .start_at = sample_start_at,
        .folder_refusal = sample_folder_refusal,
        .accept = sample_accept,
    },
    // `LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition` (`:84-88`): the MIDI plug
    // (two pieces), "Load midi device", and the base class's XML extensions
    // (`Browser::opened`, `browser.cpp:181`) with note names off and folder/file name sharing
    // disallowed (`:182`).
    {
        .title = "Load midi device",
        .extensions = allowedFileExtensionsXML,
        .default_dir = kMidiDeviceDir,
        .file_icon = &gfx::oled::icons::browserMidi,
        .file_icon_pt2 = &gfx::oled::icons::browserMidiEdge,
        .interpret_note_names = false,
        .allow_folders_sharing_name_with_file = false,
        .can = midi_device_can,
        .start_at = midi_device_start_at,
        .folder_refusal = nullptr,
        .accept = midi_device_accept,
    },
    // `DxSyxBrowser::DxSyxBrowser` and `opened`: the wave icon, "DX7 syx files", `.syx` only, note names off, and
    // folders allowed to share a name with a file.
    {
        .title = "DX7 syx files",
        .extensions = kSyxExtensions,
        .default_dir = "DX7",
        .file_icon = &gfx::oled::icons::browserWave,
        .file_icon_pt2 = nullptr,
        .interpret_note_names = false,
        .allow_folders_sharing_name_with_file = true,
        .can = dx_syx_can,
        .start_at = dx_syx_start_at,
        .folder_refusal = nullptr,
        .accept = dx_syx_accept,
    },
}};

/// @brief The entry the selection names, or null -- `Browser::getCurrentFileItem`.
/// @param index The selected index.
/// @return The entry, or null when nothing is selected or the index is out of range.
const FileItem* item_at(int32_t index) {
	if (index < 0 || index >= static_cast<int32_t>(FileCatalogue::fileItems.size())) {
		return nullptr;
	}
	return &FileCatalogue::fileItems[static_cast<size_t>(index)];
}

/// @brief One `Result` carrying a full repaint.
Result repaint_all() {
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	return r;
}

} // namespace

const Descriptor& descriptor(BrowserId id) {
	const size_t i = static_cast<size_t>(id);
	return kDescriptors[i < kDescriptors.size() ? i : 0];
}

// ---------------------------------------------------------------------------------------------

void BrowserScene::open(const menu::MenuContext& ctx) {
	ctx_ = ctx;
}

bool BrowserScene::on_enter(const SceneId& id) {
	load_blink_pending_ = false;
	const bool from_creator = std::exchange(drum_creator_next_, false);
	last_enter_refused_ = true; // until the enter has succeeded
	if (id.arg >= static_cast<uint16_t>(BrowserId::Count)) {
		return false;
	}
	id_ = static_cast<BrowserId>(id.arg);
	holding_ = false;
	hold_deadline_ms_.reset();
	long_press_done_.reset();
	awaiting_load_ = false;
	awaiting_deferred_ = false;
	deferred_done_.reset();
	slice_out_ = false;
	slice_ready_.reset();
	slice_from_shift_ = false;
	if (!descriptor(id_).can(ctx_)) {
		return false;
	}

	entered_len_ = 0;
	file_index_ = -1;
	scroll_ = 0;
	edit_ = deluge::ui::oled::TextEditState{};
	kb_ = deluge::ui::grid::TextKeyboardState{};
	resp_ = {};
	reveal_press_.reset();
	pending_.reset();
	choice_.reset();
	searching_ = false;
	select_after_listing_.clear();
	// `SampleBrowser::opened`: no keyboard and no preview yet, so the root view shows and the BROWSE pad blinks. The
	// MIDI device definition browser shows its keyboard for good (`qwertyAlwaysVisible`).
	pads_ = id_ == BrowserId::Sample ? BrowserPads::Root : BrowserPads::Keyboard;
	keyboard_visible_ = id_ != BrowserId::Sample;
	keys_drawn_ = keyboard_visible_;
	keyboard_always_visible_ = id_ != BrowserId::Sample;
	showing_preview_ = false;
	auto_load_ = false;
	xenc_held_ = false;
	preview_path_.clear();
	preview_direction_ = 0;
	row_scroll_ = deluge::ui::ScrollText{};
	pad_scroll_.reset();
	waveform_scroll_ = 0;
	nav_identity_ = 0;
	pads_changed_ = false;
	// The file selector opens the browser inside `beginScreen`, whose `updatePadLights` arms the item's own blink after
	// `openUI` returns: over the browser's, and after the open's first preview. The drum creator has no `beginScreen`.
	browse_blink_ =
	    id_ != BrowserId::Sample ? BrowseBlink::Off : (from_creator ? BrowseBlink::Browser : BrowseBlink::Menu);
	blink_anchor_ms_.reset();
	blink_fired_step_ = UINT32_MAX;
	first_preview_ = true;
	spare_blink_from_preview_ = false;
	// `keyboardChars[util::to_underlying(FlashStorage::keyboardLayout)]` (`QwertyUI`'s `keyboardChars`).
	// `KeyboardLayoutKind`'s values match legacy's enum exactly, so this is a cast (`keyboard.h`).
	layout_ = static_cast<deluge::ui::grid::KeyboardLayoutKind>(static_cast<uint8_t>(FlashStorage::keyboardLayout));
	cursor_blink_ = deluge::ui::Blink{};

	favourite_resp_ = {};
	bank_resp_ = {};
	favourite_flash_anchor_ms_.reset();
	back_blink_restart_ = true;
	if (id_ == BrowserId::Sample) {
		favourites().set_category("SAMPLES"); // `SampleBrowser::opened`
	}

	// `LoadUI::focusRegained`, the last step of a definition browser's successful `opened`.
	load_blink_pending_ = id_ == BrowserId::MidiDeviceDefinition;
	request_open();
	last_enter_refused_ = false;
	return true;
}

void BrowserScene::on_exit() {
	// `Browser::close()` empties the catalogue. Collect first: an uncollected
	// result still holds `FileCatalogue`'s claim, and leaving it held would refuse every later
	// listing -- this layer's or a legacy browser's -- for the life of the process.
	//
	// A listing that is still *in flight* cannot be collected and is deliberately not waited for:
	// the seam has no cancel. Nothing the user can do reaches here in that state -- `handle()`
	// swallows button and encoder events while `seam_busy()`, and the keyboard's pad events are
	// gated by `after_render`'s own early returns -- so the only way in is an external `Pop` or
	// `Remove`. (This scene deliberately does not raise the shell's input lock: that lock *drops*
	// events where legacy's SD-busy gate defers them, and it cannot be released from `on_exit`
	// anyway. See the warning on `Action` in `shell/scene.h`.) The seam then lands, sits `Ready`,
	// and the next `on_enter`'s
	// `request_open` is refused as `ResultPending` until `after_render` drains it -- a visible jam
	// rather than a corrupted vector, which is the trade `listing.h` documents.
	(void)listing_.take_result();
	pending_.reset();
	// `SampleBrowser::exitAction` and `exitAndNeverDeleteDrum` stop the preview; nothing is left to preview.
	if (id_ == BrowserId::Sample) {
		preview_ = PreviewRequest{.id = id_};
		preview_outstanding_ = true;
		preview_path_.clear();
		preview_direction_ = 0;
		favourites().close(); // `Browser::close`
	}
	xenc_held_ = false;
	pad_scroll_.reset();
	waveform_scroll_ = 0;
	// A load still out is the host's to cancel (`DxSyxBrowser::close`): it sees no browser waiting.
	awaiting_deferred_ = false;
	deferred_done_.reset();
	FileCatalogue::emptyFileItems();
	// Fix the same hazard `RenameScene::on_exit` records: `open()` is the only writer of `ctx_`, and
	// this scene is a singleton that outlives every visit, so a re-entry that forgot its `open()`
	// must not validate `can()` against the previous song's pointers. A zeroed context fails every
	// descriptor's `can`, and `on_enter` refuses.
	ctx_ = menu::MenuContext{};
	grid_pass_ = false;
	searching_ = false;
	select_after_listing_.clear();
	browse_blink_ = BrowseBlink::Off;
}

std::string_view BrowserScene::current_dir() const {
	return FileCatalogue::currentDir;
}

std::optional<BrowserChoice> BrowserScene::take_choice() {
	std::optional<BrowserChoice> out = std::move(choice_);
	choice_.reset();
	return out;
}

std::optional<ScrollRequest> BrowserScene::take_scroll() {
	std::optional<ScrollRequest> out = pad_scroll_;
	pad_scroll_.reset();
	return out;
}

std::optional<PreviewRequest> BrowserScene::take_preview() {
	std::optional<PreviewRequest> out = std::move(preview_);
	preview_.reset();
	return out;
}

void BrowserScene::preview_settled(bool has_sample) {
	preview_outstanding_ = false;
	if (id_ != BrowserId::Sample) {
		return;
	}
	const int8_t direction = preview_direction_;
	const bool spare_blink = std::exchange(spare_blink_from_preview_, false);
	// `SampleBrowser::tickPreview`'s auto-load: a landed file is claimed as Select would claim it, keeping the
	// browser open. A choice already waiting (a press this tick) is not overwritten.
	if (auto_load_ && !ctx_.is_audio_clip && !preview_path_.empty() && !choice_.has_value()) {
		choice_ = BrowserChoice{.id = id_, .path = preview_path_, .op = BrowserChoice::Op::Enter, .keep_open = true};
	}
	const BrowserPads before = pads_;
	if (has_sample) {
		// `showPreview`'s `unsetTimer(SHORTCUT_BLINK)`, except for the open's own first preview.
		if (!spare_blink) {
			browse_blink_ = BrowseBlink::Off;
		}
		// `showPreview`: a move scrolls the waveform in unless the keyboard is always up; otherwise the keys are drawn
		// if they should be and are not, or the waveform if the keyboard is hidden.
		showing_preview_ = true;
		nav_identity_ = 0; // `showPreview` reopens the navigator: every landed preview opens zoomed out
		if (direction != 0 && !keyboard_always_visible_) {
			pads_ = BrowserPads::Waveform;
			pad_scroll_.reset();
			waveform_scroll_ = direction;
		}
		else {
			if ((keyboard_visible_ && !keys_drawn_) || keyboard_always_visible_) {
				pads_ = BrowserPads::Keyboard;
			}
			else if (!keyboard_visible_) {
				pads_ = BrowserPads::Waveform;
			}
			keys_drawn_ = keyboard_visible_;
		}
	}
	else if ((showing_preview_ || (keys_drawn_ && !keyboard_visible_)) && !keyboard_always_visible_) {
		// `clearPreviewDisplay`: a move scrolls the root view in; otherwise it shows unless the keyboard still claims
		// the pads. Its BROWSE pad blinks again (`possiblySetUpBlinking`).
		showing_preview_ = false;
		keys_drawn_ = keyboard_visible_;
		if (direction != 0 || !keyboard_visible_) {
			pads_ = BrowserPads::Root;
		}
		if (direction != 0) {
			waveform_scroll_ = 0;
			pad_scroll_ = ScrollRequest{.direction = direction, .to_root = true};
		}
		// `possiblySetUpBlinking`: the browser's own blink, unless the keyboard is up.
		if (!keyboard_visible_) {
			browse_blink_ = BrowseBlink::Browser;
			blink_anchor_ms_.reset();
		}
	}
	if (pads_ != before) {
		pads_changed_ = true;
	}
}

void BrowserScene::file_preview(int8_t direction) {
	if (id_ != BrowserId::Sample) {
		return;
	}
	spare_blink_from_preview_ = std::exchange(first_preview_, false) && browse_blink_ == BrowseBlink::Menu;
	// `SampleBrowser::previewIfPossible`: a folder or no selection previews nothing, and stops what was previewing.
	PreviewRequest request{.id = id_, .direction = direction};
	const FileItem* item = item_at(file_index_);
	if (item != nullptr && !item->isFolder) {
		const std::string_view dir = current_dir();
		request.path = dir.empty() ? item->filename : std::string{dir} + "/" + item->filename;
	}
	preview_path_ = request.path;
	preview_direction_ = direction;
	preview_ = std::move(request);
	preview_outstanding_ = true;
}

void BrowserScene::arrive_in_new_folder() {
	// `Browser::arrivedInNewFolder`: every listing of a folder hides the keyboard unless it is always up.
	if (!keyboard_always_visible_) {
		keyboard_visible_ = false;
	}
}

void BrowserScene::reveal_keyboard() {
	keyboard_visible_ = true;
	keys_drawn_ = true;
	pads_ = BrowserPads::Keyboard;
	waveform_scroll_ = 0;               // the keys replace a waveform still to scroll in
	favourite_flash_anchor_ms_.reset(); // `drawKeys` draws the favourites afresh
}

services::Favourites& BrowserScene::favourites() {
	return favourites_override_ != nullptr ? *favourites_override_ : menu::host_favourites();
}

bool BrowserScene::favourites_visible() const {
	return id_ == BrowserId::Sample && keyboard_visible_
	       && FlashStorage::defaultFavouritesLayout != FavouritesDefaultLayoutOff;
}

bool BrowserScene::banks_visible() const {
	return id_ == BrowserId::Sample && keyboard_visible_
	       && FlashStorage::defaultFavouritesLayout == FavouritesDefaultLayoutFavoritesAndBanks;
}

int32_t BrowserScene::favourite_row() const {
	// `QwertyUI::opened`: row 7, or 6 with the bank row beneath it.
	return FlashStorage::defaultFavouritesLayout == FavouritesDefaultLayoutFavoritesAndBanks ? kFavouriteRow - 1
	                                                                                         : kFavouriteRow;
}

void BrowserScene::favourites_changed() {
	favourite_flash_anchor_ms_.reset();
	pads_changed_ = true;
}

Result BrowserScene::favourite_press(uint8_t position) {
	Result r;
	r.handled = true;
	const auto popup = [&r](String text) {
		(void)r.actions.push_back(Action{
		    .kind = Action::Kind::Popup, .surface = SurfaceRole::Main, .popup_string = static_cast<uint16_t>(text)});
	};
	// A bank still arriving, or one that could not be read, may be neither edited nor recalled from.
	switch (favourites().bank_state()) {
	case services::FavouritesBank::Loading:
		return r;
	case services::FavouritesBank::Unavailable:
		popup(String::STRING_FOR_SD_CARD_ERROR);
		return r;
	case services::FavouritesBank::Loaded:
		break;
	}
	if (shift_) {
		const FileItem* item = item_at(file_index_);
		if (favourites().is_empty(position)) {
			if (item != nullptr && !item->isFolder) {
				const std::string_view dir = current_dir();
				favourites().set(position, dir.empty() ? item->filename : std::string{dir} + "/" + item->filename);
				favourites_changed();
			}
		}
		else {
			favourites().unset(position);
			favourites_changed();
		}
		return r;
	}
	const std::string path = favourites().recall(position);
	favourites_changed();
	if (path.empty()) {
		popup(String::STRING_FOR_FAVOURITES_EMPTY);
		return r;
	}
	// `setFileByFullPath`: list the favourite's folder and land on it.
	entered_len_ = 0;
	edit_ = deluge::ui::oled::TextEditState{};
	arrive_in_new_folder();
	ListingRequest req = make_request(ListingAction::ByPath);
	req.path = path;
	file_request(std::move(req));
	(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	return r;
}

void BrowserScene::render_favourites(deluge::ui::grid::GridFrame& f) {
	// `QwertyUI::renderFavourites`, then `timerCallback`'s flash of the current favourite and bank every 500 ms from
	// the draw.
	const std::array<std::optional<uint8_t>, deluge::ui::grid::kFavourites> colours = favourites().colours();
	const std::array<RGB, deluge::ui::grid::kFavourites> row = deluge::ui::grid::favourite_row_colours(colours);
	favourite_resp_ = deluge::ui::grid::row(f, favourite_row(), row);
	if (FlashStorage::defaultFavouritesLayout == FavouritesDefaultLayoutFavoritesAndBanks) {
		const std::array<RGB, deluge::ui::grid::kFavourites> banks = deluge::ui::grid::favourite_bank_colours();
		bank_resp_ = deluge::ui::grid::row(f, kFavouriteRow, banks);
	}
	if (!favourites_visible()) {
		favourite_flash_anchor_ms_.reset();
		return;
	}
	const uint32_t now = f.now_ms();
	if (!favourite_flash_anchor_ms_.has_value()) {
		favourite_flash_anchor_ms_ = now;
		favourite_flash_step_ = 0;
	}
	const uint32_t step = (now - *favourite_flash_anchor_ms_) / kFavouriteFlashMs;
	if (step != favourite_flash_step_) {
		favourite_flash_step_ = step;
		if (const std::optional<uint8_t> current = favourites().current_favourite()) {
			f.flash(*current, favourite_row(), deluge::ui::grid::FlashColour::Default);
		}
		if (banks_visible()) {
			f.flash(favourites().current_bank(), kFavouriteRow, deluge::ui::grid::FlashColour::Default);
		}
	}
	f.grid_gate().request_repaint_after(*favourite_flash_anchor_ms_ + (step + 1) * kFavouriteFlashMs - now, now);
}

// ---------------------------------------------------------------------------------------------
// Listings

ListingRequest BrowserScene::make_request(ListingAction action) const {
	const Descriptor& d = descriptor(id_);
	ListingRequest req;
	req.action = action;
	req.allowed_extensions = d.extensions;
	req.allow_folders = true; // `Browser::arrivedInNewFolder` passes `allowFolders = true`
	req.allow_folders_sharing_name_with_file = d.allow_folders_sharing_name_with_file;
	req.interpret_note_names = d.interpret_note_names;
	req.default_dir = d.default_dir;
	return req;
}

void BrowserScene::request_open() {
	const Descriptor& d = descriptor(id_);
	std::string dir;
	std::string name;
	d.start_at(ctx_, dir, name);
	if (dir.empty()) {
		dir = d.default_dir;
	}
	ListingRequest req = make_request(ListingAction::Open);
	req.path = std::move(dir);
	req.filename_to_start_at = name;
	select_after_listing_ = std::move(name);
	file_request(std::move(req));
}

void BrowserScene::file_request(ListingRequest req) {
	if (listing_.request(req) != RequestOutcome::Accepted) {
		// `Busy` (something else holds the catalogue), `ResultPending` (this seam owes a collect) or
		// `Dropped` (the owner's queue was full). None of the three loses the request: it is kept and
		// retried from `after_render`, which is legacy's own answer on the same return
		// (`browser.cpp:113-117`: clear the flag so the next event retries).
		pending_ = std::move(req);
		return;
	}
	pending_.reset();
}

Result BrowserScene::apply(const ListingResult& out) {
	// `runPendingListing`'s failure arm -> `Browser::onListingFailed`:
	// `displayError` (already raised by the seam, which is why nothing is announced here) then
	// `close()`. Reload is legacy's one silent exception (`browser.cpp:747-753`): it empties the
	// catalogue and leaves the browser standing.
	if (out.error != Error::NONE) {
		if (out.action != ListingAction::Reload) {
			return close();
		}
		entered_len_ = 0;
		file_index_ = -1;
		scroll_ = 0;
		edit_ = deluge::ui::oled::TextEditState{};
		searching_ = false;
		return repaint_all();
	}

	// The second pass of `predictExtendedText`'s `doNewRead` arm (`browser.cpp:826-838`), one tick
	// later: the folder has been re-read around the typed text, so run the search again.
	if (searching_) {
		searching_ = false;
		if (search_for_entered_text() != SearchOutcome::Found) {
			// `notFound` with `mayDefaultToBrandNewNameOnEntry` false (`:842-846`): put the selected
			// file's name back, and undo the cursor advance the keystroke made
			// (`qwerty_ui.cpp:357-359`). This is the same pair `run_search_after_typing` applies on
			// the synchronous miss; the only difference is that the cursor had to be carried across
			// the re-read in `search_revert_cursor_`.
			if (file_index_ >= 0) {
				set_entered_text_from_current_filename();
			}
			edit_.cursor = static_cast<int16_t>(search_revert_cursor_);
		}
		clamp(visible_rows_);
		return repaint_all();
	}

	if (out.action == ListingAction::Reload) {
		// `reloadImpl`'s tail (`browser.cpp:752-772`).
		const int32_t count = static_cast<int32_t>(FileCatalogue::fileItems.size());
		int32_t index;
		if (reload_from_end_) {
			index = (reload_direction_ == CATALOG_SEARCH_LEFT) ? count - 1 : 0;
		}
		else {
			FileCatalogue::repinComparator();
			index = FileCatalogue::searchFileItems(std::string{entered_text()}.c_str()) + reload_offset_;
		}
		finish_select(index, reload_offset_);
		return repaint_all();
	}

	// `arrivedInNewFolder`'s post-read tail (`browser.cpp:389-495`).
	edit_.cursor = 0;
	edit_.scroll = 0;

	std::string name = std::move(select_after_listing_);
	select_after_listing_.clear();
	// `UpLevel` fills its own: legacy assigns the folder it just left to `enteredText` and lists
	// starting at it (`browser.cpp:1388-1392`), so the entry to land on is the one you came out of.
	if (name.empty()) {
		name = out.entered_text;
	}

	const int32_t count = static_cast<int32_t>(FileCatalogue::fileItems.size());
	if (count == 0) {
		// "Or if no files found at all..." with `mayDefaultToBrandNewNameOnEntry` false
		// (`browser.cpp:483-490`): no text, no selection, top of the list.
		entered_len_ = 0;
		file_index_ = -1;
		scroll_ = 0;
		file_preview(entry_direction(out.action));
		return repaint_all();
	}

	file_index_ = 0;
	if (!name.empty()) {
		bool exact = false;
		FileCatalogue::repinComparator();
		const int32_t i = FileCatalogue::searchFileItems(name.c_str(), &exact);
		if (exact) {
			file_index_ = i;
		}
		else if (FileCatalogue::numFileItemsDeletedAtStart != 0) {
			// `noExactFileFound`'s re-read (`browser.cpp:404-407`): the search string culled entries
			// off the start of the folder and we are about to fall back to "the first one", so the
			// window has to be read again without it. Legacy `goto`s; here it is another listing.
			ListingRequest req = make_request(ListingAction::Open);
			file_request(std::move(req));
			return repaint_all();
		}
	}

	set_entered_text_from_current_filename();
	scroll_ = file_index_;
	// "Starting in the middle or end of a short folder should still fill as many display rows as
	// possible" (`browser.cpp:415-416`).
	clamp(visible_rows_);
	// `folderContentsReady(direction)`, which previews the entry the listing landed on.
	file_preview(entry_direction(out.action));
	return repaint_all();
}

// ---------------------------------------------------------------------------------------------
// Selection and text

void BrowserScene::clamp(int32_t visible_rows, bool allow_no_file_selection) {
	// `Browser::clampFileSelectionAndScroll` (`browser.cpp:1009-1063`), statement for statement.
	const int32_t count = static_cast<int32_t>(FileCatalogue::fileItems.size());
	if (count <= 0) {
		file_index_ = -1;
		scroll_ = 0;
		return;
	}
	if (file_index_ >= count) {
		file_index_ = count - 1;
	}
	else if (file_index_ < 0) {
		file_index_ = allow_no_file_selection ? -1 : 0;
	}

	const int32_t rows = visible_rows < 1 ? 1 : visible_rows;
	int32_t last_allowed_scroll = count - rows;
	if (last_allowed_scroll < 0) {
		last_allowed_scroll = 0;
	}

	if (file_index_ == -1) {
		scroll_ = 0;
		return;
	}
	if (scroll_ > file_index_) {
		scroll_ = file_index_;
	}
	else if (scroll_ < file_index_ - rows + 1) {
		scroll_ = file_index_ - rows + 1;
	}
	if (scroll_ > last_allowed_scroll) {
		scroll_ = last_allowed_scroll;
	}
	if (scroll_ < 0) {
		scroll_ = 0;
	}
}

void BrowserScene::set_entered_text(std::string_view s) {
	entered_len_ = static_cast<uint16_t>(std::min(s.size(), kMaxEnteredChars));
	std::copy_n(s.data(), entered_len_, entered_);
}

void BrowserScene::set_entered_text_from_current_filename() {
	// `Browser::setEnteredTextFromCurrentFilename`: the entry's display
	// name, with the extension cut off for a file.
	const FileItem* item = item_at(file_index_);
	if (item == nullptr) {
		entered_len_ = 0;
		return;
	}
	set_entered_text(row_label(item->displayName(), item->isFolder));
}

BrowserScene::SearchOutcome BrowserScene::search_for_entered_text() {
	// The in-memory half of `Browser::predictExtendedText`. Legacy searches
	// for the text **left of the cursor**, not the whole string (`:800`, `searchString.resize
	// (enteredTextEditPos)`), which is what makes typing over a prediction search for the prefix
	// rather than for the prediction.
	//
	// Re-pin the name comparator before searching, exactly as legacy does at `:785-786`, through
	// `FileCatalogue::repinComparator` rather than by naming `util/name_compare.h`'s globals --
	// `util/**` is outside this layer's include allowance (`scenes-includes-outside`). It restores
	// what the last `sortFileItems` set, which is the only pinning under which `fileItems`' order is
	// meaningful; relying on the last sort's pinning surviving until here would be a convention rather than a
	// mechanism.
	FileCatalogue::repinComparator();
	const size_t n = static_cast<size_t>(std::max<int32_t>(edit_.cursor, 0));
	const std::string search{entered_, std::min<size_t>(n, entered_len_)};
	const int32_t count = static_cast<int32_t>(FileCatalogue::fileItems.size());

	const int32_t i = FileCatalogue::searchFileItems(search.c_str());
	// "If that search takes us off the right-hand end of the list..." (`:822`): the cached window
	// cannot answer, so the folder has to be read again around the search string.
	if (i >= count) {
		return SearchOutcome::NeedsRead;
	}

	const FileItem& item = FileCatalogue::fileItems[static_cast<size_t>(i)];
	if (!case_prefix_equal(item.displayName(), search, search.size())) {
		// "If the search landed on the first cached item, the folder cache may be missing earlier
		// entries" (`:860-864`, issue #4584).
		if (i == 0) {
			return SearchOutcome::NeedsRead;
		}
		// Legacy's remaining arm appends up to four '0's to the search string and tries again
		// (`:868-874`), a slot-browser behaviour whose comment says "this code is original but I
		// don't know what it does". None of this scene's three descriptors is a slot browser --
		// each was split off `Browser` directly, and `filePrefix` is null for all three -- so the arm is
		// deliberately not ported rather than transcribed unexercised.
		return SearchOutcome::NotFound;
	}

	const bool moved = i != file_index_;
	file_index_ = i;
	// "Typing/prediction can land on a cached item without needing to move the viewport unless it is
	// offscreen" (`:882-883`).
	clamp(visible_rows_);
	// Replaces the typed text with the found entry's name and leaves the cursor where the typing
	// left it -- which is what `text_edit` draws as the prediction highlight.
	set_entered_text_from_current_filename();
	// "If we're now on a different file than before, preview it" (`browser.cpp:1488-1491`).
	if (moved) {
		file_preview(0);
	}
	return SearchOutcome::Found;
}

void BrowserScene::run_search_after_typing(int32_t revert_cursor_to) {
	switch (search_for_entered_text()) {
	case SearchOutcome::Found:
		break;
	case SearchOutcome::NotFound:
		// `:842-846`, with `mayDefaultToBrandNewNameOnEntry` false for every descriptor: put the
		// selected file's name back and undo the cursor advance (`qwerty_ui.cpp:357-359`).
		if (file_index_ >= 0) {
			set_entered_text_from_current_filename();
		}
		edit_.cursor = static_cast<int16_t>(revert_cursor_to);
		break;
	case SearchOutcome::NeedsRead: {
		// Legacy loops synchronously (`:826-837`); the seam cannot, so the read is dispatched and
		// the search runs again when it lands. `CATALOG_SEARCH_BOTH` around the search string is
		// legacy's own direction for this read (`:830`).
		//
		// The cursor to fall back to travels with the request. Legacy decrements
		// `enteredTextEditPos` whenever `predictExtendedText()` answers false
		// (`qwerty_ui.cpp:357-359`) -- and that includes the `doNewRead -> doSearch -> notFound`
		// route, which here lands a whole tick later. Without carrying it, a typed character that
		// needs a re-read and then still finds nothing would leave the cursor one column past
		// legacy's: the prediction highlight starts a column late and the NEXT keystroke truncates
		// at the wrong index.
		search_revert_cursor_ = revert_cursor_to;
		ListingRequest req = make_request(ListingAction::Reload);
		req.filename_to_start_at = std::string{entered_, entered_len_};
		req.search_direction = CATALOG_SEARCH_BOTH;
		searching_ = true;
		file_request(std::move(req));
		break;
	}
	}
}

void BrowserScene::type_char(char c) {
	// `QwertyUI::padAction`'s "normal keys" arm (`qwerty_ui.cpp:254-369`), minus the four
	// `currentUIMode` guards (prediction-in-progress, preset-loading) that have no counterpart
	// here: this scene's equivalent of "the card is busy" is `Listing::in_flight()`, and `handle()`
	// has already refused the press in that state.
	const int32_t pos = edit_.cursor;

	// "First character must be alphanumerical" (`:273-280`).
	if (pos == 0 && !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
		return;
	}

	// "If this char was already predicted, that's easy and we can just move forward" (`:305-317`).
	if (pos < entered_len_ && char_case_equal(entered_[pos], c)) {
		edit_.cursor = static_cast<int16_t>(pos + 1);
		return;
	}

	char nc = c;
	// "If char is a letter... If following another letter, make it lowercase" (`:334-343`).
	if (nc >= 'A' && nc <= 'Z' && pos > 0 && is_ascii_letter(entered_[pos - 1])) {
		nc = static_cast<char>(nc + 32);
	}
	if (pos >= static_cast<int32_t>(kMaxEnteredChars)) {
		return; // the buffer's own ceiling; see `kMaxEnteredChars`
	}

	// `enteredText.resize(enteredTextEditPos); enteredText.append(...)` (`:350-353`): typing
	// mid-string truncates the tail rather than inserting.
	entered_len_ = static_cast<uint16_t>(pos);
	entered_[entered_len_++] = nc;
	edit_.cursor = static_cast<int16_t>(entered_len_);
	run_search_after_typing(pos);
}

void BrowserScene::backspace() {
	// `QwertyUI::processBackspace`, including its asymmetry: the edit
	// position steps back whether or not there is any text, and the string is only truncated -- and
	// the search only re-run -- when there is.
	if (edit_.cursor > 0) {
		edit_.cursor--;
	}
	if (entered_len_ != 0) {
		entered_len_ = static_cast<uint16_t>(edit_.cursor);
		run_search_after_typing(edit_.cursor);
	}
}

void BrowserScene::finish_select(int32_t new_index, int32_t offset) {
	// `Browser::finishSelectEncoderAction`.
	file_index_ = new_index;
	// "A fast turn may be delivered as a multi-file offset; after a folder-window re-read, that
	// offset can still overshoot."
	clamp(visible_rows_, /*allow_no_file_selection=*/false);
	if (file_index_ == -1) {
		return;
	}
	edit_.cursor = 0;
	edit_.scroll = 0;
	set_entered_text_from_current_filename();
	// `finishSelectEncoderAction` hides the keyboard unless it is always up, before `currentFileChanged` (given only
	// the move's sign) reads it: so the pads scroll out at once, while the next preview loads, only from a waveform.
	const int8_t direction = static_cast<int8_t>(offset > 0 ? 1 : (offset < 0 ? -1 : 0));
	if (!keyboard_always_visible_) {
		keyboard_visible_ = false;
	}
	if (direction != 0 && showing_preview_ && !keyboard_always_visible_) {
		browse_blink_ = BrowseBlink::Off; // `currentFileChanged`'s `unsetTimer`
		waveform_scroll_ = 0;
		pad_scroll_ = ScrollRequest{.direction = direction, .into_nothing = true};
	}
	file_preview(direction);
}

void BrowserScene::step_selection(int32_t offset) {
	// `Browser::selectEncoderAction`, the non-slot path.
	//
	// @note Legacy's shift/`numberEditPos` arm (`:576-629`) walks whole slot numbers -- "SONG12" ->
	//       "SONG13" -- and is guarded by `nameAfterPrefix`, which returns null unless the name
	//       starts with `filePrefix`. Every descriptor here leaves `filePrefix` null (only the
	//       slot-based load/save browsers set it), so legacy's own guard takes the `nonNumeric`
	//       branch for every name either of them can show, and the arm is not ported.
	const int32_t count = static_cast<int32_t>(FileCatalogue::fileItems.size());
	int32_t index;
	if (file_index_ < 0) {
		if (count == 0) {
			return;
		}
		FileCatalogue::repinComparator();
		index = FileCatalogue::searchFileItems(std::string{entered_text()}.c_str());
		if (offset < 0) {
			index--;
		}
	}
	else {
		index = file_index_ + offset;
	}

	// From here a reload of the folder window off the card may be needed; `apply()` runs the tail
	// once it lands, exactly as legacy's `reloadImpl` does on the fiber.
	if (index < 0) {
		if (FileCatalogue::numFileItemsDeletedAtStart != 0) {
			scroll_ = 9999;
			request_reload(/*from_end=*/false, 0, offset);
			return;
		}
		scroll_ = 0;
		if (FileCatalogue::numFileItemsDeletedAtEnd != 0) {
			request_reload(/*from_end=*/true, CATALOG_SEARCH_LEFT, offset);
			return;
		}
		index = count - 1;
	}
	else if (index >= count) {
		if (FileCatalogue::numFileItemsDeletedAtEnd != 0) {
			scroll_ = 0;
			request_reload(/*from_end=*/false, 0, offset);
			return;
		}
		scroll_ = 9999;
		if (FileCatalogue::numFileItemsDeletedAtStart != 0) {
			request_reload(/*from_end=*/true, CATALOG_SEARCH_RIGHT, offset);
			return;
		}
		index = 0;
	}
	finish_select(index, offset);
}

void BrowserScene::request_reload(bool from_end, int32_t direction, int32_t offset) {
	// Legacy's `pendingReloadSearchFromEnd_` / `pendingReloadCatalogSearchDirection_` pair
	// (`browser.h:177-178`), which exist for exactly this: the two facts `reloadImpl` needs and
	// `ListingRequest`'s generic fields do not carry.
	reload_from_end_ = from_end;
	reload_direction_ = direction;
	reload_offset_ = offset;
	ListingRequest req = make_request(ListingAction::Reload);
	if (from_end) {
		req.search_direction = direction;
	}
	else {
		req.filename_to_start_at = std::string{entered_text()};
		req.search_direction = CATALOG_SEARCH_BOTH;
	}
	file_request(std::move(req));
}

// ---------------------------------------------------------------------------------------------
// Navigation

Result BrowserScene::enter_key_press(bool shift) {
	const Descriptor& d = descriptor(id_);
	const FileItem* item = item_at(file_index_);
	if (item == nullptr) {
		// `SampleBrowser::enterKeyPress` announces
		// `Error::FILE_NOT_FOUND`; `LoadMidiDeviceDefinitionUI` (`:144-147`) returns silently. The
		// louder of the two is kept for both -- a select press that does nothing at all is the shape
		// of bug this stack has been bitten by twice.
		notify::notice_error_code(static_cast<int16_t>(Error::FILE_NOT_FOUND));
		Result r;
		r.handled = true;
		return r;
	}

	if (item->isFolder) {
		const uint16_t refusal = d.folder_refusal != nullptr ? d.folder_refusal(current_dir(), item->filename) : 0;
		if (refusal != 0) {
			Result r;
			r.handled = true;
			(void)r.actions.push_back(
			    Action{.kind = Action::Kind::Popup, .surface = SurfaceRole::Main, .popup_string = refusal});
			return r;
		}
		// `Browser::goIntoFolderImpl` clears the entered text before it lists.
		entered_len_ = 0;
		edit_ = deluge::ui::oled::TextEditState{};
		arrive_in_new_folder();
		ListingRequest req = make_request(ListingAction::IntoFolder);
		req.path = item->filename;
		file_request(std::move(req));
		return repaint_all();
	}

	// Shift+Select on a file in a kit's sample browser opens the slicer instead of claiming
	// (`SampleBrowser::enterKeyPress`, `sample_browser.cpp:390-401`); on anything else shift changes nothing.
	if (shift && id_ == BrowserId::Sample && ctx_.is_kit) {
		Result r;
		r.handled = true;
		if (!fresh_kit()) {
			(void)r.actions.push_back(Action{
			    .kind = Action::Kind::Popup,
			    .surface = SurfaceRole::Main,
			    .popup_string = static_cast<uint16_t>(String::STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT)});
			return r;
		}
		park_operation(BrowserChoice::Op::Slice);
		slice_from_shift_ = true;
		return r;
	}

	// `getCurrentFilePath()` (`SampleBrowser`, `LoadMidiDeviceDefinitionUI`):
	// the directory, a separator, and the entry's own filename with its extension.
	std::string path{current_dir()};
	if (!path.empty()) {
		path += "/";
	}
	path += item->filename;

	Error error = Error::NONE;
	switch (d.accept(ctx_, path, error)) {
	case AcceptOutcome::Close:
		return close();
	case AcceptOutcome::Stay: {
		notify::notice_error_code(static_cast<int16_t>(error));
		Result r;
		r.handled = true;
		return r;
	}
	case AcceptOutcome::DeferAndWait: {
		// One load at a time: each legacy browser's load refuses a second Enter until it ends.
		if (!awaiting_deferred_) {
			choice_ = BrowserChoice{id_, std::move(path)};
			awaiting_deferred_ = true;
		}
		Result r;
		r.handled = true;
		return r;
	}
	}
	return close();
}

Result BrowserScene::back() {
	// `Browser::backButtonAction` -> `goUpOneDirectoryLevel`: the "already at the top" case is answered synchronously
	// and exits the browser; anything else dispatches the listing.
	const std::string_view dir = current_dir();
	// `LoadMidiDeviceDefinitionUI::buttonAction`: BACK in the definitions' default folder closes the browser rather
	// than climbing out of it.
	if (id_ == BrowserId::MidiDeviceDefinition && dir == kMidiDeviceDir) {
		return close();
	}
	const size_t slash = dir.rfind('/');
	if (slash == std::string_view::npos || slash == 0) {
		return close();
	}
	entered_len_ = 0;
	edit_ = deluge::ui::oled::TextEditState{};
	arrive_in_new_folder();
	file_request(make_request(ListingAction::UpLevel));
	return repaint_all();
}

Result BrowserScene::close() {
	// `Remove` naming this scene's own kind, not `Pop`: a notice overlay -- and this scene raises
	// one on every listing -- can legitimately be pushed above it between the tick that decided to
	// close and the router applying it, and `Pop` would take that down too (`shell/scene.h`'s note
	// on `Action::Kind::Pop`).
	// A folder menu over this browser goes with it: in `main` the closing browser takes the UIs above it.
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = context_menu::kContextMenuSceneKind});
	(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kBrowserSceneKind});
	return r;
}

// ---------------------------------------------------------------------------------------------
// Rendering

void BrowserScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	(void)role;
	const uint32_t now = f.now_ms();
	last_render_ms_ = now;

	// `setupBlink(..., true)` inverts on the spot and then flips every `kFlashTime`; one half-period
	// of back-dating is how `values.cpp` spells "lit immediately".
	if (!cursor_blink_.armed()) {
		cursor_blink_.period_ms = static_cast<uint16_t>(kFlashTime);
		cursor_blink_.restart_lit(now);
	}
	edit_.cursor_lit = deluge::ui::blink_lit(cursor_blink_, now);
	if (edit_.cursor == static_cast<int16_t>(entered_len_)) {
		// `text_edit` takes no clock and owns no state, so the panel's own repaint gate has to be
		// armed here or the bar would be computed correctly and never repainted -- exactly
		// `RenameScene::render_oled`'s reason.
		f.gate().request_repaint_after(deluge::ui::blink_next_flip_in(cursor_blink_, now), now);
	}
	// ...and this is the arm that keeps the *listing* alive: see `kListingPollMs`. Without it a
	// quiet tick never redraws, `after_render` never runs, and the landed result is never collected.
	if (listing_.in_flight() || pending_.has_value()) {
		f.gate().request_repaint_after(kListingPollMs, now);
	}

	// `canvas.drawScreenTitle(title)` (`browser.cpp:926`).
	deluge::ui::oled::title(f, descriptor(id_).title);

	// `browser.cpp:928-938`. The `NO_INVERSION` step is `Style::highlighting == Highlighting::
	// NoInversion` here -- the same mapping `list`, `columns`, `popup` and `text_edit` already
	// make for the same flash setting.
	const int32_t icon_x = f.style.highlighting != Highlighting::NoInversion ? kRowIconX : f.style.inset_x;
	const int32_t text_x = kRowTextX + (f.style.highlighting != Highlighting::NoInversion ? 0 : f.style.inset_x);
	const int32_t width = f.bounds().w;

	// Legacy reads `display->getNumBrowserAndMenuLines()`, which is a hard-coded 3 on every panel
	// (`hid/display/oled.h:152`); the port reads `Style::menu_rows`, which is 3 on the shipped
	// 48-row panel and 5 on a 64-row one. Same number where legacy runs today, and a taller panel
	// fills its rows instead of leaving two blank -- the same choice `list`/`menu_rect` already
	// made for the menu.
	const int32_t rows = f.style.menu_rows < 1 ? 1 : f.style.menu_rows;
	visible_rows_ = rows;

	int32_t y = f.style.menu_base_y;
	const Descriptor& d = descriptor(id_);
	const int32_t count = static_cast<int32_t>(FileCatalogue::fileItems.size());

	// "If we're currently typing a filename which doesn't (yet?) have a file..." (`:951-955`): one
	// row, showing the edited text against the file icon.
	const int32_t drawn = (file_index_ == -1) ? 1 : rows;

	for (int32_t o = 0; o < drawn; o++) {
		bool is_folder = false;
		bool is_selected = true;
		std::string_view name{entered_, entered_len_};

		if (file_index_ != -1) {
			const int32_t i = o + scroll_;
			if (i >= count) {
				break;
			}
			const FileItem& item = FileCatalogue::fileItems[static_cast<size_t>(i)];
			is_folder = item.isFolder;
			name = item.filename;
			is_selected = (i == file_index_);
		}

		// `drawGraphicMultiLine(graphic, iconStartX, yPixel, iconWidth)` (`:973-978`).
		const gfx::oled::Icon& glyph = is_folder ? gfx::oled::icons::browserFolder : *d.file_icon;
		deluge::ui::oled::icon(f, gfx::oled::Point{icon_x, y}, glyph);
		if (!is_folder && d.file_icon_pt2 != nullptr) {
			deluge::ui::oled::icon(f, gfx::oled::Point{icon_x + glyph.width, y}, *d.file_icon_pt2);
		}

		if (is_selected) {
			// `drawTextForOLEDEditing(textStartX, OLED_MAIN_WIDTH_PIXELS, yPixel, maxChars, canvas)`
			// (`:992`): the rect's right edge is legacy's `xPixelMax` and its width in whole
			// characters is legacy's `maxChars`.
			//
			const Rect row{text_x, y, width - text_x, kTextSpacingY};
			deluge::ui::oled::text_edit(f, row, entered_text(), edit_);
			// With the cursor at the start a name too wide for the row side-scrolls, highlighted, over what was just
			// drawn (`OLED::setupSideScroller`, which legacy arms only for a name that does not fit).
			const bool scrolls =
			    edit_.cursor == 0
			    && gfx::oled::string_width_px(entered_text(), deluge::ui::oled::text_height_for(f.style.row_font))
			           > row.w;
			if (scrolls) {
				(void)deluge::ui::oled::scroll_text(f, row, entered_text(), f.style.row_font, row_scroll_, true, true);
			}
			else {
				row_scroll_ = deluge::ui::ScrollText{};
			}
		}
		else {
			// `canvas.drawString(name[0..displayStringLength), textStartX, yPixel, ...)` (`:1000`).
			deluge::ui::oled::text(f, Rect{text_x, y, width - text_x, kTextSpacingY}, row_label(name, is_folder),
			                       {.font = f.style.row_font});
		}
		y += f.style.row_height;
	}
}

void BrowserScene::render_grid(deluge::ui::grid::GridFrame& f) {
	grid_pass_ = true;
	last_render_ms_ = f.now_ms();
	if (pads_ != BrowserPads::Keyboard || !keyboard_visible_) {
		// `SampleBrowser::padAction`'s main-pad arm with the keyboard hidden, or with keys on the pads that are not
		// `qwertyVisible`, acted on by `after_render`.
		reveal_press_ = f.first_press_in(deluge::gfx::grid::Rect{0, 0, kDisplayWidth, kDisplayHeight});
	}
	switch (pads_) {
	case BrowserPads::Root:
		break;
	case BrowserPads::Waveform:
		render_waveform(f);
		break;
	case BrowserPads::Keyboard:
		// `QwertyUI::drawKeys` (`qwerty_ui.cpp:75-125`). The sidebar is deliberately untouched -- legacy clears the
		// *main* pads only.
		resp_ = deluge::ui::grid::text_keyboard(f, kb_, layout_, deluge::ui::grid::kTextKeyboardHomeRow);
		if (id_ == BrowserId::DxSyx) {
			// `DxSyxBrowser::opened` sets `qwertyVisible = false` and never draws the keys: the pads still type, dark.
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					(void)f.pad(x, y, RGB{0, 0, 0});
				}
			}
			return;
		}
		// The rows stay drawn with the keys once the keyboard is hidden, as `main`'s pads keep them; only the
		// flash and the presses need the keyboard visible.
		if (id_ == BrowserId::Sample && FlashStorage::defaultFavouritesLayout != FavouritesDefaultLayoutOff) {
			render_favourites(f);
		}
		break;
	}
	// `blinkShortcut` flashes the pad over whatever the main pads show: the timer is the sound editor's, not the pads'.
	if (browse_blink_ != BrowseBlink::Off) {
		render_browse_blink(f);
	}
}

void BrowserScene::render_browse_blink(deluge::ui::grid::GridFrame& f) {
	// `SoundEditor::blinkShortcut` on the BROWSE pad of the source being edited (column 0 for an audio clip): every
	// cycle of legacy's blink counter for the file selector's blink, every other for the browser's, counted from when
	// the blink was armed.
	const uint32_t now = f.now_ms();
	if (!blink_anchor_ms_.has_value()) {
		blink_anchor_ms_ = now;
		blink_fired_step_ = UINT32_MAX;
	}
	const menu::BlinkPlan plan = menu::exclusive_blink(ctx_.is_audio_clip ? 0 : static_cast<uint8_t>(ctx_.source_index),
	                                                   5, browse_blink_ == BrowseBlink::Menu ? uint8_t{0} : uint8_t{1});
	const uint32_t step = menu::blink_step(*blink_anchor_ms_, now);
	if (step != blink_fired_step_) {
		menu::fire_blink_step(f, plan, step);
		blink_fired_step_ = step;
	}
	f.grid_gate().request_repaint_after(menu::ms_to_next_step(*blink_anchor_ms_, now), now);
}

void BrowserScene::render_waveform(deluge::ui::grid::GridFrame& f) {
	// `showPreview`'s `waveformRenderer.renderFullScreen` of the preview drum's sample, opened zoomed out on each new
	// sample as `waveformBasicNavigator.opened()` does.
	const services::WaveformAddress preview{.kind = services::WaveformAddress::Kind::PREVIEW};
	const std::optional<services::WaveformInfo> info = menu::host_waveforms().info(preview);
	const int32_t main_cols = std::min<int32_t>(f.bounds().w, kDisplayWidth);
	const int32_t rows = std::min<int32_t>(f.bounds().h, kDisplayHeight);
	if (!info) {
		// The preview stopped and gave its sample up (Shift+SAVE frees the file): `main`'s pads keep the waveform they
		// were last sent, so these keep the last one painted.
		for (int32_t y = 0; y < rows; y++) {
			for (int32_t x = 0; x < main_cols; x++) {
				(void)f.pad(x, y, waveform_shown_[y][x]);
			}
		}
		return;
	}
	const uint32_t now = f.now_ms();
	if (info->identity != nav_identity_) {
		nav_identity_ = info->identity;
		nav_.sample = nullptr;
		nav_.length_samples = info->length_samples;
		nav_.opened(0, 0);
		redraw_resolved_cols_ = -1;
	}
	bool complete = true;
	f.region(deluge::gfx::grid::Rect{0, 0, main_cols, f.bounds().h}, [&](deluge::ui::grid::GridFrame& main) {
		complete = services::paint_full(main, menu::host_waveforms(), preview, *info, nav_);
	});
	for (int32_t y = 0; y < rows; y++) {
		for (int32_t x = 0; x < main_cols; x++) {
			waveform_shown_[y][x] = f.peek(x, y);
		}
	}
	if (complete) {
		redraw_resolved_cols_ = -1;
		return;
	}
	// `tickPreviewRedraw`: a render the card has not caught up with yet is drawn again as its clusters land, until it
	// is whole or `kWaveformRedrawGiveUpMs` pass with no column newly resolved.
	int32_t resolved = 0;
	for (const uint8_t status : nav_.renderData.colStatus) {
		resolved += status != COL_STATUS_NOT_INVESTIGATED ? 1 : 0;
	}
	if (resolved > redraw_resolved_cols_) {
		redraw_resolved_cols_ = resolved;
		redraw_progress_ms_ = now;
	}
	if (static_cast<int32_t>(now - redraw_progress_ms_) < static_cast<int32_t>(kWaveformRedrawGiveUpMs)) {
		f.grid_gate().request_repaint_after(kListingPollMs, now);
	}
}

void BrowserScene::leds(deluge::ui::LedFrame& f) {
	// `QwertyUI::opened`'s `blinkLed(IndicatorLED::BACK)`, restarted as the browser opens.
	if (back_blink_restart_) {
		back_blink_restart_ = false;
		f.restart(deluge::ui::BlinkRate::Normal);
	}
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back),
	            deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal));
	// `LoadUI::focusRegained`'s `blinkLed(IndicatorLED::LOAD)`, on BACK's phase. The sample browser is not a `LoadUI`.
	if (id_ == BrowserId::MidiDeviceDefinition) {
		(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Load),
		            deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal));
	}
	if (id_ != BrowserId::Sample) {
		return;
	}
	// `SampleBrowser::opened`'s LEDs: SYNTH for a synth, KIT while editing a kit, the three view LEDs off.
	const auto state = [](bool on) {
		return deluge::ui::LedState{on ? deluge::ui::LedState::Kind::On : deluge::ui::LedState::Kind::Off, 0, 0};
	};
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Synth), state(!ctx_.is_kit && !ctx_.is_audio_clip));
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Kit), state(ctx_.is_kit));
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::CrossScreen), state(false));
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Session), state(false));
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Scale), state(false));
	// `SampleBrowser::buttonAction`'s two toggles light their buttons.
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Keyboard), state(keyboard_always_visible_));
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Load), state(auto_load_));
}

// ---------------------------------------------------------------------------------------------
// The tick

Result BrowserScene::after_render() {
	// Read and cleared up front, so every early return below drops this tick's keystroke rather
	// than acting on it a tick later against a catalogue that has since been replaced. That is
	// legacy's own answer too: `padAction` opens with `if (listingInProgress_) return
	// ActionResult::DEALT_WITH;` (`browser.cpp:1187`), which swallows the press outright.
	const deluge::ui::Response resp = resp_;
	resp_ = {};
	const bool grid_pass = grid_pass_;
	grid_pass_ = false;

	Result r;

	// A waveform to scroll in is on the shell's grid now: hand the scroll to the host. One the pads left before it was
	// drawn is dropped.
	if (waveform_scroll_ != 0 && pads_ != BrowserPads::Waveform) {
		waveform_scroll_ = 0;
	}
	if (waveform_scroll_ != 0 && grid_pass && pads_ == BrowserPads::Waveform) {
		pad_scroll_ = ScrollRequest{.direction = waveform_scroll_};
		waveform_scroll_ = 0;
	}

	// The host settled a preview between ticks (`preview_settled`): the pads change with no input to repaint them.
	if (pads_changed_) {
		pads_changed_ = false;
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}

	// 1. **No input lock, deliberately -- see `handle()`.** Filing `Action::Kind::LockInput` while
	//    `in_flight()` would be a whole-device failure waiting for an external `Pop`: `on_exit()`
	//    returns `void`, so the release could never be filed from the one path that needs it. The
	//    scene swallows input in `handle()` instead, which is all the lock would buy here.

	// 1b. A folder menu's load ended (`long_press_done`): close what `main` closes.
	if (long_press_done_.has_value()) {
		const bool success = *long_press_done_;
		long_press_done_.reset();
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = context_menu::kContextMenuSceneKind});
		if (success) {
			(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kBrowserSceneKind});
		}
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	// 1b'. A `DeferAndWait` accept's load ended: a success closes this browser and a folder menu over it, as `close()`
	//      does; a failure leaves both up.
	if (deferred_done_.has_value()) {
		const bool success = *deferred_done_;
		deferred_done_.reset();
		r.handled = true;
		if (success) {
			(void)r.actions.push_back(
			    Action{.kind = Action::Kind::Remove, .pop_to = context_menu::kContextMenuSceneKind});
			(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kBrowserSceneKind});
		}
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	// 1c. A slice the host opened: the slicer goes on top, over the menu on the SLICE door, with main's popup on the
	//     Shift+Select door (`SampleBrowser::enterKeyPress`'s `displayPopup("SLICER")`).
	if (slice_ready_.has_value()) {
		const bool opened = *slice_ready_;
		slice_ready_.reset();
		r.handled = true;
		if (opened) {
			if (std::exchange(slice_from_shift_, false)) {
				notify::notice_text("SLICER");
			}
			(void)r.actions.push_back(Action{.kind = Action::Kind::Push, .id = SceneId{slicing::kSlicerSceneKind, 0}});
		}
		slice_from_shift_ = false;
		return r;
	}

	// 2. Collect a landed listing. **Every tick** -- `listing.h`'s `@warning`: a seam left in
	//    `Ready` refuses every later request, and the natural home for the poll is here.
	if (const std::optional<ListingResult> out = listing_.take_result(); out.has_value()) {
		const Result applied = apply(*out);
		r.handled = true;
		for (const Action& a : applied.actions) {
			(void)r.actions.push_back(a);
		}
		return r;
	}

	// 3. Retry a request the seam refused. `Busy` can clear at any tick (a legacy browser or
	//    another seam finishing), and `Dropped` clears as soon as the owner's queue drains.
	if (pending_.has_value() && !listing_.in_flight()) {
		ListingRequest req = std::move(*pending_);
		pending_.reset();
		file_request(std::move(req));
		if (!pending_.has_value()) {
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		}
		return r;
	}

	// 4. Nothing may touch the catalogue while a listing is running over it.
	if (listing_.in_flight()) {
		return r;
	}

	// 5. The long press, answered once at its deadline. While a sample load runs the hold stays and nothing answers
	//    it, so its release still enters (`SampleBrowser::timerCallback`'s gate, `sample_browser.cpp:307-310`).
	if (hold_deadline_ms_.has_value() && static_cast<int32_t>(last_render_ms_ - *hold_deadline_ms_) >= 0) {
		hold_deadline_ms_.reset();
		if (ctx_.facts == nullptr || !ctx_.facts->sample_load_in_flight()) {
			holding_ = false;
			return long_press();
		}
	}

	// 6. A key typed while Select is held is dropped: `QwertyUI::padAction` types only with no UI mode set. So is one
	//    typed while a sample load runs (`SampleBrowser::padAction`'s gate).
	if (holding_ || load_running()) {
		return r;
	}

	bool changed = false;

	// A main pad with the keyboard hidden reveals it, the cursor at the start, and types only below the favourites
	// row, so a blind press on a favourite does nothing else. With the keyboard visible but not on the pads (a waveform
	// scrolled in over it) the press types blind, as `Browser::padAction` does.
	const std::optional<deluge::ui::PadEvent> reveal = reveal_press_;
	reveal_press_.reset();
	const deluge::ui::Response favourite = favourite_resp_;
	const deluge::ui::Response bank = bank_resp_;
	favourite_resp_ = {};
	bank_resp_ = {};
	if (grid_pass && favourite.index >= 0 && favourites_visible()) {
		const Result pressed = favourite_press(static_cast<uint8_t>(favourite.index));
		r.handled = true;
		for (const Action& a : pressed.actions) {
			(void)r.actions.push_back(a);
		}
		return r;
	}
	if (grid_pass && bank.index >= 0 && banks_visible()) {
		// `Browser::padAction`'s bank-row arm.
		favourites().select_bank(static_cast<uint8_t>(bank.index));
		favourites_changed();
		r.handled = true;
		return r;
	}
	if (grid_pass && reveal.has_value() && keyboard_visible_ && favourites_visible() && reveal->y == favourite_row()) {
		// The keys are not on the pads but the keyboard is visible: the favourites row still answers.
		const Result pressed = favourite_press(static_cast<uint8_t>(reveal->x));
		r.handled = true;
		for (const Action& a : pressed.actions) {
			(void)r.actions.push_back(a);
		}
		return r;
	}
	if (grid_pass && reveal.has_value()) {
		if (!keyboard_visible_) {
			reveal_keyboard();
			browse_blink_ = BrowseBlink::Off; // `padAction`'s reveal unsets the timer
			edit_.cursor = 0;
		}
		const char c = reveal->y < favourite_row()
		                   ? deluge::ui::grid::text_keyboard_char_at(layout_, reveal->x, reveal->y,
		                                                             deluge::ui::grid::kTextKeyboardHomeRow, shift_)
		                   : 0;
		const Result typed = c != 0 ? press_key(c) : repaint_all();
		r.handled = true;
		for (const Action& a : typed.actions) {
			(void)r.actions.push_back(a);
		}
		return r;
	}

	if (grid_pass && resp.index >= 0) {
		const char c = static_cast<char>(resp.index);
		if (c == deluge::ui::grid::kTextKeyboardEnter) {
			const Result done = press_key(c);
			r.handled = true;
			for (const Action& a : done.actions) {
				(void)r.actions.push_back(a);
			}
			return r;
		}
		(void)press_key(c);
		changed = true;
	}

	if (changed) {
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}
	return r;
}

Result BrowserScene::press_key(char c) {
	if (c == deluge::ui::grid::kTextKeyboardEnter) {
		// `QwertyUI::padAction`'s enter arm (`qwerty_ui.cpp:231-252`) -> `enterKeyPress()`.
		return enter_key_press(shift_);
	}
	if (c == deluge::ui::grid::kTextKeyboardBackspace) {
		backspace();
	}
	else {
		type_char(c);
	}
	return repaint_all();
}

Result BrowserScene::handle(const UiEvent& event, const UiContext& ctx) {
	shift_ = ctx.shift;
	// The hold ends on its release whatever else refuses the event, so it never sticks.
	if ((event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton)
	    && event.button == ButtonId::XEnc && !event.on) {
		xenc_held_ = false;
	}

	// **What keeps button and encoder input out while a listing runs** (pad events are gated by
	// `after_render` instead -- see the class docstring), and legacy's own first line:
	// `padAction`, `mainButtonAction`, `backButtonAction` and `selectEncoderAction` each open with
	// `if (listingInProgress_) return;`. Swallowed rather than passed down: this scene is
	// `SceneStack::input_top()` (`input_transparent()` is false), so nothing beneath it would see
	// the event either, and the listing is about to redraw everything underneath the press.
	//
	// This scene deliberately does **not** take `Action::Kind::LockInput` -- see the warning on
	// `Action` (`shell/scene.h`). Three reasons, in order of severity: a scene cannot release that
	// lock from `on_exit()` (which returns `void`), so an external `Pop` mid-listing would deafen
	// the whole device; the lock *drops* events rather than deferring them, which is the
	// stem-export/song-load bracket rather than legacy's `waitingForSDRoutineToEnd` (that one is
	// `InputLowering::set_sd_busy`, which has no `Action` at all); and it buys this scene nothing
	// the line below does not already buy, because every event routes here first.
	if (seam_busy()) {
		Result r;
		r.handled = true;
		return r;
	}

	if ((event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton) && load_running()) {
		// A refused release leaves the hold standing, as `main`'s UI mode does: the next release enters.
		Result r;
		r.handled = true;
		return r;
	}

	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		if (id_ == BrowserId::Sample) {
			if (std::optional<Result> taken = sample_button(event.button, event.on)) {
				return *taken;
			}
		}
		if (event.button == ButtonId::Back) {
			// `Browser::buttonAction`'s BACK arm (`browser.cpp:1783-1787`): a press, with no UI mode up (a Select hold
			// or a held X_ENC is one). Claimed either way.
			if (event.on && !mode_up()) {
				return back();
			}
			Result r;
			r.handled = true;
			return r;
		}
		if (event.button == ButtonId::SelectEnc) {
			// `Browser::mainButtonAction` (`browser.cpp:1874`): the press, with no UI mode up, starts a hold and the
			// release of that hold enters. The sample browser answers a hold that lasts `kLongPressMs`
			// (`after_render`); a release after that answer, or one with no press before it, does nothing.
			Result r;
			r.handled = true;
			if (event.on) {
				if (!mode_up()) {
					holding_ = true;
					if (id_ == BrowserId::Sample) {
						hold_deadline_ms_ = ctx.now_ms + kLongPressMs;
					}
				}
				return r;
			}
			if (!holding_) {
				return r;
			}
			holding_ = false;
			hold_deadline_ms_.reset();
			return enter_key_press(ctx.shift);
		}
		break;

	case UiEvent::Kind::Pad:
		// A main pad with the keyboard hidden was read by `render_grid` (`reveal_press_`), beneath a context menu too.
		if (id_ == BrowserId::Sample && event.x < kDisplayWidth
		    && (pads_ != BrowserPads::Keyboard || !keyboard_visible_)) {
			Result r;
			r.handled = true;
			return r;
		}
		// `SampleBrowser::padAction`'s mute-column arm: a press with no UI mode stops the preview and exits.
		if (id_ == BrowserId::Sample && event.x == kDisplayWidth) {
			if (event.velocity != 0 && !holding_) {
				return close();
			}
			Result r;
			r.handled = true;
			return r;
		}
		// `Browser::padAction` -> `QwertyUI::padAction`: a main pad belongs to the keyboard or the favourites and bank
		// rows, which `render_grid` read as it painted them, so anything arriving here does nothing.
		break;

	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::EncoderRole::Select && holding_) {
			// `Browser::selectEncoderAction` turns only with no UI mode set, and a hold is one.
			Result r;
			r.handled = true;
			return r;
		}
		if (event.encoder == deluge::ui::EncoderRole::Select) {
			// `Browser::selectEncoderAction`, applied here, before the tick's render; the
			// `RepaintAll` makes that render draw the moved selection.
			step_selection(event.delta);
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
			return r;
		}
		if (event.encoder == deluge::ui::EncoderRole::Horizontal) {
			return horizontal_turn(event.delta);
		}
		if (event.encoder == deluge::ui::EncoderRole::Vertical && ctx.shift && favourites_visible()) {
			// `Browser::verticalEncoderAction`: SHIFT recolours the current favourite.
			Result r;
			r.handled = true;
			if (favourites().bank_state() == services::FavouritesBank::Unavailable) {
				(void)r.actions.push_back(
				    Action{.kind = Action::Kind::Popup,
				           .surface = SurfaceRole::Main,
				           .popup_string = static_cast<uint16_t>(String::STRING_FOR_SD_CARD_ERROR)});
			}
			else if (const std::optional<uint8_t> current = favourites().current_favourite()) {
				favourites().change_colour(*current, event.delta);
				favourites_changed();
			}
			return r;
		}
		if (event.encoder == deluge::ui::EncoderRole::Vertical) {
			// `SampleBrowser::verticalEncoderAction`: SHIFT's recolour is the arm above, and without SHIFT the host
			// hands the turn to the instrument clip view before it reaches here (`forwardBrowserVertical`). What is
			// left scrolls nothing else.
			Result r;
			r.handled = true;
			return r;
		}
		break;

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

Result BrowserScene::horizontal_turn(int32_t delta) {
	Result r;
	r.handled = true;
	if (id_ == BrowserId::Sample && showing_preview_ && nav_identity_ != 0 && (xenc_held_ || nav_.is_zoomed_in())) {
		// `SampleBrowser::horizontalEncoderAction`: X_ENC held zooms, and zoomed in a turn scrolls, with no other
		// mode up. Either draws the waveform over whatever the pads showed.
		if (!holding_ && (xenc_held_ ? nav_.zoom(delta) : nav_.scroll(delta))) {
			pads_ = BrowserPads::Waveform;
			redraw_resolved_cols_ = -1; // a new view: its redraw counts its own columns
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		}
		return r;
	}
	if (id_ == BrowserId::Sample) {
		// Otherwise the keys are drawn, and the turn walks the cursor.
		reveal_keyboard();
		browse_blink_ = BrowseBlink::Off; // `horizontalEncoderAction`'s reveal unsets the timer
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}
	// `QwertyUI::horizontalEncoderAction`: walk the cursor, and at either end do nothing. Legacy's two
	// `predictExtendedText()` calls at the ends are what extend a prediction further; here the search runs on every
	// keystroke instead, so re-running it on a turn that moves nothing would only re-read the card.
	const int32_t at = edit_.cursor;
	if ((delta > 0 && at >= entered_len_) || (delta < 0 && at == 0) || delta == 0) {
		return r;
	}
	// Legacy clamps the accumulated delta to the string (`QwertyUI::horizontalEncoderAction`).
	edit_.cursor = static_cast<int16_t>(std::clamp<int32_t>(at + delta, 0, entered_len_));
	(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
	return r;
}

std::optional<Result> BrowserScene::sample_button(ButtonId button, bool on) {
	Result r;
	r.handled = true;
	switch (button) {
	case ButtonId::XEnc:
		// A held state, entered only with no other mode up; its release is read at the top of `handle`.
		if (on && !mode_up()) {
			xenc_held_ = true;
		}
		return r;
	case ButtonId::Load:
		// Auto-load toggles on the release, and never for an audio clip, whose LOAD goes on to the base browser.
		if (ctx_.is_audio_clip) {
			return std::nullopt;
		}
		if (!on) {
			auto_load_ = !auto_load_;
		}
		return r;
	case ButtonId::Record: {
		// The recorder replaces the browser with no UI mode up -- a held audition pad is `main`'s
		// `UI_MODE_AUDITIONING`, which legacy holds because the view beneath takes that pad -- and never for an audio
		// clip, whose RECORD goes on to the base browser and then to the global RECORD.
		if (ctx_.is_audio_clip) {
			return std::nullopt;
		}
		const bool auditioning = ctx_.facts != nullptr && ctx_.facts->auditioning();
		if (on && !mode_up() && !auditioning) {
			// `changeUISideways(&audioRecorder)`: the browser goes (by kind, as `close` removes it, so a notice above
			// it is not what leaves) and the recorder takes its place.
			recorder::recorder_scene().open(ctx_);
			(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kBrowserSceneKind});
			(void)r.actions.push_back(
			    Action{.kind = Action::Kind::Push, .id = SceneId{recorder::kRecorderSceneKind, 0}});
		}
		return r;
	}
	case ButtonId::Save: {
		// Shift+SAVE on a file offers to delete it, once the song is known not to use it; the preview stops either
		// way.
		if (!shift_) {
			return std::nullopt;
		}
		const FileItem* item = item_at(file_index_);
		if (!on || mode_up() || item == nullptr || item->isFolder) {
			return r;
		}
		const std::string_view dir = current_dir();
		const std::string path = dir.empty() ? item->filename : std::string{dir} + "/" + item->filename;
		if (!menu::host_release_sample_file(path)) {
			(void)r.actions.push_back(
			    Action{.kind = Action::Kind::Popup,
			           .surface = SurfaceRole::Main,
			           .popup_string = static_cast<uint16_t>(String::STRING_FOR_AUDIO_FILE_IS_USED_IN_CURRENT_SONG)});
			return r;
		}
		context_menu::context_menu_scene().open(ctx_);
		(void)r.actions.push_back(Action{.kind = Action::Kind::Push,
		                                 .id = SceneId{context_menu::kContextMenuSceneKind,
		                                               static_cast<uint16_t>(context_menu::DescriptorId::DeleteFile)}});
		return r;
	}
	case ButtonId::Keyboard:
		if (!on) {
			return std::nullopt;
		}
		keyboard_always_visible_ = !keyboard_always_visible_;
		keyboard_visible_ = keyboard_always_visible_;
		if (keyboard_visible_) {
			reveal_keyboard();
		}
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	default:
		return std::nullopt;
	}
}

void BrowserScene::park_operation(BrowserChoice::Op op, BrowserChoice::ClaimArgs claim) {
	const FileItem* item = item_at(file_index_);
	if (item == nullptr) {
		return;
	}
	BrowserChoice choice{.id = id_,
	                     .op = op,
	                     .claim = claim,
	                     .dir = std::string{current_dir()},
	                     .filename = item->filename,
	                     .is_folder = item->isFolder};
	choice.path = choice.dir.empty() ? choice.filename : choice.dir + "/" + choice.filename;
	choice_ = std::move(choice);
	awaiting_load_ = op != BrowserChoice::Op::Enter && op != BrowserChoice::Op::DeleteFile;
	slice_out_ = op == BrowserChoice::Op::Slice;
	slice_from_shift_ = false;
}

void BrowserScene::slice_ready(bool opened) {
	if (!slice_out_) {
		return;
	}
	slice_out_ = false;
	slice_ready_ = opened;
	if (!opened) {
		awaiting_load_ = false; // nothing more comes for this slice
	}
}

void BrowserScene::file_deleted(std::string_view path) {
	// `Browser::currentFileDeleted`, only while the deleted file is still the one selected.
	const FileItem* item = item_at(file_index_);
	if (item == nullptr || item->isFolder) {
		return;
	}
	const std::string_view dir = current_dir();
	const std::string selected = dir.empty() ? item->filename : std::string{dir} + "/" + item->filename;
	if (selected != path) {
		return;
	}
	FileCatalogue::fileItems.erase(FileCatalogue::fileItems.begin() + file_index_);
	if (file_index_ == static_cast<int32_t>(FileCatalogue::fileItems.size())) {
		file_index_--; // none left: no selection
		entered_len_ = 0;
		edit_.cursor = 0;
	}
	else {
		set_entered_text_from_current_filename();
	}
	clamp(visible_rows_);
	file_preview(0); // `currentFileChanged(0)`
	pads_changed_ = true;
}

bool BrowserScene::fresh_kit() const {
	return ctx_.source_index == 0 && ctx_.facts != nullptr && ctx_.addresses.slot
	       && ctx_.facts->kit_takes_whole_folder(*ctx_.addresses.slot);
}

void BrowserScene::deferred_load_done(bool success) {
	if (!awaiting_deferred_) {
		return;
	}
	awaiting_deferred_ = false;
	deferred_done_ = success;
}

void BrowserScene::long_press_done(bool success) {
	if (!awaiting_load_) {
		return;
	}
	awaiting_load_ = false;
	long_press_done_ = success;
}

bool BrowserScene::current_is_folder() const {
	const FileItem* item = item_at(file_index_);
	return item != nullptr && item->isFolder;
}

Result BrowserScene::long_press() {
	Result r;
	r.handled = true;
	if (item_at(file_index_) == nullptr) {
		return r;
	}
	const auto popup = [&r](String text) {
		(void)r.actions.push_back(Action{
		    .kind = Action::Kind::Popup, .surface = SurfaceRole::Main, .popup_string = static_cast<uint16_t>(text)});
		return r;
	};
	const auto menu = [&r, this](context_menu::DescriptorId id) {
		context_menu::context_menu_scene().open(ctx_);
		(void)r.actions.push_back(Action{
		    .kind = Action::Kind::Push, .id = SceneId{context_menu::kContextMenuSceneKind, static_cast<uint16_t>(id)}});
		return r;
	};
	if (ctx_.is_audio_clip) {
		return popup(String::STRING_FOR_CANT_IMPORT_WHOLE_FOLDER_INTO_AUDIO_CLIP);
	}
	if (ctx_.is_kit) {
		// `SampleBrowser::canImportWholeKit` (`sample_browser.cpp`).
		return fresh_kit() ? menu(context_menu::DescriptorId::SampleBrowserKit)
		                   : popup(String::STRING_FOR_CAN_ONLY_IMPORT_WHOLE_FOLDER_INTO_BRAND_NEW_KIT);
	}
	std::array<uint8_t, context_menu::kMaxOptions> available{};
	if (context_menu::sample_browser_synth_options(ctx_, current_is_folder(), available) == 0) {
		return r;
	}
	return menu(context_menu::DescriptorId::SampleBrowserSynth);
}

std::optional<uint32_t> BrowserScene::next_timer() const {
	std::optional<uint32_t> next;

	// The cursor bar moves without input, but only while the cursor sits at the end of the string
	// (`text_edit`: `cursor_lit` gates that arm and nothing else).
	if (cursor_blink_.armed() && edit_.cursor == static_cast<int16_t>(entered_len_)) {
		next = deluge::ui::blink_next_flip_in(cursor_blink_, last_render_ms_);
	}

	// The listing poll, and the reason this scene must be woken with no input at all: nothing else
	// would render it, and `after_render` is the only place a landed result can be collected or a
	// refused request retried. See `kListingPollMs`.
	if (listing_.in_flight() || pending_.has_value()) {
		next = next.has_value() ? std::min(*next, kListingPollMs) : kListingPollMs;
	}

	// A preview or a `DeferAndWait` load the host has not answered: the host's dispatcher ticks its load only when the
	// guest is woken.
	if (preview_outstanding_ || awaiting_deferred_) {
		next = next.has_value() ? std::min(*next, kListingPollMs) : kListingPollMs;
	}

	// A folder menu's load that ended, or a slice the host opened, is acted on by `after_render`, on the next render.
	if (long_press_done_.has_value() || deferred_done_.has_value() || slice_ready_.has_value() || pads_changed_) {
		return 0;
	}

	// The long press is answered by `after_render`, so its deadline must render.
	if (hold_deadline_ms_.has_value()) {
		const int32_t left = static_cast<int32_t>(*hold_deadline_ms_ - last_render_ms_);
		const uint32_t in = left > 0 ? static_cast<uint32_t>(left) : 0;
		next = next.has_value() ? std::min(*next, in) : in;
	}
	return next;
}

BrowserScene& browser_scene() {
	static BrowserScene scene;
	return scene;
}

} // namespace deluge::scenes::browser
