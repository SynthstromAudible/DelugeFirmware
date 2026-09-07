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
#include "gui/ui/load/load_instrument_preset_ui.h" // presetAcquisitionsRun(), the acquire stage's inert counter
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/save/save_song_ui.h" // the converted caller, driven end to end below
#include "gui/views/arranger_view.h"  // beginNewInstrumentAcquisition(), the acquire stage's REAL gesture entry point
#include "hid/led/pad_leds.h"         // the image both backings render into
#include "io/file.hpp"                // the card swap's unlink + rename
#include "libdeluge/file_directory.h" // deluge_file_directory_async_active
#include "libdeluge/file_prefix.h"    // deluge_file_prefix_async_active
#include "model/mod_controllable/mod_controllable_audio.h" // the cast writeToFile's backed-up-paramManager lookup needs
#include "model/output.h"                                  // Output::name, read by the acquire stage
#include "model/song/song.h"                               // currentSong, getNonAudioInstrumentToSwitchTo
#include "modulation/params/param_manager.h"               // ParamManagerForTimeline, for the preset fixture writer
#include "processing/sound/sound_instrument.h"             // setupAsBlankSynth(), for the preset fixture writer
#include "storage/audio/audio_file_manager.h"
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
	/// with entries culled from the start, which is the condition the second open exists for. The
	/// stage refuses to run its sweep at opens < 2 rather than reporting a green run over one N that
	/// covers no retry path.
	static void beginOpenListing(const char* dir, const char* filenameToStartAt, const char* defaultDir = "") {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		(void)loadSongUI.beginListing({.action = Browser::ListingAction::Open,
		                               .direction = 0,
		                               .filenameToStartAt = filenameToStartAt,
		                               .defaultDir = defaultDir,
		                               .folderOrPath = ""});
	}
	static void tick() { loadSongUI.graphicsRoutine(); }
	static bool inProgress() { return Browser::isListingInProgress(); }
	/// How many opens the listing that just finished performed. Read after a non-deferring run.
	static int32_t opensPerformed() { return Browser::listing_.opensThisListing; }
	static void deferAtOpen(int32_t n) { Browser::deferAtOpenNumber_ = n; }
	/// Run a ByPath listing's MACHINE to completion and report whether it matched exactly.
	///
	/// Drives `startPendingListing` rather than `beginListing`, so `runPendingListing`'s completion
	/// tail never runs: that tail's failure path calls `onListingFailed` -> `close()`, and this
	/// headless driver never pushed a browser onto the UI stack, so a deliberate miss would abort the
	/// process instead of reporting. What is under test is the existence ANSWER, which is the flag.
	static bool byPathMatchesExactly(const char* fullPath) {
		Browser::pendingListing_ = Browser::ListingRequest{.action = Browser::ListingAction::ByPath,
		                                                   .direction = 0,
		                                                   .filenameToStartAt = "",
		                                                   .defaultDir = "",
		                                                   .folderOrPath = fullPath};
		(void)loadSongUI.startPendingListing();
		return Browser::listing_.foundExactMatch;
	}
	/// Whether the finished listing matched its search string EXACTLY. For a ByPath listing this IS
	/// the existence answer the retired fileExists() used to give.
	static bool foundExactMatch() { return Browser::listing_.foundExactMatch; }
	/// Open the SAVE browser on `dir` and drive it to completion.
	///
	/// saveSongUI, not loadSongUI: `mayDefaultToBrandNewNameOnEntry` is set only by SaveUI's
	/// constructor, and without it a listing never reaches `pickBrandNewName` at all.
	/// @return Whether the listing COMPLETED and named a slot, rather than failing.
	static bool openSaveBrowserOn(const char* dir) {
		Browser::currentDir = dir;
		(void)saveSongUI.beginListing({.action = Browser::ListingAction::Open,
		                               .direction = 0,
		                               .filenameToStartAt = "",
		                               .defaultDir = "",
		                               .folderOrPath = ""});
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveSongUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress() && !Browser::enteredText.empty();
	}

	struct PredictionOutcome {
		std::string enteredText;
		int32_t fileIndexSelected;
		int32_t reads;   ///< reads that reached the filesystem, across resumptions
		int32_t editPos; ///< enteredTextEditPos afterwards — what the retraction cases assert on
	};

	/// Drive the REAL predictExtendedText and the REAL tick surface, never a restatement of the loop:
	/// a probe that reimplemented it would pass against a browser that never re-asks.
	static PredictionOutcome predictWith(int32_t deferAtRead, const char* dir, const char* prefix,
	                                     std::optional<int32_t> undoPos) {
		Browser::currentDir = dir;
		Browser::enteredText = prefix;
		Browser::enteredTextEditPos = static_cast<int32_t>(strlen(prefix));
		Browser::deferAtOpenNumber_ = deferAtRead;
		const int32_t before = Browser::totalDirectoryOpens();
		(void)loadSongUI.predictExtendedText(undoPos);
		for (int32_t t = 0; Browser::prediction_.active && t < 200; t++) {
			loadSongUI.graphicsRoutine();
		}
		Browser::deferAtOpenNumber_ = 0;
		return PredictionOutcome{.enteredText = Browser::enteredText,
		                         .fileIndexSelected = Browser::fileIndexSelected,
		                         .reads = Browser::totalDirectoryOpens() - before,
		                         .editPos = Browser::enteredTextEditPos};
	}

	/// How many times the listing that just finished suspended. Zero on the synchronous arm by
	/// construction, so requiring it non-zero proves the channel was actually reached.
	static int32_t deferrals() { return Browser::listing_.deferrals; }
	/// `Browser::fileIndexSelected` is protected; the snapshot needs it, and it is half of what
	/// "the listing finished the same way" means.
	static int32_t selectedIndex() { return Browser::fileIndexSelected; }

	/// Drives ArrangerView's REAL new-instrument pad gesture, rather than restating its callee
	/// (createNewInstrument) directly: a later task converts this gesture's CALLER, not the callee,
	/// and a probe that skipped it would keep testing the unconverted path forever.
	static void beginNewInstrumentAcquisition(int32_t yPressed) {
		arrangerView.beginNewInstrumentAcquisition(yPressed);
	}
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
enum class Stage : uint8_t { Cases, Window, CorpusCount, Corpus, Swap, Listing, Acquire, Done };
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
// ── Stage: listing idempotence ──────────────────────────────────────────────
//
// A folder listing performs up to four directory opens, and a listing whose Nth open is not ready
// is RE-RUN FROM THE TOP on a later tick rather than resumed. That is only sound if the second pass
// sees the first pass's inputs rather than its outputs, and three of the listing's own mutations
// break it -- currentDir is overwritten on the fallback path, enteredText is advanced by
// nextDefaultName, and triedCreatingFolder used to be a local that reset. Browser::ListingAttempt
// restores them; this is what proves it, and each of the three breaks a different N.
//
// Driven WITHOUT the directory channel compiled in, on purpose. What is under test is a property of
// the listing, not of the channel: `Browser::deferAtOpenNumber_` makes the Nth open defer and the
// browser's own per-tick surface is what re-runs the attempt. Nothing here runs a retry loop of its
// own, so a browser that stopped re-asking would hang this stage rather than be rescued by it.

/// Everything a finished listing leaves behind that a caller can observe.
struct ListingSnapshot {
	std::string dir;
	std::string entered;
	int32_t selected = -1;
	deluge::vector<std::string> names;

	bool operator==(const ListingSnapshot& other) const {
		if (dir != other.dir || entered != other.entered || selected != other.selected
		    || names.size() != other.names.size()) {
			return false;
		}
		for (size_t i = 0; i < names.size(); i++) {
			if (names[i] != other.names[i]) {
				return false;
			}
		}
		return true;
	}
};

/// The fixture directory. Deliberately more than FILE_ITEMS_MAX_NUM_ELEMENTS (20) entries, because
/// that is what makes `cullSomeFileItems` fire and `arrivedInNewFolder`'s numFileItemsDeletedAtStart
/// re-read happen -- the SECOND open, and the common case this stage exists to cover. A fixture of
/// 20 or fewer would list in one open and the sweep would have exactly one N to check, silently
/// covering none of the retry paths.
constexpr const char* kListingDir = "LISTTEST";
constexpr int32_t kListingEntries = 25;
/// Mid-folder AND deliberately absent -- see beginOpenListing for why both halves matter.
constexpr const char* kListingStartAt = "SONG012Z.XML";
static_assert(kListingEntries > FILE_ITEMS_MAX_NUM_ELEMENTS,
              "the fixture must overflow the file-item window, or no re-read is ever provoked");

/// A directory comfortably past the ~230 entries that once panicked the readdir snapshot arena and
/// bricked the device on open. This rung holds that snapshot across a request/collect gap, which is
/// exactly what makes the arena's window longer, so the case is the rung's own regression check
/// rather than a rider on somebody else's corpus.
/// The fallback's CREATE case: a default directory deliberately not present, so the fallback has to
/// make it. Removed before the case runs -- a leftover from a previous run would let the fallback's
/// first attempt succeed and the case would test nothing.
constexpr const char* kMakeDir = "MADEDIR";

/// EMPTY on purpose: `pickBrandNewName` is only reached when a listing finds no files at all, and it
/// is the path whose getUnusedSlot read defers from INSIDE the listing.
constexpr const char* kEmptyDir = "EMPTYDIR";
/// Higher than any listing's open count, so arming the seam with it selects the tick surface without
/// ever firing.
constexpr int32_t kNeverDefers = 99;

constexpr const char* kBigDir = "BIGLIST";
constexpr int32_t kBigEntries = 300;
static_assert(kBigEntries > 230, "the case must exceed the arena threshold it exists to guard");

enum class ListingStep : uint8_t { Fixture, Reference, Sweep, BigFolder, Fallback, Create, Mount, Finished };
ListingStep g_listingStep = ListingStep::Fixture;
int32_t g_listingDeferAt = 0;  ///< which N the sweep is on; 0 = the reference run
int32_t g_listingOpens = 0;    ///< how many opens the reference run performed
bool g_listingRunning = false; ///< a listing is in flight and being ticked
int32_t g_listingTicks = 0;    ///< tick budget guard for the run in flight
ListingSnapshot g_listingReference;
int32_t g_listingCasesRun = 0;
uint32_t g_mountNoticesBefore = 0;
/// How many items the list held when the run in flight started. A suspended listing must not blank
/// the list it is replacing -- but only where there WAS one, so the very first listing (with nothing
/// to preserve) is not counted as a violation.
size_t g_itemsWhenRunBegan = 0;
/// Set if a suspended listing was ever seen with an empty list it should have been preserving.
bool g_sawBlankWhileSuspended = false;

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
	// short would make the sweep trivially true, and would make the big-folder case stop exercising
	// the arena threshold entirely while still reporting a green run -- the "gate that covers
	// nothing" shape this branch has hit twice.
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

/// A ByPath listing must answer "is this file still there?" -- the question the blocking
/// `StorageManager::fileExists` pre-flight used to answer before every favourite open.
///
/// The absent case is the regression the pre-flight was hiding: the old completion tail tested
/// `fileIndexSelected > fileItems.size()`, but `searchFileItems` returns an INSERTION POINT, which is
/// always <= size -- so a file that had been deleted fell straight through and the browser opened
/// whatever sorted into its place.
///
/// Synchronous leg only. Which comparison answers the existence question is not an async property,
/// and running it on the channel leg would need the tick loop for no added coverage.
void runByPathCases() {
	if (deluge_file_directory_async_active()) {
		printf("SP-LISTING bypath skipped on the channel leg (not an async property)\n");
		return;
	}
	char path[64];
	snprintf(path, sizeof(path), "%s/SONG005.XML", kListingDir); // the fixture created this one
	const bool hit = BrowserListingAbProbe::byPathMatchesExactly(path);
	snprintf(path, sizeof(path), "%s/SONG999.XML", kListingDir); // and deliberately not this one
	const bool miss = BrowserListingAbProbe::byPathMatchesExactly(path);
	printf("SP-LISTING bypath present=%d absent=%d\n", hit ? 1 : 0, miss ? 1 : 0);
	if (!hit) {
		printf("SP-LISTING FAIL bypath: a file that exists did not match exactly\n");
		g_failures++;
	}
	if (miss) {
		printf("SP-LISTING FAIL bypath: a file that does not exist matched -- the browser would open "
		       "whatever sorted into its place\n");
		g_failures++;
	}
}

/// A listing that finds NO files names a brand-new slot, and naming it reads the folder again
/// (`getUnusedSlot`) from INSIDE the listing. A deferral there must SUSPEND the listing, not fail it:
/// before the fix it propagated out of `pickBrandNewName` as an ordinary error, and `stepListing`
/// turned that into emptyFileItems() + onListingFailed -- an error popup and a closed browser, on a
/// folder that is perfectly readable.
///
/// The resumption must also not re-name. `pickBrandNewName`'s `enteredText.empty()` guard is what
/// prevents it; a guard asserted rather than measured is how this rung's earlier drafts went wrong,
/// so the call count is checked.
void runBrandNewNameCases() {
	if (deluge_file_directory_async_active()) {
		// Synchronous leg only, like the ByPath cases. What is under test is a property of the listing
		// MACHINE -- that a deferral arising inside its selection tail suspends it rather than
		// restarting it -- and the seam exercises that without the channel. Driving it on the channel
		// leg would need this case to become a step of the scenario's own machine, because a tight
		// tick loop here never yields to the driver thread.
		printf("SP-NEWNAME skipped on the channel leg (a machine property, not an async one)\n");
		return;
	}
	Browser::enteredText.clear();
	// Armed with a number no listing reaches, so the reference takes the TICK SURFACE like the sweep
	// runs do but never actually defers. `beginListing` chooses the tick surface on `deferAtOpenNumber_
	// != 0`, and with 0 it dispatches to the storage owner instead -- which this probe's loop does not
	// pump, so the listing would simply never run. Reference and sweep must travel the same path or
	// the comparison is between two different things.
	BrowserListingAbProbe::deferAtOpen(kNeverDefers);
	const int32_t openBefore = Browser::totalDirectoryOpens();
	if (!BrowserListingAbProbe::openSaveBrowserOn(kEmptyDir)) {
		printf("SP-NEWNAME FAIL the reference listing did not name a slot\n");
		g_failures++;
		return;
	}
	const std::string referenceName = Browser::enteredText;
	// The TOTAL, not listing_.opensThisListing: a restart re-arms listing_ and resets that member, so
	// it cannot see the very restart this case exists to catch.
	const int32_t referenceOpens = Browser::totalDirectoryOpens() - openBefore;
	printf("SP-NEWNAME reference name=%s opens=%d\n", referenceName.c_str(), referenceOpens);
	if (referenceOpens < 2) {
		printf("SP-NEWNAME FAIL only %d open(s) -- getUnusedSlot's read was never reached\n", referenceOpens);
		g_failures++;
		return;
	}

	for (int32_t k = 1; k <= referenceOpens; k++) {
		Browser::enteredText.clear(); // the pickBrandNewName precondition
		BrowserListingAbProbe::deferAtOpen(k);
		const int32_t slotCallsBefore = Browser::unusedSlotCalls();
		const int32_t before = Browser::totalDirectoryOpens();
		const bool ok = BrowserListingAbProbe::openSaveBrowserOn(kEmptyDir);
		const int32_t opens = Browser::totalDirectoryOpens() - before;
		// slotCalls is reported, not asserted: a naming read that deferred and was then re-run is
		// legitimately two calls. It is here because it is what first exposed the restart, when an
		// open count that resets on re-arm could not see it.
		printf("SP-NEWNAME deferAt=%d ok=%d opens=%d slotCalls=%d name=%s\n", k, ok ? 1 : 0, opens,
		       Browser::unusedSlotCalls() - slotCallsBefore, Browser::enteredText.c_str());
		if (!ok || Browser::enteredText != referenceName) {
			printf("SP-NEWNAME FAIL deferAt=%d named %s, the reference named %s\n", k, Browser::enteredText.c_str(),
			       referenceName.c_str());
			g_failures++;
		}
		// The property, and the one an identical-name check cannot see: a deferral inside the
		// SELECTION must suspend the listing where it stands. stepListing used to clear
		// listing_.active on its way out, so the pump found no active listing and called
		// startPendingListing() -- restarting from the top and redoing every open before the
		// deferral. Against the real channel, one slot and consumed at take, that livelocks.
		if (opens != referenceOpens) {
			printf("SP-NEWNAME FAIL deferAt=%d made %d opens, the reference made %d -- the listing "
			       "restarted instead of resuming\n",
			       k, opens, referenceOpens);
			g_failures++;
		}
	}
	BrowserListingAbProbe::deferAtOpen(0);
}

/// Reaching prediction's SECOND read takes a big folder, and that is the point rather than an
/// awkwardness of the fixture.
///
/// The re-read happens when a search runs off the end of the CACHED window. In a 25-entry folder the
/// whole folder fits, so a prefix either matches, or sorts past everything and hits `notFound` with
/// `doneNewRead` already set — one read either way, and a sweep over one read covers no resumption.
/// In the 300-entry folder the window holds twenty, so the extra-zeroes branch (which clears
/// `doneNewRead`) walks the extended string off the cached end and reads again. That is exactly the
/// #4584 case the re-read exists for.
///
/// Measured, not assumed: over the 25-entry fixture every candidate tried gave reads<=1; over the
/// 300-entry one this gives 2.
constexpr const char* kPredictDir = kBigDir;
constexpr const char* kPredictPrefix = "SONG0129";
/// Matches nothing however many zeroes are appended, so the prediction FAILS — which is what the
/// retraction cases need.
constexpr const char* kMissingPrefix = "ZZZZNOPE";

/// Prediction must survive being suspended at ANY of its reads and finish identically.
///
/// Synchronous leg only, like the other machine-level cases: what is under test is that the loop
/// RESUMES rather than restarts, which the seam exercises without the channel. On the channel leg a
/// tight tick loop here would never yield to the driver thread.
void runPredictionCases() {
	if (deluge_file_directory_async_active()) {
		printf("SP-PREDICT skipped on the channel leg (a machine property, not an async one)\n");
		return;
	}

	const auto reference = BrowserListingAbProbe::predictWith(0, kPredictDir, kPredictPrefix, std::nullopt);
	printf("SP-PREDICT reference text=%s selected=%d reads=%d\n", reference.enteredText.c_str(),
	       reference.fileIndexSelected, reference.reads);
	if (reference.reads < 2) {
		printf("SP-PREDICT FAIL only %d read(s) -- the sweep would cover no resumption\n", reference.reads);
		g_failures++;
		return;
	}

	for (int32_t k = 1; k <= reference.reads; k++) {
		const auto got = BrowserListingAbProbe::predictWith(k, kPredictDir, kPredictPrefix, std::nullopt);
		printf("SP-PREDICT deferAt=%d text=%s selected=%d reads=%d\n", k, got.enteredText.c_str(),
		       got.fileIndexSelected, got.reads);
		if (got.enteredText != reference.enteredText || got.fileIndexSelected != reference.fileIndexSelected) {
			printf("SP-PREDICT FAIL deferAt=%d text=%s/%s selected=%d/%d\n", k, got.enteredText.c_str(),
			       reference.enteredText.c_str(), got.fileIndexSelected, reference.fileIndexSelected);
			g_failures++;
		}
		// The property an identical-result check cannot see: a suspended prediction RESUMED at the
		// read that deferred rather than starting over. A deferral reads nothing, so a suspended run
		// makes exactly as many reads as the reference; a restart would redo every read before it.
		if (got.reads != reference.reads) {
			printf("SP-PREDICT FAIL deferAt=%d made %d reads, the reference made %d -- it did not resume "
			       "where it suspended\n",
			       k, got.reads, reference.reads);
			g_failures++;
		}
	}

	// Only the pad-typing caller undoes. Its undo must happen even when the prediction failed on a
	// LATER tick, long after that caller returned true and moved on.
	const int32_t posAtCall = static_cast<int32_t>(strlen(kMissingPrefix));
	const auto undone = BrowserListingAbProbe::predictWith(1, kPredictDir, kMissingPrefix, posAtCall - 1);
	const auto kept = BrowserListingAbProbe::predictWith(1, kPredictDir, kMissingPrefix, std::nullopt);
	printf("SP-PREDICT retraction undone=%d kept=%d wanted=%d/%d\n", undone.editPos, kept.editPos, posAtCall - 1,
	       posAtCall);
	if (undone.editPos != posAtCall - 1) {
		printf("SP-PREDICT FAIL the pad site's failure left editPos=%d, wanted %d\n", undone.editPos, posAtCall - 1);
		g_failures++;
	}
	if (kept.editPos != posAtCall) {
		printf("SP-PREDICT FAIL a non-pad failure moved editPos to %d, wanted %d\n", kept.editPos, posAtCall);
		g_failures++;
	}
	printf("SP-PREDICT-SUMMARY reads=%d sweeps=%d\n", reference.reads, reference.reads);
}

void runListingTick() {
	if (g_listingRunning) {
		// Checked BEFORE the tick, so it observes the state a user would see between frames.
		// readFileItemsForFolder clears the list before it opens, so a suspended listing blanks the
		// folder it is replacing -- invisible for the listing itself, which hides behind
		// listingInProgress_, but the typing prediction reaches the same function with no such gate.
		if (g_itemsWhenRunBegan > 0 && BrowserListingAbProbe::inProgress() && Browser::fileItems.empty()) {
			g_sawBlankWhileSuspended = true;
		}
		BrowserListingAbProbe::tick();
		if (++g_listingTicks > 200) {
			printf("SP-LISTING FAIL deferAt=%d never finished -- nothing re-asked\n", g_listingDeferAt);
			g_failures++;
			g_listingStep = ListingStep::Finished;
			g_listingRunning = false;
			return;
		}
		if (BrowserListingAbProbe::inProgress()) {
			return; // still deferring; the tick surface will run it again
		}
		g_listingRunning = false;

		if (g_listingStep == ListingStep::Mount) {
			const ListingSnapshot got = takeListingSnapshot();
			// One notice, not zero and not two. Zero is the hazard the mount generation exists for: a
			// driver that mounts silently leaves is_mounted() true, and an edge detector that sampled
			// that would skip the notification forever. Two is the other side of it -- both initSD()
			// and the tick surface noticing the same mount.
			const uint32_t notices = StorageManager::cardMountNotices() - g_mountNoticesBefore;
			printf("SP-LISTING mount listed=%zu notices=%u deferrals=%d\n", got.names.size(), notices,
			       BrowserListingAbProbe::deferrals());
			if (got.names.size() != g_listingReference.names.size()) {
				printf("SP-LISTING FAIL mount: listed %zu entries from an unmounted card, wanted %zu\n",
				       got.names.size(), g_listingReference.names.size());
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
			printf("SP-LISTING-SUMMARY opens=%d sweeps=%d big=%d blanked=%d\n", g_listingOpens, g_listingCasesRun,
			       kBigEntries, g_sawBlankWhileSuspended ? 1 : 0);
			g_listingStep = ListingStep::Finished;
			return;
		}

		if (g_listingStep == ListingStep::Create) {
			// The fallback had to CREATE its default directory. On the synchronous arm that mkdir runs
			// inline on the storage owner; on the channel arm it rides the request and runs on the
			// storage tier with the open, because a card WRITE is no more allowed on the interaction
			// tier than a read is. Either way the directory must exist afterwards and the listing must
			// have landed on it.
			const ListingSnapshot got = takeListingSnapshot();
			const bool exists = deluge::io::Directory::open(kMakeDir).has_value();
			printf("SP-LISTING create dir=%s exists=%d deferrals=%d\n", got.dir.c_str(), exists ? 1 : 0,
			       BrowserListingAbProbe::deferrals());
			if (!exists || !deluge::string::caselessEquals(got.dir, kMakeDir)) {
				printf("SP-LISTING FAIL create: landed on %s, %s exists=%d\n", got.dir.c_str(), kMakeDir,
				       exists ? 1 : 0);
				g_failures++;
			}
			runByPathCases();
			runPredictionCases();
			runBrandNewNameCases();

			// Now with the filesystem UNMOUNTED. Mounting is real card I/O, so on the channel arm it
			// belongs to the driver, not to StorageManager::initSD() running on the interaction tier.
			// The listing has to succeed anyway, and -- the half that is invisible in its result -- the
			// unmounted->mounted edge has to reach firstCardRead() exactly once, even though nothing
			// in C++ did the mounting.
			g_mountNoticesBefore = StorageManager::cardMountNotices();
			deluge_efatfs_unmount();
			BrowserListingAbProbe::beginOpenListing(kListingDir, kListingStartAt);
			g_listingRunning = true;
			g_listingTicks = 0;
			g_listingStep = ListingStep::Mount;
			return;
		}

		if (g_listingStep == ListingStep::Fallback) {
			// The listing must have SUCCEEDED, against the default directory rather than the one that
			// does not exist. Before the directory open got its own error mapping this could not
			// happen: the open reported FILE_NOT_FOUND, the fallback tests FOLDER_DOESNT_EXIST, and
			// the listing simply failed.
			const ListingSnapshot got = takeListingSnapshot();
			if (got.names.empty() || !deluge::string::caselessEquals(got.dir, kBigDir)) {
				printf("SP-LISTING FAIL fallback landed on dir=%s with %zu entries, wanted %s\n", got.dir.c_str(),
				       got.names.size(), kBigDir);
				g_failures++;
			}
			else {
				printf("SP-LISTING fallback dir=%s listed=%zu deferrals=%d\n", got.dir.c_str(), got.names.size(),
				       BrowserListingAbProbe::deferrals());
			}
			// Now the same fallback against a default directory that does NOT exist, so it has to be
			// created.
			(void)deluge::io::unlink(kMakeDir);
			if (deluge::io::Directory::open(kMakeDir).has_value()) {
				printf("SP-LISTING FAIL fixture: %s still exists, the create case would test nothing\n", kMakeDir);
				g_failures++;
				g_listingStep = ListingStep::Finished;
				return;
			}
			BrowserListingAbProbe::beginOpenListing("NOSUCHDIR2", kListingStartAt, kMakeDir);
			g_listingRunning = true;
			g_listingTicks = 0;
			g_listingStep = ListingStep::Create;
			return;
		}

		if (g_listingStep == ListingStep::BigFolder) {
			// Surviving the open at all is the property: >230 entries once panicked the snapshot arena
			// on open and bricked the device. Reaching here means the open, the enumeration and the
			// close all completed.
			printf("SP-LISTING big entries=%d listed=%zu deferrals=%d\n", kBigEntries,
			       takeListingSnapshot().names.size(), BrowserListingAbProbe::deferrals());
			// The missing-folder fallback, landing on a directory big enough to CULL, entered WITH a
			// search string. All three matter. The fallback drops the search string, and dropping it
			// without also resetting catalogSearchDirection sends the cull into
			// searchFileItems(filenameToStartSearchAt) -- now null -- and straight to strcmpspecial.
			// So this case covers both halves of the revival: that the fallback happens at all, and
			// that it does not take the crash with it.
			BrowserListingAbProbe::beginOpenListing("NOSUCHDIR", kListingStartAt, kBigDir);
			g_listingRunning = true;
			g_listingTicks = 0;
			g_listingStep = ListingStep::Fallback;
			return;
		}

		if (g_listingStep == ListingStep::Reference) {
			g_listingReference = takeListingSnapshot();
			g_listingOpens = BrowserListingAbProbe::opensPerformed();
			// `deferrals` is what separates the two legs. On the synchronous arm it is 0 by
			// construction; on the channel leg it must be non-zero, or the listing was quietly served
			// by a direct open and the leg tested nothing.
			printf("SP-LISTING reference entries=%zu opens=%d deferrals=%d\n", g_listingReference.names.size(),
			       g_listingOpens, BrowserListingAbProbe::deferrals());
			if (g_listingOpens < 2) {
				printf("SP-LISTING FAIL only %d open(s) -- the sweep would cover no retry path\n", g_listingOpens);
				g_failures++;
				g_listingStep = ListingStep::Finished;
				return;
			}
			g_listingDeferAt = 0;
			g_listingStep = ListingStep::Sweep;
			return;
		}

		// A sweep run just finished.
		const ListingSnapshot got = takeListingSnapshot();
		g_listingCasesRun++;

		// The state machine's central property, and the one an identical-result check alone cannot
		// see: a listing that suspended RESUMED at the pending open rather than starting over, so no
		// open was made twice. A deferral opens nothing, so a suspended run makes exactly as many
		// opens as the reference; a run that restarted from the top would redo every open before the
		// deferral and make more.
		//
		// This is what the earlier restart-on-defer design failed on, invisibly: the seam disarms
		// after firing, so a restart still converges here and an identical-result check passes. It is
		// against the REAL channel -- one slot, answer consumed at take -- that a restart livelocks,
		// re-asking for open #1 forever. Counting opens is what makes that visible without the
		// channel.
		const int32_t opens = BrowserListingAbProbe::opensPerformed();
		if (opens != g_listingOpens) {
			printf("SP-LISTING FAIL deferAt=%d made %d opens, the reference made %d -- an open was redone\n",
			       g_listingDeferAt, opens, g_listingOpens);
			g_failures++;
		}
		if (!(got == g_listingReference)) {
			printf("SP-LISTING FAIL deferAt=%d dir=%s/%s entered=%s/%s sel=%d/%d n=%zu/%zu\n", g_listingDeferAt,
			       got.dir.c_str(), g_listingReference.dir.c_str(), got.entered.c_str(),
			       g_listingReference.entered.c_str(), got.selected, g_listingReference.selected, got.names.size(),
			       g_listingReference.names.size());
			g_failures++;
		}
		return;
	}

	switch (g_listingStep) {
	case ListingStep::Fixture:
		if (!buildFixtureDir(kListingDir, kListingEntries)) {
			g_failures++;
			g_listingStep = ListingStep::Finished;
			return;
		}
		BrowserListingAbProbe::deferAtOpen(0);
		BrowserListingAbProbe::beginOpenListing(kListingDir, kListingStartAt);
		g_listingRunning = true;
		g_listingTicks = 0;
		g_listingStep = ListingStep::Reference;
		return;
	case ListingStep::Sweep:
		if (deluge_file_directory_async_active()) {
			// The channel leg does not sweep. Its deferrals come from the real channel -- one slot,
			// answer consumed at take -- which is the thing the seam only models, so arming the seam on
			// top would defer twice at one open and prove nothing the reference run has not already
			// proved. What this leg contributes is that the listing COMPLETES against that channel and
			// lands on the same result as the synchronous leg, which is what the script diffs.
			printf("SP-LISTING sweep skipped: the channel supplies the deferrals on this leg\n");
			g_listingDeferAt = g_listingOpens;
		}
		if (g_listingDeferAt >= g_listingOpens) {
			BrowserListingAbProbe::deferAtOpen(0);
			// The big folder is listed LAST, so a panic in it cannot be mistaken for a sweep failure.
			if (!buildFixtureDir(kEmptyDir, 0) || !buildFixtureDir(kBigDir, kBigEntries)) {
				g_failures++;
				g_listingStep = ListingStep::Finished;
				return;
			}
			BrowserListingAbProbe::beginOpenListing(kBigDir, kListingStartAt);
			g_listingRunning = true;
			g_listingTicks = 0;
			g_listingStep = ListingStep::BigFolder;
			return;
		}
		g_listingDeferAt++;
		BrowserListingAbProbe::deferAtOpen(g_listingDeferAt);
		g_itemsWhenRunBegan = Browser::fileItems.size();
		BrowserListingAbProbe::beginOpenListing(kListingDir, kListingStartAt);
		g_listingRunning = true;
		g_listingTicks = 0;
		return;
	case ListingStep::Reference:
	case ListingStep::Finished:
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
// the rung 5 spec, section 2b). Every leg runs it synchronously today; a later task converts the
// PAD-PRESS CALLER, not this method, onto the storage channel -- so driving this entry point (rather
// than createNewInstrument() directly) is what keeps this gate covering the converted path once that
// happens.
//
// The recursive descent (load_instrument_preset_ui.cpp:1291-1296 vs :1317) is only reached once the
// TOP LEVEL is exhausted -- a preset sitting at the top level is always returned first, however many
// subfolders sit alongside it. So the fixture keeps exactly ONE top-level preset: Reference acquires
// and releases it (still top-level), Converted acquires it again and keeps it (top-level now
// permanently empty), and every acquisition from AcquireStep::Nested onward MUST descend into
// SYNTHS/NESTED to find anything at all. `AcquireStep::Nested` asserts the acquired name is a nested
// one, which is the only way to prove the walk actually descended rather than merely that some
// acquisition happened to succeed.
enum class AcquireStep : uint8_t { Fixture, Reference, Converted, Nested, Coalesce, NonAudio, Finished };
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
// One for AcquireStep::Nested's proof, three more for Coalesce's three gestures, plus a spare --
// Coalesce doesn't assert identity, but running out would make its gestures fail silently instead of
// exercising the walk.
constexpr int32_t kAcqNestedCount = 5;
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
// that actually RUN, not dispatch attempts. Nothing calls dispatchPresetAcquisition() yet -- a later
// task wires a real gesture through it -- which is why the NonAudio and Coalesce checks below are
// inert until then.
int32_t acqDispatches() {
	return LoadInstrumentPresetUI::presetAcquisitionsRun();
}
int32_t g_acqLoads = 0;         // acquisition gestures that actually ran
std::string g_acqReferenceName; // the instrument the synchronous leg settled on

/// The instrument name an acquisition settled on, or "" if it failed. Reads the Output's name
/// rather than any FileItem -- a FileItem does not survive emptyFileItems() (spec section 6).
std::string acquiredName(Output* output) {
	return (output != nullptr) ? output->name : std::string{};
}

/// Run the acquisition gesture on row `kAcqRow` and report the name it settled on. Reads back
/// through `arrangerView.yPressedEffective` (see `kAcqRow`'s doc) -- exactly what the real pad
/// handler does immediately after this same call (arranger_view.cpp).
std::string runAcquisitionGesture() {
	BrowserListingAbProbe::beginNewInstrumentAcquisition(kAcqRow);
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
		g_acqReferenceName = runAcquisitionGesture();
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
		const std::string name = runAcquisitionGesture();
		g_acqLoads++;
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
		// stage exists to cover. Asserting the exact nested name is what proves the walk actually
		// descended, rather than merely that some acquisition happened to succeed.
		const std::string name = runAcquisitionGesture();
		g_acqLoads++;
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
		// Three gestures with no owner tick between them. Coalescing would produce fewer preset LOADS
		// than gestures pressed; "loads" here is read from the production dispatch counter, not
		// merely counted per gesture -- counting gestures would make this always equal the loop
		// bound by construction and could never fail, whatever the production code did.
		//
		// NOTE: inert until Task 3 -- runPresetAcquisitionOp() (Task 2) increments
		// presetAcquisitionsRun_ once per op that actually runs, but nothing calls
		// dispatchPresetAcquisition() until the arranger's caller is converted, so
		// `loads` reads 0 and `0 >= kCoalesceGestures` is always false here. Written as a real
		// assertion now (not a stub) so it arms itself the moment the counter starts counting, the
		// same shape as AcquireStep::NonAudio below.
		constexpr int32_t kCoalesceGestures = 3;
		const int32_t dispatchesBefore = acqDispatches();
		for (int32_t i = 0; i < kCoalesceGestures; ++i) {
			(void)runAcquisitionGesture();
			g_acqLoads++;
		}
		const int32_t loads = acqDispatches() - dispatchesBefore;
		printf("SP-ACQUIRE coalesce gestures=%d loads=%d\n", kCoalesceGestures, loads);
		if (loads >= kCoalesceGestures) {
			printf("SP-ACQUIRE FAIL coalescing produced %d load(s) for %d gesture(s) -- expected fewer\n", loads,
			       kCoalesceGestures);
			g_failures++;
		}
		g_acquireStep = AcquireStep::NonAudio;
		break;
	}
	case AcquireStep::NonAudio: {
		// The MIDI/CV fast path (spec section 2c): no card, and so no dispatch at all.
		// NOTE: inert until Task 3 -- presetAcquisitionsRun() is 0 before anything calls
		// dispatchPresetAcquisition(), so this passes trivially here. It becomes a real check once
		// a real gesture starts dispatching.
		const int32_t before = acqDispatches();
		bool alreadyInSong = false;
		(void)currentSong->getNonAudioInstrumentToSwitchTo(OutputType::CV, Availability::INSTRUMENT_UNUSED, 0, -1,
		                                                   &alreadyInSong);
		printf("SP-ACQUIRE nonaudio dispatches=%d\n", acqDispatches() - before);
		if (acqDispatches() != before) {
			printf("SP-ACQUIRE FAIL a CV switch dispatched a storage op; it reads no card\n");
			g_failures++;
		}
		g_acquireStep = AcquireStep::Finished;
		break;
	}
	case AcquireStep::Finished:
		break;
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
	g_listingDeferAt = 0;
	g_listingOpens = 0;
	g_listingRunning = false;
	g_listingTicks = 0;
	g_listingCasesRun = 0;
	g_listingReference = ListingSnapshot{};
	g_itemsWhenRunBegan = 0;
	g_sawBlankWhileSuspended = false;
	g_acquireStep = AcquireStep::Fixture;
	g_acqLoads = 0;
	g_acqReferenceName.clear();

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
