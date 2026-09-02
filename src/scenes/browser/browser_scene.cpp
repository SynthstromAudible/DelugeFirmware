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
#include "ui/core/metrics.h"
#include "ui/oled/misc.h"
#include "ui/oled/text.h"

#include "gfx/oled/icons.h"

#include "gui/l10n/strings.h"

#include "model/clip/audio_clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace deluge::scenes::browser {

using deluge::l10n::String;
using deluge::storage::FileCatalogue;
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

/// @brief `charCaseEqual` (`util/functions.cpp:1635`): equal ignoring ASCII letter case.
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

/// @brief Is the blinking cursor lit at @p now? `values.cpp`'s `blink_lit`, which is
///        `OLED::performBlink`'s square wave.
bool blink_lit(const deluge::ui::Blink& b, uint32_t now) {
	return ((static_cast<uint32_t>(now - b.until_ms) / b.period_ms) & 1u) != 0;
}

uint32_t blink_next_flip_in(const deluge::ui::Blink& b, uint32_t now) {
	return b.period_ms - ((now - b.until_ms) % b.period_ms);
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
// The two descriptors. Each block is one legacy `Browser` subclass, virtual for virtual.

/// @brief `SampleBrowser::allowedFileExtensionsAudio` (`sample_browser.cpp:85`).
char const* kAudioExtensions[] = {"WAV", "AIFF", "AIF", nullptr};

// --- Sample: `SampleBrowser` (`gui/ui/browser/sample_browser.cpp`) ---

/// @brief `SoundEditor::getCurrentAudioFileHolder` (`gui/ui/sound_editor.cpp:1971-1980`), reached
///        through the context rather than through the `soundEditor` global.
///
/// The two arms are the same two: an audio clip's own `sampleHolder`, or the selected
/// `MultiRange`'s holder. `context_for` already resolved both (`context.cpp`), so no UI read is
/// needed to pick between them.
/// @param ctx What the browser was opened over.
/// @return The holder, or null when neither arm resolves.
AudioFileHolder* audio_file_holder(const menu::MenuContext& ctx) {
	if (ctx.is_audio_clip) {
		return ctx.clip != nullptr ? &static_cast<AudioClip*>(ctx.clip)->sampleHolder : nullptr;
	}
	return ctx.range != nullptr ? ctx.range->getAudioFileHolder() : nullptr;
}

bool sample_can(const menu::MenuContext& ctx) {
	// `SampleBrowser::opened` has no refusal of its own: it walks straight into
	// `soundEditor.getCurrentAudioFileHolder()->filePath` (`sample_browser.cpp:131`). Declining to
	// open is the survivable spelling of that dereference, exactly as `rename::Target::can` is for
	// `RenameDrumUI`'s `FREEZE_WITH_ERROR`.
	return audio_file_holder(ctx) != nullptr;
}

void sample_start_at(const menu::MenuContext& ctx, std::string& dir, std::string& name) {
	// `SampleBrowser::opened` (`sample_browser.cpp:135-170`): take the holder's path, and if it is
	// blank or outside SAMPLES/ fall through to the default folder. Then dissect it into a
	// directory and the filename to select.
	//
	// **One named divergence.** Legacy's middle arm falls back to `SampleBrowser::lastFilePathLoaded`
	// (`sample_browser.h:58`) before it falls back to the folder. That member is written inside
	// `claimCurrentFile()`, which this layer cannot perform (see `Descriptor::accept`), so there is
	// nothing here that could keep it up to date -- a mirror would be authoritative for nothing and
	// stale from the first load. Dropped rather than half-built: the browser opens on the default
	// folder where legacy would have reopened on the last manually-loaded sample's folder. Whoever
	// wires `claimCurrentFile` up owns closing it, and the natural home is a mirror beside
	// `menu::performance_editing_mode()`.
	const AudioFileHolder* holder = audio_file_holder(ctx);
	const std::string_view path = holder != nullptr ? std::string_view{holder->filePath} : std::string_view{};
	if (path.empty() || !case_prefix_equal(path, "SAMPLES/", 8)) {
		return; // dir stays empty -> `Descriptor::default_dir`
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

uint16_t sample_folder_refusal(std::string_view dir, std::string_view name) {
	// "Don't allow user to go into TEMP clips folder" (`sample_browser.cpp:345-350`).
	if (caseless_equals(name, "TEMP") && caseless_equals(dir, "SAMPLES/CLIPS")) {
		return sid(String::STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED);
	}
	return 0;
}

AcceptOutcome sample_accept(const menu::MenuContext& ctx, std::string_view path, Error& error) {
	(void)ctx;
	(void)path;
	(void)error;
	// See `Descriptor::accept`: `SampleBrowser::enterKeyPress`'s file arm is `claimCurrentFile()`
	// (`sample_browser.cpp:398`), which names `soundEditor`, `AudioEngine`, `slicer`, `PadLEDs` and
	// `instrumentClipView`. The scene parks the path instead, and the wiring plan's dispatcher runs
	// the load against the legacy object -- the noun half of `scenes/menu/action.h`'s seam.
	return AcceptOutcome::Defer;
}

// --- MIDI device definition: `LoadMidiDeviceDefinitionUI`
//     (`gui/ui/load/load_midi_device_definition_ui.cpp`) ---

/// @brief `MIDI_DEVICES_DEFINITION_DEFAULT_FOLDER` (`load_midi_device_definition_ui.cpp:36`).
constexpr std::string_view kMidiDeviceDir = "MIDI_DEVICES/DEFINITION";

MIDIInstrument* midi_instrument_of(const menu::MenuContext& ctx) {
	return (ctx.output != nullptr && ctx.output->type == OutputType::MIDI_OUT)
	           ? static_cast<MIDIInstrument*>(ctx.output)
	           : nullptr;
}

bool midi_device_can(const menu::MenuContext& ctx) {
	// `LoadMidiDeviceDefinitionUI::opened` (`:45-47`) refuses unless the current output is a MIDI
	// one. Its other conjunct, `getRootUI()->toClipMinder()`, is a UI read this layer may not make
	// (`scenes-no-root-ui`); the menu tree only reaches this item from a clip minder anyway, since
	// `midi_device_linked` hangs off the MIDI/CV root, so the surviving test is the one that
	// discriminates.
	return midi_instrument_of(ctx) != nullptr;
}

void midi_device_start_at(const menu::MenuContext& ctx, std::string& dir, std::string& name) {
	// `setupForLoadingMidiDeviceDefinition` (`:83-124`): with no remembered file, open on the
	// default folder; otherwise split the remembered path and select that file, extension included.
	const MIDIInstrument* instrument = midi_instrument_of(ctx);
	if (instrument == nullptr || instrument->deviceDefinitionFileName.empty()) {
		return; // dir stays empty -> `Descriptor::default_dir`
	}
	const std::string_view path = instrument->deviceDefinitionFileName;
	const size_t slash = path.rfind('/');
	if (slash == std::string_view::npos) {
		return;
	}
	dir = path.substr(0, slash);
	name = path.substr(slash + 1);
	// Legacy appends ".XML" to the *base* name it split out; the remembered path already carries
	// the extension, so appending again would search for "FOO.XML.XML". `searchFileItems` compares
	// against `FileItem::displayName()`, which is the filename with its extension, so the name as
	// split is already the search key legacy meant.
	if (name.size() < 4 || !case_prefix_equal(name.substr(name.size() - 4), ".XML", 4)) {
		name += ".XML";
	}
}

AcceptOutcome midi_device_accept(const menu::MenuContext& ctx, std::string_view path, Error& error) {
	// `LoadMidiDeviceDefinitionUI::performLoad` (`:198-224`) then its caller's tail (`:156-166`):
	// load, announce on the console, close. Reachable in full from here -- `StorageManager` is in
	// `storage/`, which this layer may include -- so this descriptor performs its own action rather
	// than deferring it.
	MIDIInstrument* instrument = midi_instrument_of(ctx);
	if (instrument == nullptr) {
		error = Error::FILE_NOT_FOUND;
		return AcceptOutcome::Stay;
	}
	std::string file_name{path};
	error = StorageManager::loadMidiDeviceDefinitionFile(instrument, file_name.c_str(), &file_name);
	if (error != Error::NONE) {
		return AcceptOutcome::Stay;
	}
	// `display->consoleText(STRING_FOR_MIDI_DEVICE_LOADED)` (`:164`).
	notify::notice_string(sid(String::STRING_FOR_MIDI_DEVICE_LOADED), notify::Notice::Kind::Console);
	return AcceptOutcome::Close;
}

const std::array<Descriptor, static_cast<size_t>(BrowserId::Count)> kDescriptors{{
    // `SampleBrowser::SampleBrowser` (`sample_browser.cpp:88-94`) plus `SampleBrowser::opened`
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
}};

/// @brief The entry the selection names, or null -- `Browser::getCurrentFileItem`
///        (`browser.cpp:1138`).
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
	if (id.arg >= static_cast<uint16_t>(BrowserId::Count)) {
		return false;
	}
	id_ = static_cast<BrowserId>(id.arg);
	if (!descriptor(id_).can(ctx_)) {
		return false;
	}

	entered_len_ = 0;
	file_index_ = -1;
	scroll_ = 0;
	edit_ = deluge::ui::oled::TextEditState{};
	kb_ = deluge::ui::grid::TextKeyboardState{};
	resp_ = {};
	pending_.reset();
	choice_.reset();
	searching_ = false;
	select_after_listing_.clear();
	// `keyboardChars[util::to_underlying(FlashStorage::keyboardLayout)]` (`qwerty_ui.cpp:268`).
	// `KeyboardLayoutKind`'s values match legacy's enum exactly, so this is a cast (`keyboard.h`).
	layout_ = static_cast<deluge::ui::grid::KeyboardLayoutKind>(static_cast<uint8_t>(FlashStorage::keyboardLayout));
	cursor_blink_ = deluge::ui::Blink{.period_ms = 0, .until_ms = 0};

	request_open();
	return true;
}

void BrowserScene::on_exit() {
	// `Browser::close()` (`browser.cpp:189`) empties the catalogue. Collect first: an uncollected
	// result still holds `FileCatalogue`'s claim, and leaving it held would refuse every later
	// listing -- this layer's or a legacy browser's -- for the life of the process.
	//
	// A listing that is still *in flight* cannot be collected and is deliberately not waited for:
	// the seam has no cancel. Nothing the user can do reaches here in that state (the scene holds
	// the shell's input lock and `handle()` swallows everything while `in_flight()`), so the only
	// way in is an external `Pop`; the seam then lands, sits `Ready`, and the next `on_enter`'s
	// `request_open` is refused as `ResultPending` until `after_render` drains it -- a visible jam
	// rather than a corrupted vector, which is the trade `listing.h` documents.
	(void)listing_.take_result();
	pending_.reset();
	FileCatalogue::emptyFileItems();
	// Fix the same hazard `RenameScene::on_exit` records: `open()` is the only writer of `ctx_`, and
	// this scene is a singleton that outlives every visit, so a re-entry that forgot its `open()`
	// must not validate `can()` against the previous song's pointers. A zeroed context fails every
	// descriptor's `can`, and `on_enter` refuses.
	ctx_ = menu::MenuContext{};
	grid_pass_ = false;
	locked_ = false;
}

std::string_view BrowserScene::current_dir() const {
	return FileCatalogue::currentDir;
}

std::optional<BrowserChoice> BrowserScene::take_choice() {
	std::optional<BrowserChoice> out = std::move(choice_);
	choice_.reset();
	return out;
}

// ---------------------------------------------------------------------------------------------
// Listings

ListingRequest BrowserScene::make_request(ListingAction action) const {
	const Descriptor& d = descriptor(id_);
	ListingRequest req;
	req.action = action;
	req.allowed_extensions = d.extensions;
	req.allow_folders = true; // `arrivedInNewFolder` passes `allowFolders = true` (`browser.cpp:382`)
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
	// `runPendingListing`'s failure arm -> `Browser::onListingFailed` (`browser.cpp:166`):
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
		if (search_for_entered_text() != SearchOutcome::Found && file_index_ >= 0) {
			// `notFound` with `mayDefaultToBrandNewNameOnEntry` false (`:842-846`): put the selected
			// file's name back.
			set_entered_text_from_current_filename();
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
		return repaint_all();
	}

	file_index_ = 0;
	if (!name.empty()) {
		bool exact = false;
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
	// `Browser::setEnteredTextFromCurrentFilename` (`browser.cpp:1316-1332`): the entry's display
	// name, with the extension cut off for a file.
	const FileItem* item = item_at(file_index_);
	if (item == nullptr) {
		entered_len_ = 0;
		return;
	}
	set_entered_text(row_label(item->displayName(), item->isFolder));
}

BrowserScene::SearchOutcome BrowserScene::search_for_entered_text() {
	// The in-memory half of `Browser::predictExtendedText` (`browser.cpp:782-898`). Legacy searches
	// for the text **left of the cursor**, not the whole string (`:800`, `searchString.resize
	// (enteredTextEditPos)`), which is what makes typing over a prediction search for the prefix
	// rather than for the prediction.
	//
	// @note The comparator's globals (`shouldInterpretNoteNames`, `octaveStartsFromA`) are NOT
	//       re-pinned here the way legacy re-pins them at `:785-786`: they live in
	//       `util/name_compare.h`, which is outside this layer's include allowance
	//       (`scenes-includes-outside`), and the listing whose entries are being searched pinned
	//       them itself when it sorted (`FileCatalogue::sortFileItems`, called from
	//       `Listing::list_current_dir`). Same values, set one step earlier.
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
		// don't know what it does". Neither of this scene's two descriptors is a slot browser --
		// both were split off `Browser` directly, and `filePrefix` is null for both -- so the arm is
		// deliberately not ported rather than transcribed unexercised.
		return SearchOutcome::NotFound;
	}

	file_index_ = i;
	// "Typing/prediction can land on a cached item without needing to move the viewport unless it is
	// offscreen" (`:882-883`).
	clamp(visible_rows_);
	// Replaces the typed text with the found entry's name and leaves the cursor where the typing
	// left it -- which is what `text_edit` draws as the prediction highlight.
	set_entered_text_from_current_filename();
	return SearchOutcome::Found;
}

void BrowserScene::run_search_after_typing(int32_t revert_cursor_to) {
	switch (search_for_entered_text()) {
	case SearchOutcome::Found:
		break;
	case SearchOutcome::NotFound:
		// `:842-846`, with `mayDefaultToBrandNewNameOnEntry` false for both descriptors: put the
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
	// `QwertyUI::processBackspace` (`qwerty_ui.cpp:408-418`), including its asymmetry: the edit
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
	// `Browser::finishSelectEncoderAction` (`browser.cpp:702-730`).
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
	(void)offset; // legacy passes the sign on to `currentFileChanged`, which this scene has none of
}

void BrowserScene::step_selection(int32_t offset) {
	// `Browser::selectEncoderAction` (`browser.cpp:549-697`), the non-slot path.
	//
	// @note Legacy's shift/`numberEditPos` arm (`:576-629`) walks whole slot numbers -- "SONG12" ->
	//       "SONG13" -- and is guarded by `nameAfterPrefix`, which returns null unless the name
	//       starts with `filePrefix`. Both descriptors here leave `filePrefix` null (only the
	//       slot-based load/save browsers set it), so legacy's own guard takes the `nonNumeric`
	//       branch for every name either of them can show, and the arm is not ported.
	const int32_t count = static_cast<int32_t>(FileCatalogue::fileItems.size());
	int32_t index;
	if (file_index_ < 0) {
		if (count == 0) {
			return;
		}
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

Result BrowserScene::enter_key_press() {
	const Descriptor& d = descriptor(id_);
	const FileItem* item = item_at(file_index_);
	if (item == nullptr) {
		// `SampleBrowser::enterKeyPress` (`sample_browser.cpp:337-340`) announces
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
		// `goIntoFolderImpl` (`browser.cpp:1339`) clears the entered text before it lists.
		entered_len_ = 0;
		edit_ = deluge::ui::oled::TextEditState{};
		ListingRequest req = make_request(ListingAction::IntoFolder);
		req.path = item->filename;
		file_request(std::move(req));
		return repaint_all();
	}

	// `getCurrentFilePath()` (`sample_browser.h:125 getCurrentFilePath`, `load_midi_device_definition_ui.cpp:213-217`):
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
	case AcceptOutcome::Defer:
		choice_ = BrowserChoice{id_, std::move(path)};
		return close();
	}
	return close();
}

Result BrowserScene::back() {
	// `Browser::backButtonAction` (`browser.cpp:1283-1300`) -> `goUpOneDirectoryLevel`
	// (`:1359-1372`): the "already at the top" case is answered synchronously and exits the
	// browser; anything else dispatches the listing.
	const std::string_view dir = current_dir();
	const size_t slash = dir.rfind('/');
	if (slash == std::string_view::npos || slash == 0) {
		return close();
	}
	entered_len_ = 0;
	edit_ = deluge::ui::oled::TextEditState{};
	file_request(make_request(ListingAction::UpLevel));
	return repaint_all();
}

Result BrowserScene::close() {
	// `Remove` naming this scene's own kind, not `Pop`: a notice overlay -- and this scene raises
	// one on every listing -- can legitimately be pushed above it between the tick that decided to
	// close and the router applying it, and `Pop` would take that down too (`shell/scene.h`'s note
	// on `Action::Kind::Pop`).
	Result r;
	r.handled = true;
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
	if (cursor_blink_.period_ms != kFlashTime) {
		cursor_blink_.period_ms = static_cast<uint16_t>(kFlashTime);
		cursor_blink_.until_ms = now - static_cast<uint32_t>(kFlashTime);
	}
	edit_.cursor_lit = blink_lit(cursor_blink_, now);
	if (edit_.cursor == static_cast<int16_t>(entered_len_)) {
		// `text_edit` takes no clock and owns no state, so the panel's own repaint gate has to be
		// armed here or the bar would be computed correctly and never repainted -- exactly
		// `RenameScene::render_oled`'s reason.
		f.gate().request_repaint_after(blink_next_flip_in(cursor_blink_, now), now);
	}
	// ...and this is the arm that keeps the *listing* alive: see `kListingPollMs`. Without it a
	// quiet tick never redraws, `after_render` never runs, and the landed result is never collected.
	if (listing_.in_flight() || pending_.has_value()) {
		f.gate().request_repaint_after(kListingPollMs, now);
	}

	// `canvas.drawScreenTitle(title)` (`browser.cpp:926`).
	deluge::ui::oled::title(f, descriptor(id_).title);

	// `browser.cpp:928-938`. The `NO_INVERSION` step is `Style::invert_highlight` here -- the same
	// mapping `list`, `columns`, `popup` and `text_edit` already make for the same flash setting.
	const int32_t icon_x = f.style.invert_highlight ? kRowIconX : f.style.inset_x;
	const int32_t text_x = kRowTextX + (f.style.invert_highlight ? 0 : f.style.inset_x);
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
			// @note Legacy also arms a side-scroller for this row when the cursor is at 0 (`:993-997`).
			//       That is NOT part of `renderOLED`'s output at all -- `OLED::setupSideScroller`
			//       registers a scroller that `OLED::sendMainImage` composites afterwards
			//       (`hid/display/oled.cpp`) -- so it is not ported here and its absence is exactly
			//       why the pixel comparison against the legacy canvas can be exact.
			//       `ui::oled::scroll_text` is the widget that would do it.
			deluge::ui::oled::text_edit(f, Rect{text_x, y, width - text_x, kTextSpacingY}, entered_text(), edit_);
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
	// `QwertyUI::drawKeys` (`qwerty_ui.cpp:75-125`), already ported. The sidebar is deliberately
	// untouched -- legacy clears the *main* pads only.
	//
	// @note Legacy's `qwertyVisible` toggle is not ported. `SampleBrowser` starts with the keyboard
	//       hidden and the sample's waveform on the pads instead (`sample_browser.cpp:112`,
	//       `SampleBrowser::renderMainPads`), and swaps between the two as the selection moves. That
	//       half is the sample *preview*, which needs `AudioEngine::previewSample`, `PadLEDs`'
	//       horizontal scroll transition and the marker editor -- none of it reachable from here,
	//       and the marker editor is its own task in this plan. The keyboard is therefore always
	//       drawn, which is `LoadMidiDeviceDefinitionUI`'s own behaviour
	//       (`load_midi_device_definition_ui.cpp:129 drawKeys`) and the base `Browser`'s
	//       (`qwertyAlwaysVisible = true`, `browser.cpp:89`).
	resp_ = deluge::ui::grid::text_keyboard(f, kb_, layout_, deluge::ui::grid::kTextKeyboardHomeRow);
	grid_pass_ = true;
	last_render_ms_ = f.now_ms();
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

	// 1. The input lock. `Action::Kind::LockInput` is the port of legacy's
	//    `waitingForSDRoutineToEnd` gate; filed on the edges only, so a lock is taken once and
	//    released once.
	const bool busy = listing_.in_flight();
	if (busy != locked_) {
		locked_ = busy;
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::LockInput, .lock = busy});
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

	bool changed = false;

	if (grid_pass && resp.index >= 0) {
		const char c = static_cast<char>(resp.index);
		if (c == deluge::ui::grid::kTextKeyboardEnter) {
			// `QwertyUI::padAction`'s enter arm (`qwerty_ui.cpp:231-252`) -> `enterKeyPress()`.
			const Result done = enter_key_press();
			r.handled = true;
			for (const Action& a : done.actions) {
				(void)r.actions.push_back(a);
			}
			return r;
		}
		if (c == deluge::ui::grid::kTextKeyboardBackspace) {
			backspace();
		}
		else {
			type_char(c);
		}
		changed = true;
	}

	if (changed) {
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}
	return r;
}

Result BrowserScene::handle(const UiEvent& event, const UiContext& ctx) {
	(void)ctx;

	// The second line of defence behind the input lock, and legacy's own first: `padAction`
	// (`browser.cpp:1187`), `mainButtonAction` (`:1251`), `backButtonAction` (`:1284`) and
	// `selectEncoderAction` (`:550`) each open with `if (listingInProgress_) return;`. Swallowed
	// rather than passed down: this scene is the input top and the listing is about to redraw
	// everything underneath the press.
	//
	// `has_result()` as well as `in_flight()`: post-render input (step 8) arrives after
	// `after_render` has already run for the tick, so an op that landed in between would leave a
	// result uncollected with `fileItems` already replaced. Acting on that would move the selection
	// over a window the scene has not reconciled.
	if (seam_busy()) {
		Result r;
		r.handled = true;
		return r;
	}

	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		if (event.button == ButtonId::Back) {
			// `Browser::buttonAction`'s BACK arm (`browser.cpp:1174-1178`) is press-only.
			if (event.on) {
				return back();
			}
			break;
		}
		if (event.button == ButtonId::SelectEnc) {
			// `Browser::mainButtonAction` (`browser.cpp:1250-1279`): the press arms a long-press
			// timer and the **release** is what calls `enterKeyPress()`.
			//
			// @note The long-press arm itself is not ported. Legacy's press sets
			//       `UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS` and `SampleBrowser::timerCallback`
			//       (`sample_browser.cpp:283 timerCallback`) answers it by opening one of the
			//       import-whole-folder context menus -- `context_menu::sample_browser::kit` or
			//       `::synth` (`gui/context_menu/sample_browser/kit.h`,
			//       `gui/context_menu/sample_browser/synth.h`). Those are two of the legacy
			//       `ContextMenu` subclasses `ContextMenuScene`'s `DescriptorId` note lists as
			//       deliberately absent, because nothing in the ported menu tree opens them; porting
			//       them here would be building the descriptors that task decided not to build.
			if (!event.on) {
				return enter_key_press();
			}
			Result r;
			r.handled = true;
			return r;
		}
		break;

	case UiEvent::Kind::Pad:
		// `Browser::padAction` -> `QwertyUI::padAction`: a main pad belongs to the keyboard, which
		// has already claimed it while `render_grid` painted it, so anything arriving here is
		// outside the keyboard's columns. Legacy's sidebar arms are the favourites row and the bank
		// row (`browser.cpp:1191-1227`), neither of which this scene draws -- `favouritesManager` is
		// reached through `getCurrentUI()` (`browser.cpp:1463`), a root-UI read this layer may not
		// make -- so a sidebar press does nothing rather than doing something half-right.
		break;

	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::EncoderRole::Select) {
			// `Browser::selectEncoderAction` (`browser.cpp:549`), applied here rather than parked for
			// `after_render`. The turn is one of the shell's widget-owned shapes, so it is delivered
			// **after** the render pass and after every `after_render()` call for the tick
			// (`Shell::tick` step 8, `shell/shell.cpp:367 deliver_post_render_input`) -- parking it
			// would cost a whole tick per detent, which a level-2 script would have found as a
			// browser that lags one click behind the encoder. The frame is already built either way,
			// which is what the `Repaint` below is for.
			step_selection(event.delta);
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
			return r;
		}
		if (event.encoder == deluge::ui::EncoderRole::Horizontal) {
			// `QwertyUI::horizontalEncoderAction` (`qwerty_ui.cpp:420-447`): walk the cursor, and at
			// either end do nothing. Legacy's two `predictExtendedText()` calls at the ends are what
			// extend a prediction further; here the search runs on every keystroke instead, so
			// re-running it on a turn that moves nothing would only re-read the card.
			const int32_t at = edit_.cursor;
			Result r;
			r.handled = true;
			if ((event.delta > 0 && at >= entered_len_) || (event.delta < 0 && at == 0) || event.delta == 0) {
				return r;
			}
			// Legacy adds the offset unclamped, which an accelerated turn can push past the end of
			// the string. Clamped here; every in-range turn lands where legacy's does.
			edit_.cursor = static_cast<int16_t>(std::clamp<int32_t>(at + event.delta, 0, entered_len_));
			(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			return r;
		}
		if (event.encoder == deluge::ui::EncoderRole::Vertical) {
			// `Browser::verticalEncoderAction` (`browser.cpp:1238-1248`) only ever recolours a
			// favourite, which this scene does not draw; it still answers `DEALT_WITH`, so the turn
			// does not scroll whatever is underneath.
			Result r;
			r.handled = true;
			return r;
		}
		break;

	case UiEvent::Kind::AllReleased:
		break;
	}
	return Result::unhandled();
}

std::optional<uint32_t> BrowserScene::next_timer() const {
	std::optional<uint32_t> next;

	// The cursor bar moves without input, but only while the cursor sits at the end of the string
	// (`text_edit`: `cursor_lit` gates that arm and nothing else).
	if (cursor_blink_.period_ms != 0 && edit_.cursor == static_cast<int16_t>(entered_len_)) {
		next = blink_next_flip_in(cursor_blink_, last_render_ms_);
	}

	// The listing poll, and the reason this scene must be woken with no input at all: nothing else
	// would render it, and `after_render` is the only place a landed result can be collected or a
	// refused request retried. See `kListingPollMs`.
	if (listing_.in_flight() || pending_.has_value()) {
		next = next.has_value() ? std::min(*next, kListingPollMs) : kListingPollMs;
	}
	return next;
}

BrowserScene& browser_scene() {
	static BrowserScene scene;
	return scene;
}

} // namespace deluge::scenes::browser
