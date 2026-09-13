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

#include "harness/song_preview_ab_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "gui/context_menu/clip_settings/new_clip_type.h" // the picker's chosen type, set by the grid ENTER probe
#include "gui/ui/load/load_instrument_preset_ui.h"        // presetAcquisitionsRun(), the acquire stage's inert counter
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/save/save_song_ui.h"   // the converted caller, driven end to end below
#include "gui/ui/ui.h"                  // currentUIMode, UI_MODE_NONE -- View's gestures gate on it
#include "gui/views/arranger_view.h"    // beginNewInstrumentAcquisition(), the acquire stage's REAL gesture entry point
#include "gui/views/session_view.h"     // the `sessionView` global driving SessionView's three gestures
#include "gui/views/view.h"             // the `view` global driving View's two acquisition gestures
#include "hid/button.h"                 // the SYNTH/CV buttons the Grid output-type gesture is pressed with
#include "hid/led/pad_leds.h"           // the image both backings render into
#include "hid/matrix/matrix_driver.h"   // padStates, which the audition-pad completion asks about
#include "io/file.hpp"                  // the card swap's unlink + rename
#include "libdeluge/file_directory.h"   // deluge_file_directory_set_latency_ms
#include "libdeluge/file_prefix.h"      // deluge_file_prefix_async_active
#include "model/clip/instrument_clip.h" // the boot song's session clip, driven by the View gestures
#include "model/mod_controllable/mod_controllable_audio.h" // the cast writeToFile's backed-up-paramManager lookup needs
#include "model/model_stack.h"                             // setupModelStackWithTimelineCounter
#include "model/output.h"                                  // Output::name, read by the acquire stage
#include "model/song/song.h"                               // currentSong, getNonAudioInstrumentToSwitchTo
#include "modulation/params/param_manager.h"               // ParamManagerForTimeline, for the preset fixture writer
#include "processing/sound/sound_instrument.h"             // setupAsBlankSynth(), for the preset fixture writer
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "storage/song_preview.h"
#include "storage/storage_manager.h"
#include "util/functions.h" // getInstrumentFolder, to pin kAcqDir against drift
#include "util/string.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines --
// this harness links the Rust BSP and has no provider, so the alias turns every line this file
// writes into an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

using deluge::storage::SongPreviewOutcome;

/// Drives the real `LoadSongUI` preview path, including its private members.
///
/// A `friend` of `LoadSongUI` (declared there), for the reason `SlicerPreviewProbe` is a friend of
/// `Slicer`: a gate that restated the caller instead of calling it would not have caught the defect
/// this exists for. Everything below goes through `drawSongPreview()` and `graphicsRoutine()` -- the
/// two surfaces the browser itself uses -- and reads only what no out-of-process observer can see.
class SongPreviewAbProbe {
public:
	/// Put the browser in the state a completed song listing leaves it in, with `filename` selected.
	///
	/// `drawSongPreview()` reads `getCurrentFileItem()` and builds `currentDir + "/" + filename`, so
	/// this is the whole of what a cursor move supplies. Rebuilt per case rather than mutated, so a
	/// case cannot inherit a stale entry from the one before it.
	static void select(const char* dir, const char* filename) {
		Browser::currentDir = dir;
		Browser::fileItems.clear();
		FileItem item;
		item.filename = filename;
		item.isFolder = false;
		item.filenameIncludesExtension = true;
		Browser::fileItems.push_back(item);
		// Protected in `Browser`; reachable here because this class is a friend of `LoadSongUI`, which
		// inherits it. Without it `getCurrentFileItem()` returns null and every case takes the
		// nothing-to-preview branch.
		LoadSongUI::fileIndexSelected = 0;
	}

	/// The ONE user-driven action of a case: the browser is told to preview the current selection,
	/// exactly as a cursor move tells it. Nothing after this calls it again.
	///
	/// `toStore = true` -- the value a cursor move passes, and the one this gate can actually read
	/// back. The render lands in `PadLEDs::imageStore`, the scroll store, which nothing else in a
	/// headless run writes. `PadLEDs::image` is NOT usable here: the app's own render pass repaints it
	/// from the current UI between steps, so a preview drawn there is overwritten before it can be
	/// fingerprinted -- and by a different amount on each leg, because the file backing renders one
	/// step earlier than the prefix backing does. (Found by running it: every `image` fingerprint came
	/// back as one constant, identical for songs with visibly different artwork.)
	static void draw() { (void)loadSongUI.drawSongPreview(/*toStore=*/true); }

	/// The per-tick surface, and the only thing driven once a case has started.
	static void tick() { loadSongUI.graphicsRoutine(); }

	/// Whether a preview deferred and is waiting to be asked for again.
	static bool pending() { return loadSongUI.previewLoad_.pending(); }

	/// Whether an op is dispatched and has not reported back (the synchronous backing's owner op).
	static bool inFlight() { return loadSongUI.previewLoad_.in_flight(); }

	/// What the last finished render produced. The half of the comparison no image can carry.
	/// @{
	static SongPreviewOutcome outcome() { return loadSongUI.lastPreviewOutcome_; }
	static Error error() { return loadSongUI.lastPreviewError_; }
	/// @}
};

/// Reaches Browser's listing internals. Declared a friend by `Browser`; defined here because this
/// is the only gate that drives them.
class BrowserListingAbProbe {
public:
	/// Start a listing of `dir`, exactly as opening a browser on it does.
	///
	/// `filenameToStartAt` must be mid-folder AND absent, and both halves were found by running it:
	///
	/// - With no search string at all, the cull deletes from the RIGHT of the window, so
	///   numFileItemsDeletedAtStart stays 0 and arrivedInNewFolder's re-read never fires. opens=1.
	/// - With a search string that matches EXACTLY, arrivedInNewFolder jumps straight to
	///   setEnteredTextAndUseFoundFile and never reaches the re-read either. Also opens=1.
	///
	/// Only a name that sorts into the middle of the folder and is not there takes `noExactFileFound`
	/// with entries culled from the start, which is the condition the second open exists for.
	static void beginOpenListing(const char* dir, const char* filenameToStartAt, const char* defaultDir = "") {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		(void)loadSongUI.beginListing({.action = Browser::ListingAction::Open,
		                               .direction = 0,
		                               .filenameToStartAt = filenameToStartAt,
		                               .defaultDir = defaultDir,
		                               .folderOrPath = ""});
	}
	/// Start the SAVE browser's listing of `dir`, to be driven by @ref tickSave.
	///
	/// saveSongUI, not loadSongUI: `mayDefaultToBrandNewNameOnEntry` is set only by SaveUI's
	/// constructor, and without it a listing never reaches `pickBrandNewName` at all.
	static void beginSaveListing(const char* dir) {
		Browser::currentDir = dir;
		Browser::enteredText.clear(); // the pickBrandNewName precondition
		(void)saveSongUI.beginListing({.action = Browser::ListingAction::Open,
		                               .direction = 0,
		                               .filenameToStartAt = "",
		                               .defaultDir = "",
		                               .folderOrPath = ""});
	}
	static void tick() { loadSongUI.graphicsRoutine(); }
	static void tickSave() { saveSongUI.graphicsRoutine(); }
	static bool inProgress() { return Browser::isListingInProgress(); }
	/// Whether the finished listing matched its search string EXACTLY. For a ByPath listing this IS
	/// the existence answer the retired fileExists() used to give.
	static bool foundExactMatch() { return Browser::listing_.foundExactMatch; }

	/// Start the REAL predictExtendedText on `dir`, typed as far as `prefix`, to be finished by @ref tick. Never a
	/// restatement of its loop: a probe that reimplemented it would pass against a browser that never re-asks.
	///
	/// It searches whatever list the browser already holds, as a keystroke does.
	static void beginPrediction(const char* dir, const char* prefix, std::optional<int32_t> undoPos) {
		Browser::currentDir = dir;
		Browser::enteredText = prefix;
		Browser::enteredTextEditPos = static_cast<int32_t>(strlen(prefix));
		(void)loadSongUI.predictExtendedText(undoPos);
	}
	/// Whether a prediction is still running. One that suspended stays active until the tick finishes it.
	static bool predicting() { return Browser::prediction_.active; }
	/// enteredTextEditPos afterwards -- what the retraction cases assert on.
	static int32_t editPos() { return Browser::enteredTextEditPos; }

	/// How many times the listing that just finished suspended.
	static int32_t deferrals() { return Browser::listing_.deferrals; }
	/// `Browser::fileIndexSelected` is protected; the snapshot needs it, and it is half of what
	/// "the listing finished the same way" means.
	static int32_t selectedIndex() { return Browser::fileIndexSelected; }
	/// Name the next free song slot in `dir`, as creating a new song does. Call on the storage owner.
	static std::string unusedSlotIn(const char* dir) {
		Browser::currentDir = dir;
		std::string name;
		const Error error = saveSongUI.getUnusedSlot(OutputType::NONE, &name, "SONG");
		return error == Error::NONE ? name : "error " + std::to_string(static_cast<int>(error));
	}

	/// Drives ArrangerView's REAL new-instrument pad gesture, rather than restating its callee
	/// (createNewInstrument) directly. Dispatches onto the storage owner (rung 5, task 3); see
	/// runAcquireTick()'s *Check states for why the result isn't readable until a later tick.
	static void beginNewInstrumentAcquisition(int32_t yPressed) {
		arrangerView.beginNewInstrumentAcquisition(yPressed);
	}
	/// Drives the arranger's audition-pad press through the handler MatrixDriver::padAction() actually
	/// reaches (ArrangerView::handleAuditionPadAction), one level ABOVE
	/// beginNewInstrumentAcquisition().
	///
	/// That distinction is the whole point: the gesture entry point above covers the card READ, but the
	/// press has a tail of its own -- audition sub-mode, output name, note, mod-controllable -- and
	/// since the acquisition became a dispatch the handler returns before any of it can run. Nothing
	/// covered that tail, and on the Embassy backing all of it silently stopped happening.
	///
	/// `padStates` is written first, exactly as MatrixDriver::padAction() does before handing the press
	/// to the UI. The completion asks the matrix whether the finger is still down before it starts a
	/// note; a headless driver that skipped this would look like an instantly-released pad and the
	/// audition half would correctly refuse to run, covering nothing.
	static void pressArrangerAuditionPad(int32_t y, bool on) {
		matrixDriver.padStates[kDisplayWidth + 1][y] = on;
		(void)arrangerView.handleAuditionPadAction(y, on ? 1 : 0, &arrangerView);
	}
	/// The first arranger row with no Output on it, or -1. The press has to land on an empty row for
	/// the new-instrument branch to be the one taken, and by this point in the stage most rows are
	/// occupied by earlier gestures.
	static int32_t firstEmptyArrangerRow() {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (arrangerView.outputsOnScreen[y] == nullptr) {
				return y;
			}
		}
		return -1;
	}
	/// Drives ArrangerView's REAL output-type gesture, rather than calling
	/// Song::getNonAudioInstrumentToSwitchTo directly: that helper isn't what changeOutputType()
	/// calls for MIDI/CV, so a probe built on it would prove nothing about ArrangerView's own
	/// synchronous/dispatch split (AcquireStep::NonAudio, rung 5 task 3).
	static void changeOutputType(OutputType newOutputType) { arrangerView.changeOutputType(newOutputType); }

	/// Drives View's REAL preset-navigation gesture on the boot song's session InstrumentClip
	/// (rung 5, task 4), rather than calling LoadInstrumentPresetUI::doPresetNavigation directly --
	/// that helper isn't what navigateThroughPresetsForInstrumentClip() calls for MIDI/CV, so a probe
	/// built on it would prove nothing about View's own synchronous/dispatch split. Both
	/// `navigateThroughPresetsForInstrumentClip` and `changeOutputType` are already public on View,
	/// so this wraps them only for symmetry with the ArrangerView wrappers above, not for access.
	static void navigateThroughPresetsForInstrumentClip(InstrumentClip* clip, int32_t offset) {
		// Unlike changeOutputType(), this gesture gates on currentUIMode (UI_MODE_NONE or
		// UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) before it will even dispatch -- a real caller only
		// reaches it from one of those modes, and an earlier stage in this same process (Listing)
		// can leave currentUIMode sitting on whatever mode its own simulated browsing left behind.
		// Forcing it to NONE here reproduces the precondition a real gesture would already have,
		// rather than either skipping the check or leaving this probe at the mercy of stage order.
		currentUIMode = UI_MODE_NONE;
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);
		view.navigateThroughPresetsForInstrumentClip(offset, modelStack, true);
	}
	/// Drives View's REAL output-type gesture on `clip`. See navigateThroughPresetsForInstrumentClip()
	/// above for why this goes through View rather than InstrumentClip::changeOutputType directly.
	static bool changeOutputType(InstrumentClip* clip, OutputType newOutputType) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);
		return view.changeOutputType(newOutputType, modelStack, true);
	}

	/// Drives InstrumentClipView's REAL shift/FM "create a brand-new instrument" gesture -- the one
	/// LoadInstrumentPresetUI reader NEVER covers (Browser::getUnusedSlot(), not one of that class's
	/// three readers) and the one rung 5 task 6's fix round found still reaching the directory
	/// channel from the interaction tier. Reads getCurrentInstrumentClip() live, exactly as the real
	/// gesture (handleInstrumentChange() -> createNewInstrument()) does -- no clip parameter, to keep
	/// this a faithful call of the production entry point rather than a reimplementation of it.
	static bool createNewInstrument(OutputType newOutputType, bool isDx) {
		return instrumentClipView.createNewInstrument(newOutputType, isDx);
	}

	/// Put SessionView into Grid layout with `clip` under the first grid press, which is what BOTH
	/// Grid-layout gestures below read their subject from (getClipForLayout() ->
	/// gridClipFromCoords(gridFirstPressedX, gridFirstPressedY)).
	///
	/// Re-derived before every gesture rather than once: the grid X comes from the clip's TRACK, so a
	/// gesture that swaps the Output moves the clip's column out from under the previous coordinates.
	/// @return Whether the coordinates really do resolve back to `clip` -- false means the gesture
	///         would act on nothing and the step must fail rather than pass vacuously.
	static bool aimGridPressAt(Clip* clip) {
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeGrid;
		const Cartesian xy = sessionView.gridXYFromClip(*clip);
		sessionView.gridFirstPressedX = xy.x;
		sessionView.gridFirstPressedY = xy.y;
		return sessionView.getClipForLayout() == clip;
	}

	/// Drives SessionView's REAL Grid-layout preset-navigation gesture (the select encoder's
	/// commandChangeClipPreset), not Song::navigateThroughPresetsForInstrument underneath it -- that
	/// helper is what the COMPLETION calls, so a probe built on it would prove nothing about the
	/// dispatch split. Private, hence this view's friend declaration.
	static void commandChangeClipPreset(int8_t offset) {
		currentUIMode = UI_MODE_NONE;
		sessionView.commandChangeClipPreset(offset);
	}

	/// Drives SessionView's REAL Grid-layout output-type button, through buttonAction() so the whole
	/// `goto changeOutputType` region (mode gate, audio-clip refusal, LOAD-held branch) is the thing
	/// under test.
	///
	/// Reproduces the preconditions a real press already satisfies: the gesture only runs while a clip
	/// is held in song view, and selectedClipYDisplay names the row its redraw invalidates (255, which
	/// clipPressEnded() leaves behind, would shift by 255 -- undefined on a 32-bit value).
	static void sessionButtonChangeOutputType(deluge::hid::button::Button b) {
		currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
		sessionView.selectedClipYDisplay = 0;
		(void)sessionView.buttonAction(b, true, false);
	}

	/// Drives SessionView's REAL empty-pad "create a clip here" gesture, through padAction() in Rows
	/// layout. Restores Rows layout first: the Grid steps above leave the song in Grid, where padAction
	/// hands straight over to gridHandlePads() and this branch is unreachable.
	static void pressEmptyPad(int32_t xDisplay, int32_t yDisplay) {
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
		currentSong->songViewYScroll = 0;
		currentUIMode = UI_MODE_NONE;
		// What the gesture creates: it reads lastTypeCreated when defaultUseLastClipType is set, and
		// SYNTH is the only type the SYNTHS fixture can satisfy.
		FlashStorage::defaultUseLastClipType = true;
		sessionView.lastTypeCreated = OutputType::SYNTH;
		(void)sessionView.padAction(xDisplay, yDisplay, 1);
	}

	/// Whether the empty-pad gesture is still waiting for its acquisition's completion to hold the pad.
	static bool newTrackPressHeld() { return sessionView.newTrackPressHeld_; }

	/// Drives the grid new-track gesture's ENTER continuation, resuming from the state the clip-type
	/// picker leaves behind and going through exitTrackCreation()'s real funnel.
	///
	/// The picker itself is NOT opened: setupTrackCreation() calls openUI() on a context menu, and
	/// this stage runs with no UI stack for one to be pushed onto (the same reason the listing stage
	/// starts its listings with beginListing rather than opening a browser). Everything the
	/// conversion touches lives below that -- finishGridNewTrackClip() -> dispatch ->
	/// completeNewTrackGridEnter() -> gridNewTrackClipEntered() -- so the pending state the picker
	/// would have set is written directly and the funnel is entered for real.
	static void beginGridNewTrackEnter(int32_t x, int32_t y) {
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeGrid;
		currentUIMode = UI_MODE_NONE;
		sessionView.gridFirstPressedX = x;
		sessionView.gridFirstPressedY = y;
		sessionView.gridResetPresses(false, true);
		sessionView.pendingGridClip = SessionView::PendingGridClip::ENTER;
		sessionView.pendingGridClipXY = (static_cast<uint32_t>(y) << 16) | static_cast<uint32_t>(x);
		sessionView.createClip = true;
		deluge::gui::context_menu::clip_settings::newClipType.toCreate = OutputType::SYNTH;
		sessionView.exitTrackCreation();
	}

	/// Reproduce the caller's own tail: both release paths that call exitTrackCreation() follow it
	/// immediately with clipPressEnded(), which runs gridResetPresses() and sets gridFirstPressedX/Y
	/// to -1 -- the reset that made the deferred continuation read "pad released" when the pad was in
	/// fact still down. Driving the gesture without this would not exercise the bug at all.
	static void endGridPress() { sessionView.clipPressEnded(); }
};

namespace {

// ── The fixture ─────────────────────────────────────────────────────────────
//
// `pack_song_preview_ab_image()` in the golden_vt_render harness writes every file named here; the
// two must be read together. Each synthetic song's preview is a UNIFORM colour, which is what lets
// the expected image be asserted absolutely from a constant rather than by re-reading the card.

/// The folder every case's song lives in.
constexpr const char* kSongsDir = "SONGS";

/// The greyOut proportion `runPreviewWalk` applies to every pad it paints. Restated here because the
/// expected images below are built from the fixture's hex payloads, and the walk's transform is part
/// of what a correct render produces.
constexpr int32_t kPreviewGreyOut = 6500000;

/// Uniform preview channel values, one per synthetic song. All different, so no two songs' artwork
/// can be mistaken for each other -- which is the entire content of the card-swap assertion.
/// @{
constexpr uint8_t kRenderPad = 0x30;
constexpr uint8_t kJsonPad = 0x48;
constexpr uint8_t kLatePad = 0x50;
constexpr uint8_t kSwapPadA = 0x20;
constexpr uint8_t kSwapPadB = 0x60;
/// @}

/// What a case's rendered image must be, absolutely.
enum class ExpectedImage : uint8_t {
	/// Every pad is the uniform colour in `Case::pad`, greyed as the walk greys it.
	Uniform,
	/// Every pad is zero: `beginPreviewRender` cleared the image and nothing painted over it.
	Blank,
	/// No absolute expectation -- a corpus song, whose artwork is whatever its author made. The
	/// cross-leg diff is the whole check for these.
	Unchecked,
};

/// One song the browser is asked to preview, and everything both backings must agree about it.
///
/// The expectations are ABSOLUTE, not merely cross-leg. The diff alone would pass a regression that
/// broke both backings the same way -- and they share the decode, the walk and the image, so a
/// regression there takes both down together, agreeing perfectly.
struct Case {
	/// Label for the printed line -- short, stable, independent of the path.
	const char* label;
	/// Filename inside `kSongsDir`.
	const char* filename;
	/// What the decode must report. Both backings, the same value: this is the field the gate exists
	/// for, and it is invisible to any comparison of pixels.
	SongPreviewOutcome outcome;
	/// The error that must have reached `displayError`, or `Error::NONE` for an outcome that shows
	/// nothing.
	Error error;
	ExpectedImage image;
	/// The uniform channel value behind `ExpectedImage::Uniform`.
	uint8_t pad;
};

/// The case list. Every entry is a DIFFERENT shape of answer, and the mix is the fixture's content:
///
/// - `render` is the ordinary case: a song with artwork inside the read window.
/// - `json` is the same, through the other deserializer. `isJsonSongPath()` picks it from the path,
///   which is a decision only the buffer backing has to make (the file backing infers it from the
///   filename inside `openDelugeFile`), so a wrong answer there shows up here and nowhere else.
/// - `noprev` is a song with no `preview` attribute at all, SHORTER than the read window. The
///   shortness is load-bearing: a longer one would legitimately report Truncated on the buffer
///   backing (the walk runs out of bytes before it can say there is no preview) and NoPreview on the
///   file backing, which is the `window` case below rather than a bug.
/// - `missing` does not exist. This is the case the two backings USED to disagree about, silently:
///   the file backing showed an error popup, and the prefix channel published a failed read as an
///   empty prefix indistinguishable from a song with no artwork, so the buffer backing drew blank
///   and said nothing. Both must now report Error/FILE_NOT_FOUND.
/// - `corrupt` opens and is not a song (its root tag is wrong).
/// - `empty` opens and has nothing in it -- a DIFFERENT failure from `missing`, and the one that
///   makes "READY with zero bytes" mean exactly one thing. Collapse the two and this case and
///   `missing` stop being distinguishable.
constexpr std::array<Case, 6> kCases{{
    {"render", "RENDER.XML", SongPreviewOutcome::Rendered, Error::NONE, ExpectedImage::Uniform, kRenderPad},
    {"json", "JSONP.Json", SongPreviewOutcome::Rendered, Error::NONE, ExpectedImage::Uniform, kJsonPad},
    {"noprev", "NOPREV.XML", SongPreviewOutcome::NoPreview, Error::NONE, ExpectedImage::Blank, 0},
    {"missing", "MISSING.XML", SongPreviewOutcome::Error, Error::FILE_NOT_FOUND, ExpectedImage::Blank, 0},
    {"corrupt", "CORRUPT.XML", SongPreviewOutcome::Error, Error::FILE_CORRUPTED, ExpectedImage::Blank, 0},
    {"empty", "EMPTY.XML", SongPreviewOutcome::Error, Error::FILE_CORRUPTED, ExpectedImage::Blank, 0},
}};

/// The ONE case whose two backings must legitimately DISAGREE, and the reason it is not in the list
/// above.
///
/// Its `preview` attribute sits past `kSongPreviewWindow`, so the buffer backing runs out of bytes
/// and reports Truncated -- blank artwork, and honestly so -- while the file backing walks the whole
/// document and renders it. That is the known limit of reading a bounded prefix, not a defect, and
/// it is the third outcome the spec requires stay distinguishable: a real failure shows an error, a
/// song with no artwork draws blank, and a preview past the window draws blank WITHOUT claiming the
/// song has none.
///
/// Printed on its own line so the diffed case body stays a strict equality. Each leg asserts its own
/// expected outcome absolutely, which is stronger than a diff would be here: a backing that reported
/// Truncated for everything, or Rendered for everything, fails.
constexpr const char* kWindowLabel = "window";
constexpr const char* kWindowFilename = "LATE.XML";

/// Corpus songs, staged by the packer as `SONGS/CORPUS/C%02u.XML` when `DELUGE_SONG_CORPUS` names a
/// directory.
///
/// They carry no absolute expectation -- their artwork is whatever their author made -- so the
/// cross-leg diff is the whole check. What they add is real documents: real attribute orders, real
/// firmware versions, real preview offsets, which is the thing a synthetic fixture cannot be.
constexpr const char* kCorpusDir = "SONGS/CORPUS";

/// How many corpus songs the packer staged, written by it and read by this side.
///
/// The count is READ, not discovered, and that is the whole point of the file. The first version
/// probed `File::open` per index and stopped at the first miss, which is a coverage check that can
/// quietly cover nothing: one failed probe -- for any reason, on either leg -- silently drops every
/// real song, `MIN_CASES` in the gate script counts only the six synthetic cases, and the
/// "did anything render?" check below is itself gated on at least one corpus case having run. The
/// gate stays green having tested 6 songs instead of 20 and says nothing.
///
/// With the count read, running fewer cases than the fixture staged is a failure with a number in
/// it. Its absence is a failure too: this scenario and its packer are one fixture, and a card
/// without the file is not that fixture.
constexpr const char* kCorpusCountPath = "SONGS/CORPUS/COUNT.TXT";

/// Upper bound on staged corpus songs, hand-duplicated with `MAX_CORPUS_CASES` in
/// `golden_vt_render/src/main.rs` (the packer).
///
/// Asserted against its own literal below, which looks tautological and is the point: it makes a
/// change take two deliberate edits, one on each side, the same way `DELUGE_FILE_PREFIX_CAPACITY` is
/// pinned at both ends of that boundary. What actually CATCHES drift is the runtime check in
/// `runCorpusCountTick()`: a packer that staged more than this side can hold fails by name rather
/// than by silently running fewer cases.
constexpr size_t kMaxCorpusCases = 16;
static_assert(kMaxCorpusCases == 16,
              "kMaxCorpusCases is hand-duplicated as MAX_CORPUS_CASES in golden_vt_render/src/main.rs; "
              "change both");

// ── The card swap ───────────────────────────────────────────────────────────
//
// The hazard: a prefix is keyed by PATH and nothing about it records which card it was read from, so
// a prefix read from card A and collected after card B is in the slot draws card A's artwork over
// card B's song, with no error and nothing to say so. Each read is stamped with the card generation
// it began under, and the take refuses a stamp that is not the taker's own.
//
// Reproducing that needs an UNCOLLECTED result sitting in the slot across the ejection -- and the
// channel is a single slot, so the trick `audio_load_ab_scenario.cpp` uses (queue a second file
// behind the first and treat its arrival as proof the first published) cannot work here: a second
// request would REPLACE the very entry the stage needs to go stale.
//
// So the handshake is a REHEARSAL instead, and it is measured rather than assumed:
//
//   1. Ask for SWAP.XML, then spend `kPublishTicks` steps NOT collecting.
//   2. Collect it in a SINGLE tick. Landing in one tick is only possible if the prefix was already
//      published, so this MEASURES that `kPublishTicks` is enough. If it is not, the stage fails
//      here and says so, rather than going on to test nothing.
//   3. Ask for SWAP.XML again and spend the SAME `kPublishTicks` steps not collecting. The
//      rehearsal is what makes "a result is now sitting in the slot" a measured fact.
//   4. Eject, put a DIFFERENT song at that path, re-insert.
//   5. Collect. It must be the NEW song's artwork, asserted against the known second image rather
//      than merely against "not the first" -- an inequality check passes when both reads fail.

constexpr const char* kSwapFilename = "SWAP.XML";
/// The "other card's" song, staged elsewhere and moved onto `kSwapFilename` at the swap.
constexpr const char* kSwapReplacementFilename = "SWAPB.XML";

/// Steps spent not collecting, which the rehearsal MEASURES is enough for a read to publish.
///
/// Generous: each step is one dispatch round trip through the platform driver's executor, and a
/// prefix read is one open, one size and one read.
constexpr int32_t kPublishTicks = 64;

/// Ticks one case may spend waiting before it is called wedged. Each is one graphicsRoutine() call
/// plus a dispatch round trip, so this is generous: the file backing lands on tick 1, a healthy
/// prefix read within a few.
constexpr int32_t kMaxCaseTicks = 2000;

// ── State ───────────────────────────────────────────────────────────────────

/// Which stage `dispatch_step` is in, in order.
enum class Stage : uint8_t { Cases, Window, CorpusCount, Corpus, Swap, Listing, Acquire, ByPath, Done };
Stage g_stage = Stage::Cases;

/// Where the swap stage is in the sequence above.
enum class SwapStep : uint8_t { RehearseRequest, RehearseWait, RehearseCollect, Arm, Settle, Eject, Collect, Finished };
SwapStep g_swapStep = SwapStep::RehearseRequest;
int32_t g_swapWaited = 0;
int32_t g_swapCollectDeferrals = 0;
bool g_swapFailed = false;
/// Whether the file backing has been asked afresh for the post-swap song. One ask, not one per tick.
bool g_swapCollectAsked = false;

size_t g_caseIndex = 0;
size_t g_corpusIndex = 0;
size_t g_corpusRan = 0;
size_t g_corpusRendered = 0;
/// How many corpus songs the packer says it staged. Read from the card, never discovered.
size_t g_corpusExpected = 0;
bool g_corpusCountDispatched = false;
bool g_corpusCountFailed = false;
bool g_caseStarted = false;
int32_t g_caseTicks = 0;
int32_t g_caseDeferrals = 0;
int32_t g_failures = 0;
bool g_stepDone = true;
bool g_summaryPrinted = false;

/// Deferrals recorded PER CASE, kept after the case settles.
///
/// A run-wide total is not enough, and the difference is a hole big enough to hide the thing this
/// gate exists to pin: one deferral anywhere satisfies a total, while every OTHER song could have
/// been answered without the async path being reached at all. Rung 1 shipped exactly that.
std::array<int32_t, kCases.size()> g_deferralsByCase{};
std::array<int32_t, kMaxCorpusCases> g_deferralsByCorpusCase{};
int32_t g_windowDeferrals = 0;

/// Bytes one rendered preview occupies: exactly what `beginPreviewRender` clears, and the front of
/// `PadLEDs::imageStore` (which is twice this tall, the rest belonging to a scroll).
constexpr size_t kImageBytes = kDisplayHeight * (kDisplayWidth + kSideBarWidth) * sizeof(RGB);

/// Plain CRC32 (IEEE 802.3, reflected) -- the same routine `audio_load_ab_scenario.cpp` uses.
uint32_t crc32(const void* data, size_t len) {
	const auto* p = static_cast<const uint8_t*>(data);
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
		}
	}
	return ~crc;
}

/// Fingerprint the pads the preview actually drew on.
uint32_t imageCrc() {
	return crc32(&PadLEDs::imageStore[0][0], kImageBytes);
}

/// The image a song whose every preview pad carries `value` must render to.
///
/// Built from the fixture's own constant, NOT by re-reading the card, which is what makes the
/// card-swap check absolute: a stale image from the previous card is a different constant, and a
/// read that failed leaves a blank image, so neither can satisfy this.
uint32_t uniformImageCrc(uint8_t value) {
	std::array<std::array<RGB, kDisplayWidth + kSideBarWidth>, kDisplayHeight> expected{};
	RGB pad{.r = value, .g = value, .b = value};
	const RGB greyed = pad.greyOut(kPreviewGreyOut);
	for (auto& row : expected) {
		row.fill(greyed);
	}
	return crc32(expected.data(), kImageBytes);
}

/// The image a song that drew nothing renders to: `beginPreviewRender` cleared it and no walk
/// painted over it.
uint32_t blankImageCrc() {
	const std::array<std::array<RGB, kDisplayWidth + kSideBarWidth>, kDisplayHeight> blank{};
	return crc32(blank.data(), kImageBytes);
}

/// A pattern no render can leave behind, written before every case.
///
/// The positive witness that a render RAN. `beginPreviewRender` clears the whole image on every
/// render, blank ones included, so poison surviving a case means nothing drew at all -- which is
/// precisely the failure this gate exists for, and which neither the outcome (stale from the case
/// before) nor a cross-leg diff (both legs equally silent) would show.
void poisonImage() {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			PadLEDs::imageStore[y][x] = RGB{.r = 0xA5, .g = 0x5A, .b = 0xC3};
		}
	}
}

uint32_t poisonImageCrc() {
	std::array<std::array<RGB, kDisplayWidth + kSideBarWidth>, kDisplayHeight> poison{};
	for (auto& row : poison) {
		row.fill(RGB{.r = 0xA5, .g = 0x5A, .b = 0xC3});
	}
	return crc32(poison.data(), kImageBytes);
}

const char* outcomeName(SongPreviewOutcome outcome) {
	switch (outcome) {
	case SongPreviewOutcome::Rendered:
		return "rendered";
	case SongPreviewOutcome::NoPreview:
		return "noPreview";
	case SongPreviewOutcome::Truncated:
		return "truncated";
	case SongPreviewOutcome::Error:
		return "error";
	}
	return "?";
}

/// Print one preview's whole outcome as a single stable, diffable line.
///
/// Every field here must match between the two builds. Nothing that legitimately differs between
/// them (deferral counts, which backing ran) appears on this line -- those go to `SP-DEFER` and
/// `SP-SUMMARY`, which the diff does not cover.
void printCaseLine(const char* tag, const char* label, const char* path, uint32_t crc) {
	printf("%s %-9s | path=\"%s\" | outcome=%s | error=%d | crc=%08X\n", tag, label, path,
	       outcomeName(SongPreviewAbProbe::outcome()), static_cast<int>(SongPreviewAbProbe::error()), crc);
}

/// The absolute half of a case's check: what BOTH backings must independently produce.
void checkCase(const char* label, const Case& expected, uint32_t crc) {
	if (SongPreviewAbProbe::outcome() != expected.outcome) {
		printf("FAIL [songPreviewAB/%s]: outcome=%s, expected %s -- the two backings must report the SAME "
		       "outcome, and an unreadable song, a song with no artwork and a preview past the read window "
		       "all draw the same blank pads, so nothing else can tell them apart\n",
		       label, outcomeName(SongPreviewAbProbe::outcome()), outcomeName(expected.outcome));
		g_failures++;
	}
	if (SongPreviewAbProbe::error() != expected.error) {
		printf("FAIL [songPreviewAB/%s]: error=%d, expected %d -- the two backings must put the SAME thing "
		       "on screen for this song\n",
		       label, static_cast<int>(SongPreviewAbProbe::error()), static_cast<int>(expected.error));
		g_failures++;
	}
	const uint32_t want = (expected.image == ExpectedImage::Uniform) ? uniformImageCrc(expected.pad) : blankImageCrc();
	if (expected.image != ExpectedImage::Unchecked && crc != want) {
		printf("FAIL [songPreviewAB/%s]: image crc=%08X, expected %08X -- asserted absolutely, not merely "
		       "against the other leg, because both backings share the decode and a regression there takes "
		       "them down together\n",
		       label, crc, want);
		g_failures++;
	}
}

/// Did anything render at all? See poisonImage().
void checkRendered(const char* label, uint32_t crc) {
	if (crc == poisonImageCrc()) {
		printf("FAIL [songPreviewAB/%s]: the pad image is untouched after the whole case -- no render ever ran, "
		       "so the browser drew nothing and reported nothing, which to the user is indistinguishable "
		       "from a song that does not exist\n",
		       label);
		g_failures++;
	}
}

/// Per-case deferral discipline, asserted for each case in turn.
///
/// Only the FILE leg is checked, and deliberately. On the prefix leg a nonzero count is guaranteed by
/// construction rather than by anything under test: `startCase()`'s drive is the first this channel
/// has ever heard of the path, so its collect answers UNKNOWN, which arms a deferral before
/// `driveCase()` has ticked once -- so tick 1 always counts one and `deferrals == 0` is unreachable.
/// A check that cannot fail is assurance this file does not actually provide, so it is not written.
/// The property it looked like it covered is covered for real elsewhere: a channel that never answers
/// wedges the case at `kMaxCaseTicks`, and a leg that drew nothing is caught by the poison witness
/// (`checkRendered()`) and by the absolute image CRC.
void checkDeferrals(const char* label, int32_t deferrals) {
	if (deluge_file_prefix_async_active()) {
		return;
	}
	if (deferrals != 0) {
		printf("FAIL [songPreviewAB/%s]: the file backing deferred %d time(s), which it has no way to do\n", label,
		       deferrals);
		g_failures++;
	}
}

/// Has the case in progress finished -- nothing queued for a re-ask, and no op outstanding?
bool caseSettled() {
	return !SongPreviewAbProbe::pending() && !SongPreviewAbProbe::inFlight();
}

/// Start one case: select the song and make the ONE user-driven call.
void startCase(const char* dir, const char* filename) {
	g_caseStarted = true;
	g_caseTicks = 0;
	g_caseDeferrals = 0;
	poisonImage();
	SongPreviewAbProbe::select(dir, filename);
	SongPreviewAbProbe::draw();
}

/// Drive one tick of a case in progress. @return true once it has settled or wedged.
bool driveCase(const char* label) {
	g_caseTicks++;
	if (SongPreviewAbProbe::pending()) {
		g_caseDeferrals++;
	}
	// The per-tick surface, and the ONLY thing driven from here on. Nothing in this file retries a
	// deferred preview itself: if `graphicsRoutine()` stops re-asking, the async leg never lands.
	SongPreviewAbProbe::tick();
	if (caseSettled()) {
		return true;
	}
	if (g_caseTicks >= kMaxCaseTicks) {
		printf("FAIL [songPreviewAB/%s]: still waiting after %d graphicsRoutine() tick(s) with no further "
		       "input -- a deferred preview nothing asks for again is blank, errorless and silent\n",
		       label, g_caseTicks);
		g_failures++;
		return true;
	}
	return false;
}

std::string songPath(const char* dir, const char* filename) {
	return std::string{dir} + "/" + filename;
}

// ── Stages ──────────────────────────────────────────────────────────────────

void runCasesTick() {
	const Case& c = kCases[g_caseIndex];
	if (!g_caseStarted) {
		startCase(kSongsDir, c.filename);
		return;
	}
	if (!driveCase(c.label)) {
		return;
	}

	const std::string path = songPath(kSongsDir, c.filename);
	const uint32_t crc = imageCrc();
	printCaseLine("SP-CASE", c.label, path.c_str(), crc);
	checkRendered(c.label, crc);
	checkCase(c.label, c, crc);
	checkDeferrals(c.label, g_caseDeferrals);
	g_deferralsByCase[g_caseIndex] = g_caseDeferrals;

	g_caseStarted = false;
	g_caseIndex++;
	if (g_caseIndex == kCases.size()) {
		g_stage = Stage::Window;
	}
}

/// The read-window case: the one place the two backings must DIFFER, and each says so absolutely.
void runWindowTick() {
	if (!g_caseStarted) {
		startCase(kSongsDir, kWindowFilename);
		return;
	}
	if (!driveCase(kWindowLabel)) {
		return;
	}

	const bool async = deluge_file_prefix_async_active();
	const uint32_t crc = imageCrc();
	// NOT a diffed line: this is the one case whose two legs legitimately disagree, so it is printed
	// under its own tag and each leg is held to its own expectation instead.
	printf("SP-WINDOW asyncActive=%d | outcome=%s | error=%d | crc=%08X | deferrals=%d\n", async ? 1 : 0,
	       outcomeName(SongPreviewAbProbe::outcome()), static_cast<int>(SongPreviewAbProbe::error()), crc,
	       g_caseDeferrals);
	checkRendered(kWindowLabel, crc);

	const Case expected =
	    async ? Case{kWindowLabel, kWindowFilename, SongPreviewOutcome::Truncated, Error::NONE, ExpectedImage::Blank, 0}
	          : Case{kWindowLabel, kWindowFilename,        SongPreviewOutcome::Rendered,
	                 Error::NONE,  ExpectedImage::Uniform, kLatePad};
	checkCase(kWindowLabel, expected, crc);
	checkDeferrals(kWindowLabel, g_caseDeferrals);
	g_windowDeferrals = g_caseDeferrals;

	g_caseStarted = false;
	g_stage = Stage::CorpusCount;
}

/// `C00.XML`, `C01.XML`, ... -- built by hand rather than with snprintf, which `src/lib/printf.h`
/// soft-aliases the same way it aliases printf (see this file's #undef).
std::string corpusFilename(size_t index) {
	std::string name = "C";
	name += static_cast<char>('0' + (index / 10) % 10);
	name += static_cast<char>('0' + index % 10);
	name += ".XML";
	return name;
}

/// Read `kCorpusCountPath` into @ref g_corpusExpected. Runs on the STORAGE OWNER.
///
/// On the owner rather than inline, and that is not ceremony here of all places: a blocking card
/// read on the interaction tier is the exact thing the channel under test exists to remove, and a
/// gate that did it in its own setup would be asserting a discipline it breaks. It is also the only
/// card read this scenario performs of its own accord -- everything else goes through the browser.
void readCorpusCount(void*) {
	auto opened = deluge::io::File::open(kCorpusCountPath, DELUGE_FILE_READ);
	if (!opened.has_value()) {
		printf("FAIL [songPreviewAB/corpus]: '%s' is missing, so this side cannot know how many real songs the "
		       "packer staged -- the scenario and its packer are one fixture, and a card without this file is "
		       "not that fixture\n",
		       kCorpusCountPath);
		g_failures++;
		g_corpusCountFailed = true;
		return;
	}
	std::array<std::byte, 16> buffer{};
	const auto read = opened->read(buffer);
	if (!read.has_value()) {
		printf("FAIL [songPreviewAB/corpus]: '%s' could not be read\n", kCorpusCountPath);
		g_failures++;
		g_corpusCountFailed = true;
		return;
	}
	size_t count = 0;
	bool anyDigit = false;
	for (std::byte b : read.value()) {
		const auto c = static_cast<char>(b);
		if (c < '0' || c > '9') {
			break;
		}
		count = count * 10 + static_cast<size_t>(c - '0');
		anyDigit = true;
	}
	if (!anyDigit) {
		printf("FAIL [songPreviewAB/corpus]: '%s' holds no decimal count\n", kCorpusCountPath);
		g_failures++;
		g_corpusCountFailed = true;
		return;
	}
	if (count > kMaxCorpusCases) {
		// The one thing that actually catches the two hand-duplicated bounds drifting apart. Without
		// it, a packer that staged more than this side can hold would simply run fewer cases and say
		// nothing.
		printf("FAIL [songPreviewAB/corpus]: the packer staged %zu song(s), more than the %zu this side can "
		       "hold -- kMaxCorpusCases and MAX_CORPUS_CASES have drifted apart\n",
		       count, kMaxCorpusCases);
		g_failures++;
		g_corpusCountFailed = true;
		return;
	}
	g_corpusExpected = count;
}

/// Dispatch the count read and move on once it has landed.
void runCorpusCountTick() {
	if (!g_corpusCountDispatched) {
		g_corpusCountDispatched = true;
		if (!deluge::storage::Owner::run(&readCorpusCount, nullptr)) {
			printf("FAIL [songPreviewAB/corpus]: the storage owner would not take the count read\n");
			g_failures++;
			g_corpusCountFailed = true;
		}
		return;
	}
	// The marker op behind the dispatch has run by now (the owner queue is FIFO), so the count is in.
	printf("SP-CORPUS expected=%zu\n", g_corpusExpected);
	g_stage = g_corpusCountFailed ? Stage::Swap : Stage::Corpus;
}

void runCorpusTick() {
	if (g_corpusIndex >= g_corpusExpected) {
		g_stage = Stage::Swap;
		return;
	}
	const std::string filename = corpusFilename(g_corpusIndex);
	const std::string path = songPath(kCorpusDir, filename.c_str());

	if (!g_caseStarted) {
		startCase(kCorpusDir, filename.c_str());
		return;
	}
	if (!driveCase(filename.c_str())) {
		return;
	}

	const uint32_t crc = imageCrc();
	printCaseLine("SP-CASE", filename.c_str(), path.c_str(), crc);
	checkRendered(filename.c_str(), crc);
	checkDeferrals(filename.c_str(), g_caseDeferrals);
	g_deferralsByCorpusCase[g_corpusIndex] = g_caseDeferrals;
	if (SongPreviewAbProbe::outcome() == SongPreviewOutcome::Rendered) {
		g_corpusRendered++;
	}
	if (SongPreviewAbProbe::outcome() == SongPreviewOutcome::Truncated) {
		// A real song whose preview falls outside the read window is not a fixture decision, it is the
		// window being too small -- which is what `kSongPreviewWindow` was measured to avoid and what
		// `song_preview_window_tests.cpp` asserts over the same corpus.
		printf("FAIL [songPreviewAB/%s]: a REAL song's preview fell past the read window (%u bytes), so the "
		       "prefix backing draws it blank while the file backing draws its artwork\n",
		       filename.c_str(), static_cast<unsigned>(deluge::storage::kSongPreviewWindow));
		g_failures++;
	}

	g_caseStarted = false;
	g_corpusRan++;
	g_corpusIndex++;
}

/// Put the "other card's" song at the swap path: the same path, a different file.
///
/// An unlink + rename through the filesystem rather than a block-device exchange, for the reason
/// `audio_load_ab_scenario.cpp` gives at length: the host BSP has no card-swap primitive, and what
/// the code under test sees is identical either way -- it keyed a result by a path, and the file at
/// that path is now a different file. The eject and re-insert EDGES are driven for real.
bool performCardSwap() {
	const std::string swapPath = songPath(kSongsDir, kSwapFilename);
	const std::string replacementPath = songPath(kSongsDir, kSwapReplacementFilename);
	if (!deluge::io::unlink(swapPath).has_value()) {
		printf("FAIL [songPreviewAB/swap]: could not remove '%s' to stage the swapped card\n", swapPath.c_str());
		return false;
	}
	if (!deluge::io::rename(replacementPath, swapPath).has_value()) {
		printf("FAIL [songPreviewAB/swap]: could not move '%s' into place at '%s'\n", replacementPath.c_str(),
		       swapPath.c_str());
		return false;
	}
	return true;
}

/// The eject/re-insert pair, on the storage owner: it renames a file and remounts the volume.
void runSwapEject(void*) {
	// The ejection, through the production entry point. This is the call the flush lives in, and it
	// must run BEFORE the new file appears -- flushing on re-insertion instead would leave the window
	// open for as long as the slot stayed empty.
	audioFileManager.setCardEjected();
	if (!performCardSwap()) {
		g_swapFailed = true;
		g_failures++;
		g_swapStep = SwapStep::Finished;
		return;
	}
	// And the re-insertion: remount against the "new" card, exactly as slowRoutine() drives it.
	audioFileManager.reinitEjectedCard();
	g_swapStep = SwapStep::Collect;
	g_caseStarted = true; // the pending re-ask from Arm IS this case; do not start a new one
	g_caseTicks = 0;
	g_caseDeferrals = 0;
}

void finishSwapStage() {
	const bool async = deluge_file_prefix_async_active();
	printf("SP-SWAP-SUMMARY asyncActive=%d swapped=%d collectDeferrals=%d\n", async ? 1 : 0, g_swapFailed ? 0 : 1,
	       g_swapCollectDeferrals);
	if (!async) {
		if (g_swapCollectDeferrals != 0) {
			printf("FAIL [songPreviewAB/swap]: the file backing deferred %d time(s) after the swap\n",
			       g_swapCollectDeferrals);
			g_failures++;
		}
		return;
	}
	// The direct witness that the stale prefix was REFUSED rather than served, and the threshold is
	// two rather than one for a reason that a run with the belt removed made plain:
	//
	//   - The first tick always finds the ask armed before the ejection still pending, so it counts
	//     one deferral whatever happens next. A `> 0` test here is satisfied by that alone, and a run
	//     with the whole card belt removed -- which served the PREVIOUS card's artwork -- still
	//     reported exactly one. (Measured, not reasoned: it read `collectDeferrals=1`.)
	//   - A SECOND deferral means the collect did NOT answer READY on the first ask -- either UNKNOWN
	//     (the stale prefix was refused, so the collector re-requested) or PENDING (that re-read is
	//     still running). Both are the refusal; only the first ask distinguishes them, and neither is
	//     reachable while a prefix from the previous card is still servable, because that answers
	//     READY on tick 1 and the case settles there.
	if (!g_swapFailed && g_swapCollectDeferrals < 2) {
		printf("FAIL [songPreviewAB/swap]: the post-swap collect deferred %d time(s), fewer than the 2 a refused "
		       "prefix forces -- so it was answered from a prefix read BEFORE the ejection rather than from the "
		       "card now in the slot\n",
		       g_swapCollectDeferrals);
		g_failures++;
	}
}

void runSwapTick() {
	switch (g_swapStep) {

	case SwapStep::RehearseRequest:
		poisonImage();
		SongPreviewAbProbe::select(kSongsDir, kSwapFilename);
		SongPreviewAbProbe::draw();
		g_swapWaited = 0;
		g_swapStep = SwapStep::RehearseWait;
		return;

	case SwapStep::RehearseWait:
	case SwapStep::Settle:
		// Deliberately NOT calling graphicsRoutine(): the point of these steps is that nothing
		// collects, so the reader's result is left sitting in the channel.
		g_swapWaited++;
		if (g_swapWaited >= kPublishTicks) {
			g_swapStep = (g_swapStep == SwapStep::RehearseWait) ? SwapStep::RehearseCollect : SwapStep::Eject;
		}
		return;

	case SwapStep::RehearseCollect: {
		// ONE tick. Landing here is what MEASURES that `kPublishTicks` is long enough for a read to
		// have published -- which is the whole basis for the claim, two steps later, that an
		// uncollected result is sitting in the channel when the card is pulled.
		SongPreviewAbProbe::tick();
		const uint32_t crc = imageCrc();
		if (!caseSettled()) {
			printf("FAIL [songPreviewAB/swap]: '%s' had not published after %d step(s) of waiting, so this stage "
			       "cannot know a result was in the channel when the card was pulled -- raise kPublishTicks\n",
			       kSwapFilename, kPublishTicks);
			g_failures++;
			g_swapFailed = true;
			g_swapStep = SwapStep::Finished;
			return;
		}
		checkRendered("swap/before", crc);
		const Case before{"swap/before", kSwapFilename,          SongPreviewOutcome::Rendered,
		                  Error::NONE,   ExpectedImage::Uniform, kSwapPadA};
		checkCase("swap/before", before, crc);
		printCaseLine("SP-SWAP-BEFORE", "swap", songPath(kSongsDir, kSwapFilename).c_str(), crc);
		g_swapStep = SwapStep::Arm;
		return;
	}

	case SwapStep::Arm:
		// Ask again, and this time never collect: the result this produces is the one that has to go
		// stale. On the file backing this simply re-renders the same song, which is harmless -- that
		// backing holds no card-derived state to invalidate, which is what makes it the oracle here.
		poisonImage();
		SongPreviewAbProbe::select(kSongsDir, kSwapFilename);
		SongPreviewAbProbe::draw();
		g_swapWaited = 0;
		g_swapStep = SwapStep::Settle;
		return;

	case SwapStep::Eject:
		// Onto the storage owner: it renames a file and remounts the volume.
		if (!deluge::storage::Owner::run(&runSwapEject, nullptr)) {
			printf("FAIL [songPreviewAB/swap]: the storage owner would not take the ejection\n");
			g_failures++;
			g_swapFailed = true;
			g_swapStep = SwapStep::Finished;
		}
		return;

	case SwapStep::Collect: {
		if (!deluge_file_prefix_async_active() && !g_swapCollectAsked) {
			g_swapCollectAsked = true;
			// The file backing has nothing pending -- its Arm render finished inline, against the OLD
			// card. It is the oracle for the diffed line below, so ask it afresh for the song that is at
			// that path NOW.
			poisonImage();
			SongPreviewAbProbe::select(kSongsDir, kSwapFilename);
			SongPreviewAbProbe::draw();
			return;
		}
		if (!driveCase("swap")) {
			return;
		}
		g_swapCollectDeferrals = g_caseDeferrals;

		const uint32_t crc = imageCrc();
		printCaseLine("SP-EJECT", "swap", songPath(kSongsDir, kSwapFilename).c_str(), crc);
		checkRendered("swap", crc);
		// ABSOLUTE, against the known second image. "Different from the first" would pass when both
		// reads fail -- a blank image is different from card A's artwork too.
		const Case after{"swap",      kSwapFilename,          SongPreviewOutcome::Rendered,
		                 Error::NONE, ExpectedImage::Uniform, kSwapPadB};
		checkCase("swap", after, crc);
		g_swapStep = SwapStep::Finished;
		return;
	}

	case SwapStep::Finished:
		return;
	}
}

/// Print the per-run facts that are EXPECTED to differ between the two builds, and assert the one
/// thing each build must independently be able to say about itself: that the backing it was built
/// with is the backing that actually ran.
// ── Stage: listing ──────────────────────────────────────────────────────────
//
// A folder listing performs up to four directory opens and a typing prediction up to five reads, and every one of
// them goes through the directory channel: the open is requested, the run suspends until the channel answers, and it
// resumes at the open it was waiting for. Each run below is a step of this scenario's own tick machine, so the
// channel's driver really answers between ticks; a tight loop here would never yield to it.
//
// Every run is checked twice over: its RESULT against what the fixture implies, and that it RESUMED rather than
// restarted. A run that restarts asks again for opens it has already made, so requests must equal opens. A hold on
// every open keeps each one pending across several ticks, so every run resumes from PENDING as well as UNKNOWN.

/// Everything a finished listing leaves behind that a caller can observe.
struct ListingSnapshot {
	std::string dir;
	std::string entered;
	int32_t selected = -1;
	deluge::vector<std::string> names;
};

/// The fixture directory. More than FILE_ITEMS_MAX_NUM_ELEMENTS (20) entries, because that is what makes
/// `cullSomeFileItems` fire and `arrivedInNewFolder`'s numFileItemsDeletedAtStart re-read happen: the SECOND open.
constexpr const char* kListingDir = "LISTTEST";
constexpr int32_t kListingEntries = 25;
/// Mid-folder AND deliberately absent -- see beginOpenListing for why both halves matter.
constexpr const char* kListingStartAt = "SONG012Z.XML";
static_assert(kListingEntries > FILE_ITEMS_MAX_NUM_ELEMENTS,
              "the fixture must overflow the file-item window, or no re-read is ever provoked");

/// The fallback's CREATE case: a default directory deliberately not present, so the fallback has to make it. Removed
/// before the case runs -- a leftover from a previous run would let the fallback's first attempt succeed.
constexpr const char* kMakeDir = "MADEDIR";

/// EMPTY on purpose: `pickBrandNewName` is only reached when a listing finds no files at all, and it is the path whose
/// getUnusedSlot read defers from INSIDE the listing.
constexpr const char* kEmptyDir = "EMPTYDIR";

/// A directory comfortably past the ~230 entries that once panicked the readdir snapshot arena and bricked the device
/// on open. The channel holds that snapshot across a request/collect gap, which is exactly what makes the arena's
/// window longer.
constexpr const char* kBigDir = "BIGLIST";
constexpr int32_t kBigEntries = 300;
static_assert(kBigEntries > 230, "the case must exceed the arena threshold it exists to guard");

/// What a listing of LISTTEST or BIGLIST from its start holds: the fixture's first nine songs, in order, with the
/// first selected. Measured (2026-09-12) with the read's edge trims in place. Without them the window held
/// SONG000-009 and then SONG020-023: entries read after a cull that sorted into the region it had just deleted.
constexpr std::array<const char*, 9> kReferenceNames = {"SONG000.XML", "SONG001.XML", "SONG002.XML",
                                                        "SONG003.XML", "SONG004.XML", "SONG005.XML",
                                                        "SONG006.XML", "SONG007.XML", "SONG008.XML"};

/// The slot-naming fixture: two numbered songs, and one whose name sorts AFTER getUnusedSlot's "SONG:" search key.
/// That read keeps only what sorts before the key and numbers the next slot from the last entry kept, so the one-sided
/// trim is what keeps SONGZZZ from being that entry. Without it the slot comes out as SONG1.
constexpr const char* kSlotDir = "SLOTTEST";
constexpr const char* kSlotExpected = "SONG003";

/// Reaching prediction's SECOND read takes a big folder, and that is the point rather than an awkwardness of the
/// fixture. The re-read happens when a search runs off the end of the CACHED window. In the 300-entry folder the
/// window holds twenty, so the extra-zeroes branch (which clears `doneNewRead`) walks the extended string off the
/// cached end and reads again -- exactly the #4584 case the re-read exists for.
constexpr const char* kPredictDir = kBigDir;
constexpr const char* kPredictPrefix = "SONG0129";
/// Where that prediction lands from the list the reference listing leaves, measured (2026-09-12): the first entry of
/// its final window.
constexpr const char* kPredictText = "SONG281";
constexpr int32_t kPredictSelected = 0;
/// Fewer reads than this and the prediction never resumed across a second read.
constexpr int32_t kPredictMinReads = 2;
/// Matches nothing however many zeroes are appended, so the prediction FAILS -- which is what the retraction cases
/// need.
constexpr const char* kMissingPrefix = "ZZZZNOPE";

/// The name the save browser gives a brand-new song in EMPTYDIR, and the opens it takes: the listing's read, the
/// slot-naming read from inside its selection, and the re-read for the new name. Measured (2026-09-12).
constexpr const char* kNewName = "SONG1";
constexpr int32_t kNewNameOpens = 3;

/// How long every directory open is held in flight while the listing and by-path stages run, in milliseconds of the
/// harness's virtual clock. Long enough that each open is still pending when it is asked for again on the next ticks.
constexpr uint32_t kDirectoryHoldMs = 5;
/// Ticks one run gets before it is called wedged. A run here takes a few dozen.
constexpr int32_t kMaxRunTicks = 400;

enum class ListingStep : uint8_t {
	Fixture,
	Reference,
	Predict,
	RetractUndone,
	RetractKept,
	BigFolder,
	Fallback,
	Create,
	NewName,
	Slot,
	Mount,
	Finished,
};
ListingStep g_listingStep = ListingStep::Fixture;
bool g_listingRunning = false;   ///< a run is in flight and being ticked
int32_t g_listingTicks = 0;      ///< tick budget guard for the run in flight
int32_t g_listingRuns = 0;       ///< runs that finished
int32_t g_runRequestsBefore = 0; ///< Browser::directoryRequests() when the run in flight began
int32_t g_runOpensBefore = 0;    ///< Browser::totalDirectoryOpens() when the run in flight began
int32_t g_slotCallsBefore = 0;   ///< Browser::unusedSlotCalls() when the new-name run began
uint32_t g_mountNoticesBefore = 0;
/// How many items the list held when the run in flight started. A suspended listing must not blank the list it is
/// replacing -- but only where there WAS one, so a listing with nothing to preserve is not counted as a violation.
size_t g_itemsWhenRunBegan = 0;
/// Set if a suspended listing was ever seen with an empty list it should have been preserving.
bool g_sawBlankWhileSuspended = false;
bool g_slotRan = false;  ///< the owner op naming the slot has run
std::string g_slotName;  ///< what it named
int32_t g_slotTicks = 0; ///< tick budget guard for it

ListingSnapshot takeListingSnapshot() {
	ListingSnapshot snap;
	snap.dir = Browser::currentDir;
	snap.entered = Browser::enteredText;
	snap.selected = BrowserListingAbProbe::selectedIndex();
	for (const FileItem& item : Browser::fileItems) {
		snap.names.push_back(item.filename);
	}
	return snap;
}

/// The names in `snap`, comma-separated, for a failure message.
std::string joinNames(const ListingSnapshot& snap) {
	std::string all;
	for (const std::string& name : snap.names) {
		if (!all.empty()) {
			all += ',';
		}
		all += name;
	}
	return all;
}

/// Whether `snap` lists exactly kReferenceNames.
bool listsReferenceNames(const ListingSnapshot& snap) {
	if (snap.names.size() != kReferenceNames.size()) {
		return false;
	}
	for (size_t i = 0; i < kReferenceNames.size(); i++) {
		if (snap.names[i] != kReferenceNames[i]) {
			return false;
		}
	}
	return true;
}

/// Build kSlotDir. Idempotent, like buildFixtureDir: a reused image already has the files.
void buildSlotFixture() {
	(void)deluge::io::mkdir(kSlotDir);
	for (const char* name : {"SONG001.XML", "SONG002.XML", "SONGZZZ.XML"}) {
		char path[64];
		snprintf(path, sizeof(path), "%s/%s", kSlotDir, name);
		auto file = deluge::io::File::open(path, DELUGE_FILE_WRITE_CREATE_NEW);
		if (!file.has_value()) {
			continue; // already present from a previous run
		}
		const char body[] = "<song firmwareVersion=\"c1.3.0\"></song>";
		(void)file->write(std::as_bytes(std::span{body, sizeof(body) - 1}));
		(void)file->close();
	}
}

/// Create a directory of `count` tiny song files. Idempotent: a re-run of the harness against a
/// reused image finds them already there.
bool buildFixtureDir(const char* dir, int32_t count) {
	(void)deluge::io::mkdir(dir);
	for (int32_t i = 0; i < count; i++) {
		char path[64];
		snprintf(path, sizeof(path), "%s/SONG%03d.XML", dir, static_cast<int>(i));
		auto file = deluge::io::File::open(path, DELUGE_FILE_WRITE_CREATE_NEW);
		if (!file.has_value()) {
			continue; // already present from a previous run
		}
		const char body[] = "<song firmwareVersion=\"c1.3.0\"></song>";
		(void)file->write(std::as_bytes(std::span{body, sizeof(body) - 1}));
		(void)file->close();
	}
	// Prove the fixture is what the case needs rather than assuming it. A directory that came back
	// short would make the big-folder case stop exercising the arena threshold while still reporting
	// a green run -- the "gate that covers nothing" shape this branch has hit twice.
	auto opened = deluge::io::Directory::open(dir);
	if (!opened.has_value()) {
		printf("SP-LISTING FAIL could not open fixture %s\n", dir);
		return false;
	}
	int32_t counted = 0;
	while (true) {
		auto entry = opened->read();
		if (!entry.has_value() || !entry->has_value()) {
			break;
		}
		if ((*entry)->name[0] != '.') {
			counted++;
		}
	}
	if (counted < count) {
		printf("SP-LISTING FAIL fixture %s has %d entries, needs %d\n", dir, counted, count);
		return false;
	}
	return true;
}

/// Note where the counters stand, so a finished run's own requests and opens can be told from everything before it.
void markCounters() {
	g_runRequestsBefore = Browser::directoryRequests();
	g_runOpensBefore = Browser::totalDirectoryOpens();
}

int32_t runRequests() {
	return Browser::directoryRequests() - g_runRequestsBefore;
}

int32_t runOpens() {
	return Browser::totalDirectoryOpens() - g_runOpensBefore;
}

/// Require the finished run to have RESUMED at every open: exactly one channel request per open, and at least one
/// open. A run that restarted from the top asks again for the opens it had already made.
void checkResumed(const char* label) {
	const int32_t requests = runRequests();
	const int32_t opens = runOpens();
	if (opens == 0) {
		printf("SP-LISTING FAIL %s made no open at all\n", label);
		g_failures++;
	}
	else if (requests != opens) {
		printf("SP-LISTING FAIL %s made %d request(s) for %d open(s) -- an open was asked for again, so the run "
		       "restarted instead of resuming\n",
		       label, requests, opens);
		g_failures++;
	}
}

/// Begin the run for `step`. Call immediately BEFORE starting it: a prediction makes its first request inline.
void beginRun(ListingStep step) {
	g_listingStep = step;
	markCounters();
	g_itemsWhenRunBegan = Browser::fileItems.size();
	g_listingTicks = 0;
	g_listingRunning = true;
}

/// End the stage, releasing the hold so nothing after it runs with its opens delayed.
void finishListingStage() {
	deluge_file_directory_set_latency_ms(0);
	g_listingRunning = false;
	g_listingStep = ListingStep::Finished;
}

const char* listingStepName(ListingStep step) {
	switch (step) {
	case ListingStep::Fixture:
		return "fixture";
	case ListingStep::Reference:
		return "reference";
	case ListingStep::Predict:
		return "predict";
	case ListingStep::RetractUndone:
		return "retraction(undone)";
	case ListingStep::RetractKept:
		return "retraction(kept)";
	case ListingStep::BigFolder:
		return "big";
	case ListingStep::Fallback:
		return "fallback";
	case ListingStep::Create:
		return "create";
	case ListingStep::NewName:
		return "newname";
	case ListingStep::Slot:
		return "slot";
	case ListingStep::Mount:
		return "mount";
	case ListingStep::Finished:
		return "finished";
	}
	return "?";
}

/// One tick of the run in flight. @return Whether it has finished.
bool tickRun() {
	switch (g_listingStep) {
	case ListingStep::Predict:
	case ListingStep::RetractUndone:
	case ListingStep::RetractKept:
		// A prediction that suspended stays active until the load browser's tick finishes it.
		if (BrowserListingAbProbe::predicting()) {
			BrowserListingAbProbe::tick();
		}
		return !BrowserListingAbProbe::predicting();
	case ListingStep::NewName:
		if (BrowserListingAbProbe::inProgress()) {
			BrowserListingAbProbe::tickSave();
		}
		return !BrowserListingAbProbe::inProgress();
	default:
		// Checked BEFORE the tick, so it observes the state a user would see between frames.
		if (g_itemsWhenRunBegan > 0 && BrowserListingAbProbe::inProgress() && Browser::fileItems.empty()) {
			g_sawBlankWhileSuspended = true;
		}
		if (BrowserListingAbProbe::inProgress()) {
			BrowserListingAbProbe::tick();
		}
		return !BrowserListingAbProbe::inProgress();
	}
}

/// Check the run that just finished, then start the next one.
void finishRun() {
	const ListingSnapshot got = takeListingSnapshot();
	switch (g_listingStep) {
	case ListingStep::Reference: {
		const int32_t deferrals = BrowserListingAbProbe::deferrals();
		printf("SP-LISTING reference entries=%zu opens=%d requests=%d deferrals=%d\n", got.names.size(), runOpens(),
		       runRequests(), deferrals);
		checkResumed("reference");
		if (runOpens() < 2) {
			printf("SP-LISTING FAIL reference made only %d open(s) -- the re-read after the cull never happened\n",
			       runOpens());
			g_failures++;
		}
		// Each request is one UNKNOWN answer, and so one deferral. Deferrals beyond that are PENDING answers: an open
		// still in flight when asked again. None at all means the hold kept nothing pending and resuming from PENDING
		// went untested.
		if (deferrals <= runRequests()) {
			printf("SP-LISTING FAIL reference deferred %d time(s) for %d request(s) -- no open was ever still pending "
			       "when asked again\n",
			       deferrals, runRequests());
			g_failures++;
		}
		if (!listsReferenceNames(got) || got.entered != "SONG000" || got.selected != 0) {
			printf("SP-LISTING FAIL reference listed %s entered=%s sel=%d, wanted SONG000-008 entered=SONG000 sel=0\n",
			       joinNames(got).c_str(), got.entered.c_str(), got.selected);
			g_failures++;
		}
		// A prediction searches the list a listing leaves, as a keystroke does, so it runs straight after this one.
		beginRun(ListingStep::Predict);
		BrowserListingAbProbe::beginPrediction(kPredictDir, kPredictPrefix, std::nullopt);
		return;
	}

	case ListingStep::Predict:
		printf("SP-PREDICT text=%s selected=%d reads=%d requests=%d\n", got.entered.c_str(), got.selected, runOpens(),
		       runRequests());
		checkResumed("predict");
		if (got.entered != kPredictText || got.selected != kPredictSelected || runOpens() < kPredictMinReads) {
			printf("SP-PREDICT FAIL text=%s selected=%d reads=%d, wanted %s selected=%d reads>=%d\n",
			       got.entered.c_str(), got.selected, runOpens(), kPredictText, kPredictSelected, kPredictMinReads);
			g_failures++;
		}
		// Only the pad-typing caller undoes. Its undo must happen even when the prediction fails on a LATER tick, long
		// after that caller returned true and moved on.
		beginRun(ListingStep::RetractUndone);
		BrowserListingAbProbe::beginPrediction(kPredictDir, kMissingPrefix,
		                                       static_cast<int32_t>(strlen(kMissingPrefix)) - 1);
		return;

	case ListingStep::RetractUndone: {
		const int32_t wanted = static_cast<int32_t>(strlen(kMissingPrefix)) - 1;
		printf("SP-PREDICT retraction undone editPos=%d wanted=%d reads=%d requests=%d\n",
		       BrowserListingAbProbe::editPos(), wanted, runOpens(), runRequests());
		checkResumed("retraction(undone)");
		if (BrowserListingAbProbe::editPos() != wanted) {
			printf("SP-PREDICT FAIL the pad site's failure left editPos=%d, wanted %d\n",
			       BrowserListingAbProbe::editPos(), wanted);
			g_failures++;
		}
		beginRun(ListingStep::RetractKept);
		BrowserListingAbProbe::beginPrediction(kPredictDir, kMissingPrefix, std::nullopt);
		return;
	}

	case ListingStep::RetractKept: {
		const int32_t wanted = static_cast<int32_t>(strlen(kMissingPrefix));
		printf("SP-PREDICT retraction kept editPos=%d wanted=%d reads=%d requests=%d\n",
		       BrowserListingAbProbe::editPos(), wanted, runOpens(), runRequests());
		checkResumed("retraction(kept)");
		if (BrowserListingAbProbe::editPos() != wanted) {
			printf("SP-PREDICT FAIL a non-pad failure moved editPos to %d, wanted %d\n",
			       BrowserListingAbProbe::editPos(), wanted);
			g_failures++;
		}
		printf("SP-PREDICT-SUMMARY runs=3\n");
		// Surviving the open at all is the property of the next run: >230 entries once panicked the snapshot arena on
		// open and bricked the device.
		beginRun(ListingStep::BigFolder);
		BrowserListingAbProbe::beginOpenListing(kBigDir, kListingStartAt);
		return;
	}

	case ListingStep::BigFolder:
		printf("SP-LISTING big entries=%d listed=%zu requests=%d deferrals=%d\n", kBigEntries, got.names.size(),
		       runRequests(), BrowserListingAbProbe::deferrals());
		checkResumed("big");
		if (!listsReferenceNames(got) || got.entered != "SONG000" || got.selected != 0) {
			printf("SP-LISTING FAIL big listed %s entered=%s sel=%d, wanted SONG000-008 entered=SONG000 sel=0\n",
			       joinNames(got).c_str(), got.entered.c_str(), got.selected);
			g_failures++;
		}
		// The missing-folder fallback, landing on a directory big enough to CULL, entered WITH a search string. All
		// three matter. The fallback drops the search string, and dropping it without also resetting
		// catalogSearchDirection sends the cull into a search for a name that is no longer there. So this case covers
		// both halves: that the fallback happens at all, and that it does not take that fault with it.
		beginRun(ListingStep::Fallback);
		BrowserListingAbProbe::beginOpenListing("NOSUCHDIR", kListingStartAt, kBigDir);
		return;

	case ListingStep::Fallback:
		printf("SP-LISTING fallback dir=%s listed=%zu requests=%d deferrals=%d\n", got.dir.c_str(), got.names.size(),
		       runRequests(), BrowserListingAbProbe::deferrals());
		checkResumed("fallback");
		if (!deluge::string::caselessEquals(got.dir, kBigDir) || !listsReferenceNames(got)) {
			printf("SP-LISTING FAIL fallback landed on dir=%s listing %s, wanted %s listing SONG000-008\n",
			       got.dir.c_str(), joinNames(got).c_str(), kBigDir);
			g_failures++;
		}
		// Now the same fallback against a default directory that does NOT exist, so it has to be created. Off the
		// storage owner the creation rides the channel's request, on the storage tier, with the open.
		(void)deluge::io::unlink(kMakeDir);
		if (deluge::io::Directory::open(kMakeDir).has_value()) {
			printf("SP-LISTING FAIL fixture: %s still exists, the create case would test nothing\n", kMakeDir);
			g_failures++;
			finishListingStage();
			return;
		}
		beginRun(ListingStep::Create);
		BrowserListingAbProbe::beginOpenListing("NOSUCHDIR2", kListingStartAt, kMakeDir);
		return;

	case ListingStep::Create: {
		const bool exists = deluge::io::Directory::open(kMakeDir).has_value();
		printf("SP-LISTING create dir=%s exists=%d requests=%d deferrals=%d\n", got.dir.c_str(), exists ? 1 : 0,
		       runRequests(), BrowserListingAbProbe::deferrals());
		checkResumed("create");
		if (!exists || !deluge::string::caselessEquals(got.dir, kMakeDir) || !got.names.empty()) {
			printf("SP-LISTING FAIL create: landed on %s listing %zu entries, %s exists=%d\n", got.dir.c_str(),
			       got.names.size(), kMakeDir, exists ? 1 : 0);
			g_failures++;
		}
		// A listing that finds NO files names a brand-new slot, and naming it reads the folder again (getUnusedSlot)
		// from INSIDE the listing. A deferral there must SUSPEND the listing, not fail it and not restart it --
		// `pickBrandNewName`'s `enteredText.empty()` guard is what stops a resumption naming twice.
		g_slotCallsBefore = Browser::unusedSlotCalls();
		beginRun(ListingStep::NewName);
		BrowserListingAbProbe::beginSaveListing(kEmptyDir);
		return;
	}

	case ListingStep::NewName:
		// slotCalls is reported, not asserted: a naming read that deferred and was then re-run is legitimately two
		// calls. It is here because it is what first exposed a restart, when an open count that reset on re-arm could
		// not see one.
		printf("SP-NEWNAME name=%s opens=%d requests=%d slotCalls=%d deferrals=%d\n", got.entered.c_str(), runOpens(),
		       runRequests(), Browser::unusedSlotCalls() - g_slotCallsBefore, BrowserListingAbProbe::deferrals());
		checkResumed("newname");
		if (got.entered != kNewName || runOpens() != kNewNameOpens) {
			printf("SP-NEWNAME FAIL the save browser named %s in %d open(s), wanted %s in %d\n", got.entered.c_str(),
			       runOpens(), kNewName, kNewNameOpens);
			g_failures++;
		}
		// Slot naming, on the storage owner, where creating a new song names one. Here, after a listing that leaves no
		// file selected: getUnusedSlot empties the list, and a selection left pointing into an empty list is
		// dereferenced by the next prediction.
		buildSlotFixture();
		g_slotRan = false;
		g_slotTicks = 0;
		(void)deluge::storage::Owner::run(
		    [](void*) {
			    g_slotName = BrowserListingAbProbe::unusedSlotIn(kSlotDir);
			    g_slotRan = true;
		    },
		    nullptr);
		g_listingStep = ListingStep::Slot;
		return;

	case ListingStep::Mount: {
		// One notice, not zero and not two. Zero is the hazard the mount generation exists for: a driver that mounts
		// silently leaves is_mounted() true, and an edge detector that sampled that would skip the notification
		// forever. Two is the other side of it -- both initSD() and the tick surface noticing the same mount.
		const uint32_t notices = StorageManager::cardMountNotices() - g_mountNoticesBefore;
		printf("SP-LISTING mount listed=%zu notices=%u requests=%d deferrals=%d\n", got.names.size(), notices,
		       runRequests(), BrowserListingAbProbe::deferrals());
		checkResumed("mount");
		if (!listsReferenceNames(got)) {
			printf("SP-LISTING FAIL mount: listed %s from an unmounted card, wanted SONG000-008\n",
			       joinNames(got).c_str());
			g_failures++;
		}
		if (notices != 1) {
			printf("SP-LISTING FAIL mount: the mount edge fired %u times, wanted exactly 1\n", notices);
			g_failures++;
		}
		if (g_sawBlankWhileSuspended) {
			printf("SP-LISTING FAIL a suspended listing blanked the folder it was replacing\n");
			g_failures++;
		}
		printf("SP-LISTING-SUMMARY runs=%d big=%d blanked=%d\n", g_listingRuns, kBigEntries,
		       g_sawBlankWhileSuspended ? 1 : 0);
		finishListingStage();
		return;
	}

	case ListingStep::Fixture:
	case ListingStep::Slot:
	case ListingStep::Finished:
		finishListingStage();
		return;
	}
}

void runListingTick() {
	if (g_listingRunning) {
		if (!tickRun()) {
			if (++g_listingTicks > kMaxRunTicks) {
				printf("SP-LISTING FAIL %s never finished -- nothing re-asked\n", listingStepName(g_listingStep));
				g_failures++;
				finishListingStage();
			}
			return; // still waiting on the channel; the tick surface runs it again
		}
		g_listingRunning = false;
		g_listingRuns++;
		finishRun();
		return;
	}

	switch (g_listingStep) {
	case ListingStep::Fixture:
		// Every fixture up front: the prediction runs straight after the reference listing and searches BIGLIST.
		if (!buildFixtureDir(kListingDir, kListingEntries) || !buildFixtureDir(kEmptyDir, 0)
		    || !buildFixtureDir(kBigDir, kBigEntries)) {
			g_failures++;
			finishListingStage();
			return;
		}
		deluge_file_directory_set_latency_ms(kDirectoryHoldMs);
		beginRun(ListingStep::Reference);
		BrowserListingAbProbe::beginOpenListing(kListingDir, kListingStartAt);
		return;

	case ListingStep::Slot:
		// The marker op behind this step's dispatch makes one tick enough; bounded anyway.
		if (!g_slotRan && ++g_slotTicks < 200) {
			return;
		}
		printf("SP-SLOT dir=%s name=%s\n", kSlotDir, g_slotRan ? g_slotName.c_str() : "(never ran)");
		if (!g_slotRan || g_slotName != kSlotExpected) {
			printf("SP-SLOT FAIL named %s, wanted %s -- an entry sorting after the search key stayed in the read, "
			       "and the slot was numbered from it\n",
			       g_slotRan ? g_slotName.c_str() : "(never ran)", kSlotExpected);
			g_failures++;
		}

		// Now with the filesystem UNMOUNTED. Mounting is real card I/O, so it belongs to the channel's driver, not to
		// StorageManager::initSD() on the interaction tier. The listing has to succeed anyway, and -- the half that is
		// invisible in its result -- the unmounted->mounted edge has to reach firstCardRead() exactly once, even though
		// nothing in C++ did the mounting.
		g_mountNoticesBefore = StorageManager::cardMountNotices();
		deluge_efatfs_unmount();
		beginRun(ListingStep::Mount);
		BrowserListingAbProbe::beginOpenListing(kListingDir, kListingStartAt);
		return;

	default:
		return;
	}
}

// ---- Acquire stage -------------------------------------------------------

/// Write a real, loadable synth preset to `path` by round-tripping the SAME production path a
/// "new synth" gesture and a preset save use -- StorageManager::createNewInstrument() +
/// SoundInstrument::setupAsBlankSynth(), then Output::writeToFile() -- rather than hand-writing an
/// XML stub.
///
/// A minimal `<sound></sound>` with no param-collection tags at all is not a shape any real preset
/// takes: nothing in it ever calls ENSURE_PARAM_MANAGER_EXISTS (sound.cpp), so
/// loadInstrumentFromFile finds no backed-up ParamManager, takes its FILE_CORRUPTED/delete cleanup
/// path (storage_manager.cpp, `paramManagersMissing`), and segfaults freeing the Instrument -- a
/// real, pre-existing bug, found by running this gate, and not this stage's to fix (see the task
/// report). Building the fixture from a real Instrument sidesteps it and is more representative of
/// what the acquisition walk actually reads on a card.
///
/// The helper Instrument is deliberately leaked: it is never added to currentSong (no addOutput
/// call), so nothing else ever scans it, and this is a short-lived test process.
bool writeBlankSynthPresetFile(const char* path) {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		return false;
	}
	static_cast<SoundInstrument*>(instrument)->setupAsBlankSynth(&paramManager, false);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(instrument->toModControllable()), nullptr,
	                                &paramManager, true);

	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return false;
	}
	instrument->writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</sound>\n");
	return error == Error::NONE;
}

/// Create a directory of `count` real synth presets named `namePrefix00.XML`, `namePrefix01.XML`,
/// etc. Mirrors buildFixtureDir(), which writes SONG xml -- a synth preset's root tag is "sound"
/// (storage_manager.cpp:300), and a file whose root tag is wrong fails to load, which would make
/// every acquisition here fail identically on both legs and the comparison pass while covering
/// nothing.
///
/// `namePrefix` must differ between the top-level directory and any subfolder placed under it:
/// `Browser::deleteFolderAndDuplicateItems` drops a file whose display name collides with another
/// entry already seen, so two directories both handing out "PRESET00" would make the recursive
/// descent this stage exists to prove silently lose an entry instead of genuinely finding a nested
/// one.
bool buildPresetFixture(const char* dir, const char* namePrefix, int32_t count) {
	(void)deluge::io::mkdir(dir);
	for (int32_t i = 0; i < count; i++) {
		char path[64];
		snprintf(path, sizeof(path), "%s/%s%02d.XML", dir, namePrefix, static_cast<int>(i));
		auto existing = deluge::io::File::open(path, DELUGE_FILE_READ);
		if (existing.has_value()) {
			(void)existing->close();
			continue; // already present from a previous run
		}
		if (!writeBlankSynthPresetFile(path)) {
			printf("SP-ACQUIRE FAIL could not write preset fixture %s\n", path);
			return false;
		}
	}
	auto opened = deluge::io::Directory::open(dir);
	if (!opened.has_value()) {
		printf("SP-ACQUIRE FAIL could not open fixture %s\n", dir);
		return false;
	}
	int32_t counted = 0;
	while (true) {
		auto entry = opened->read();
		if (!entry.has_value() || !entry->has_value()) {
			break;
		}
		if ((*entry)->name[0] != '.') {
			counted++;
		}
	}
	if (counted < count) {
		printf("SP-ACQUIRE FAIL fixture %s has %d entries, needs %d\n", dir, counted, count);
		return false;
	}
	return true;
}

// Drives the REAL gesture entry point that acquires an instrument -- ArrangerView::
// beginNewInstrumentAcquisition(), extracted verbatim from the pad-press handler -- which is the
// only thing that exercises findAnUnlaunchedPresetIncludingWithinSubfolders' recursive descent (see
// the rung 5 spec, section 2b). Since rung 5 task 3, this gesture DISPATCHES onto the storage owner
// (Owner::run_or_inline enqueues rather than running inline -- this Embassy host harness's
// on_owner() is false while a tick is running, since the tick itself runs on the UI tier, not the
// worker fiber). The op only runs during the queue-drain wait AFTER dispatch_step() returns (see
// that function's marker-op comment), so every step that reads a dispatched gesture's result is
// split into a dispatching state and a `*Check` state one tick later -- reading in the SAME tick as
// the dispatch would see the pre-dispatch state, not a race so much as a guarantee that the op has
// not run yet.
//
// The recursive descent (load_instrument_preset_ui.cpp:1291-1296 vs :1317) is only reached once the
// TOP LEVEL is exhausted -- a preset sitting at the top level is always returned first, however many
// subfolders sit alongside it. So the fixture keeps exactly ONE top-level preset: Reference acquires
// and releases it (still top-level), Converted acquires it again and keeps it (top-level now
// permanently empty), and every acquisition from AcquireStep::Nested onward MUST descend into
// SYNTHS/NESTED to find anything at all. `AcquireStep::NestedCheck` asserts the acquired name is a
// nested one, which is the only way to prove the walk actually descended rather than merely that
// some acquisition happened to succeed.
enum class AcquireStep : uint8_t {
	Fixture,
	Reference,
	ReferenceCheck,
	Converted,
	ConvertedCheck,
	Nested,
	NestedCheck,
	Coalesce,
	CoalesceCheck,
	NonAudio,
	NonAudioCheck,
	ViewNavigate,
	ViewNavigateCheck,
	ViewChangeTypeCV,
	ViewChangeTypeCVCheck,
	ViewChangeTypeSynth,
	ViewChangeTypeSynthCheck,
	SessionChangeTypeCV,
	SessionChangeTypeCVCheck,
	SessionChangeTypeSynth,
	SessionChangeTypeSynthCheck,
	SessionNewTrack,
	SessionNewTrackCheck,
	SessionNavigate,
	SessionNavigateCheck,
	SessionNewTrackEnter,
	SessionNewTrackEnterCheck,
	ViewNewInstrument,
	ViewNewInstrumentCheck,
	ArrangerAuditionPad,
	ArrangerAuditionPadCheck,
	ArrangerAuditionRelease,
	ArrangerAuditionReleaseCheck,
	Finished
};
AcquireStep g_acquireStep = AcquireStep::Fixture;

// The INSTRUMENT folder, not a song folder: findAnUnlaunchedPresetIncludingWithinSubfolders reads
// getInstrumentFolder(outputType), which is "SYNTHS". BIGLIST holds song XML and would make every
// acquisition fail identically on both legs -- a gate comparing two failures. Pinned against drift
// by AcquireStep::Fixture, which compares this literal to getInstrumentFolder(OutputType::SYNTH)
// directly rather than trusting the two to stay in sync silently.
constexpr const char* kAcqDir = "SYNTHS";
constexpr const char* kAcqSubDir = "SYNTHS/NESTED";
// Exactly one: Reference+Converted consume it (see the recursive-descent doc above), so the top
// level is provably empty by the time Nested and Coalesce run.
constexpr int32_t kAcqTopLevelCount = 1;
// One for AcquireStep::Nested's proof, three more for Coalesce's three gestures, one each for the
// four dispatching View/Session gestures further down, one for the audition-pad press that closes
// the stage, plus spares -- none of those assert identity, but running the folder dry would make
// their gestures fail silently instead of exercising the walk, and a silent failure is exactly what
// their outcome assertions are there to catch.
constexpr int32_t kAcqNestedCount = 14;
// buildPresetFixture() names files "<prefix><NN>.XML"; kAcqTopLevelCount == 1 makes this the only
// top-level name there is, so it can be asserted absolutely rather than merely checked non-empty.
constexpr const char* kAcqTopLevelName = "PRESET00";
// What buildPresetFixture() names SYNTHS/NESTED's first entry -- the recursive descent's target.
// Distinct from "PRESET..." on purpose: Browser::deleteFolderAndDuplicateItems drops a file whose
// display name collides with one already seen, so reusing the top-level names here would make
// AcquireStep::Nested's find silently skip the very entries recursion is supposed to prove reachable.
constexpr const char* kAcqNestedFirstName = "NESTED00";
// The row the gesture is pressed on. `beginNewInstrumentAcquisition` clamps this into
// [-arrangementYScroll-1, -arrangementYScroll+getNumOutputs()] and stores the CLAMPED value back
// into `yPressedEffective` before it ever indexes `outputsOnScreen` -- arrangementYScroll is not 0
// by the time this stage runs (the arranger view auto-scrolls as outputs are added by earlier
// gestures in this same stage), so the row actually written can differ from this one. Read back via
// `arrangerView.yPressedEffective`, not this constant, for exactly that reason.
constexpr int32_t kAcqRow = 0;
// Read from the production counter (LoadInstrumentPresetUI::presetAcquisitionsRun()): it counts ops
// that actually RUN, not dispatch attempts.
int32_t acqDispatches() {
	return LoadInstrumentPresetUI::presetAcquisitionsRun();
}
int32_t g_acqLoads = 0;          // acquisition gestures that actually ran
std::string g_acqReferenceName;  // the instrument the reference gesture settled on
int32_t g_acqCoalesceBefore = 0; // presetAcquisitionsRun() sampled before Coalesce's 3 gestures
int32_t g_acqNonAudioBefore = 0; // presetAcquisitionsRun() sampled before NonAudio's CV switch

// The boot song's session InstrumentClip -- View::changeOutputType() and
// View::navigateThroughPresetsForInstrumentClip() (rung 5, task 4) act on a Clip, not an Output row,
// so the arranger's outputsOnScreen rows above cannot drive them at all. setupBlankSong() (deluge.cpp)
// calls Song::ensureAtLeastOneSessionClip() before this scenario ever runs, so a session Clip -- with
// a real SYNTH Instrument, dirPath "SYNTHS" -- already exists by the time AcquireStep::ViewNavigate
// reads it back via getCurrentInstrumentClip().
InstrumentClip* g_viewClip = nullptr;
int32_t g_acqViewBefore = 0;       // presetAcquisitionsRun() sampled before each View gesture below
std::string g_acqViewNameBefore{}; // g_viewClip->output's name, sampled before ViewNavigate dispatches

// The empty-pad new-track gesture (AcquireStep::SessionNewTrack). The pad has to be one with no clip
// on it: the boot song holds a single session clip at index 0 and songViewYScroll is forced to 0, so
// any row past the first is empty.
constexpr int32_t kAcqNewTrackX = 0;
constexpr int32_t kAcqNewTrackY = 3;
int32_t g_acqNewTrackClipsBefore = 0; // sessionClips size sampled before the gesture dispatches
// What that gesture created. AcquireStep::SessionNavigate drives its Grid gesture on THIS clip rather
// than the boot one: its Instrument comes from SYNTHS/NESTED, the only folder here with enough entries
// for a one-step navigation to land somewhere else (see that step's doc).
Clip* g_acqNewTrackClip = nullptr;

// Where AcquireStep::SessionNewTrackEnter presses. Any grid cell whose section maps in range does:
// the gesture creates its own track, so nothing has to already be at these coordinates.
constexpr int32_t kAcqEnterX = 1;
constexpr int32_t kAcqEnterY = kGridHeight - 1;

// The arranger row AcquireStep::ArrangerAuditionPad presses on. Chosen at run time rather than pinned:
// by the time that step runs, every gesture above has placed an Output on some row, and which rows are
// left empty depends on arrangementYScroll. -1 means none were, which that step reports as a failure
// rather than skipping quietly.
int32_t g_acqAuditionRow = -1;

/// The instrument name an acquisition settled on, or "" if it failed. Reads the Output's name
/// rather than any FileItem -- a FileItem does not survive emptyFileItems() (spec section 6).
std::string acquiredName(Output* output) {
	return (output != nullptr) ? output->name : std::string{};
}

/// Dispatch the acquisition gesture on row `kAcqRow`. Does NOT read back the result -- the op it
/// dispatches has not run by the time this returns (see the doc above AcquireStep). Callers read the
/// outcome via currentAcquiredName() from their step's own `*Check` state, one tick later.
void dispatchAcquisitionGesture() {
	BrowserListingAbProbe::beginNewInstrumentAcquisition(kAcqRow);
}

/// The row's current Output name, or "" if none. Reads back through `arrangerView.yPressedEffective`
/// (see `kAcqRow`'s doc) -- exactly what the real pad handler reads after a completed acquisition.
std::string currentAcquiredName() {
	return acquiredName(arrangerView.outputsOnScreen[arrangerView.yPressedEffective]);
}

/// Undo the acquisition gesture's `addOutput`, so the SAME preset is available to the next gesture
/// on the same row.
///
/// Without this, `Availability::INSTRUMENT_UNUSED` (createNewInstrument's own filter,
/// arranger_view.cpp) excludes whatever the last gesture settled on from the very next search: two
/// gestures in a row always land on two DIFFERENT presets, never the same one twice. Left alone,
/// AcquireStep::Converted would be comparing a DIFFERENT question ("what's the next unused preset
/// after PRESET00?") against the reference's, not the same question asked twice through a different
/// backing -- which is what this stage exists to check.
void releaseAcquiredInstrument() {
	Output* acquired = arrangerView.outputsOnScreen[arrangerView.yPressedEffective];
	if (acquired == nullptr) {
		return;
	}
	arrangerView.outputsOnScreen[arrangerView.yPressedEffective] = nullptr;
	currentSong->deleteOutputThatIsInMainList(acquired);
}

void runAcquireTick() {
	switch (g_acquireStep) {
	case AcquireStep::Fixture: {
		// The duplicated "SYNTHS" literal (kAcqDir), pinned against the two silently drifting apart.
		if (std::string{kAcqDir} != getInstrumentFolder(OutputType::SYNTH)) {
			printf("SP-ACQUIRE FAIL kAcqDir=%s no longer matches getInstrumentFolder(SYNTH)=%s\n", kAcqDir,
			       getInstrumentFolder(OutputType::SYNTH));
			g_failures++;
		}
		// One top-level preset, and a nested folder holding several more: see the recursive-descent
		// doc above AcquireStep's declaration for why the counts are exactly this shape.
		if (!buildPresetFixture(kAcqDir, "PRESET", kAcqTopLevelCount)
		    || !buildPresetFixture(kAcqSubDir, "NESTED", kAcqNestedCount)) {
			printf("SP-ACQUIRE FAIL could not build the preset fixture under %s\n", kAcqDir);
			g_failures++;
		}
		g_acquireStep = AcquireStep::Reference;
		break;
	}
	case AcquireStep::Reference: {
		dispatchAcquisitionGesture();
		g_acquireStep = AcquireStep::ReferenceCheck;
		break;
	}
	case AcquireStep::ReferenceCheck: {
		g_acqReferenceName = currentAcquiredName();
		printf("SP-ACQUIRE reference name=%s\n", g_acqReferenceName.c_str());
		if (g_acqReferenceName.empty()) {
			printf("SP-ACQUIRE FAIL reference acquired nothing -- the stage would compare two failures\n");
			g_failures++;
		}
		else {
			// Pinned absolutely, not just non-empty: with exactly one top-level preset, the walk has
			// only one possible correct answer here, and asserting it is what would catch a later
			// conversion that changed the acquired preset identically on every leg (a bug the
			// cross-leg shell check below cannot see, because it only diffs legs against each other).
			if (g_acqReferenceName != kAcqTopLevelName) {
				printf("SP-ACQUIRE FAIL reference acquired %s, expected the top-level preset %s\n",
				       g_acqReferenceName.c_str(), kAcqTopLevelName);
				g_failures++;
			}
			// Release it so AcquireStep::Converted asks the SAME question the reference did, rather
			// than the next one along -- see releaseAcquiredInstrument's doc.
			releaseAcquiredInstrument();
		}
		g_acquireStep = AcquireStep::Converted;
		break;
	}
	case AcquireStep::Converted: {
		// Re-run the SAME gesture in the SAME process and require the SAME instrument. This can only
		// detect in-process NONDETERMINISM: both calls run identical code in the identical binary, so
		// this pair alone proves nothing about a channel conversion. The one check that spans
		// backings is the shell's file-vs-dir comparison of this exact "converted" line
		// (song_preview_ab.sh, the acquisition-agreement loop).
		dispatchAcquisitionGesture();
		g_acqLoads++;
		g_acquireStep = AcquireStep::ConvertedCheck;
		break;
	}
	case AcquireStep::ConvertedCheck: {
		const std::string name = currentAcquiredName();
		printf("SP-ACQUIRE converted name=%s dispatches=%d\n", name.c_str(), acqDispatches());
		if (name != g_acqReferenceName) {
			printf("SP-ACQUIRE FAIL converted acquired %s, reference acquired %s\n", name.c_str(),
			       g_acqReferenceName.c_str());
			g_failures++;
		}
		// Deliberately NOT released: the top-level preset is now permanently consumed, so every
		// acquisition from here on must descend into SYNTHS/NESTED to find anything at all.
		g_acquireStep = AcquireStep::Nested;
		break;
	}
	case AcquireStep::Nested: {
		// The top level is now empty (Reference released, but Converted kept, the one preset there
		// ever was), so findAnUnlaunchedPresetIncludingWithinSubfolders MUST reach its subfolder loop
		// (load_instrument_preset_ui.cpp:1317) to find anything -- the recursive descent this whole
		// stage exists to cover.
		dispatchAcquisitionGesture();
		g_acqLoads++;
		g_acquireStep = AcquireStep::NestedCheck;
		break;
	}
	case AcquireStep::NestedCheck: {
		// Asserting the exact nested name is what proves the walk actually descended, rather than
		// merely that some acquisition happened to succeed.
		const std::string name = currentAcquiredName();
		printf("SP-ACQUIRE nested name=%s\n", name.c_str());
		if (name != kAcqNestedFirstName) {
			printf("SP-ACQUIRE FAIL acquisition did not descend into the subfolder: got %s, expected %s\n",
			       name.c_str(), kAcqNestedFirstName);
			g_failures++;
		}
		g_acquireStep = AcquireStep::Coalesce;
		break;
	}
	case AcquireStep::Coalesce: {
		// Three gestures with no owner tick between them: presetCoalescer_ (a LatestWins) is
		// single-flight, so the second and third `request()`s land while the first op is still
		// in-flight and only update its latest-wins slot rather than dispatching a NEW op --
		// coalescing them onto (at most) one actual run. "loads" is read from the production
		// counter (LoadInstrumentPresetUI::presetAcquisitionsRun()) on CoalesceCheck, one tick after
		// this dispatches all three -- reading it here, in the same tick, would always see a delta of
		// 0 (none of the three has run yet regardless of whether coalescing happened), which would
		// make this check pass vacuously rather than for the reason it names.
		g_acqCoalesceBefore = acqDispatches();
		constexpr int32_t kCoalesceGestures = 3;
		for (int32_t i = 0; i < kCoalesceGestures; ++i) {
			dispatchAcquisitionGesture();
			g_acqLoads++;
		}
		g_acquireStep = AcquireStep::CoalesceCheck;
		break;
	}
	case AcquireStep::CoalesceCheck: {
		constexpr int32_t kCoalesceGestures = 3;
		const int32_t loads = acqDispatches() - g_acqCoalesceBefore;
		printf("SP-ACQUIRE coalesce gestures=%d loads=%d\n", kCoalesceGestures, loads);
		// Both bounds matter: an upper bound alone would let `loads=0` (nothing ran at all -- the
		// dispatch mechanism silently broken) pass as "fewer than 3", same vacuous-pass shape as the
		// NonAudio bug below. The lower bound requires that SOMETHING actually completed.
		if (loads < 1 || loads >= kCoalesceGestures) {
			printf("SP-ACQUIRE FAIL coalescing produced %d load(s) for %d gesture(s) -- expected 1..%d\n", loads,
			       kCoalesceGestures, kCoalesceGestures - 1);
			g_failures++;
		}
		g_acquireStep = AcquireStep::NonAudio;
		break;
	}
	case AcquireStep::NonAudio: {
		// The MIDI/CV fast path (spec section 2c): changeOutputType() must call
		// completeChangeOutputType() INLINE for CV/MIDI rather than dispatching, so no card is read
		// and no storage op runs at all. Drives the REAL ArrangerView gesture on the row Coalesce
		// just left a SYNTH Output on -- not Song::getNonAudioInstrumentToSwitchTo() directly, which
		// changeOutputType() never calls, and so would prove nothing about this split.
		//
		// The Output ending up CV is readable in this same tick (changeOutputType()'s synchronous arm
		// really does run to completion inline), but presetAcquisitionsRun() is not: if
		// changeOutputType() ever regressed to dispatching on this arm, the op would only be
		// ENQUEUED by the time this tick's read ran, and the delta would still read 0 -- passing for
		// the wrong reason, the same same-tick trap the rest of this stage was fixed for. So the
		// counter is read one tick later, in NonAudioCheck, like every other dispatched gesture here.
		g_acqNonAudioBefore = acqDispatches();
		BrowserListingAbProbe::changeOutputType(OutputType::CV);
		g_acquireStep = AcquireStep::NonAudioCheck;
		break;
	}
	case AcquireStep::NonAudioCheck: {
		const int32_t dispatched = acqDispatches() - g_acqNonAudioBefore;
		printf("SP-ACQUIRE nonaudio dispatches=%d\n", dispatched);
		if (dispatched != 0) {
			printf("SP-ACQUIRE FAIL a CV switch dispatched a storage op; it reads no card\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::ViewNavigate;
		break;
	}
	case AcquireStep::ViewNavigate: {
		// View's Synth/Kit preset-navigation dispatch (rung 5, task 4), driven on the boot song's
		// session InstrumentClip -- see g_viewClip's doc above for why this can't run through any
		// arranger row. Reuses the SYNTHS fixture the steps above already built; doPresetNavigation()
		// reads only the top-level folder (it does not recurse into NESTED), so nothing new needs
		// building. ViewNavigateCheck asserts a REAL outcome (the clip's Output actually changed),
		// not just that an op ran -- a dispatch-count-only check would pass identically with
		// PresetConsumer::ViewNavigateClipPreset's switch arm still an empty `break;` (see the rung 5
		// task 4 review).
		g_viewClip = getCurrentInstrumentClip();
		if (g_viewClip == nullptr || g_viewClip->output == nullptr) {
			printf("SP-ACQUIRE FAIL no current InstrumentClip to drive View's gestures with\n");
			g_failures++;
			g_acquireStep = AcquireStep::Finished;
			break;
		}
		g_acqViewNameBefore = acquiredName(g_viewClip->output);
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::navigateThroughPresetsForInstrumentClip(g_viewClip, 1);
		g_acquireStep = AcquireStep::ViewNavigateCheck;
		break;
	}
	case AcquireStep::ViewNavigateCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const std::string nameAfter = acquiredName(g_viewClip->output);
		printf("SP-ACQUIRE view-navigate dispatches=%d before=%s after=%s\n", dispatched, g_acqViewNameBefore.c_str(),
		       nameAfter.c_str());
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL View::navigateThroughPresetsForInstrumentClip's Synth/Kit arm ran "
			       "%d op(s), expected 1\n",
			       dispatched);
			g_failures++;
		}
		// The real outcome: the boot clip's Output ("0") does not match anything in the fixture, so a
		// working ViewNavigateClipPreset arm MUST land on the top-level PRESET00 -- proving
		// completeNavigateThroughPresetsForInstrumentClip() actually ran the swap, not merely that the
		// op executed.
		if (nameAfter == g_acqViewNameBefore) {
			printf("SP-ACQUIRE FAIL navigate did not change the clip's Output -- "
			       "ViewNavigateClipPreset's switch arm may not be wired\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::ViewChangeTypeCV;
		break;
	}
	case AcquireStep::ViewChangeTypeCV: {
		// The MIDI/CV fast path (spec section 2c), for View this time: changeOutputType() must call
		// completeChangeOutputType() INLINE for CV/MIDI, so the bool it returns is a real synchronous
		// answer (its callers use it to decide whether to beep) and no card is read. Readable in the
		// SAME tick, unlike every dispatched gesture above -- see AcquireStep::NonAudio's doc for why
		// that's legitimate only for the synchronous arm.
		g_acqViewBefore = acqDispatches();
		const bool ok = BrowserListingAbProbe::changeOutputType(g_viewClip, OutputType::CV);
		printf("SP-ACQUIRE view-changetype-cv ok=%d type=%d\n", ok ? 1 : 0, static_cast<int>(g_viewClip->output->type));
		if (!ok || g_viewClip->output->type != OutputType::CV) {
			printf("SP-ACQUIRE FAIL View::changeOutputType(CV) did not complete synchronously\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::ViewChangeTypeCVCheck;
		break;
	}
	case AcquireStep::ViewChangeTypeCVCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		printf("SP-ACQUIRE view-changetype-cv dispatches=%d\n", dispatched);
		if (dispatched != 0) {
			printf("SP-ACQUIRE FAIL View::changeOutputType(CV) dispatched a storage op; it reads no card\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::ViewChangeTypeSynth;
		break;
	}
	case AcquireStep::ViewChangeTypeSynth: {
		// The Synth/Kit dispatch arm again, this time via changeOutputType() (CV -> SYNTH) rather than
		// navigate -- proves PresetConsumer::ViewChangeType's switch arm (load_instrument_preset_ui.cpp)
		// is actually WIRED to View::completeChangeOutputTypeFor(), asserted by the REAL outcome below
		// (the clip really becomes SYNTH), not merely a nonzero dispatch count -- a dispatch-count-only
		// check would pass identically with the switch arm still an empty `break;`.
		//
		// Targets SYNTH, not KIT: SYNTH reuses the SYNTHS fixture the steps above already proved
		// correct (a real, loadable preset written via the production save path). A KIT preset needs
		// its own from-scratch fixture writer (Kit's XML root tag and blank-instrument setup differ
		// from SoundInstrument's, and writeBlankSynthPresetFile()'s doc above already records ONE
		// real crash this stage found from getting that shape subtly wrong for Synth) -- building and
		// proving a second one correct was judged higher-risk, for the same proof, than this fix round
		// had room for. SYNTH and KIT share the exact same code path here (View::changeOutputType()
		// branches only on MIDI/CV vs everything else), so this is not a narrower proof, just a safer
		// one. Recorded here rather than silently substituted.
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::changeOutputType(g_viewClip, OutputType::SYNTH);
		g_acquireStep = AcquireStep::ViewChangeTypeSynthCheck;
		break;
	}
	case AcquireStep::ViewChangeTypeSynthCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const OutputType typeAfter = g_viewClip->output->type;
		printf("SP-ACQUIRE view-changetype-synth dispatches=%d type=%d\n", dispatched, static_cast<int>(typeAfter));
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL View::changeOutputType(SYNTH)'s Synth/Kit arm ran %d op(s), expected 1\n",
			       dispatched);
			g_failures++;
		}
		if (typeAfter != OutputType::SYNTH) {
			printf("SP-ACQUIRE FAIL clip did not actually become SYNTH -- "
			       "ViewChangeType's switch arm may not be wired\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::SessionChangeTypeCV;
		break;
	}
	case AcquireStep::SessionChangeTypeCV: {
		// The MIDI/CV fast path (spec section 2c) for SessionView's Grid output-type button: it must
		// call completeChangeOutputType() INLINE, reading no card. Readable in the SAME tick precisely
		// because it is the synchronous arm -- see AcquireStep::NonAudio's doc.
		if (!BrowserListingAbProbe::aimGridPressAt(g_viewClip)) {
			printf("SP-ACQUIRE FAIL grid coordinates do not resolve back to the session clip\n");
			g_failures++;
			g_acquireStep = AcquireStep::Finished;
			break;
		}
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::sessionButtonChangeOutputType(deluge::hid::button::CV);
		printf("SP-ACQUIRE session-changetype-cv type=%d\n", static_cast<int>(g_viewClip->output->type));
		if (g_viewClip->output->type != OutputType::CV) {
			printf("SP-ACQUIRE FAIL SessionView's Grid changeOutputType(CV) did not complete synchronously\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::SessionChangeTypeCVCheck;
		break;
	}
	case AcquireStep::SessionChangeTypeCVCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		printf("SP-ACQUIRE session-changetype-cv dispatches=%d\n", dispatched);
		if (dispatched != 0) {
			printf("SP-ACQUIRE FAIL a CV switch dispatched a storage op; it reads no card\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::SessionChangeTypeSynth;
		break;
	}
	case AcquireStep::SessionChangeTypeSynth: {
		// CV -> SYNTH, so the type really does change and an unwired SessionChangeType arm leaves the
		// clip on CV. SYNTH rather than KIT for the same reason AcquireStep::ViewChangeTypeSynth gives:
		// the two share this code path exactly, and only SYNTH has a fixture proven safe to load.
		if (!BrowserListingAbProbe::aimGridPressAt(g_viewClip)) {
			printf("SP-ACQUIRE FAIL grid coordinates do not resolve back to the session clip\n");
			g_failures++;
			g_acquireStep = AcquireStep::Finished;
			break;
		}
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::sessionButtonChangeOutputType(deluge::hid::button::SYNTH);
		g_acquireStep = AcquireStep::SessionChangeTypeSynthCheck;
		break;
	}
	case AcquireStep::SessionChangeTypeSynthCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const OutputType typeAfter = g_viewClip->output->type;
		printf("SP-ACQUIRE session-changetype-synth dispatches=%d type=%d\n", dispatched, static_cast<int>(typeAfter));
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL SessionView's Grid changeOutputType(SYNTH) ran %d op(s), expected 1\n", dispatched);
			g_failures++;
		}
		if (typeAfter != OutputType::SYNTH) {
			printf("SP-ACQUIRE FAIL clip did not actually become SYNTH -- "
			       "SessionChangeType's switch arm may not be wired\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::SessionNewTrack;
		break;
	}
	case AcquireStep::SessionNewTrack: {
		// The empty-pad "create a clip here" gesture, in Rows layout, driven through the REAL
		// padAction() -- the riskiest conversion in this task, since createNewTrackForInstrumentClip()'s
		// bool answer feeds three different abandon paths. Those stayed synchronous; what moved is the
		// gesture ABOVE them, so this drives the gesture.
		g_acqNewTrackClipsBefore = std::ssize(currentSong->sessionClips);
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::pressEmptyPad(kAcqNewTrackX, kAcqNewTrackY);
		// Nothing may have been created yet: the op is only ENQUEUED here.
		if (std::ssize(currentSong->sessionClips) != g_acqNewTrackClipsBefore) {
			printf("SP-ACQUIRE FAIL the empty-pad gesture created a clip synchronously; it reads the card\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::SessionNewTrackCheck;
		break;
	}
	case AcquireStep::SessionNewTrackCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const int32_t added = static_cast<int32_t>(std::ssize(currentSong->sessionClips)) - g_acqNewTrackClipsBefore;
		printf("SP-ACQUIRE session-newtrack dispatches=%d clipsAdded=%d uiMode=%d\n", dispatched, added,
		       static_cast<int>(currentUIMode));
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL the empty-pad gesture ran %d op(s), expected 1\n", dispatched);
			g_failures++;
		}
		// The real outcome: a clip that did not exist before now does, with an Instrument loaded off
		// the card. An unwired SessionNewTrack arm dispatches and counts identically but adds nothing.
		if (added != 1) {
			printf("SP-ACQUIRE FAIL the empty-pad gesture added %d session clip(s), expected 1 -- "
			       "SessionNewTrack's switch arm may not be wired\n",
			       added);
			g_failures++;
		}
		else {
			Clip* created = currentSong->sessionClips[std::ssize(currentSong->sessionClips) - 1];
			g_acqNewTrackClip = created;
			const std::string name = acquiredName(created->output);
			printf("SP-ACQUIRE session-newtrack name=%s\n", name.c_str());
			if (created->output == nullptr || created->output->type != OutputType::SYNTH || name.empty()) {
				printf("SP-ACQUIRE FAIL the created clip has no SYNTH Instrument from the fixture\n");
				g_failures++;
			}
		}
		// The gesture's own tail, which used to run through `goto startHoldingDown` on the caller's
		// stack and now runs from completeNewTrack() a tick later. The probe forced UI_MODE_NONE before
		// the press, and newTrackPressHeld_ was never cleared (no release was injected), so the held
		// mode being set proves beginHoldingClipPress() actually ran after the acquisition.
		if (currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
			printf("SP-ACQUIRE FAIL the created clip was not left held -- completeNewTrack()'s tail did not run\n");
			g_failures++;
		}
		if (BrowserListingAbProbe::newTrackPressHeld()) {
			printf("SP-ACQUIRE FAIL the new-track held marker outlived its gesture\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::SessionNavigate;
		break;
	}
	case AcquireStep::SessionNavigate: {
		// SessionView's Grid-layout preset-navigation dispatch. The Rows arm delegates to View (already
		// covered by AcquireStep::ViewNavigate); this is the separate Grid arm this task converted.
		//
		// Driven on the clip AcquireStep::SessionNewTrack just created, not the boot clip, and sequenced
		// after it on purpose: doPresetNavigation() walks ONE folder -- oldInstrument->dirPath -- and
		// never descends. Everything reachable from the top level is a single preset by construction
		// (see kAcqTopLevelCount), so navigating from an instrument that lives THERE wraps straight back
		// onto itself and the outcome assertion below could not tell a wired arm from an unwired one.
		// The freshly created clip's Instrument came from SYNTHS/NESTED, which holds many, so a working
		// arm MUST land on a different preset.
		if (g_acqNewTrackClip == nullptr) {
			printf("SP-ACQUIRE FAIL no created clip to drive the Grid navigate gesture with\n");
			g_failures++;
			g_acquireStep = AcquireStep::Finished;
			break;
		}
		if (!BrowserListingAbProbe::aimGridPressAt(g_acqNewTrackClip)) {
			printf("SP-ACQUIRE FAIL grid coordinates do not resolve back to the created clip\n");
			g_failures++;
			g_acquireStep = AcquireStep::Finished;
			break;
		}
		g_acqViewNameBefore = acquiredName(g_acqNewTrackClip->output);
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::commandChangeClipPreset(1);
		g_acquireStep = AcquireStep::SessionNavigateCheck;
		break;
	}
	case AcquireStep::SessionNavigateCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const std::string nameAfter = acquiredName(g_acqNewTrackClip->output);
		printf("SP-ACQUIRE session-navigate dispatches=%d before=%s after=%s\n", dispatched,
		       g_acqViewNameBefore.c_str(), nameAfter.c_str());
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL SessionView's Grid navigate arm ran %d op(s), expected 1\n", dispatched);
			g_failures++;
		}
		// The real outcome, not the dispatch count: presetAcquisitionsRun_ increments at the TOP of
		// runPresetAcquisitionOp, before the follow-up switch, so a count-only check passes identically
		// with SessionNavigate's arm still an empty `break;`.
		if (nameAfter == g_acqViewNameBefore) {
			printf("SP-ACQUIRE FAIL navigate did not change the clip's Output -- "
			       "SessionNavigate's switch arm may not be wired\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::SessionNewTrackEnter;
		break;
	}
	case AcquireStep::SessionNewTrackEnter: {
		// The grid new-track gesture's ENTER continuation, the one consumer whose OUTCOME the deferral
		// silently changed: gridNewTrackClipEntered() used to compare the press coordinates against the
		// live gridFirstPressedX/Y, and the caller resets those (clipPressEnded()) the moment
		// exitTrackCreation() returns -- so once the acquisition was deferred, every ENTER gesture read
		// "pad released" and transitioned INTO the clip instead of holding it. The check below asserts
		// the held outcome the synchronous version produced.
		g_acqNewTrackClipsBefore = std::ssize(currentSong->sessionClips);
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::beginGridNewTrackEnter(kAcqEnterX, kAcqEnterY);
		if (std::ssize(currentSong->sessionClips) != g_acqNewTrackClipsBefore) {
			printf("SP-ACQUIRE FAIL the grid ENTER gesture created a clip synchronously; it reads the card\n");
			g_failures++;
		}
		// The caller's tail, which is what resets the coordinates the completion must NOT re-read.
		BrowserListingAbProbe::endGridPress();
		g_acquireStep = AcquireStep::SessionNewTrackEnterCheck;
		break;
	}
	case AcquireStep::SessionNewTrackEnterCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const int32_t added = static_cast<int32_t>(std::ssize(currentSong->sessionClips)) - g_acqNewTrackClipsBefore;
		printf("SP-ACQUIRE session-newtrack-enter dispatches=%d clipsAdded=%d uiMode=%d\n", dispatched, added,
		       static_cast<int>(currentUIMode));
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL the grid ENTER gesture ran %d op(s), expected 1\n", dispatched);
			g_failures++;
		}
		if (added != 1) {
			printf("SP-ACQUIRE FAIL the grid ENTER gesture added %d session clip(s), expected 1 -- "
			       "SessionNewTrackEnter's switch arm may not be wired\n",
			       added);
			g_failures++;
		}
		// The outcome the snapshot exists to preserve: still holding the clip, NOT transitioned into
		// it. A completion that re-read gridFirstPressedX/Y would have taken the released branch and
		// left currentUIMode alone.
		if (currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
			printf("SP-ACQUIRE FAIL the grid ENTER gesture did not leave the new clip held -- "
			       "gridNewTrackClipEntered() read the reset press coordinates instead of the snapshot\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::ViewNewInstrument;
		break;
	}
	case AcquireStep::ViewNewInstrument: {
		// The shift/FM new-instrument gesture: a DIFFERENT card read from every step above (Browser::
		// getUnusedSlot(), not one of the three LoadInstrumentPresetUI readers -- see the rung 5 task 6
		// fix round). Deliberately LAST: it creates a genuinely new "SYNT<N>"-named Instrument and adds
		// it to the live Song (createNewInstrument() -> currentSong->addOutput()), which earlier steps'
		// findAnUnlaunchedPresetIncludingWithinSubfolders()-backed scans would otherwise pick up as an
		// in-memory candidate (readFileItemsFromFolderAndMemory() merges card files with live Outputs)
		// and land on instead of the fixture file they expect -- found by running this step earlier and
		// watching AcquireStep::SessionNavigate fail downstream.
		//
		// Reads getCurrentInstrumentClip() fresh, not g_viewClip: SessionNewTrackEnter above holds a
		// DIFFERENT clip (beginHoldingClipPress() -> currentSong->setCurrentClip()), and this step
		// means to drive the real gesture on whichever clip is actually current, exactly as
		// createNewInstrument() itself does -- not to reach past it back to the boot clip. That clip is
		// SYNTH (created via SessionNewTrackEnter's own acquisition), so the clip-not-empty guard
		// (KIT-only) does not apply and this reads the card unconditionally. ViewNewInstrumentCheck
		// asserts a REAL outcome (the clip's Output actually became a NEW, unused-slot-named
		// instrument), not just that an op ran -- same standard as ViewNavigateCheck above.
		InstrumentClip* clip = getCurrentInstrumentClip();
		g_acqViewNameBefore = acquiredName(clip->output);
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::createNewInstrument(OutputType::SYNTH, false);
		g_acquireStep = AcquireStep::ViewNewInstrumentCheck;
		break;
	}
	case AcquireStep::ViewNewInstrumentCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const std::string nameAfter = acquiredName(getCurrentInstrumentClip()->output);
		printf("SP-ACQUIRE view-newinstrument dispatches=%d before=%s after=%s\n", dispatched,
		       g_acqViewNameBefore.c_str(), nameAfter.c_str());
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL InstrumentClipView::createNewInstrument()'s dispatch ran %d op(s), "
			       "expected 1\n",
			       dispatched);
			g_failures++;
		}
		// The real outcome: getUnusedSlot() names the new instrument from the "SYNT:"-prefixed slot
		// search (browser.cpp), which nothing in this fixture's SYNTHS folder collides with, so a
		// working ViewNewInstrument arm MUST land on a freshly minted "SYNT"-prefixed name -- proving
		// InstrumentClipMinder::createNewInstrument() actually ran the card read AND
		// InstrumentClipView::completeNewInstrument() actually ran the swap, not merely that the op
		// executed (a dispatch-count-only check would pass identically with the switch arm still an
		// empty `break;`).
		if (nameAfter == g_acqViewNameBefore || !nameAfter.starts_with("SYNT")) {
			printf("SP-ACQUIRE FAIL createNewInstrument did not create a new, unused-slot-named "
			       "instrument -- ViewNewInstrument's switch arm may not be wired, or "
			       "Browser::getUnusedSlot() never ran\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::ArrangerAuditionPad;
		break;
	}
	case AcquireStep::ArrangerAuditionPad: {
		// The arranger's new-instrument PAD PRESS, driven through ArrangerView::handleAuditionPadAction()
		// -- the handler MatrixDriver::padAction() reaches -- rather than through
		// beginNewInstrumentAcquisition(), which every step above uses. Those steps cover the card read;
		// this covers what the press does with the result, and that is where the deferral actually broke
		// something: the handler used to read outputsOnScreen back immediately after the call and fall
		// through to the audition sub-mode, the output name, the note and the mod-controllable. Once the
		// acquisition became a dispatch, that read-back is null on every deferring backing and the entire
		// tail stopped running -- invisibly, because the synchronous backing still ran it inline.
		//
		// currentUIMode is forced to NONE for the same reason the View probes do it: earlier steps in
		// this stage leave it wherever their own gestures left it (SessionNewTrackEnter leaves a clip
		// held), and auditionPadAction() only takes the new-press branch from UI_MODE_NONE.
		currentUIMode = UI_MODE_NONE;
		g_acqAuditionRow = BrowserListingAbProbe::firstEmptyArrangerRow();
		if (g_acqAuditionRow < 0) {
			printf("SP-ACQUIRE FAIL no empty arranger row left to press -- the new-instrument branch is "
			       "unreachable, so this step would pass without covering it\n");
			g_failures++;
			g_acquireStep = AcquireStep::Finished;
			break;
		}
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::pressArrangerAuditionPad(g_acqAuditionRow, /*on=*/true);
		// The press must NOT have entered the audition mode yet: on this backing the Output does not
		// exist, so anything the handler did with it here would be acting on a row it has not filled.
		if (currentUIMode != UI_MODE_NONE) {
			printf("SP-ACQUIRE FAIL the audition pad entered mode %d synchronously; the Instrument it "
			       "auditions has not been acquired yet\n",
			       static_cast<int>(currentUIMode));
			g_failures++;
		}
		g_acquireStep = AcquireStep::ArrangerAuditionPadCheck;
		break;
	}
	case AcquireStep::ArrangerAuditionPadCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const std::string name = currentAcquiredName();
		printf("SP-ACQUIRE arranger-audition-pad row=%d dispatches=%d name=%s uiMode=%d\n", g_acqAuditionRow,
		       dispatched, name.c_str(), static_cast<int>(currentUIMode));
		if (dispatched != 1) {
			printf("SP-ACQUIRE FAIL the audition pad press ran %d op(s), expected 1\n", dispatched);
			g_failures++;
		}
		if (name.empty()) {
			printf("SP-ACQUIRE FAIL the audition pad press left the row empty -- no Instrument was placed\n");
			g_failures++;
		}
		// The finding this step exists for. completeNewInstrument() used to restore only the row and its
		// repaint, so the press ended with the arranger in UI_MODE_NONE and no note sounding -- the whole
		// point of holding an audition pad. A row-only check would pass either way.
		if (currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
			printf("SP-ACQUIRE FAIL the audition pad press did not enter the audition mode -- "
			       "completeNewInstrument() is missing the rest of the press's tail\n");
			g_failures++;
		}
		// Release it, and require the release to land: exitSubModeWithoutAction() only fires when the
		// released row matches yPressedActual, so this also proves the completion left that member
		// pointing at the pad that was actually pressed rather than at whatever an earlier gesture left
		// there. Without it the stage would also walk away leaving a note sounding.
		BrowserListingAbProbe::pressArrangerAuditionPad(g_acqAuditionRow, /*on=*/false);
		if (currentUIMode != UI_MODE_NONE) {
			printf("SP-ACQUIRE FAIL releasing the audition pad left mode %d -- the completion's "
			       "yPressedActual does not match the pad that was pressed\n",
			       static_cast<int>(currentUIMode));
			g_failures++;
		}
		g_acquireStep = AcquireStep::ArrangerAuditionRelease;
		break;
	}
	case AcquireStep::ArrangerAuditionRelease: {
		// The other half of the same guard: the finger comes UP while the acquisition is still in
		// flight. This is the window the deferral opened and nothing else closes -- the press never
		// entered UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION, so exitSubModeWithoutAction() finds nothing
		// to exit and the release leaves no trace at all. A completion that started a note anyway would
		// start one nobody can ever end.
		//
		// Both edges are delivered in THIS tick, before the op runs: the dispatched op only executes
		// during the queue drain after this function returns (see the doc above AcquireStep), which is
		// exactly the ordering a real fast tap produces.
		currentUIMode = UI_MODE_NONE;
		g_acqAuditionRow = BrowserListingAbProbe::firstEmptyArrangerRow();
		if (g_acqAuditionRow < 0) {
			printf("SP-ACQUIRE FAIL no empty arranger row left for the release-during-flight press\n");
			g_failures++;
			g_acquireStep = AcquireStep::Finished;
			break;
		}
		g_acqViewBefore = acqDispatches();
		BrowserListingAbProbe::pressArrangerAuditionPad(g_acqAuditionRow, /*on=*/true);
		BrowserListingAbProbe::pressArrangerAuditionPad(g_acqAuditionRow, /*on=*/false);
		g_acquireStep = AcquireStep::ArrangerAuditionReleaseCheck;
		break;
	}
	case AcquireStep::ArrangerAuditionReleaseCheck: {
		const int32_t dispatched = acqDispatches() - g_acqViewBefore;
		const std::string name = currentAcquiredName();
		printf("SP-ACQUIRE arranger-audition-release row=%d dispatches=%d name=%s uiMode=%d\n", g_acqAuditionRow,
		       dispatched, name.c_str(), static_cast<int>(currentUIMode));
		// The acquisition still has to happen -- the row is filled either way, which is what the
		// synchronous version did too. Only the audition is conditional.
		if (dispatched != 1 || name.empty()) {
			printf("SP-ACQUIRE FAIL a released press still has to acquire: ran %d op(s), row name=%s\n", dispatched,
			       name.c_str());
			g_failures++;
		}
		if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
			printf("SP-ACQUIRE FAIL the completion auditioned a pad that was already released -- "
			       "that note has nothing left to end it\n");
			g_failures++;
			// Do not leave it sounding for the rest of the process.
			currentUIMode = UI_MODE_NONE;
		}
		g_acquireStep = AcquireStep::Finished;
		break;
	}
	case AcquireStep::Finished:
		break;
	}
}

// ── Stage: listing by path ──────────────────────────────────────────────────
//
// A ByPath listing must answer "is this file still there?" -- the question the blocking StorageManager::fileExists
// pre-flight used to answer before every favourite open. The absent case is the regression that pre-flight was hiding:
// the old completion tail tested `fileIndexSelected > fileItems.size()`, but `searchFileItems` returns an INSERTION
// POINT, always <= size, so a deleted file fell straight through and the browser opened whatever sorted into its place.
//
// Through a load browser that is really on the UI stack, because whether a failed listing leaves it open is part of
// what is checked. After the acquire stage, which runs with no UI stack at all, so nothing it relies on changes.

/// A song the fixture made, the name opening it by path must select, and a song it never made.
constexpr const char* kByPathPresent = "LISTTEST/SONG005.XML";
constexpr const char* kByPathPresentName = "SONG005";
constexpr const char* kByPathAbsent = "LISTTEST/SONG999.XML";

/// How long the stall cases hold every directory open, in milliseconds of the harness's virtual clock: far past the
/// browser's 600-tick bound, since each tick sleeps at least a millisecond.
constexpr uint32_t kStallHoldMs = 30000;
/// Ticks a stall case waits for the browser to give up: well above the bound, well below the hold.
constexpr int32_t kMaxStallTicks = 2000;

enum class ByPathStep : uint8_t {
	Open,
	OpenWait,
	Present,
	PresentWait,
	Absent,
	AbsentWait,
	StallInside,
	StallInsideWait,
	StallEntry,
	StallEntryWait,
	Finished,
};
ByPathStep g_byPathStep = ByPathStep::Open;
int32_t g_byPathTicks = 0;
int32_t g_listingFailuresBefore = 0;
/// What the load browser showed before a listing that is expected to fail, to check that it is put back.
ListingSnapshot g_byPathBefore;

/// Whether two snapshots show the same listing: directory, entered text, selection and names.
bool sameListing(const ListingSnapshot& a, const ListingSnapshot& b) {
	return a.dir == b.dir && a.entered == b.entered && a.selected == b.selected && a.names == b.names;
}

/// End the stage, releasing the hold.
void finishByPathStage() {
	deluge_file_directory_set_latency_ms(0);
	g_byPathStep = ByPathStep::Finished;
}

/// Tick the load browser while its listing runs. @return Whether the listing has finished; reports a wedge itself.
bool byPathListingSettled(const char* label) {
	if (!BrowserListingAbProbe::inProgress()) {
		return true;
	}
	BrowserListingAbProbe::tick();
	if (!BrowserListingAbProbe::inProgress()) {
		return true;
	}
	if (++g_byPathTicks > kMaxRunTicks) {
		printf("SP-BYPATH FAIL %s never finished -- nothing re-asked\n", label);
		g_failures++;
		finishByPathStage();
	}
	return false;
}

void runByPathTick() {
	switch (g_byPathStep) {
	case ByPathStep::Open:
		// openUI() needs a root UI beneath it, and this harness boots without one.
		changeRootUI(&arrangerView);
		deluge_file_directory_set_latency_ms(kDirectoryHoldMs);
		g_byPathTicks = 0;
		if (!openUI(&loadSongUI)) {
			printf("SP-BYPATH FAIL the load browser did not open\n");
			g_failures++;
			finishByPathStage();
			return;
		}
		g_byPathStep = ByPathStep::OpenWait;
		return;

	case ByPathStep::OpenWait:
		// Its own listing first: a second listing begun over one still in flight would take over its machine.
		if (!byPathListingSettled("the load browser's own listing")) {
			return;
		}
		if (g_byPathStep == ByPathStep::Finished) {
			return;
		}
		if (getCurrentUI() != &loadSongUI) {
			printf("SP-BYPATH FAIL the load browser's own listing failed (error %d) and closed it\n",
			       static_cast<int>(Browser::lastListingFailure()));
			g_failures++;
			finishByPathStage();
			return;
		}
		g_byPathStep = ByPathStep::Present;
		return;

	case ByPathStep::Present:
		markCounters();
		g_byPathTicks = 0;
		(void)loadSongUI.setFileByFullPath(OutputType::NONE, kByPathPresent);
		g_byPathStep = ByPathStep::PresentWait;
		return;

	case ByPathStep::PresentWait: {
		if (!byPathListingSettled("the present file's listing") || g_byPathStep == ByPathStep::Finished) {
			return;
		}
		const bool exact = BrowserListingAbProbe::foundExactMatch();
		const bool open = getCurrentUI() == &loadSongUI;
		printf("SP-BYPATH present exact=%d entered=%s open=%d opens=%d requests=%d\n", exact ? 1 : 0,
		       Browser::enteredText.c_str(), open ? 1 : 0, runOpens(), runRequests());
		checkResumed("bypath(present)");
		if (!exact || Browser::enteredText != kByPathPresentName || !open) {
			printf("SP-BYPATH FAIL a file that exists did not open by path: exact=%d entered=%s open=%d\n",
			       exact ? 1 : 0, Browser::enteredText.c_str(), open ? 1 : 0);
			g_failures++;
		}
		if (!open) {
			finishByPathStage();
			return;
		}
		g_byPathStep = ByPathStep::Absent;
		return;
	}

	case ByPathStep::Absent:
		markCounters();
		g_byPathTicks = 0;
		g_listingFailuresBefore = Browser::listingFailures();
		g_byPathBefore = takeListingSnapshot();
		(void)loadSongUI.setFileByFullPath(OutputType::NONE, kByPathAbsent);
		g_byPathStep = ByPathStep::AbsentWait;
		return;

	case ByPathStep::AbsentWait: {
		if (!byPathListingSettled("the absent file's listing") || g_byPathStep == ByPathStep::Finished) {
			return;
		}
		const int32_t failed = Browser::listingFailures() - g_listingFailuresBefore;
		const Error error = Browser::lastListingFailure();
		const bool open = getCurrentUI() == &loadSongUI;
		const bool restored = sameListing(takeListingSnapshot(), g_byPathBefore);
		printf("SP-BYPATH absent failed=%d error=%d open=%d restored=%d opens=%d requests=%d\n", failed,
		       static_cast<int>(error), open ? 1 : 0, restored ? 1 : 0, runOpens(), runRequests());
		checkResumed("bypath(absent)");
		if (failed != 1 || error != Error::FILE_NOT_FOUND || !open || !restored) {
			printf(
			    "SP-BYPATH FAIL a file that does not exist was not refused in place: failed=%d error=%d open=%d "
			    "restored=%d, wanted one FILE_NOT_FOUND with the browser still open on the listing it had -- it must "
			    "neither open whatever sorted into the missing file's place nor throw the user out of the browser\n",
			    failed, static_cast<int>(error), open ? 1 : 0, restored ? 1 : 0);
			g_failures++;
		}
		if (!open) {
			finishByPathStage();
			return;
		}
		g_byPathStep = ByPathStep::StallInside;
		return;
	}

	case ByPathStep::StallInside:
		// SP-LISTING-STALL, inside. A favourite opened from inside the browser, with every directory open held far past
		// the browser's bound: the listing must give up with the card error and leave the browser open on what it had.
		deluge_file_directory_set_latency_ms(kStallHoldMs);
		g_byPathTicks = 0;
		g_listingFailuresBefore = Browser::listingFailures();
		g_byPathBefore = takeListingSnapshot();
		(void)loadSongUI.setFileByFullPath(OutputType::NONE, kByPathPresent);
		g_byPathStep = ByPathStep::StallInsideWait;
		return;

	case ByPathStep::StallInsideWait: {
		if (BrowserListingAbProbe::inProgress()) {
			BrowserListingAbProbe::tick();
			if (BrowserListingAbProbe::inProgress()) {
				if (++g_byPathTicks > kMaxStallTicks) {
					printf("SP-LISTING-STALL FAIL inside: still waiting after %d ticks -- it never gave up\n",
					       g_byPathTicks);
					g_failures++;
					finishByPathStage();
				}
				return;
			}
		}
		const int32_t failed = Browser::listingFailures() - g_listingFailuresBefore;
		const Error error = Browser::lastListingFailure();
		const bool open = getCurrentUI() == &loadSongUI;
		const bool restored = sameListing(takeListingSnapshot(), g_byPathBefore);
		printf("SP-LISTING-STALL inside failed=%d error=%d open=%d restored=%d ticks=%d\n", failed,
		       static_cast<int>(error), open ? 1 : 0, restored ? 1 : 0, g_byPathTicks);
		if (failed != 1 || error != Error::SD_CARD || !open || !restored) {
			printf("SP-LISTING-STALL FAIL inside: failed=%d error=%d open=%d restored=%d, wanted one SD_CARD with the "
			       "browser still open on the listing it had\n",
			       failed, static_cast<int>(error), open ? 1 : 0, restored ? 1 : 0);
			g_failures++;
		}
		g_byPathStep = ByPathStep::StallEntry;
		return;
	}

	case ByPathStep::StallEntry:
		// SP-LISTING-STALL, entry. The browser closed and opened again with the hold still on: the listing that opens
		// it has nothing to fall back to, so it must give up with the card error and close the browser.
		if (getCurrentUI() == &loadSongUI) {
			loadSongUI.close();
		}
		g_byPathTicks = 0;
		g_listingFailuresBefore = Browser::listingFailures();
		if (!openUI(&loadSongUI)) {
			printf("SP-LISTING-STALL FAIL entry: the load browser did not open\n");
			g_failures++;
			finishByPathStage();
			return;
		}
		g_byPathStep = ByPathStep::StallEntryWait;
		return;

	case ByPathStep::StallEntryWait: {
		if (BrowserListingAbProbe::inProgress()) {
			BrowserListingAbProbe::tick();
			if (BrowserListingAbProbe::inProgress()) {
				if (++g_byPathTicks > kMaxStallTicks) {
					printf("SP-LISTING-STALL FAIL entry: still waiting after %d ticks -- it never gave up\n",
					       g_byPathTicks);
					g_failures++;
					finishByPathStage();
				}
				return;
			}
		}
		const int32_t failed = Browser::listingFailures() - g_listingFailuresBefore;
		const Error error = Browser::lastListingFailure();
		const bool closed = getCurrentUI() != &loadSongUI;
		printf("SP-LISTING-STALL entry failed=%d error=%d closed=%d ticks=%d\n", failed, static_cast<int>(error),
		       closed ? 1 : 0, g_byPathTicks);
		if (failed != 1 || error != Error::SD_CARD || !closed) {
			printf("SP-LISTING-STALL FAIL entry: failed=%d error=%d closed=%d, wanted one SD_CARD and a closed "
			       "browser\n",
			       failed, static_cast<int>(error), closed ? 1 : 0);
			g_failures++;
		}
		finishByPathStage();
		return;
	}

	case ByPathStep::Finished:
		return;
	}
}

void printSummary() {
	const bool async = deluge_file_prefix_async_active();
	printf("SP-SUMMARY asyncActive=%d cases=%zu corpus=%zu failures=%d\n", async ? 1 : 0, kCases.size(), g_corpusRan,
	       g_failures);

	printf("SP-DEFER");
	for (size_t i = 0; i < kCases.size(); i++) {
		printf(" %s=%d", kCases[i].label, g_deferralsByCase[i]);
	}
	printf(" %s=%d", kWindowLabel, g_windowDeferrals);
	for (size_t i = 0; i < g_corpusRan; i++) {
		printf(" C%02u=%d", static_cast<unsigned>(i), g_deferralsByCorpusCase[i]);
	}
	printf("\n");

	// The floor the corpus stage did not have. Every song the packer staged has to have RUN -- not
	// "as many as a per-index existence probe happened to find", which is a count that can quietly
	// go to zero and take fourteen real songs with it while the run stays green.
	if (g_corpusRan != g_corpusExpected) {
		printf("FAIL [songPreviewAB]: the packer staged %zu corpus song(s) and only %zu ran\n", g_corpusExpected,
		       g_corpusRan);
		g_failures++;
	}
	if (g_corpusRan > 0 && g_corpusRendered == 0) {
		printf("FAIL [songPreviewAB]: %zu corpus song(s) ran and not one of them rendered artwork -- the corpus "
		       "leg proved nothing\n",
		       g_corpusRan);
		g_failures++;
	}
	printf("SP-ACQUIRE-SUMMARY loads=%d dispatches=%d\n", g_acqLoads, acqDispatches());

	// No synchronous browser directory operation may begin off the storage owner after boot. Every listing above went
	// through the channel; a count here means one of them did not. (In an alpha or beta build the first one freezes
	// the run instead, which fails it just as surely.)
	const int32_t tier = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Browser);
	printf("SP-TIER browser=%d\n", tier);
	if (tier != 0) {
		printf("SP-TIER FAIL %d browser directory operation(s) began off the storage owner after boot\n", tier);
		g_failures++;
	}

	printf("SP-RESULT failures=%d\n", g_failures);
	fflush(stdout);
}

} // namespace

extern "C" {

void deluge_scenario_song_preview_ab_begin(void) {
	g_stage = Stage::Cases;
	g_swapStep = SwapStep::RehearseRequest;
	g_swapWaited = 0;
	g_swapCollectDeferrals = 0;
	g_swapFailed = false;
	g_swapCollectAsked = false;
	g_caseIndex = 0;
	g_corpusIndex = 0;
	g_corpusRan = 0;
	g_corpusRendered = 0;
	g_corpusExpected = 0;
	g_corpusCountDispatched = false;
	g_corpusCountFailed = false;
	g_caseStarted = false;
	g_caseTicks = 0;
	g_caseDeferrals = 0;
	g_windowDeferrals = 0;
	g_failures = 0;
	g_stepDone = true;
	g_summaryPrinted = false;
	g_deferralsByCase.fill(0);
	g_deferralsByCorpusCase.fill(0);
	g_listingStep = ListingStep::Fixture;
	g_listingRunning = false;
	g_listingTicks = 0;
	g_listingRuns = 0;
	g_runRequestsBefore = 0;
	g_runOpensBefore = 0;
	g_slotCallsBefore = 0;
	g_mountNoticesBefore = 0;
	g_itemsWhenRunBegan = 0;
	g_sawBlankWhileSuspended = false;
	g_slotRan = false;
	g_slotName.clear();
	g_slotTicks = 0;
	g_byPathStep = ByPathStep::Open;
	g_byPathTicks = 0;
	g_listingFailuresBefore = 0;
	g_byPathBefore = {};
	g_acquireStep = AcquireStep::Fixture;
	g_acqLoads = 0;
	g_acqReferenceName.clear();
	g_viewClip = nullptr;
	g_acqViewBefore = 0;

	// Line-buffer stdout for the rest of the run. The platform driver ends the process with a raw
	// `exit_group` syscall, which flushes NOTHING of C's stdio -- so a block-buffered stdout, which is
	// what stdout is whenever this harness's output is piped, discards every line this scenario
	// printed. Losing exactly the output the gate diffs.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	printf("SP-BEGIN asyncActive=%d cases=%zu\n", deluge_file_prefix_async_active() ? 1 : 0, kCases.size());
}

bool deluge_scenario_song_preview_ab_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_song_preview_ab_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}

	g_stepDone = false;
	switch (g_stage) {
	case Stage::Cases:
		runCasesTick();
		break;
	case Stage::Window:
		runWindowTick();
		break;
	case Stage::CorpusCount:
		runCorpusCountTick();
		break;
	case Stage::Corpus:
		runCorpusTick();
		break;
	case Stage::Swap:
		runSwapTick();
		if (g_swapStep == SwapStep::Finished) {
			finishSwapStage();
			g_stage = Stage::Listing;
		}
		break;
	case Stage::Listing:
		runListingTick();
		if (g_listingStep == ListingStep::Finished) {
			g_stage = Stage::Acquire;
		}
		break;
	case Stage::Acquire:
		runAcquireTick();
		if (g_acquireStep == AcquireStep::Finished) {
			g_stage = Stage::ByPath;
		}
		break;
	case Stage::ByPath:
		runByPathTick();
		if (g_byPathStep == ByPathStep::Finished) {
			g_stage = Stage::Done;
			printSummary();
			g_summaryPrinted = true;
		}
		break;
	case Stage::Done:
		break;
	}

	// Everything above runs on the UI tier, which is where the browser calls these from; the file
	// backing dispatches its own card work onto the storage owner itself. A marker op behind it
	// releases the latch: the owner queue is FIFO, so it runs after whatever was just queued (and
	// immediately if nothing was).
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_song_preview_ab_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_song_preview_ab_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
