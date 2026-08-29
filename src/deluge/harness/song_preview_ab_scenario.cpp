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
#include "gui/ui/load/load_song_ui.h" // the converted caller, driven end to end below
#include "hid/led/pad_leds.h"         // the image both backings render into
#include "io/file.hpp"                // the card swap's unlink + rename
#include "libdeluge/file_prefix.h"    // deluge_file_prefix_async_active
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "storage/song_preview.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
/// directory, and probed for by index because this side has no directory listing to walk.
///
/// They carry no absolute expectation -- their artwork is whatever their author made -- so the
/// cross-leg diff is the whole check. What they add is real documents: real attribute orders, real
/// firmware versions, real preview offsets, which is the thing a synthetic fixture cannot be.
constexpr const char* kCorpusDir = "SONGS/CORPUS";
constexpr size_t kMaxCorpusCases = 16;

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
enum class Stage : uint8_t { Cases, Window, Corpus, Swap, Done };
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
void checkDeferrals(const char* label, int32_t deferrals) {
	if (deluge_file_prefix_async_active()) {
		if (deferrals == 0) {
			printf("FAIL [songPreviewAB/%s]: the prefix channel is active but this case never deferred -- its "
			       "preview was answered without the async path being reached, so it is not under test in "
			       "this leg at all\n",
			       label);
			g_failures++;
		}
	}
	else if (deferrals != 0) {
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
	g_stage = Stage::Corpus;
}

/// Whether the packer staged a corpus song at this index. Probed rather than listed: this side has
/// no directory walk, and an absent index simply ends the corpus run.
bool corpusSongExists(const std::string& path) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	const auto present = deluge::io::presence_from_open(opened);
	return present.has_value() && *present;
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

void runCorpusTick() {
	if (g_corpusIndex >= kMaxCorpusCases) {
		g_stage = Stage::Swap;
		return;
	}
	const std::string filename = corpusFilename(g_corpusIndex);
	const std::string path = songPath(kCorpusDir, filename.c_str());

	if (!g_caseStarted) {
		if (!corpusSongExists(path)) {
			// The first gap ends the corpus: the packer writes them contiguously from 0.
			g_corpusIndex = kMaxCorpusCases;
			g_stage = Stage::Swap;
			return;
		}
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
	//   - A SECOND deferral can only come from the collect answering UNKNOWN and re-requesting, which
	//     is what refusing a prefix stamped with the previous card's generation forces. A stale
	//     prefix that is still servable is READY on that ask and there is no second deferral.
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

	if (g_corpusRan > 0 && g_corpusRendered == 0) {
		printf("FAIL [songPreviewAB]: %zu corpus song(s) ran and not one of them rendered artwork -- the corpus "
		       "leg proved nothing\n",
		       g_corpusRan);
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
	g_caseStarted = false;
	g_caseTicks = 0;
	g_caseDeferrals = 0;
	g_windowDeferrals = 0;
	g_failures = 0;
	g_stepDone = true;
	g_summaryPrinted = false;
	g_deferralsByCase.fill(0);
	g_deferralsByCorpusCase.fill(0);

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
	case Stage::Corpus:
		runCorpusTick();
		break;
	case Stage::Swap:
		runSwapTick();
		if (g_swapStep == SwapStep::Finished) {
			finishSwapStage();
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
