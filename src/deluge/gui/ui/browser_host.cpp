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
/// @brief The browser scene's host. See `browser_host.h` for why it lives here and how its answers reach the
///        browser.

#include "gui/ui/browser_host.h"
#include "gui/ui/shell_slice_ops.h"

#include "gui/l10n/strings.h"
#include "gui/ui/browser/dx_cartridge_read.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/browser/sample_preview.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_root.h"
#include "hid/led/pad_leds.h"
#include "io/file.hpp"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/packed_owner.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/last_file_path_loaded.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/source_resources.h"
#include "storage/byte_channel.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/registry.h"
#include "util/menu_entry.h"
#include "util/notify.h"
#include "util/sound_menu_open.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::gui::ui {

using deluge::ui::scenes::browser::browser_scene;
using deluge::ui::scenes::browser::BrowserChoice;
using deluge::ui::scenes::browser::BrowserId;
using deluge::ui::scenes::browser::PreviewRequest;

namespace {

/// @brief How one of the browser host's requests ended: what the browser scene, and the menu beneath it, are told.
struct BrowserAnswer {
	/// @brief Which completion this is.
	enum class Kind : uint8_t {
		SampleClaimDone,     ///< A chosen file's claim ended: `deliverSampleClaimDone`.
		LongPressClaimDone,  ///< A folder menu's claim ended: `BrowserScene::long_press_done`, then as a claim.
		LongPressImportDone, ///< A folder menu's import ended: `BrowserScene::long_press_done`.
		FileDeleteDone,      ///< The card answered a deletion: `deliverFileDeleteDone`.
		DeferredLoadDone,    ///< A load the browser waits on ended: `BrowserScene::deferred_load_done`.
		DxReadSettled,       ///< The `.syx` read ended or could not start: `deliverDxCartridgeRead`.
		SliceReady,          ///< The slicer's open ended: `BrowserScene::slice_ready`.
		PreviewSettled,      ///< A preview's load ended, or the preview stopped: `BrowserScene::preview_settled`.
	};
	Kind kind = Kind::DxReadSettled;
	/// Claimed, imported, loaded or opened; for a preview, a sample to draw.
	bool success = false;
	/// The file a deletion named.
	std::string path{};
	/// How a deletion ended.
	FileDeleteOutcome deleted = FileDeleteOutcome::Failed;
	/// Where the `.syx` read stands.
	DxCartridgeRead::Status dx = DxCartridgeRead::Status::Idle;
};

/// @brief The answers produced while a request was being performed, in the order they came.
std::vector<BrowserAnswer>& heldAnswers() {
	static std::vector<BrowserAnswer> answers;
	return answers;
}

/// @brief How many of the browser's requests the host is inside now.
int32_t performingDepth = 0;

/// @brief Marks the host as inside one of the browser's requests for its lifetime: an answer produced meanwhile is
/// held.
class Performing {
public:
	Performing() { performingDepth++; }
	~Performing() { performingDepth--; }
	Performing(const Performing&) = delete;
	Performing& operator=(const Performing&) = delete;
};

void deliverAnswer(const BrowserAnswer& answer);

/// @brief Tell the browser how a request ended: held when the request that produced the answer is still being
///        performed (the scene that asked is inside its hook), delivered at once otherwise.
void answerBrowser(BrowserAnswer answer) {
	if (performingDepth > 0) {
		heldAnswers().push_back(std::move(answer));
		return;
	}
	deliverAnswer(answer);
}

/// @brief The shipped claim dispatcher: what a browser choice's claim asks unless a spec installs another.
void requestSampleClaim(const deluge::gui::ClaimRequest& request) {
	deluge::gui::sampleClaim.request(request);
}

/// @brief What the claim is requested from. `setSampleClaimDispatch`'s subject.
SampleClaimDispatch claimDispatch = &requestSampleClaim;

/// @brief The file the shipped deleter is removing: the card's answer names no file.
std::string& pendingDelete() {
	static std::string path;
	return path;
}

/// @brief The card's answer to the shipped deleter's removal.
void unlinkDone(void*, deluge::storage::bytes::SettingsOutcome outcome) {
	using deluge::storage::bytes::SettingsOutcome;
	shellFileDeleteDone(pendingDelete(), outcome == SettingsOutcome::Written  ? FileDeleteOutcome::Removed
	                                     : outcome == SettingsOutcome::Absent ? FileDeleteOutcome::Missing
	                                                                          : FileDeleteOutcome::Failed);
}

/// @brief During boot, where blocking on the card is legal: the removal inline.
void unlinkInline(void*) {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Browser);
	auto removed = deluge::io::unlink(pendingDelete());
	shellFileDeleteDone(pendingDelete(), removed.has_value() ? FileDeleteOutcome::Removed
	                                     : removed.error() == deluge::io::Status::NOT_FOUND
	                                         ? FileDeleteOutcome::Missing
	                                         : FileDeleteOutcome::Failed);
}

/// @brief The shipped deleter: `DeleteFile::acceptCurrentOption`'s removal, through the card's byte channel.
void deleteFromCard(std::string_view path) {
	namespace bytes = deluge::storage::bytes;
	pendingDelete() = std::string{path};
	if (bytes::usingByteChannel()) {
		if (!bytes::submitUnlink(pendingDelete().c_str(), &unlinkDone, nullptr)) {
			shellFileDeleteDone(pendingDelete(), FileDeleteOutcome::Failed); // the path does not fit the channel
		}
		return;
	}
	if (!bytes::runDuringBoot(&unlinkInline, nullptr)) {
		shellFileDeleteDone(pendingDelete(), FileDeleteOutcome::Failed);
	}
}

/// @brief What removes a file from the card. `setFileDeleteDispatch`'s subject.
FileDeleteDispatch deleteDispatch = &deleteFromCard;

/// @brief The shipped auto-load cancel.
void cancelSampleAutoLoad() {
	deluge::gui::sampleClaim.cancelAutoLoad();
}

/// @brief What cancels an auto-load claim. `setSampleClaimCancel`'s subject.
SampleClaimCancel claimCancel = &cancelSampleAutoLoad;

/// @brief The shipped folder-import dispatcher.
void requestFolderImport(deluge::gui::FolderImportRequest request) {
	deluge::gui::folderImport.request(std::move(request));
}

/// @brief What a folder import is requested from. `setFolderImportDispatch`'s subject.
FolderImportDispatch importDispatch = &requestFolderImport;

/// @brief The browser's entered text for @p name, as `Browser::setEnteredTextFromCurrentFilename`
///        sets it for a file: the name with its extension cut off. A kit drum is named after it.
std::string enteredTextFor(std::string_view name) {
	const size_t dot = name.rfind('.');
	return std::string{dot == std::string_view::npos ? name : name.substr(0, dot)};
}

/// @brief The engine objects the menu's context names by address, resolved in the current song: what
///        `SoundEditor::setup` would have resolved for the same visit.
/// @param menuCtx The menu's context.
/// @return The sound, its source and range, the clip's (or kit row's) params and arp.
deluge::gui::SoundTarget resolvedFor(const deluge::ui::scenes::menu::MenuContext& menuCtx) {
	deluge::gui::SoundTarget resolved{.sound = nullptr,
	                                  .mod = {},
	                                  .params = nullptr,
	                                  .arp = nullptr,
	                                  .range = nullptr,
	                                  .rangeIndex = menuCtx.range_index,
	                                  .sourceIndex = menuCtx.source_index,
	                                  .source = nullptr};
	if (currentSong == nullptr) {
		return resolved;
	}
	// The sound, or the chain's owner, by address.
	if (menuCtx.addresses.chain) {
		resolved.mod = deluge::bridge::chain_owner_at(*menuCtx.addresses.chain, *currentSong);
	}
	::Clip* clip = menuCtx.addresses.clip ? currentSong->clipForId(menuCtx.addresses.clip->clip_id) : nullptr;
	auto* instrumentClip =
	    clip != nullptr && clip->type == ClipType::INSTRUMENT ? static_cast<InstrumentClip*>(clip) : nullptr;
	if (clip != nullptr) {
		resolved.params = &clip->paramManager;
		resolved.arp = instrumentClip != nullptr ? &instrumentClip->arpSettings : nullptr;
	}
	if (menuCtx.addresses.slot) {
		::Sound* sound = deluge::bridge::soundAt(*currentSong, *menuCtx.addresses.slot);
		resolved.sound = sound;
		resolved.mod = sound != nullptr ? deluge::model::LaneOwner{sound} : deluge::model::LaneOwner{};
		if (sound != nullptr) {
			resolved.source = &sound->sourceResources[menuCtx.source_index];
			if (menuCtx.range_index >= 0 && menuCtx.range_index < std::ssize(resolved.source->ranges)) {
				resolved.range = resolved.source->ranges.getElement(menuCtx.range_index);
			}
			if (sound->isDrum() && instrumentClip != nullptr) {
				auto* drum = static_cast<SoundDrum*>(sound);
				NoteRow* row = instrumentClip->getNoteRowForDrum(drum);
				resolved.params = row != nullptr ? &row->paramManager : nullptr;
				resolved.arp = &drum->arpSettings;
			}
		}
	}
	return resolved;
}

/// @brief Load the chosen sample, the way `SampleBrowser::enterKeyPress`'s file arm does.
///
/// The request names the sound the menu edits (`menuTarget`); the claim reads nothing else for it.
///
/// Only a claim reaches here: `BrowserScene` decides Shift+Select on a file in a kit with the press, asking its host
/// for a slice or refusing.
///
/// @param path   The chosen file's full path.
/// @param target @p path split; its filename names the entered text.
/// @param args       The detection choices: the browser's own for an Enter, a folder menu's for its claims.
/// @param completion Told how the claim ended.
/// @param origin     Explicit, or the auto-load of a landed preview, which the next move cancels.
void claimSampleFile(std::string_view path, const SampleClaimTarget& target, const SampleClaimArgs& args,
                     void (*completion)(bool success),
                     deluge::gui::ClaimOrigin origin = deluge::gui::ClaimOrigin::Explicit) {
	claimDispatch({
	    .path = std::string{path},
	    .enteredText = enteredTextFor(target.name),
	    .mayDoPitchDetection = args.mayDoPitchDetection,
	    .mayDoSingleCycle = args.mayDoSingleCycle,
	    .mayDoWaveTable = args.mayDoWaveTable,
	    .origin = origin,
	    .completion = completion,
	    .target = menuTarget(),
	});
}

/// @brief `FolderImportRequest::rangeChosen` for an import the browser scene asked for: the range the import makes
///        current goes to the menu beneath, as `SoundEditor::setCurrentMultiRange` makes it the legacy editor's.
void shellImportRangeChosen(int32_t index) {
	deluge::ui::scenes::menu_scene().import_chose_range(index);
}

/// @brief The folder import for a folder menu's choice: the item's own folder, or the folder it is in, starting
///        from it. The browser scene closes itself, so the import does not.
void importFolder(const BrowserChoice& choice, deluge::gui::FolderImportKind kind) {
	if (deluge::gui::sampleClaim.explicitInFlight() || deluge::gui::folderImport.inFlight()) {
		return; // the gate refuses the presses that would ask; nothing that does is left to answer
	}
	// An auto-load claim would bind into the sound the import is replacing.
	claimCancel();
	deluge::gui::FolderImportRequest request{.kind = kind,
	                                         .completion = &shellLongPressImportDone,
	                                         .target = menuTarget(),
	                                         .rangeChosen = &shellImportRangeChosen};
	if (choice.is_folder) {
		request.folder = choice.path;
	}
	else {
		request.folder = choice.dir;
		request.previouslyViewedFilename = choice.filename;
	}
	importDispatch(std::move(request));
}

/// @brief Announce how a MIDI device definition's load ended and answer the waiting browser, as
///        `LoadMidiDeviceDefinitionUI::definitionLoaded` does: an error leaves it open, a load closes it.
void midiDefinitionLoaded(Error error) {
	if (error != Error::NONE) {
		deluge::base::notify::notice(error);
		answerBrowser({.kind = BrowserAnswer::Kind::DeferredLoadDone, .success = false});
		return;
	}
	deluge::base::notify::notice(deluge::l10n::String::STRING_FOR_MIDI_DEVICE_LOADED,
	                             deluge::base::notify::Notice::Kind::Console);
	answerBrowser({.kind = BrowserAnswer::Kind::DeferredLoadDone, .success = true});
}

/// @brief Load the chosen MIDI device definition into the current MIDI output, as
///        `LoadMidiDeviceDefinitionUI::enterKeyPress`'s file arm does.
/// @param path The definition file's full path.
void requestMidiDefinitionLoad(std::string_view path) {
	Output* output = getCurrentOutput();
	if (output == nullptr || output->type != OutputType::MIDI_OUT) {
		// Nothing started: the browser must not wait for it.
		answerBrowser({.kind = BrowserAnswer::Kind::DeferredLoadDone, .success = false});
		return;
	}
	const deluge::gui::LoadTicket ticket = deluge::gui::instrumentLoad.request(
	    {.commit = true,
	     .completion = midiDefinitionLoaded,
	     .kind = deluge::gui::InstrumentLoadRequest::Kind::MidiDefinition,
	     .definition = {.path = std::string{path}, .instrument = static_cast<MIDIInstrument*>(output)}});
	if (ticket.generation == 0) {
		// Refused: no completion will answer.
		answerBrowser({.kind = BrowserAnswer::Kind::DeferredLoadDone, .success = false});
	}
}

} // namespace

SamplePreviewer& shellPreviewer() {
	static SamplePreviewer previewer{&deluge::model::lastFilePathLoaded()};
	return previewer;
}

namespace {

/// @brief The sound a preview is headed for: the menu's resolved sound and params (a kit row's, for a drum), and for a
///        kit its clip, whose output volume the preview folds in -- what `main`'s browser reads from the sound editor.
PreviewSound previewSoundFor(const deluge::ui::scenes::menu::MenuContext& menuCtx) {
	const deluge::gui::SoundTarget resolved = resolvedFor(menuCtx);
	::Clip* clip = currentSong != nullptr && menuCtx.addresses.clip
	                   ? currentSong->clipForId(menuCtx.addresses.clip->clip_id)
	                   : nullptr;
	return PreviewSound{
	    .sound = resolved.sound,
	    .params = resolved.params,
	    .kit = menuCtx.is_kit,
	    .clip = clip != nullptr && clip->type == ClipType::INSTRUMENT ? static_cast<InstrumentClip*>(clip) : nullptr,
	};
}

/// @brief Tick the preview in hand, for the auto-load of main's `SampleBrowser::previewIfPossible`
///        (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): a load that ends is answered through
///        `BrowserScene::preview_settled`.
void tickPreview() {
	SamplePreviewer& previewer = shellPreviewer();
	if (!previewer.pending()) {
		return;
	}
	if (const std::optional<SamplePreviewer::Landed> landed =
	        previewer.tick(previewSoundFor(deluge::ui::scenes::menu_scene().menu_context()))) {
		answerBrowser({.kind = BrowserAnswer::Kind::PreviewSettled, .success = landed->hasSample});
	}
}

/// @brief Start legacy's horizontal scroll for the browser scene, as `SampleBrowser` does where it calls
///        `PadLEDs::horizontal::setupScroll`: the root view, or the browser's main pads off the guest's grid, into the
///        image store (or nothing, for the scroll out), and the guest's main columns left to the animation until
///        `ShellHostUI::scrollFinished`.
void startBrowserScroll(const deluge::ui::scenes::browser::ScrollRequest& scroll) {
	GuestSurfaceSet& surfaces = deluge::gui::ui::shellRuntime().surfaces();
	if (scroll.to_root) {
		getRootUI()->renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	}
	else if (!scroll.into_nothing) {
		const deluge::ui::gfx::grid::PadGrid& grid = surfaces.grid().grid();
		const int32_t rows = std::min<int32_t>(grid.dims().rows, kDisplayHeight);
		for (int32_t y = 0; y < rows; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				PadLEDs::imageStore[y][x] = grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
			}
		}
	}
	std::memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
	surfaces.hold_main_columns(true);
	PadLEDs::horizontal::setupScroll(scroll.direction, kDisplayWidth, scroll.into_nothing);
	currentUIMode = UI_MODE_HORIZONTAL_SCROLL;
}

/// @brief Run the browser's scroll, or keep the main columns for one it has asked for and not yet drawn; hand them
///        back if nothing scrolls or will (the browser left before its scroll began).
void dispatchScroll() {
	GuestSurfaceSet& surfaces = deluge::gui::ui::shellRuntime().surfaces();
	if (const std::optional<deluge::ui::scenes::browser::ScrollRequest> scroll = browser_scene().take_scroll()) {
		startBrowserScroll(*scroll);
		return;
	}
	if (browser_scene().scroll_pending()) {
		surfaces.hold_main_columns(true);
		return;
	}
	if (surfaces.main_columns_held() && !isUIModeActive(UI_MODE_HORIZONTAL_SCROLL)) {
		surfaces.hold_main_columns(false);
		surfaces.take_grid();
	}
}

} // namespace

void shellSampleClaimDone(bool success) {
	answerBrowser({.kind = BrowserAnswer::Kind::SampleClaimDone, .success = success});
}

void shellLongPressClaimDone(bool success) {
	answerBrowser({.kind = BrowserAnswer::Kind::LongPressClaimDone, .success = success});
}

void shellLongPressImportDone(bool success) {
	answerBrowser({.kind = BrowserAnswer::Kind::LongPressImportDone, .success = success});
}

void shellSliceReady(bool opened) {
	answerBrowser({.kind = BrowserAnswer::Kind::SliceReady, .success = opened});
}

void setFolderImportDispatch(FolderImportDispatch dispatch) {
	importDispatch = dispatch != nullptr ? dispatch : &requestFolderImport;
}

void setSampleClaimDispatch(SampleClaimDispatch dispatch) {
	claimDispatch = dispatch != nullptr ? dispatch : &requestSampleClaim;
}

void shellFileDeleteDone(std::string_view path, FileDeleteOutcome outcome) {
	answerBrowser({.kind = BrowserAnswer::Kind::FileDeleteDone, .path = std::string{path}, .deleted = outcome});
}

void setFileDeleteDispatch(FileDeleteDispatch dispatch) {
	deleteDispatch = dispatch != nullptr ? dispatch : &deleteFromCard;
}

void setSampleClaimCancel(SampleClaimCancel cancel) {
	claimCancel = cancel != nullptr ? cancel : &cancelSampleAutoLoad;
}

std::optional<SampleClaimTarget> sampleClaimTargetFor(std::string_view path) {
	// `SampleBrowser::getCurrentFilePath` run backwards: it appends the separator only when
	// `currentDir` is non-empty, so a path with no separator is a file at the card root and the whole
	// of it is the filename.
	const size_t slash = path.rfind('/');
	if (slash == std::string_view::npos) {
		if (path.empty()) {
			return std::nullopt;
		}
		return SampleClaimTarget{.dir = {}, .name = path};
	}
	// A trailing separator names a directory, not a file. `BrowserScene::enter_key_press` cannot build one --
	// it appends `item->filename`, which a `FileItem` never has empty -- so this is defence in depth against a caller
	// that is not the scene, in the same spirit as `openPerformanceView`'s `DISABLED` arm
	// (`gui/ui/shell_action_dispatch.cpp`) and unreachable for the same kind of reason.
	const std::string_view name = path.substr(slash + 1);
	if (name.empty()) {
		return std::nullopt;
	}
	return SampleClaimTarget{.dir = path.substr(0, slash), .name = name};
}

void setShellBrowserFromDrumCreator(bool fromCreator) {
	browser_scene().set_drum_creator_next(fromCreator);
}

namespace {
/// What `publishMenuTarget` last published. Playback automation reads it on the audio tier, so it is one lock-free
/// word: an owner is its pointer and its alternative's index.
std::atomic<uint64_t> publishedOwner{0};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
std::atomic<ParamManagerForTimeline*> publishedParams{nullptr};
} // namespace

void publishMenuTarget() {
	const deluge::gui::SoundTarget target = menuTarget();
	publishedOwner.store(deluge::model::pack_owner(target.mod), std::memory_order_relaxed);
	publishedParams.store(target.params, std::memory_order_relaxed);
}

deluge::model::LaneOwner sound_menu_owner() {
	return deluge::model::unpack_owner(publishedOwner.load(std::memory_order_relaxed));
}

ParamManagerForTimeline* soundMenuParamManager() {
	return publishedParams.load(std::memory_order_relaxed);
}

deluge::gui::SoundTarget menuTarget() {
	return resolvedFor(deluge::ui::scenes::menu_scene().menu_context());
}

namespace {

/// @brief What the `.syx` read's standing means for the browser: a bank read lands the menu on the cartridge page and
///        closes the browser (`DxSyxBrowser`'s `enterLoadedCartridge`); a failure leaves the browser up for another
///        file.
void deliverDxCartridgeRead(DxCartridgeRead::Status status) {
	switch (status) {
	case DxCartridgeRead::Status::Loaded:
		deluge::ui::scenes::menu_scene().land_dx_cartridge();
		browser_scene().deferred_load_done(true);
		break;
	case DxCartridgeRead::Status::Failed:
	case DxCartridgeRead::Status::Idle: // nothing started: the browser must not wait for it
		browser_scene().deferred_load_done(false);
		break;
	case DxCartridgeRead::Status::Pending:
		break;
	}
}

/// @brief Answer the browser with where the `.syx` read stands, once it has ended or could not start.
void dxCartridgeReadSettled(DxCartridgeRead::Status status) {
	if (status != DxCartridgeRead::Status::Pending) {
		answerBrowser({.kind = BrowserAnswer::Kind::DxReadSettled, .dx = status});
	}
}

/// @brief Collect the `.syx` browser scene's read on the guest's tick; a browser that has gone takes its read with
///        it.
void pollDxCartridgeRead() {
	DxCartridgeRead& read = dxCartridgeRead();
	if (!read.busy()) {
		return;
	}
	if (!browser_scene().awaiting_deferred_load()) {
		read.cancel();
		return;
	}
	dxCartridgeReadSettled(read.poll());
}

/// @brief Perform what the browser scene asks its host for: the claim, import, slice, deletion, MIDI definition load
///        or `.syx` read it names.
void dispatchChoice(const BrowserChoice& choice) {
	switch (choice.id) {
	case BrowserId::Sample:
		// The split is
		// made before anything acts, so `tests/integration/ui/scenes/browser_choice_spec.cpp` can pin the
		// decision over every path shape without the arm below it having to be reachable.
		switch (choice.op) {
		case BrowserChoice::Op::Enter:
			if (const std::optional<SampleClaimTarget> target = sampleClaimTargetFor(choice.path)) {
				if (choice.keep_open) {
					// Auto-load: nothing to close and nothing to re-root, so no completion.
					claimSampleFile(choice.path, *target, kShellSampleClaimArgs, nullptr,
					                deluge::gui::ClaimOrigin::AutoLoad);
				}
				else {
					claimSampleFile(choice.path, *target, kShellSampleClaimArgs, &shellSampleClaimDone);
				}
			}
			break;
		case BrowserChoice::Op::Claim:
			if (const std::optional<SampleClaimTarget> target = sampleClaimTargetFor(choice.path)) {
				claimSampleFile(choice.path, *target,
				                {.mayDoPitchDetection = choice.claim.pitch,
				                 .mayDoSingleCycle = choice.claim.single_cycle,
				                 .mayDoWaveTable = choice.claim.wavetable},
				                &shellLongPressClaimDone);
			}
			break;
		case BrowserChoice::Op::ImportKit:
			importFolder(choice, deluge::gui::FolderImportKind::Kit);
			break;
		case BrowserChoice::Op::ImportMultisamples:
			importFolder(choice, deluge::gui::FolderImportKind::Multisamples);
			break;
		case BrowserChoice::Op::Slice:
			openShellSlice(choice.path);
			break;
		case BrowserChoice::Op::DeleteFile:
			deleteDispatch(choice.path);
			break;
		}
		break;

	case BrowserId::MidiDeviceDefinition:
		requestMidiDefinitionLoad(choice.path);
		break;

	case BrowserId::DxSyx:
		if (choice.op == BrowserChoice::Op::DeleteFile) {
			deleteDispatch(choice.path);
			break;
		}
		dxCartridgeReadSettled(dxCartridgeRead().start(choice.path));
		break;

	case BrowserId::Count:
		// Not a browser -- it is the table's size (`BrowserId::Count`). Unreachable:
		// `BrowserScene::enter_key_press` files `id_`, and `BrowserScene::on_enter`
		// refuses an out-of-range arg outright before assigning it. Spelled out because `-Werror=switch` counts it, and
		// the alternative is the `default:` the header rules out.
		break;
	}
}

/// @brief A chosen file's claim ended: the waiting browser hears first (a success closes it, a failure leaves it up
///        for another file, as `claimCurrentFile`'s error arm leaves `main`'s), then a success re-roots the menu
///        beneath over the claimed source.
void deliverSampleClaimDone(bool success) {
	// Ignored by a browser that is not waiting (a folder menu's claim).
	browser_scene().deferred_load_done(success);
	if (!success) {
		return;
	}
	uiNeedsRendering(&audioClipView);
	// Answers false, harmlessly, when there is nothing to move: only the guest's menu can have
	// opened the browser scene, so a claim with no recorded opener came from elsewhere.
	if (deluge::ui::scenes::menu_scene().reroot_to_scene_opener_page()) {
		// Legacy re-enters its editor here (`SampleBrowser::claimCurrentFile`'s `enterOrUpdateSoundEditor`), whose
		// write to `setupKitGlobalFXMenu` the latch takes.
		latchMenuKitGlobalFxOnReentry();
	}
}

/// @brief The card answered a deletion: `main`'s popup, and the file leaves the browser's list once it is gone.
void deliverFileDeleteDone(std::string_view path, FileDeleteOutcome outcome) {
	switch (outcome) {
	case FileDeleteOutcome::Removed:
		deluge::base::notify::notice(deluge::l10n::String::STRING_FOR_FILE_DELETED);
		browser_scene().file_deleted(path);
		break;
	case FileDeleteOutcome::Missing:
		// Not on the card: a sample's list entry has no instrument to drop, so nothing changes.
		break;
	case FileDeleteOutcome::Failed:
		deluge::base::notify::notice(deluge::l10n::String::STRING_FOR_ERROR_DELETING_FILE);
		break;
	}
}

/// @brief Tell the browser, and the menu beneath it, how a request ended.
void deliverAnswer(const BrowserAnswer& answer) {
	switch (answer.kind) {
	case BrowserAnswer::Kind::SampleClaimDone:
		deliverSampleClaimDone(answer.success);
		break;
	case BrowserAnswer::Kind::LongPressClaimDone:
		browser_scene().long_press_done(answer.success);
		deliverSampleClaimDone(answer.success);
		break;
	case BrowserAnswer::Kind::LongPressImportDone:
		browser_scene().long_press_done(answer.success);
		break;
	case BrowserAnswer::Kind::FileDeleteDone:
		deliverFileDeleteDone(answer.path, answer.deleted);
		break;
	case BrowserAnswer::Kind::DeferredLoadDone:
		browser_scene().deferred_load_done(answer.success);
		break;
	case BrowserAnswer::Kind::DxReadSettled:
		deliverDxCartridgeRead(answer.dx);
		break;
	case BrowserAnswer::Kind::SliceReady:
		browser_scene().slice_ready(answer.success);
		break;
	case BrowserAnswer::Kind::PreviewSettled:
		browser_scene().preview_settled(answer.success);
		break;
	}
}

/// @brief The browser scene's host: each request is performed when the browser makes it, and an answer it produces
///        before returning is held (`Performing`).
class ShellBrowserHost final : public deluge::ui::scenes::browser::BrowserHost {
public:
	void perform(const BrowserChoice& choice) override {
		const Performing performing;
		dispatchChoice(choice);
	}
	/// A file is previewed as main's `SampleBrowser::previewIfPossible`
	/// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) does; an empty path stops previewing as
	/// `AudioEngine::stopAnyPreviewing` does.
	void preview(const PreviewRequest& request) override {
		const Performing performing;
		// A new preview supersedes an older one's held answer: delivered now, it would end this preview's wait.
		std::erase_if(heldAnswers(),
		              [](const BrowserAnswer& answer) { return answer.kind == BrowserAnswer::Kind::PreviewSettled; });
		// A move supersedes an auto-load claim of the file it has left.
		claimCancel();
		SamplePreviewer& previewer = shellPreviewer();
		if (request.path.empty()) {
			previewer.cancel();
			AudioEngine::stopAnyPreviewing();
			answerBrowser({.kind = BrowserAnswer::Kind::PreviewSettled, .success = false});
			return;
		}
		previewer.request(request.path, request.direction);
	}
};

/// @brief The one host, made on first use.
ShellBrowserHost& shellBrowserHost() {
	static ShellBrowserHost host;
	return host;
}

} // namespace

void pollShellBrowserHost() {
	// The held answers first: a scroll one asks for is started below, before the next tick renders the pads.
	deliverShellBrowserAnswers();
	pollDxCartridgeRead();
	tickPreview();
	dispatchScroll();
}

void deliverShellBrowserAnswers() {
	// The batch held so far. An answer a delivery's own request produces is held for the next delivery.
	const std::vector<BrowserAnswer> batch = std::exchange(heldAnswers(), {});
	for (const BrowserAnswer& answer : batch) {
		deliverAnswer(answer);
	}
}

bool shellBrowserAnswersHeld() {
	return !heldAnswers().empty();
}

void wireShellBrowserHost() {
	browser_scene().set_host(&shellBrowserHost());
}

bool shellVisitFellBackToEditor() {
	return deluge::ui::scenes::menu_scene().pushed_scene_refused();
}

} // namespace deluge::gui::ui
