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
/// @brief The choice seam's consumer. See `shell_choice_dispatch.h` for why it lives here, why its
///        caller may not gate it, and what the split between the choosing and acting halves is for.

#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/shell_slice_ops.h"

#include "gui/l10n/strings.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/browser/sample_preview.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "hid/led/pad_leds.h"
#include "io/file.hpp"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"
#include "storage/byte_channel.h"
#include "util/notify.h"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::gui::ui {

using deluge::scenes::browser::browser_scene;
using deluge::scenes::browser::BrowserChoice;
using deluge::scenes::browser::BrowserId;
using deluge::scenes::browser::PreviewRequest;

namespace {

/// @brief The shipped claim dispatcher: the one `SampleBrowser::requestClaim` asks.
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
/// @return The sound, its source, range and sample controls, the clip's (or kit row's) params and arp.
SoundEditor::ResolvedContext resolvedFor(const deluge::scenes::menu::MenuContext& menuCtx) {
	SoundEditor::ResolvedContext resolved{.sound = nullptr,
	                                      .mod = nullptr,
	                                      .params = nullptr,
	                                      .arp = nullptr,
	                                      .range = nullptr,
	                                      .rangeIndex = menuCtx.range_index,
	                                      .sourceIndex = menuCtx.source_index,
	                                      .source = nullptr,
	                                      .sampleControls = nullptr,
	                                      .kitGlobalFx = menuCtx.is_kit_global_fx};
	if (currentSong == nullptr) {
		return resolved;
	}
	// The sound, or the chain's owner, by address.
	if (menuCtx.addresses.chain) {
		resolved.mod = deluge::bridge::chainOwnerAt(*menuCtx.addresses.chain, *currentSong);
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
		resolved.mod = sound;
		if (sound != nullptr) {
			resolved.source = &sound->sources[menuCtx.source_index];
			resolved.sampleControls = &resolved.source->sampleControls;
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
	else if (menuCtx.is_audio_clip && clip != nullptr) {
		resolved.sampleControls = &static_cast<AudioClip*>(clip)->sampleControls;
	}
	return resolved;
}

/// @brief Load the chosen sample, the way `SampleBrowser::enterKeyPress`'s file arm does.
///
/// The claim reads the editor's resolved context (`soundEditor.currentSound`, `currentSource`,
/// `currentMultiRange` and their indices) throughout, and under the toggle `SoundEditor::setup`
/// never ran -- `openShellMenu` opens the guest instead, so on a fresh boot `currentSound` is still
/// `nullptr`. The context is mirrored first from `MenuScene`'s own `MenuContext`, the one
/// `openShellMenu` resolved for this visit, so it is a mirror of a resolution rather than a second
/// one. See `SoundEditor::adoptResolvedContext`.
///
/// Only a claim reaches here: Shift+Select on a file in a kit is decided by `BrowserScene` with the press, which parks
/// a slice or refuses, so the shift state never has to be read a tick later.
///
/// @param path   The chosen file's full path.
/// @param target @p path split; its filename names the entered text.
/// @param args       The detection choices: the browser's own for an Enter, a folder menu's for its claims.
/// @param completion Told how the claim ended.
/// @param origin     Explicit, or the auto-load of a landed preview, which the next move cancels.
void claimSampleFile(std::string_view path, const SampleClaimTarget& target, const SampleClaimArgs& args,
                     void (*completion)(bool success),
                     deluge::gui::ClaimOrigin origin = deluge::gui::ClaimOrigin::Explicit) {
	adoptShellMenuContext();
	claimDispatch({
	    .path = std::string{path},
	    .enteredText = enteredTextFor(target.name),
	    .mayDoPitchDetection = args.mayDoPitchDetection,
	    .mayDoSingleCycle = args.mayDoSingleCycle,
	    .mayDoWaveTable = args.mayDoWaveTable,
	    .loadWithoutExiting = args.loadWithoutExiting,
	    .origin = origin,
	    .completion = completion,
	});
}

/// @brief `SampleBrowser::requestFolderImport` (`sample_browser.cpp:437-453`) for a folder menu's choice: the item's
///        own folder, or the folder it is in, starting from it. The ported browser closes itself, so the import does
///        not.
void importFolder(const BrowserChoice& choice, deluge::gui::FolderImportKind kind) {
	if (deluge::gui::sampleClaim.explicitInFlight() || deluge::gui::folderImport.inFlight()) {
		return; // the gate refuses the presses that would ask; nothing that does is left to answer
	}
	// An auto-load claim would bind into the sound the import is replacing.
	claimCancel();
	soundEditor.adoptResolvedContext(resolvedFor(deluge::scenes::menu_scene().menu_context()));
	deluge::gui::FolderImportRequest request{
	    .kind = kind, .completion = &shellLongPressImportDone, .exitBrowser = false};
	if (choice.is_folder) {
		request.folder = choice.path;
	}
	else {
		request.folder = choice.dir;
		request.previouslyViewedFilename = choice.filename;
	}
	importDispatch(std::move(request));
}

/// @brief Announce how a MIDI device definition's load ended, as
///        `LoadMidiDeviceDefinitionUI::definitionLoaded` does, minus closing a browser that is gone.
void midiDefinitionLoaded(Error error) {
	if (error != Error::NONE) {
		deluge::notify::notice(error);
		return;
	}
	deluge::notify::notice(deluge::l10n::String::STRING_FOR_MIDI_DEVICE_LOADED, deluge::notify::Notice::Kind::Console);
}

/// @brief Load the chosen MIDI device definition into the current MIDI output, as
///        `LoadMidiDeviceDefinitionUI::enterKeyPress`'s file arm does.
/// @param path The definition file's full path.
void requestMidiDefinitionLoad(std::string_view path) {
	Output* output = getCurrentOutput();
	if (output == nullptr || output->type != OutputType::MIDI_OUT) {
		return;
	}
	(void)deluge::gui::instrumentLoad.request(
	    {.commit = true,
	     .completion = midiDefinitionLoaded,
	     .kind = deluge::gui::InstrumentLoadRequest::Kind::MidiDefinition,
	     .definition = {.path = std::string{path}, .instrument = static_cast<MIDIInstrument*>(output)}});
}

/// @brief The ported browser's preview. Its own previewer (the legacy browser keeps one too; only one browser is up at
///        a time), recording each load's path as legacy's browser's last file loaded, which both browsers open beside.
SamplePreviewer& shellPreviewer() {
	static SamplePreviewer previewer{&sampleBrowser.lastFilePathLoaded};
	return previewer;
}

/// @brief The sound a ported preview is headed for: the menu's resolved sound and params (a kit row's, for a drum), and
///        for a kit its clip, whose output volume the preview folds in -- what the legacy browser reads from the sound
///        editor.
PreviewSound previewSoundFor(const deluge::scenes::menu::MenuContext& menuCtx) {
	const SoundEditor::ResolvedContext resolved = resolvedFor(menuCtx);
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

/// @brief Act on the ported browser's preview request, and tick the preview in hand: a file is previewed as the legacy
///        browser's `previewFile` does, an empty path stops previewing as its `cancelPreview` and `stopAnyPreviewing`
///        do; a load that ends, or a stop, is answered through `BrowserScene::preview_settled`.
void dispatchPreview() {
	SamplePreviewer& previewer = shellPreviewer();
	if (const std::optional<PreviewRequest> request = browser_scene().take_preview()) {
		// A move supersedes an auto-load claim of the file it has left (`previewFile`, `cancelPreview`).
		claimCancel();
		if (request->path.empty()) {
			previewer.cancel();
			AudioEngine::stopAnyPreviewing();
			browser_scene().preview_settled(false);
		}
		else {
			previewer.request(request->path, request->direction);
		}
	}
	if (!previewer.pending()) {
		return;
	}
	if (const std::optional<SamplePreviewer::Landed> landed =
	        previewer.tick(previewSoundFor(deluge::scenes::menu_scene().menu_context()))) {
		browser_scene().preview_settled(landed->hasSample);
	}
}

/// @brief Start legacy's horizontal scroll for the ported browser, as `SampleBrowser` does where it calls
///        `PadLEDs::horizontal::setupScroll`: the root view, or the browser's main pads off the guest's grid, into the
///        image store (or nothing, for the scroll out), and the guest's main columns left to the animation until
///        `ShellHostUI::scrollFinished`.
void startBrowserScroll(const deluge::scenes::browser::ScrollRequest& scroll) {
	GuestSurfaceSet& surfaces = shellHostUI.surfaces();
	if (scroll.to_root) {
		getRootUI()->renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);
	}
	else if (!scroll.into_nothing) {
		const deluge::gfx::grid::PadGrid& grid = surfaces.grid().grid();
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
	GuestSurfaceSet& surfaces = shellHostUI.surfaces();
	if (const std::optional<deluge::scenes::browser::ScrollRequest> scroll = browser_scene().take_scroll()) {
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
	if (!success) {
		return;
	}
	uiNeedsRendering(&audioClipView);
	// Answers false, harmlessly, when there is nothing to move: only the guest's menu can have
	// opened the ported browser, so a claim with no recorded opener came from elsewhere.
	(void)deluge::scenes::menu_scene().reroot_to_scene_opener_page();
}

void shellLongPressClaimDone(bool success) {
	browser_scene().long_press_done(success);
	shellSampleClaimDone(success);
}

void shellLongPressImportDone(bool success) {
	browser_scene().long_press_done(success);
}

void setFolderImportDispatch(FolderImportDispatch dispatch) {
	importDispatch = dispatch != nullptr ? dispatch : &requestFolderImport;
}

void setSampleClaimDispatch(SampleClaimDispatch dispatch) {
	claimDispatch = dispatch != nullptr ? dispatch : &requestSampleClaim;
}

void shellFileDeleteDone(std::string_view path, FileDeleteOutcome outcome) {
	switch (outcome) {
	case FileDeleteOutcome::Removed:
		deluge::notify::notice(deluge::l10n::String::STRING_FOR_FILE_DELETED);
		browser_scene().file_deleted(path);
		break;
	case FileDeleteOutcome::Missing:
		// Not on the card: a sample's list entry has no instrument to drop, so nothing changes.
		break;
	case FileDeleteOutcome::Failed:
		deluge::notify::notice(deluge::l10n::String::STRING_FOR_ERROR_DELETING_FILE);
		break;
	}
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

void adoptShellMenuContext() {
	soundEditor.adoptResolvedContext(resolvedFor(deluge::scenes::menu_scene().menu_context()));
}

namespace {

/// @brief Perform a choice the ported browser parked: the claim, import, slice or MIDI definition load it names.
void dispatchChoice(const BrowserChoice& choice) {
	switch (choice.id) {
	case BrowserId::Sample:
		// The split is
		// made before anything acts, so `sim/scenes_spec/browser_choice_spec.cpp` can pin the
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
				                 .mayDoWaveTable = choice.claim.wavetable,
				                 .loadWithoutExiting = true},
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

	case BrowserId::Count:
		// Not a browser -- it is the table's size (`BrowserId::Count`). Unreachable:
		// `BrowserScene::enter_key_press` files `id_`, and `BrowserScene::on_enter`
		// refuses an out-of-range arg outright before assigning it. Spelled out because `-Werror=switch` counts it, and
		// the alternative is the `default:` the header rules out.
		break;
	}
}

} // namespace

void dispatchShellChoices() {
	// One read of each outbox, unconditionally, and the read is what empties the slot -- `take_choice()` and
	// `take_preview()` move the request out and reset the member in the same call. So an early return above either
	// read would not be "nothing to do", it would be "the request stays in the outbox until something else asks",
	// which is the stale delivery the header describes.
	if (const std::optional<BrowserChoice> choice = browser_scene().take_choice()) {
		dispatchChoice(*choice);
	}
	dispatchPreview();
	dispatchScroll();
}

} // namespace deluge::gui::ui
