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

#include "gui/l10n/strings.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"
#include "util/notify.h"

#include <optional>
#include <string>
#include <string_view>

namespace deluge::gui::ui {

using deluge::scenes::browser::browser_scene;
using deluge::scenes::browser::BrowserChoice;
using deluge::scenes::browser::BrowserId;

namespace {

/// @brief The shipped claim dispatcher: the one `SampleBrowser::requestClaim` asks.
void requestSampleClaim(const deluge::gui::ClaimRequest& request) {
	deluge::gui::sampleClaim.request(request);
}

/// @brief What the claim is requested from. `setSampleClaimDispatch`'s subject.
SampleClaimDispatch claimDispatch = &requestSampleClaim;

/// @brief The browser's entered text for @p name, as `Browser::setEnteredTextFromCurrentFilename`
///        sets it for a file: the name with its extension cut off. A kit drum is named after it.
std::string enteredTextFor(std::string_view name) {
	const size_t dot = name.rfind('.');
	return std::string{dot == std::string_view::npos ? name : name.substr(0, dot)};
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
/// @warning The file arm is a fan-out and only its `else` is deferred: shift+select on a kit opens
///          the slicer in the legacy browser, and here loads the sample. `Descriptor::accept` carries
///          no shift bit, and this runs a tick after the press, so the shift state cannot be read
///          here. Closing it means the shift state travelling on `BrowserChoice`.
///
/// @param path   The chosen file's full path.
/// @param target @p path split; its filename names the entered text.
void claimSampleFile(std::string_view path, const SampleClaimTarget& target) {
	const deluge::scenes::menu::MenuContext& menuCtx = deluge::scenes::menu_scene().menu_context();
	soundEditor.adoptResolvedContext(SoundEditor::ResolvedContext{
	    .sound = menuCtx.sound,
	    .mod = menuCtx.mod,
	    .params = menuCtx.params,
	    .arp = menuCtx.arp,
	    .range = menuCtx.range,
	    .rangeIndex = menuCtx.range_index,
	    .sourceIndex = menuCtx.source_index,
	    .source = menuCtx.source,
	    .sampleControls = menuCtx.sample_controls,
	});
	claimDispatch({
	    .path = std::string{path},
	    .enteredText = enteredTextFor(target.name),
	    .mayDoPitchDetection = kShellSampleClaimArgs.mayDoPitchDetection,
	    .mayDoSingleCycle = kShellSampleClaimArgs.mayDoSingleCycle,
	    .mayDoWaveTable = kShellSampleClaimArgs.mayDoWaveTable,
	    .loadWithoutExiting = kShellSampleClaimArgs.loadWithoutExiting,
	    .origin = deluge::gui::ClaimOrigin::Explicit,
	    .completion = &shellSampleClaimDone,
	});
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

void setSampleClaimDispatch(SampleClaimDispatch dispatch) {
	claimDispatch = dispatch != nullptr ? dispatch : &requestSampleClaim;
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
	// A trailing separator names a directory, not a file. `BrowserScene::enter` cannot build one --
	// it appends `item->filename`, which a `FileItem` never has empty
	// (`scenes/browser/browser_scene.cpp:880-884`) -- so this is defence in depth against a caller
	// that is not the scene, in the same spirit as `openPerformanceView`'s `DISABLED` arm
	// (`gui/ui/shell_action_dispatch.cpp:86-88`) and unreachable for the same kind of reason.
	const std::string_view name = path.substr(slash + 1);
	if (name.empty()) {
		return std::nullopt;
	}
	return SampleClaimTarget{.dir = path.substr(0, slash), .name = name};
}

void dispatchShellChoices() {
	// One read, unconditionally, and the read is what empties the slot -- `take_choice()` moves the
	// choice out and resets the member in the same call (`scenes/browser/browser_scene.cpp:417-421`).
	// So an early return above this line would not be "nothing to do", it would be "the choice stays
	// in the outbox until something else asks", which is the stale delivery the header describes.
	const std::optional<BrowserChoice> choice = browser_scene().take_choice();
	if (!choice.has_value()) {
		return;
	}

	switch (choice->id) {
	case BrowserId::Sample:
		// The split is
		// made before anything acts, so `sim/scenes_spec/browser_choice_spec.cpp` can pin the
		// decision over every path shape without the arm below it having to be reachable.
		if (const std::optional<SampleClaimTarget> target = sampleClaimTargetFor(choice->path)) {
			claimSampleFile(choice->path, *target);
		}
		break;

	case BrowserId::MidiDeviceDefinition:
		requestMidiDefinitionLoad(choice->path);
		break;

	case BrowserId::Count:
		// Not a browser -- it is the table's size (`scenes/browser/browser_scene.h:118`). Unreachable:
		// `BrowserScene::enter` files `id_` (`browser_scene.cpp:897`), and `BrowserScene::on_enter`
		// refuses the scene outright before assigning it (`:357-361`, the guard at `:358-360`) rather
		// than letting an out-of-range arg through. Spelled out because `-Werror=switch` counts it, and the
		// alternative is the `default:` the header rules out.
		break;
	}
}

} // namespace deluge::gui::ui
