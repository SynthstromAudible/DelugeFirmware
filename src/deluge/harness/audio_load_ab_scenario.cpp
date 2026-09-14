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

#include "harness/audio_load_ab_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "gui/ui/browser/sample_browser.h"         // the converted caller, driven end to end below
#include "gui/views/instrument_clip_view.h"        // fileBrowserShouldNotPreview
#include "gui/waveform/waveform_basic_navigator.h" // proof the preview actually DREW
#include "harness/load_invariants.h"               // checkLoadInvariants -- shared with deluge_loadcheck
#include "io/file.hpp"                             // the card swap's unlink + rename
#include "libdeluge/audio_load.h"                  // deluge_audio_load_async_active, _set_latency_ms
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/owner.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines
// (`src/bsp/host/host_platform.c`) -- this harness links the Rust BSP and has no provider, so the
// alias turns every line this file writes into an undefined symbol at link time. Every one of those
// lines IS the gate's output (the script reads them), so they must go to real stdout regardless of
// which headers a future edit pulls in.
#ifdef printf
#undef printf
#endif

namespace {

/// One sample the load asks for, in the order the load asks for it.
///
/// The order is behaviour, not presentation. The load context's alternate-dir status is per-LOAD state that each
/// sample MUTATES as it goes (a regular-path miss is what earns the alternate-dir probe), so the
/// try order for every later sample depends on what the earlier ones did. A list that resolved
/// every file from the regular directory would never exercise that at all.
struct Case {
	/// Label for the printed line — short, stable, and independent of the path.
	const char* label;
	/// The nominal path, as a song's XML would name it.
	const char* path;
};

/// The load, in order. Deliberately mixed, and the mix is the fixture's whole content:
///
/// - `both` exists in the regular directory AND the alternate one, and is asked for FIRST — while
///   the context's alternate-dir status is still MIGHT_EXIST, so only the regular path is even proposed.
/// - `only_alt` exists only in the alternate directory. Its regular-path miss is what probes that
///   directory and promotes the status for everything after it.
/// - `both2` is the same two-locations shape as `both`, but asked for AFTER that promotion, so its
///   request carries BOTH candidates and the order between them is live. Without this case the
///   candidate order is never actually under test: every earlier two-location lookup happens while
///   only one candidate exists, and every later file exists in one place, so any order finds the
///   same file. (Established by negative control: reversing the loader's candidate order left the
///   six-case version of this list completely unchanged.)
/// - `nested` is the only case whose path is NOT flat, and that is its whole reason for existing.
///   `setupAlternateAudioFilePath`'s flattened candidate (`ALT/KIT_SNARE.WAV`) and
///   `alternateDirBareNameCandidate` (`ALT/SNARE.WAV`) are DISTINCT strings only for a nested path;
///   for every flat path in this list they stringify to the same thing, so the request carries a
///   duplicate and the three-candidate order collapses to two. All three locations exist here, with
///   three different lengths, so a wrongly constructed flattened name or a swap between candidates 1
///   and 2 changes which file is chosen.
/// - `selfdir` LIVES IN THE ALTERNATE DIRECTORY ITSELF, so the context's `alternateDir` equals its
///   own directory and the bare-name alternate candidate stringifies to EXACTLY the nominal path.
///   That is the only shape in which "which candidate won" cannot be decided by comparing strings:
///   `resolveFileSize` decides by SLOT and calls it an alternate hit, so `loadedFromAlternatePath`
///   is filled and `save_song_ui.cpp` copies the file into a saved song's folder. A backing that
///   compared the winning path against the nominal one calls the same hit "regular", leaves that
///   field empty, and the sample is silently left behind on a collect-media save. It sits after
///   `only_alt` because it needs the promoted status for the alternate candidates to exist at all.
///   The `alt="..."` field of its AB-CASE line is what carries this; nothing else in the list can.
/// - `only_reg` and `stereo24` follow, under the promoted status; `stereo24` differs in both of the
///   geometry fields the comparison names first (2 channels, 24-bit).
/// - `garbage` (bytes that are not audio), `empty` (a zero-length file) and `missing` (no file at
///   all) are three DIFFERENT ways to fail, and they are here as three cases rather than one because
///   what has to match is the `Error` IDENTITY, not the mere fact of failure. A backing that
///   collapsed two of these onto one code would still "fail" on both.
constexpr std::array<Case, 10> kCases{{
    {"both", "SAMPLES/BOTH.WAV"},
    {"only_alt", "SAMPLES/ONLYALT.WAV"},
    {"both2", "SAMPLES/BOTH2.WAV"},
    {"selfdir", "SAMPLES/ALT/SELFDIR.WAV"},
    {"nested", "SAMPLES/KIT/SNARE.WAV"},
    {"only_reg", "SAMPLES/ONLYREG.WAV"},
    {"stereo24", "SAMPLES/STEREO24.WAV"},
    {"garbage", "SAMPLES/GARBAGE.WAV"},
    {"empty", "SAMPLES/EMPTY.WAV"},
    {"missing", "SAMPLES/MISSING.WAV"},
}};

/// How many times one case may answer LOAD_IN_PROGRESS before it is called wedged. Each deferral is
/// one dispatch round trip through the platform driver's executor, so this is generous: a healthy
/// async load takes one or two.
constexpr int32_t kMaxDeferrals = 2000;

/// Cursor over kCases.
size_t g_caseIndex = 0;
/// Deferrals recorded for the current case.
int32_t g_deferralsThisCase = 0;
/// The current case's reference line, from its synchronous load, and whether it has been taken yet.
std::string g_referenceLine{};
bool g_referenceTaken = false;
/// Deferrals recorded across the whole run.
int32_t g_deferralsTotal = 0;
/// Deferrals recorded PER CASE, kept after the case settles.
///
/// A total is not enough, and the difference is a hole big enough to hide the thing this gate
/// exists to pin. `requestAsyncSampleLoad` declines permanently for three reasons (an inadmissible
/// path, a candidate list that could not be formed, a truncation sentinel), and
/// `collectAsyncSampleLoad` passes an UNKNOWN-after-decline straight through to the SYNCHRONOUS
/// path -- correctly, since a permanently refused request would otherwise never complete. So a
/// single case can quietly fall through to the same synchronous code its reference runs. Its
/// AB-CASE line is then trivially identical, the run-wide total is still non-zero because the other
/// cases deferred, and the gate passes green with that case no longer under test at all. Concretely:
/// tighten the admission rule so `both2` declines and the candidate-ORDER property disappears from
/// the gate silently. Per-case counts are what make that visible.
std::array<int32_t, kCases.size()> g_deferralsByCase{};
/// The index of `selfdir` in kCases, whose one property is checked absolutely below.
constexpr size_t kSelfdirIndex = 3;
/// Where `selfdir` must be recorded as having been loaded from.
///
/// The SAME string as its nominal path, which is the entire reason this case exists: only a
/// positional reading of "which candidate won" fills the field at all.
constexpr std::string_view kSelfdirAlternate = "SAMPLES/ALT/SELFDIR.WAV";
// The index is a bare number, and reordering kCases would silently point it at a different case --
// one that may well carry a non-empty alternate of its own and pass, under a message about a
// property nothing is testing any more. So the index is tied to the case: a reorder is a build
// failure, not a quietly weakened gate.
static_assert(std::string_view{kCases[kSelfdirIndex].path} == kSelfdirAlternate,
              "kSelfdirIndex no longer names the self-directory case; kCases was reordered");
/// What `selfdir` recorded in `loadedFromAlternatePath`.
///
/// The AB-CASE comparison cannot require this. `selfdir`'s winning candidate stringifies to
/// EXACTLY its nominal path, so "both backings say the same thing" is satisfied just as well by both
/// saying nothing -- which is what a backing that decided "an alternate won" by comparing paths
/// rather than by candidate SLOT does. And the case is only meaningful under the promoted
/// alternate-dir status, so it has to sit after `only_alt` in the list; move it above and the status
/// is still unresolved, only the regular path is proposed, both backings report an empty alternate,
/// and the comparison still matches with the property gone. An empty value here is what
/// `save_song_ui.cpp` gates the collect-media copy on: the sample is silently left out of a saved
/// song's folder.
std::string g_selfdirAlternatePath{};
/// In-process assertion failures.
int32_t g_failures = 0;
/// The load every case belongs to: a song's, whose alternate directory is SAMPLES/ALT. Set by begin().
deluge::storage::AudioLoadContext g_loadContext{};
/// Set false by dispatch_step(), true by the dispatched closure on its way out.
bool g_stepDone = true;
/// Set once the summary has been printed, so complete() latches.
bool g_summaryPrinted = false;

// ── The preview re-drive stage ──────────────────────────────────────────────
//
// Everything above drives the loader through its OWN retry loop: `runOneStep`
// returns on LOAD_IN_PROGRESS and the platform driver calls it again. That proves
// the loader completes when something asks repeatedly -- and it is exactly the
// reason the shipped caller's missing re-drive got through nine gates. The gate's
// driver WAS the handler the caller lacked.
//
// This stage removes the harness from that role. It calls the production entry
// (`SampleBrowser::previewFile`) ONCE, and from then on drives only
// `SampleBrowser::graphicsRoutine()` -- the per-tick surface, with no further
// "user input" of any kind. If the browser ever stops re-asking for a deferred
// preview, it simply never lands and this fails.

/// The file the preview stage loads. Exists only at its regular path, and is
/// asked for by nothing above, so it is never resident when this stage starts.
constexpr const char* kPreviewPath = "SAMPLES/PREVIEW.WAV";

/// Ticks the preview stage may take before it is called wedged. Each is one
/// graphicsRoutine() call plus a dispatch round trip, so this is generous: a
/// healthy load lands within a few.
constexpr int32_t kMaxPreviewTicks = 2000;

// ── The preview cancellation stage ──────────────────────────────────────────
//
// The stage above proves a deferred preview is asked for again until it lands.
// This one proves the opposite requirement, which the same re-drive makes
// necessary: a preview the user has moved OFF must stop being asked for, and
// must never be rendered.
//
// In the real UI the move is onto a folder or an empty selection — the one
// cursor move that dispatches no replacement, so latest-wins supersession never
// fires. This harness calls previewFile() directly and never populates a file
// list, so it cannot reach that branch; it drives the same invalidation through
// `SampleBrowser::cancelPreview()`, which is what that branch calls.
//
// The order below is the defect exactly: preview a file, and before the storage
// owner has run the op, cancel. If the op renders anyway, the browser draws (and
// on a real device sounds) a file the cursor has left, over a folder — and it
// re-arms itself, so it keeps trying every ~15 ms until the load lands.

/// The file the cancellation stage asks for and then abandons. Its own file, asked for by
/// nothing else, so "it drew" and "it loaded" can only be this stage's doing.
constexpr const char* kCancelPath = "SAMPLES/CANCEL.WAV";

/// graphicsRoutine() calls the cancellation stage makes after cancelling.
///
/// Fixed rather than "until something happens": the property is that nothing EVER happens, so
/// there is nothing to wait for. Generous enough for the queued op to have run and reported back
/// several times over — a healthy load takes one or two dispatch round trips.
constexpr int32_t kCancelTicks = 64;

// ── The preview stall stage ─────────────────────────────────────────────────
//
// AB-PREVIEW-STALL. The preview stage proves a deferred preview is asked for again until it lands; this one
// proves that asking again ENDS. A preview whose load is held far past the browser's 600-attempt bound must give
// up -- nothing pending, nothing drawn, one give-up counted -- rather than be asked for on every frame for as long
// as the card takes not to answer. The hold is the loader's host latency knob, which delays each load after it is
// marked in flight, so every collect during it answers PENDING, as a card that never answers would.

/// The file the stall stage previews. Its own file, asked for by nothing else, so a draw of it can only be this
/// stage's doing.
constexpr const char* kStallPath = "SAMPLES/STALL.WAV";

/// How long the loader holds each load during the stall: far past the bound, at about 2 virtual ms a tick.
constexpr uint32_t kStallHoldMs = 30000;

/// graphicsRoutine() calls the stall stage may make before the preview is called wedged. Far past the bound.
constexpr int32_t kMaxStallTicks = 2000;

/// graphicsRoutine() calls after the wait ends, with the hold lifted, in which nothing may be pending again.
constexpr int32_t kStallSettleTicks = 64;

// ── The card-ejection stage ─────────────────────────────────────────────────
//
// Everything the async loader holds is keyed by the nominal PATH, and a
// DelugeAudioLoadResult records nothing about which card produced it. So a header
// parsed from one card and collected after another has been inserted binds the
// NEW card's file (deluge_audio_load_bind_read_stream opens the path) to the OLD
// card's geometry (deluge_audio_load_apply_geometry writes the numbers the result
// carries). It loads clean, reports no error, and plays the wrong thing.
//
// This stage reproduces exactly that sequence and requires it NOT to happen:
//
//   1. Ask for kSwapPath once. That queues a request and returns
//      LOAD_IN_PROGRESS; the stage then stops asking, so the result the loader
//      produces sits in its table UNCOLLECTED -- which is the state the hazard
//      needs and which no other stage here ever creates.
//   2. Drive kWitnessPath until it lands. The loader drains its queue in order and
//      kWitnessPath was queued SECOND, so a collected witness is positive proof
//      that kSwapPath's result has been published. That is what makes this stage
//      deterministic rather than a fixed sleep hoping the parse finished.
//   3. Eject: AudioFileManager::setCardEjected(), which is where the flush lives.
//   4. Put a DIFFERENT file at the same path, and re-insert
//      (reinitEjectedCard() -> deluge_efatfs_remount() -> cardReinserted()).
//   5. Ask for kSwapPath again. It must come back with the NEW file's geometry.
//
// MODELLING NOTE, and it is the honest limit of this fixture: step 4 changes the
// file at the path through the filesystem (unlink + rename), not by swapping the
// block device underneath. The host BSP has no card-swap primitive to use instead
// -- `deluge_bsp::sd`'s host stand-in opens ONE backing file for the whole process
// lifetime behind a `OnceLock` and hard-codes `is_inserted()` to true, so there is
// no image to exchange and no card-detect edge to inject without editing the
// out-of-tree deluge-sdk BSP. What the code under test sees is identical either
// way: it keyed a result by a path, and the file at that path is now a different
// file. The eject/re-insert edges themselves ARE driven, through the same
// setCardEjected()/reinitEjectedCard() pair slowRoutine() drives on the device.

/// The file whose identity changes across the ejection.
constexpr const char* kSwapPath = "SAMPLES/SWAP.WAV";
/// The "other card's" version of it, staged elsewhere and moved into place at the swap.
constexpr const char* kSwapReplacementPath = "SAMPLES/SWAPB.WAV";
/// The file whose arrival proves kSwapPath's result has been published. Queued
/// second, drained second -- see the block comment above.
constexpr const char* kWitnessPath = "SAMPLES/WITNESS.WAV";

// The replacement file's geometry, asserted ABSOLUTELY rather than only compared
// with the reference. The comparison alone would not be enough: a defect that fed
// the reference the same stale state would agree perfectly. These numbers differ from the pre-swap
// file's in both fields (1 channel/2 bytes there, 2/3 here), so a stale geometry
// cannot coincide with a correct one.
constexpr uint8_t kSwapReplacementChannels = 2;
constexpr uint8_t kSwapReplacementByteDepth = 3;
constexpr uint32_t kSwapReplacementDataLength = 6144;

/// Ticks either half of this stage may take before it is called wedged. Same
/// generosity as the other stages: a healthy load takes one or two.
constexpr int32_t kMaxEjectTicks = 2000;

/// Where `runOneEjectTick` is in the sequence above.
enum class EjectStep : uint8_t { Request, AwaitWitness, Swap, Collect, Finished };
EjectStep g_ejectStep = EjectStep::Request;
/// Ticks spent in the current step.
int32_t g_ejectTicks = 0;
/// Deferrals seen while re-collecting kSwapPath AFTER the swap.
///
/// The single most direct witness this gate has that the flush ran. With it, the
/// post-swap collect reports UNKNOWN, re-requests, and therefore defers at least
/// once; without it, the stale result is READY on the very first ask and this
/// stays zero.
int32_t g_ejectCollectDeferrals = 0;
/// Set if the swap itself could not be performed, so a stage that never created
/// the hazard cannot report having survived it.
bool g_ejectSwapFailed = false;
/// Whether the pre-eject request ever deferred -- i.e. whether
/// there was an uncollected result to go stale at all.
int32_t g_ejectRequestDeferrals = 0;
/// The swapped file's reference line, from a synchronous load taken right after the re-insertion.
std::string g_ejectReferenceLine{};

/// Which stage `dispatch_step` is in, in order. `Eject` sits between the case list and the
/// browser-driven stages deliberately — see runOneStep()'s hand-off for why it cannot follow them.
enum class Stage : uint8_t { Cases, Eject, Preview, Cancel, Stall, Done };
Stage g_stage = Stage::Cases;
/// Whether previewFile() has been called. It is called exactly once.
bool g_previewStarted = false;
/// graphicsRoutine() calls made since then.
int32_t g_previewTicks = 0;
/// Of those, how many found a deferred preview still waiting.
int32_t g_previewRedrives = 0;
/// Whether the cancellation stage has issued its previewFile()/cancelPreview() pair.
bool g_cancelStarted = false;
/// graphicsRoutine() calls made since then.
int32_t g_cancelTicks = 0;
/// Where the stall stage is.
enum class StallStep : uint8_t { Start, Wait, Settle, Finished };
StallStep g_stallStep = StallStep::Start;
/// graphicsRoutine() calls made while the stalled preview was pending.
int32_t g_stallTicks = 0;
/// graphicsRoutine() calls made after the wait ended.
int32_t g_stallSettleTicks = 0;
/// SampleBrowser::previewGiveUps() when the stage began.
int32_t g_stallGiveUpsBefore = 0;
/// Whether the preview was still pending when the wait ended.
bool g_stallPendingAtEnd = false;

/// Plain CRC32 (IEEE 802.3, reflected) — a stable fingerprint of the bytes read back through the
/// Sample's own stream. Same routine host_loadcheck_main.cpp uses for band data.
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

/// Read the first bytes of the Sample back through its OWN read source and fingerprint them.
///
/// This is the half of "identical geometry" that a field-by-field comparison cannot give: a build
/// whose read stream was never bound loads with no error and then plays silence forever, and two
/// builds that opened DIFFERENT files agree on every geometry field whenever those files happen to
/// share a header shape. The fingerprint covers the file's header and the first data bytes, and the
/// fixture gives every file a distinct data fill, so it says which file this actually is.
///
/// @param s The published Sample.
/// @return The CRC32 of the first 64 bytes, or 0 if no read source could be made or the read failed
///         (both of which are themselves reported by the caller, since 0 is also a legal CRC).
uint32_t streamFingerprint(Sample& s, bool& readable) {
	std::array<std::byte, 64> probe{};
	auto readSource = s.stream().make_read_source();
	if (readSource == nullptr || !readSource->read(0, probe).has_value()) {
		readable = false;
		return 0;
	}
	readable = true;
	return crc32(probe.data(), probe.size());
}

/// One load's whole outcome as a single stable line: the Error, the file chosen, the whole parsed geometry, a CRC of
/// the bytes read back through the Sample's own stream, and the alternate-dir status the load left behind.
///
/// Every field must match between the in-process synchronous reference and the async load of the same file. Nothing
/// that legitimately differs between them (deferral counts, timings, pointers) appears on this line.
///
/// @param tag   The line prefix: `AB-CASE` for the case list, `AB-EJECT` for the card-swap stage. Kept apart because
///              the script counts `AB-CASE` lines against a floor, and a swap line would hide a shrunk case list.
/// @param label Short, stable, and independent of the path.
/// @param altDirStatus The alternate-dir status the load left behind.
std::string caseLine(const char* tag, const char* label, AudioFile* file, Error error, const std::string& fileNameAfter,
                     AlternateLoadDirStatus altDirStatus) {
	char line[1024];
	if (file == nullptr) {
		snprintf(line, sizeof(line), "%s %-9s | error=%d | file=none | asked=\"%s\" | altDirStatus=%d", tag, label,
		         static_cast<int>(error), fileNameAfter.c_str(), static_cast<int>(altDirStatus));
		return line;
	}

	auto& s = static_cast<Sample&>(*file);
	bool readable = false;
	const uint32_t fingerprint = streamFingerprint(s, readable);
	if (!readable) {
		printf("FAIL [audioLoadAB/%s]: the published Sample cannot be read -- its stream was never bound, so it "
		       "would load without error and play silence\n",
		       label);
		g_failures++;
	}

	snprintf(line, sizeof(line),
	         "%s %-9s | error=%d | file=\"%s\" | alt=\"%s\" | asked=\"%s\" | channels=%u byteDepth=%u "
	         "rawFormat=%u rate=%u dataStart=%u dataLen=%llu lengthSamples=%llu loop=%u/%u wtCycle=%u midiNote=%.4f | "
	         "readable=%d streamCrc=%08X | altDirStatus=%d",
	         tag, label, static_cast<int>(error), s.filePath.c_str(), s.loadedFromAlternatePath.c_str(),
	         fileNameAfter.c_str(), s.numChannels, s.byteDepth, static_cast<unsigned>(s.rawDataFormat), s.sampleRate,
	         s.audioDataStartPosBytes, static_cast<unsigned long long>(s.audioDataLengthBytes),
	         static_cast<unsigned long long>(s.lengthInSamples), s.fileLoopStartSamples, s.fileLoopEndSamples,
	         s.waveTableCycleSize, static_cast<double>(s.midiNoteFromFile), readable ? 1 : 0, fingerprint,
	         static_cast<int>(altDirStatus));
	return line;
}

/// Load @p path through the synchronous card path, as the reference its async load must match, and drop it again.
///
/// Runs on the storage-owner worker fiber, where a blocking card read is legal. It loads under a copy of the load's
/// context: the context is per-load state that each load moves, and the async load has to start from the state this
/// one started from, or the two would be proposed different candidates.
std::string referenceLine(const char* tag, const char* label, const char* path) {
	deluge::storage::AudioLoadContext referenceContext = g_loadContext;
	std::string fileName = path;
	Error error = Error::NONE;
	AudioFile* file =
	    audioFileManager.getAudioFileFromFilename(fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false,
	                                              /*mayLoadAsync=*/false, &referenceContext);
	std::string line = caseLine(tag, label, file, error, fileName, referenceContext.alternateDirStatus);
	if (file != nullptr) {
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path);
	}
	return line;
}

/// Print the async load's line, and fail if it differs from the reference taken before it.
void printAgainstReference(const std::string& line, const std::string& reference, const char* label) {
	printf("%s\n", line.c_str());
	if (line != reference) {
		printf("AB-REF %s\n", reference.c_str());
		printf("FAIL [audioLoadAB/%s]: the async load differs from the in-process synchronous load of the same file "
		       "(AB-REF above is the reference)\n",
		       label);
		g_failures++;
	}
}

/// Print the per-run facts the reference has no counterpart for, and assert that the loads really went through the
/// async loader: that it is active, and that every case answered LOAD_IN_PROGRESS at least once before settling,
/// rather than being served synchronously (or from memory) with the loader never consulted.
void printSummary() {
	const bool active = deluge_audio_load_async_active();
	printf("AB-SUMMARY asyncActive=%d cases=%zu deferrals=%d failures=%d\n", active ? 1 : 0, kCases.size(),
	       g_deferralsTotal, g_failures);

	printf("AB-DEFER");
	for (size_t i = 0; i < kCases.size(); i++) {
		printf(" %s=%d", kCases[i].label, g_deferralsByCase[i]);
	}
	printf("\n");

	if (active) {
		// EVERY case, not merely some case. See g_deferralsByCase's comment for the fall-through this
		// catches and the run-wide total misses.
		for (size_t i = 0; i < kCases.size(); i++) {
			if (g_deferralsByCase[i] == 0) {
				printf("FAIL [audioLoadAB]: the async backing is active but case '%s' never deferred -- it was "
				       "answered on the synchronous path, so it is not under test at all\n",
				       kCases[i].label);
				g_failures++;
			}
		}
	}
	// Absolute, not comparative: see g_selfdirAlternatePath. The reference and the async load both
	// resolve this candidate positionally, and a regression in the shared resolver would take the two
	// of them down together, agreeing perfectly.
	// The VALUE, not merely non-emptiness: a case that happens to resolve from some other alternate
	// candidate would satisfy "not empty" while proving nothing about this one.
	if (g_selfdirAlternatePath != kSelfdirAlternate) {
		printf("FAIL [audioLoadAB/%s]: the sample living IN the alternate dir recorded "
		       "loadedFromAlternatePath \"%s\", expected \"%.*s\" -- the winning candidate was read by "
		       "comparing paths rather than by slot, so a collect-media save would leave it behind\n",
		       kCases[kSelfdirIndex].label, g_selfdirAlternatePath.c_str(), static_cast<int>(kSelfdirAlternate.size()),
		       kSelfdirAlternate.data());
		g_failures++;
	}
	if (!active) {
		printf("FAIL [audioLoadAB]: the async loader is not active, so no load in this run went through it\n");
		g_failures++;
	}
	// Reprint with the summary's own failures folded in, so a reader of the last line sees the real
	// count rather than the count as it stood before these two checks ran.
	printf("AB-RESULT failures=%d\n", g_failures);
	fflush(stdout);
}

/// One load attempt for the current case, on the storage-owner worker fiber.
///
/// Either it defers (LOAD_IN_PROGRESS: return, the platform driver lets the loader task run and
/// dispatches again) or it settles the case: record the line, check the invariants, drop the file's
/// residency so nothing later is answered from memory, and advance.
void runOneStep() {
	const Case& c = kCases[g_caseIndex];

	if (!g_referenceTaken) {
		// The reference first, before this case's first async ask, so both start from the same state.
		g_referenceLine = referenceLine("AB-CASE", c.label, c.path);
		if (!checkLoadInvariants(c.label)) {
			g_failures++;
		}
		g_referenceTaken = true;
	}

	// getAudioFileFromFilename may REWRITE fileName to the location the file was actually found at,
	// so it takes a fresh copy each attempt and the rewritten value is printed as `asked=`.
	std::string fileName = c.path;
	Error error = Error::NONE;
	AudioFile* file =
	    audioFileManager.getAudioFileFromFilename(fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false,
	                                              /*mayLoadAsync=*/true, &g_loadContext);

	if (error == Error::LOAD_IN_PROGRESS) {
		g_deferralsThisCase++;
		g_deferralsTotal++;
		if (g_deferralsThisCase <= kMaxDeferrals) {
			return; // Ask again next tick — the contract LOAD_IN_PROGRESS imposes on its caller.
		}
		printf("FAIL [audioLoadAB/%s]: still LOAD_IN_PROGRESS after %d attempts -- the load never completed\n", c.label,
		       g_deferralsThisCase);
		g_failures++;
		// Fall through and record it as wedged, so the run finishes and both builds' lines stay
		// comparable rather than one of them ending early.
		file = nullptr;
		error = Error::LOAD_IN_PROGRESS;
	}

	printAgainstReference(caseLine("AB-CASE", c.label, file, error, fileName, g_loadContext.alternateDirStatus),
	                      g_referenceLine, c.label);

	if (g_caseIndex == kSelfdirIndex && file != nullptr) {
		g_selfdirAlternatePath = static_cast<Sample&>(*file).loadedFromAlternatePath;
	}

	if (!checkLoadInvariants(c.label)) {
		g_failures++;
	}

	// Drop residency before the next case. Nothing here shares a path, so this is not what keeps the
	// comparison honest on its own -- but a resident AudioFile short-circuits the load before the
	// async gate is even consulted, and leaving the cache to grow across cases would make that
	// short-circuit one edit away.
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(c.path);

	g_deferralsByCase[g_caseIndex] = g_deferralsThisCase;
	g_caseIndex++;
	g_deferralsThisCase = 0;
	g_referenceTaken = false;

	if (g_caseIndex == kCases.size()) {
		// The ejection stage next, not the browser ones. It runs on this same worker fiber, and it has
		// to run BEFORE the preview stage: a previewed sample stays leased for as long as it is on
		// screen, so checkLoadInvariants' leaked-reason half would report that legitimate lease as a
		// failure for every load attempted afterwards.
		g_stage = Stage::Eject;
	}
}

/// Has the preview actually DRAWN its waveform for `kPreviewPath`?
///
/// `waveformBasicNavigator.sample` is assigned by `SampleBrowser::renderPreviewForTarget` on the one
/// path that renders the waveform, so a non-null pointer naming this file is direct evidence that
/// the preview reached the screen -- not merely that the file became resident somewhere. That
/// distinction is the whole point here: the defect this stage exists for left the file unloaded, the
/// waveform undrawn and no error shown, which from outside looks identical to "the user did not
/// scroll here".
bool previewHasDrawn() {
	const Sample* drawn = waveformBasicNavigator.sample;
	return drawn != nullptr && drawn->filePath == kPreviewPath;
}

/// One tick of the preview stage. See the Stage/kPreviewPath comments above.
void runOnePreviewTick() {
	if (!g_previewStarted) {
		// The ONE user-driven action of this stage: the browser is told which file to preview,
		// exactly as a cursor move tells it. Nothing below ever calls this again.
		g_previewStarted = true;
		sampleBrowser.previewFile(kPreviewPath, /*movementDirection=*/0);
		return;
	}

	g_previewTicks++;
	if (sampleBrowser.previewLoadPending()) {
		g_previewRedrives++;
	}
	// The per-tick surface, and the ONLY thing driven from here on.
	sampleBrowser.graphicsRoutine();
}

/// Print the preview stage's outcome and assert it.
///
/// Not compared against a reference: the synchronous path never defers, so its re-drive counts have nothing to say.
void finishPreviewStage(bool drawn) {
	printf("AB-PREVIEW asyncActive=%d drawn=%d ticks=%d redrives=%d pending=%d\n",
	       deluge_audio_load_async_active() ? 1 : 0, drawn ? 1 : 0, g_previewTicks, g_previewRedrives,
	       sampleBrowser.previewLoadPending() ? 1 : 0);

	if (!drawn) {
		printf("FAIL [audioLoadAB/preview]: after ONE previewFile() and %d graphicsRoutine() tick(s) with no "
		       "further input, the preview never sounded or drew '%s'. A deferred preview that nothing asks "
		       "for again is silent, blank and errorless -- indistinguishable to the user from a file that "
		       "does not exist.\n",
		       g_previewTicks, kPreviewPath);
		g_failures++;
	}
	if (sampleBrowser.previewLoadPending()) {
		printf("FAIL [audioLoadAB/preview]: a deferral is STILL pending after this stage -- either it was never "
		       "answered (see above), or the re-drive state is not cleared once it lands and will fire again "
		       "over whatever the user looks at next\n");
		g_failures++;
	}
	// The preview must have gone through the deferred path at least once, or this stage tested
	// nothing: a preview that landed synchronously exercises no re-drive at all, and the check would
	// pass with the surface it exists to guard removed entirely.
	if (g_previewRedrives == 0) {
		printf("FAIL [audioLoadAB/preview]: the preview never deferred, so the re-drive was never under "
		       "test\n");
		g_failures++;
	}
}

/// Has anything drawn the cancelled file's waveform?
///
/// Same witness as previewHasDrawn(): `waveformBasicNavigator.sample` is assigned only on
/// `renderPreviewForTarget`'s waveform path. It still names the PREVIOUS stage's file, which is
/// exactly right -- what must not have happened is this file replacing it.
bool cancelledHasDrawn() {
	const Sample* drawn = waveformBasicNavigator.sample;
	return drawn != nullptr && drawn->filePath == kCancelPath;
}

/// Was the cancelled file loaded (and, on a device, sounded) at all?
///
/// `lastFilePathLoaded` is assigned at the very top of `renderPreviewForTarget`, before the load
/// and before anything is drawn -- so it witnesses the op having done the work at all, which is
/// the half `cancelledHasDrawn()` misses when the first attempt defers.
/// `AudioEngine::previewSample`, which that same function calls, is what starts the note.
bool cancelledWasLoaded() {
	return sampleBrowser.lastFilePathLoaded == kCancelPath;
}

/// One tick of the cancellation stage. See the block comment above kCancelPath.
void runOneCancelTick() {
	if (!g_cancelStarted) {
		g_cancelStarted = true;
		// The user-driven preview, exactly as a cursor move issues it. This QUEUES the op on the
		// storage owner; nothing below runs it yet.
		sampleBrowser.previewFile(kCancelPath, /*movementDirection=*/0);
		// And then the cursor moves onto something with nothing to preview, before that op has run.
		sampleBrowser.cancelPreview();
		return;
	}

	g_cancelTicks++;
	// The per-tick surface, and the only thing driven from here on -- the same surface that
	// re-drives a deferral. If the cancelled preview re-arms itself, this is what keeps it alive.
	sampleBrowser.graphicsRoutine();
}

/// Print the cancellation stage's outcome and assert it.
void finishCancelStage() {
	const bool drawn = cancelledHasDrawn();
	const bool loaded = cancelledWasLoaded();
	printf("AB-CANCEL asyncActive=%d drawn=%d loaded=%d ticks=%d pending=%d\n",
	       deluge_audio_load_async_active() ? 1 : 0, drawn ? 1 : 0, loaded ? 1 : 0, g_cancelTicks,
	       sampleBrowser.previewLoadPending() ? 1 : 0);

	if (drawn) {
		printf("FAIL [audioLoadAB/cancel]: the preview was cancelled before its load ran, and its waveform "
		       "was drawn anyway -- on the device that is a file the cursor has LEFT, rendered over the "
		       "folder the user moved onto, with its note-on no longer suppressed\n");
		g_failures++;
	}
	if (loaded) {
		printf("FAIL [audioLoadAB/cancel]: the preview was cancelled before its load ran, and the load ran "
		       "anyway (lastFilePathLoaded is '%s') -- the op acted on an abandoned target instead of "
		       "checking whether it was still wanted\n",
		       kCancelPath);
		g_failures++;
	}
	if (sampleBrowser.previewLoadPending()) {
		printf("FAIL [audioLoadAB/cancel]: a cancelled preview is STILL pending after %d tick(s) -- it re-armed "
		       "itself and will be re-driven every frame until it lands, and then rendered\n",
		       g_cancelTicks);
		g_failures++;
	}
}

/// Has anything drawn the stalled file's waveform? Same witness as previewHasDrawn().
bool stallHasDrawn() {
	const Sample* drawn = waveformBasicNavigator.sample;
	return drawn != nullptr && drawn->filePath == kStallPath;
}

/// One tick of the stall stage. See the block comment above kStallPath.
void runOneStallTick() {
	switch (g_stallStep) {
	case StallStep::Start:
		deluge_audio_load_set_latency_ms(kStallHoldMs);
		g_stallGiveUpsBefore = sampleBrowser.previewGiveUps();
		// The ONE user-driven action of this stage. The load it asks for is held from here on.
		sampleBrowser.previewFile(kStallPath, /*movementDirection=*/0);
		g_stallStep = StallStep::Wait;
		return;

	case StallStep::Wait:
		// Read at the top of the tick, before the re-drive: the op graphicsRoutine() queues runs after this
		// returns, so "pending" is only settled here.
		if (sampleBrowser.previewLoadPending() && g_stallTicks < kMaxStallTicks) {
			g_stallTicks++;
			sampleBrowser.graphicsRoutine();
			return;
		}
		g_stallPendingAtEnd = sampleBrowser.previewLoadPending();
		deluge_audio_load_set_latency_ms(0);
		g_stallStep = StallStep::Settle;
		return;

	case StallStep::Settle:
		if (g_stallSettleTicks < kStallSettleTicks) {
			g_stallSettleTicks++;
			sampleBrowser.graphicsRoutine();
			return;
		}
		g_stallStep = StallStep::Finished;
		return;

	case StallStep::Finished:
		return;
	}
}

/// Print the stall stage's outcome and assert it.
void finishStallStage() {
	const int32_t giveUps = sampleBrowser.previewGiveUps() - g_stallGiveUpsBefore;
	const bool drawn = stallHasDrawn();
	const bool pendingAfter = sampleBrowser.previewLoadPending();
	printf("AB-PREVIEW-STALL ticks=%d giveUps=%d pendingAtEnd=%d drawn=%d pendingAfter=%d\n", g_stallTicks, giveUps,
	       g_stallPendingAtEnd ? 1 : 0, drawn ? 1 : 0, pendingAfter ? 1 : 0);

	if (g_stallPendingAtEnd || giveUps != 1) {
		printf("FAIL [audioLoadAB/stall]: a preview whose load was held %u ms did not give up once (pending=%d after "
		       "%d graphicsRoutine() tick(s), %d give-up(s)) -- a load that never answers would be asked for on every "
		       "frame, silent and undrawn, for as long as the card takes\n",
		       static_cast<unsigned>(kStallHoldMs), g_stallPendingAtEnd ? 1 : 0, g_stallTicks, giveUps);
		g_failures++;
	}
	if (drawn) {
		printf("FAIL [audioLoadAB/stall]: '%s' was drawn, but its load was held for the whole stage\n", kStallPath);
		g_failures++;
	}
	if (pendingAfter) {
		printf("FAIL [audioLoadAB/stall]: a preview that gave up is pending again after %d tick(s) with the hold "
		       "lifted -- it re-armed itself\n",
		       g_stallSettleTicks);
		g_failures++;
	}
}

/// One load attempt for the ejection stage. Returns the resolved file (or nullptr), and reports
/// through @p deferred whether the loader answered LOAD_IN_PROGRESS instead of settling.
///
/// Residency is dropped on every settled attempt: a resident AudioFile short-circuits
/// getAudioFileFromFilename before the async gate is even consulted, and this stage asks for the
/// same path twice with a card swap in between — exactly where a leftover resident would answer the
/// second ask from memory and prove nothing.
AudioFile* ejectAttempt(const char* path, Error& error, bool& deferred) {
	std::string fileName = path;
	error = Error::NONE;
	AudioFile* file =
	    audioFileManager.getAudioFileFromFilename(fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false,
	                                              /*mayLoadAsync=*/true, &g_loadContext);
	deferred = (error == Error::LOAD_IN_PROGRESS);
	return file;
}

/// Put the "other card's" file at kSwapPath: the same path, a different file.
///
/// See the block comment above kSwapPath for why this is an unlink+rename rather than a block-device
/// swap, and why the code under test cannot tell the difference.
bool performCardSwap() {
	if (!deluge::io::unlink(kSwapPath).has_value()) {
		printf("FAIL [audioLoadAB/eject]: could not remove '%s' to stage the swapped card\n", kSwapPath);
		return false;
	}
	if (!deluge::io::rename(kSwapReplacementPath, kSwapPath).has_value()) {
		printf("FAIL [audioLoadAB/eject]: could not move '%s' into place at '%s'\n", kSwapReplacementPath, kSwapPath);
		return false;
	}
	return true;
}

/// One tick of the ejection stage. See the block comment above kSwapPath for the sequence.
void runOneEjectTick() {
	g_ejectTicks++;
	if (g_ejectTicks > kMaxEjectTicks) {
		printf("FAIL [audioLoadAB/eject]: wedged after %d tick(s) in step %d\n", g_ejectTicks,
		       static_cast<int>(g_ejectStep));
		g_failures++;
		g_ejectStep = EjectStep::Finished;
		return;
	}

	switch (g_ejectStep) {

	case EjectStep::Request: {
		// kSwapPath FIRST, so the loader's FIFO queue drains it before the witness. Its outcome is
		// deliberately discarded: it defers, and this stage then never asks again,
		// which is what leaves an uncollected result behind to go stale.
		Error error = Error::NONE;
		bool deferred = false;
		AudioFile* file = ejectAttempt(kSwapPath, error, deferred);
		g_ejectRequestDeferrals += deferred ? 1 : 0;
		if (file != nullptr) {
			// Answered at once (a declined request falls to the synchronous path). Drop it so the
			// post-swap ask is a real load.
			audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kSwapPath);
		}
		bool witnessDeferred = false;
		(void)ejectAttempt(kWitnessPath, error, witnessDeferred);
		g_ejectStep = EjectStep::AwaitWitness;
		g_ejectTicks = 0;
		return;
	}

	case EjectStep::AwaitWitness: {
		Error error = Error::NONE;
		bool deferred = false;
		AudioFile* file = ejectAttempt(kWitnessPath, error, deferred);
		if (deferred) {
			return; // Still coming. Ask again next tick — never kSwapPath.
		}
		if (file == nullptr) {
			printf("FAIL [audioLoadAB/eject]: the witness file '%s' did not load (error=%d), so this stage cannot "
			       "know whether '%s' was ever parsed\n",
			       kWitnessPath, static_cast<int>(error), kSwapPath);
			g_failures++;
			g_ejectStep = EjectStep::Finished;
			return;
		}
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kWitnessPath);
		g_ejectStep = EjectStep::Swap;
		g_ejectTicks = 0;
		return;
	}

	case EjectStep::Swap:
		// The ejection, through the production entry point. This is the call the flush lives in, and
		// it must run BEFORE the new file appears — flushing on re-insertion instead would leave the
		// window open for as long as the slot stayed empty.
		audioFileManager.setCardEjected();
		if (!performCardSwap()) {
			g_ejectSwapFailed = true;
			g_failures++;
			g_ejectStep = EjectStep::Finished;
			return;
		}
		// And the re-insertion: remount against the "new" card and re-validate residents, exactly as
		// slowRoutine() drives it on the device.
		audioFileManager.reinitEjectedCard();
		// The reference for the post-swap load: the file that is on the card NOW, read synchronously.
		g_ejectReferenceLine = referenceLine("AB-EJECT", "swap", kSwapPath);
		g_ejectStep = EjectStep::Collect;
		g_ejectTicks = 0;
		return;

	case EjectStep::Collect: {
		Error error = Error::NONE;
		bool deferred = false;
		AudioFile* file = ejectAttempt(kSwapPath, error, deferred);
		if (deferred) {
			g_ejectCollectDeferrals++;
			return;
		}
		// The one line this stage compares with its reference, and the one that carries the whole property: the
		// geometry printed here must be the file that is on the card NOW.
		printAgainstReference(
		    caseLine("AB-EJECT", "swap", file, error, std::string{kSwapPath}, g_loadContext.alternateDirStatus),
		    g_ejectReferenceLine, "eject");
		if (!checkLoadInvariants("audioLoadAB/eject")) {
			g_failures++;
		}
		if (file == nullptr) {
			printf("FAIL [audioLoadAB/eject]: '%s' did not load after the card swap (error=%d)\n", kSwapPath,
			       static_cast<int>(error));
			g_failures++;
		}
		else {
			auto& s = static_cast<Sample&>(*file);
			if (s.numChannels != kSwapReplacementChannels || s.byteDepth != kSwapReplacementByteDepth
			    || s.audioDataLengthBytes != kSwapReplacementDataLength) {
				printf("FAIL [audioLoadAB/eject]: '%s' loaded with channels=%u byteDepth=%u dataLen=%llu, but the "
				       "file at that path after the swap is channels=%u byteDepth=%u dataLen=%u -- a header parsed "
				       "from the PREVIOUS card was applied to this one's file, which loads with no error and plays "
				       "the wrong thing\n",
				       kSwapPath, s.numChannels, s.byteDepth, static_cast<unsigned long long>(s.audioDataLengthBytes),
				       kSwapReplacementChannels, kSwapReplacementByteDepth, kSwapReplacementDataLength);
				g_failures++;
			}
			audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kSwapPath);
		}
		g_ejectStep = EjectStep::Finished;
		return;
	}

	case EjectStep::Finished:
		return;
	}
}

/// Print the ejection stage's outcome and assert it.
void finishEjectStage() {
	const bool active = deluge_audio_load_async_active();
	printf("AB-EJECT-SUMMARY asyncActive=%d swapped=%d requestDeferrals=%d collectDeferrals=%d\n", active ? 1 : 0,
	       g_ejectSwapFailed ? 0 : 1, g_ejectRequestDeferrals, g_ejectCollectDeferrals);

	// Without this, the stage could "pass" having never created the hazard at all: a pre-eject ask
	// that was answered synchronously (a permanent request decline falls through to the sync path)
	// leaves no uncollected result behind, and the post-swap load is then trivially correct.
	if (g_ejectRequestDeferrals == 0) {
		printf("FAIL [audioLoadAB/eject]: the pre-ejection ask for '%s' never deferred, so no uncollected result was "
		       "left for the ejection to invalidate -- this stage tested nothing\n",
		       kSwapPath);
		g_failures++;
	}
	// The direct witness that the flush ran. A flushed path reports UNKNOWN, which makes the
	// collector request it again and therefore defer at least once. A STALE result is READY on the
	// very first ask and defers not at all -- so zero here is the bug, whatever the geometry check
	// happened to say.
	if (!g_ejectSwapFailed && g_ejectCollectDeferrals == 0) {
		printf("FAIL [audioLoadAB/eject]: the post-swap collect for '%s' was answered without a single deferral, so "
		       "it came from a result parsed BEFORE the ejection rather than from the card now in the slot\n",
		       kSwapPath);
		g_failures++;
	}
}

} // namespace

extern "C" {

void deluge_scenario_audio_load_ab_begin(void) {
	g_caseIndex = 0;
	g_deferralsThisCase = 0;
	g_deferralsTotal = 0;
	g_deferralsByCase.fill(0);
	g_failures = 0;
	g_stepDone = true;
	g_summaryPrinted = false;
	g_stage = Stage::Cases;
	g_previewStarted = false;
	g_previewTicks = 0;
	g_previewRedrives = 0;
	g_cancelStarted = false;
	g_cancelTicks = 0;
	g_stallStep = StallStep::Start;
	g_stallTicks = 0;
	g_stallSettleTicks = 0;
	g_stallGiveUpsBefore = 0;
	g_stallPendingAtEnd = false;
	g_ejectStep = EjectStep::Request;
	g_ejectTicks = 0;
	g_ejectCollectDeferrals = 0;
	g_ejectRequestDeferrals = 0;
	g_ejectSwapFailed = false;
	g_selfdirAlternatePath.clear();
	g_referenceLine.clear();
	g_referenceTaken = false;
	g_ejectReferenceLine.clear();

	// The preview stage must not start a voice: this harness has no audio path driving it, and the
	// property under test is that a DEFERRED preview is asked for again, which is upstream of
	// sounding. This is the same flag the UI itself sets to suppress preview audio.
	instrumentClipView.fileBrowserShouldNotPreview = true;

	// The state a song load starts from, which is the only state in which the alternate-dir ordering
	// this compares means anything: the directory MIGHT exist, and nothing has probed it yet.
	g_loadContext = {.alternateDir = "SAMPLES/ALT",
	                 .alternateDirStatus = AlternateLoadDirStatus::MIGHT_EXIST,
	                 .thing = ThingType::SONG};

	// Line-buffer stdout for the rest of the run. The platform driver ends the process with a raw
	// `exit_group` syscall (golden_vt_render's `hard_exit`, which deliberately skips C++ static
	// destructors), and that flushes NOTHING of C's stdio — so a block-buffered stdout, which is
	// what stdout is whenever this harness's output is piped or redirected, discards every line
	// this scenario printed. Losing exactly the output the gate reads.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	printf("AB-BEGIN asyncActive=%d cases=%zu\n", deluge_audio_load_async_active() ? 1 : 0, kCases.size());
}

bool deluge_scenario_audio_load_ab_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_audio_load_ab_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}

	if (g_stage == Stage::Preview || g_stage == Stage::Cancel || g_stage == Stage::Stall) {
		// The browser-driven stages run on the UI tier, not the worker fiber -- that is where the
		// real browser calls these from, and previewFile()/graphicsRoutine() dispatch their own card
		// work onto the owner themselves. A marker op behind them releases the latch: the owner queue
		// is FIFO, so it runs after whatever they just queued (and immediately if they queued
		// nothing). That FIFO ordering is also what makes the cancellation stage's window real: the
		// op previewFile() queues has not run when cancelPreview() is called.
		g_stepDone = false;
		if (g_stage == Stage::Preview) {
			runOnePreviewTick();
			const bool drawn = previewHasDrawn();
			if (drawn || g_previewTicks >= kMaxPreviewTicks) {
				finishPreviewStage(drawn);
				g_stage = Stage::Cancel;
			}
		}
		else if (g_stage == Stage::Cancel) {
			runOneCancelTick();
			if (g_cancelTicks >= kCancelTicks) {
				finishCancelStage();
				g_stage = Stage::Stall;
			}
		}
		else {
			runOneStallTick();
			if (g_stallStep == StallStep::Finished) {
				finishStallStage();
				g_stage = Stage::Done;
				printSummary();
				g_summaryPrinted = true;
			}
		}
		if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
			g_stepDone = true;
		}
		return;
	}

	g_stepDone = false;
	// Onto the storage-owner worker fiber: the attempt opens files and binds a read stream, which is
	// card I/O and has to run where card I/O is allowed. Same dispatch shape as
	// deluge_scenario_start_recorder_roundtrip. The ejection stage goes here too — it also renames a
	// file and remounts the volume.
	const bool queued = deluge::storage::Owner::run(
	    [](void*) {
		    if (g_stage == Stage::Eject) {
			    runOneEjectTick();
			    if (g_ejectStep == EjectStep::Finished) {
				    finishEjectStage();
				    g_stage = Stage::Preview;
			    }
		    }
		    else {
			    runOneStep();
		    }
		    g_stepDone = true;
	    },
	    nullptr);
	if (!queued) {
		// The worker queue was full, so the closure will NOT run: release the latch rather than
		// letting the platform driver wait forever for a step that was dropped.
		g_stepDone = true;
	}
}

bool deluge_scenario_audio_load_ab_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_audio_load_ab_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
