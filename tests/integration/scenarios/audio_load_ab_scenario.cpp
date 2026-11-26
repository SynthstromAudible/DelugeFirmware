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
/// @brief Host-only C ABI for the sync-vs-async load A/B: run ONE whole song-shaped load of a fixed
///        sample list through `AudioFileManager::getAudioFileFromFilename` and print each file's
///        resolved identity and geometry as a stable line, checked against a synchronous reference.
///
/// Every case loads twice in one process: first through the synchronous card path (`AsyncLoadMode::Blocking`), as the
/// reference, and then through the Rust Embassy loader (`libdeluge/audio_load.h`'s request/collect pair). The async
/// load's `AB-CASE` line must equal the reference's. `AB-SUMMARY` carries the per-run facts, such as deferral counts,
/// that the reference has no counterpart for.
///
/// The whole point is that everything else must be identical: same file chosen, same alternate-dir
/// resolution, same geometry, same `Error` on failure, same bytes readable back through the
/// Sample's own stream.
///
/// @note Runs as a STEP MACHINE rather than one blocking call, because that is what the async
///       backing requires: a deferred load answers `Error::LOAD_IN_PROGRESS` and the caller must
///       return to its own loop so the Embassy loader task can be polled at all. Each
///       `dispatch_step()` arms one attempt, which runs when the driver next polls
///       `step_done()`, and returns; the platform driver waits for `step_done()`, lets its executor run,
///       and dispatches again.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the
///       app's sources), so the Embassy host binary, which links `deluge_app`, can reach it. Inert unless called.

#include "deluge_storage_sample_peaks.h"
#include "scenarios/support/scenario_declare.h"
#include "scenarios/support/scenario_report.h"

#include "definitions_cxx.hpp"
#include "gui/ui/browser/sample_preview.h" // the loader the scenes browser previews through
#include "gui/ui/browser_host.h"           // shellPreviewer
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h" // fileBrowserShouldNotPreview
#include "io/file.hpp"                      // the card swap's unlink + rename
#include "libdeluge/audio_load.h"           // deluge_audio_load_set_latency_ms
#include "model/clip/clip.h"
#include "model/output.h"
#include "model/sample/last_file_path_loaded.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "runtime/app_loop.h"                // nowUs, the stall bounds' clock
#include "scenarios/guest_drive.h"           // the shell guest: its tick, its browser
#include "scenarios/load_invariants.h"       // checkLoadInvariants -- shared with load_policy
#include "storage/audio/async_sample_load.h" // AsyncSampleLoad::kMaxStalledUs
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h" // bytes::interactionTierIo
#include "ui/scenes/browser/browser_scene.h"
#include "util/menu_entry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// The GUI headers above reach `third_party/printf/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which this harness does not provide, so
// the alias turns every line this file writes into an undefined symbol at link time. Every one of those
// lines IS the gate's output (the script reads them), so they must go to real stdout regardless of
// which headers a future edit pulls in.
#ifdef printf
#undef printf
#endif

DELUGE_SCENARIO_DECLARE(audio_load_ab);

namespace {

using deluge::scenarios::ScenarioReport;
using deluge::scenarios::Wait;
using deluge::scenarios::WaitState;

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
/// - `bad_reg` is garbage at the regular path with a good copy in the alternate directory, asked for while the
///   status is still MIGHT_EXIST. A parse verdict is not a miss, so it must fail as FILE_UNSUPPORTED and leave the
///   status unpromoted, never retrying in the alternate directory. Both backings share that rule, so the check is
///   absolute (see g_badRegError).
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
constexpr std::array<Case, 11> kCases{{
    {"both", "SAMPLES/BOTH.WAV"},
    {"bad_reg", "SAMPLES/BADREG.WAV"},
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
/// async load takes one or two. A retry bound inside the case, which its own FAIL line judges, not a wait.
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
/// `both` and `bad_reg`: asked for before the alternate dir is probed, and checked absolutely below for leaving it
/// unprobed. Tied to their paths like kSelfdirIndex.
constexpr size_t kBothIndex = 0;
constexpr size_t kBadRegIndex = 1;
static_assert(std::string_view{kCases[kBothIndex].path} == "SAMPLES/BOTH.WAV", "kBothIndex no longer names `both`");
static_assert(std::string_view{kCases[kBadRegIndex].path} == "SAMPLES/BADREG.WAV",
              "kBadRegIndex no longer names `bad_reg`");
/// The alternate-dir status each left behind, and `bad_reg`'s Error and recorded alternate path. A regular-path hit
/// and a parse verdict are both final: neither may probe the alternate dir, so the status must still be MIGHT_EXIST,
/// and `bad_reg` must fail as FILE_UNSUPPORTED rather than load its good copy from there. The AB-CASE comparison
/// cannot see this, since the synchronous reference shares the rule.
AlternateLoadDirStatus g_bothStatusAfter = AlternateLoadDirStatus::MIGHT_EXIST;
AlternateLoadDirStatus g_badRegStatusAfter = AlternateLoadDirStatus::MIGHT_EXIST;
Error g_badRegError = Error::NONE;
bool g_badRegLoaded = false;
/// The index of `selfdir` in kCases, whose one property is checked absolutely below.
constexpr size_t kSelfdirIndex = 4;
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
ScenarioReport g_report{"AB"};
/// The load every case belongs to: a song's, whose alternate directory is SAMPLES/ALT. Set by begin().
deluge::storage::AudioLoadContext g_loadContext{};
/// Set false by dispatch_step(), true by the dispatched closure on its way out.
bool g_stepDone = true;
/// A cases or ejection step dispatched and not yet run. See deluge_scenario_audio_load_ab_dispatch_step.
bool g_cardStepPending = false;
/// Set once the summary has been printed, so complete() latches.
bool g_summaryPrinted = false;

// ── The preview re-drive stage ──────────────────────────────────────────────
//
// Everything above drives the loader through its OWN retry loop: `runOneStep`
// returns on LOAD_IN_PROGRESS and the platform driver calls it again. That proves
// the loader completes when something asks repeatedly, and nothing about whether a
// real caller asks again: a caller missing its re-drive passes every such check,
// because the gate's driver IS the handler that caller lacks.
//
// This stage removes the harness from that role. It calls the production entry
// (`SamplePreviewer::request`) ONCE, and from then on drives only the guest's tick -- the per-tick surface, with no
// further "user input" of any kind. If the browser ever stops re-asking for a deferred preview, it simply never lands
// and this fails.

/// The file the preview stage loads. Exists only at its regular path, and is
/// asked for by nothing above, so it is never resident when this stage starts.
constexpr const char* kPreviewPath = "SAMPLES/PREVIEW.WAV";

/// Ticks the preview stage may take before it is called wedged. Each is one
/// guestTick() call plus a dispatch round trip, so this is generous: a
/// healthy load lands within a few.
constexpr int32_t kMaxPreviewTicks = 2000;

/// Guest ticks the browser's opening may take to list and settle before the stages begin. A loop of guest ticks inside
/// one step, not a wait across steps: the loop reports its own exhaustion.
constexpr int32_t kMaxOpenTicks = 400;

/// Consecutive idle guest ticks (no listing, no preview) that show the browser's opening is done.
constexpr int32_t kOpenIdleTicks = 30;

// ── The preview cancellation stage ──────────────────────────────────────────
//
// The stage above proves a deferred preview is asked for again until it lands.
// This one proves the opposite requirement, which the same re-drive makes
// necessary: a preview the user has moved OFF must stop being asked for, and
// must never be rendered.
//
// In the real UI the move is onto a folder or an empty selection — the one
// cursor move that dispatches no replacement, so latest-wins supersession never
// fires. This harness calls request() directly and never populates a file
// list, so it cannot reach that branch; it drives the same invalidation through
// `SamplePreviewer::cancel()`, which is what that branch calls.
//
// The order below is the defect exactly: preview a file, and before any tick has
// started its load, cancel. If the load starts anyway, the browser draws (and on a
// real device sounds) a file the cursor has left, over a folder.

/// The file the cancellation stage asks for and then abandons. Its own file, asked for by
/// nothing else, so "it drew" and "it loaded" can only be this stage's doing.
constexpr const char* kCancelPath = "SAMPLES/CANCEL.WAV";

/// guestTick() calls the cancellation stage makes after cancelling.
///
/// Fixed rather than "until something happens": the property is that nothing EVER happens, so
/// there is nothing to wait for. Generous enough for the queued op to have run and reported back
/// several times over — a healthy load takes one or two dispatch round trips.
constexpr int32_t kCancelTicks = 64;

// ── The preview stall stage ─────────────────────────────────────────────────
//
// AB-PREVIEW-STALL. The preview stage proves a deferred preview is asked for again until it lands; this one
// proves that asking again ENDS. A preview whose load is held far past its load's stall bound
// (AsyncSampleLoad::kMaxStalledUs) must give up -- nothing pending, nothing drawn, one give-up counted -- rather
// than be asked for on every frame for as long as the card takes not to answer. The hold is the loader's host latency
// knob, which delays each load after it is marked in flight, so every collect during it answers PENDING, as a card that
// never answers would.

/// The file the stall stage previews. Its own file, asked for by nothing else, so a draw of it can only be this
/// stage's doing.
constexpr const char* kStallPath = "SAMPLES/STALL.WAV";

/// How long, in virtual time, the stall stage waits for the preview to give up before calling it wedged: the load's
/// own bound and a margin. Not a Wait: the bound is virtual time, not steps, and an expired window is judged by the
/// stage's own stall FAIL (`pendingAtEnd`).
constexpr uint64_t kStallWindowUs = deluge::storage::AsyncSampleLoad::kMaxStalledUs + 2'000'000;

/// How long the loader holds each load during the stall: past the whole wait, so every collect in it answers PENDING.
constexpr uint32_t kStallHoldMs = 30000;
static_assert(uint64_t{kStallHoldMs} * 1000 > kStallWindowUs, "the hold must outlast the stall stage's wait");

/// guestTick() calls after the wait ends, with the hold lifted, in which nothing may be pending again: the property is
/// that nothing is pending again anywhere in the window, so the window is its definition, not a bound.
constexpr int32_t kStallSettleTicks = 64;

extern "C" void deluge_streaming_set_fill_latency_ms(uint32_t ms); // host harness only; streaming_loader.rs

/// How long the streaming loader holds each drain pass during the preview stage: long enough that the first draw finds
/// its cluster unfilled, and well inside the browser's redraw give-up.
constexpr uint32_t kPreviewFillHoldMs = 10;

/// The preview-draw stall stage's file (see the Rust fixture): six clusters, each column reading its own.
constexpr const char* kDrawStallPath = "SAMPLES/DRAWSTALL.WAV";
/// How long the streaming loader holds each drain pass in that stage: far past the 300 ms redraw bound, and short
/// enough that the pass it delays is over well before the next preview's clusters are wanted. The stage runs before the
/// stall stage: that one holds the audio loader, which is single-flight, and a load asked for behind its held one
/// stalls.
constexpr uint32_t kDrawStallHoldMs = 2000;
/// Ticks the stage may take before it is called wedged: far past the 300 ms redraw bound.
constexpr int32_t kMaxDrawStallTicks = 20000;

// ── The card-ejection stage ─────────────────────────────────────────────────
//
// Everything the async loader holds is keyed by the nominal PATH, and a
// DelugeAudioLoadResult records nothing about which card produced it. So a header
// parsed from one card and collected after another has been inserted binds the
// NEW card's file (sample_build::bindReadStream opens the path) to the OLD
// card's geometry (sample_build::applyGeometry writes the numbers the result
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
/// The wait of the current step that waits on the loader.
Wait g_ejectWait{g_report, "the witness file loading", kMaxEjectTicks};
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
enum class Stage : uint8_t { Cases, Eject, Preview, PrescanRange, Cancel, DrawStall, Stall, Done };
Stage g_stage = Stage::Cases;
/// Whether request() has been called. It is called exactly once.
bool g_previewStarted = false;
/// guestTick() calls made since then, the one that ended the stage included.
int32_t g_previewTicks = 0;
/// The preview stage's wait for its first complete draw, from the request on.
Wait g_previewWait{g_report, "the preview drawing", kMaxPreviewTicks};
/// Whether the browser could not be opened, which ends the stage at once.
bool g_previewAborted = false;
/// Of those, how many found a deferred preview still waiting.
int32_t g_previewRedrives = 0;
/// Whether the cancellation stage has issued its request()/cancelPreview() pair.
bool g_cancelStarted = false;
/// guestTick() calls made since then.
int32_t g_cancelTicks = 0;
/// Where the stall stage is.
enum class StallStep : uint8_t { Start, Wait, Settle, Finished };
StallStep g_stallStep = StallStep::Start;
enum class DrawStallStep : uint8_t { Start, Armed, Release, Restart, Wait, Move, MoveWait, Finished };
DrawStallStep g_drawStallStep = DrawStallStep::Start;
/// The draw-stall stage's current wait: the redraw arming, the stalled preview settling, the moved preview drawing.
Wait g_drawStallWait{g_report, "the stalled preview arming a redraw", kMaxDrawStallTicks};
/// Whether one of the stage's waits expired, which has said so: its evidence line and judgements are skipped.
bool g_drawStallExpired = false;
int32_t g_dsRedrawsBefore = 0;
int32_t g_dsGiveUpsBefore = 0;
bool g_dsPendingAtEnd = false;
bool g_dsMovedDrawn = false;
/// previewDraws() before the stall preview and before the move: the stage's witnesses. Deliberately NOT
/// the drawn sample -- starting the next preview releases it, and reading its filePath then is a use-after-free.
int32_t g_dsDrawsBefore = 0;
int32_t g_dsMoveDrawsBefore = 0;
int32_t g_dsCompleteBefore = 0;
/// The release sub-leg: whether a redraw was armed, and whether one ran, or stayed pending, after the preview's sample
/// was released.
bool g_dsArmed = false;
int32_t g_dsRedrawsAfterRelease = -1;
bool g_dsPendingAfterRelease = true;
int32_t g_dsStallDraws = 0;
int32_t g_dsStallComplete = 0;
int32_t g_dsStallTicks = 0;
/// guestTick() calls made while the stalled preview was pending.
int32_t g_stallTicks = 0;
/// When the stall stage asked for its preview, on the stall bound's clock.
uint64_t g_stallStartUs = 0;
/// How long the stall stage waited before the preview gave up, or the whole window.
uint64_t g_stallWaitedUs = 0;
/// guestTick() calls made after the wait ended.
int32_t g_stallSettleTicks = 0;
/// the previewer's giveUps() when the stage began.
int32_t g_stallGiveUpsBefore = 0;
/// Whether the preview was still pending when the wait ended.
bool g_stallPendingAtEnd = false;

/// The interaction tier's blocking sample reads, read when a browser stage starts.
struct TierMark {
	int32_t sampleIo = 0;
};
TierMark g_previewTier{};
int32_t g_drawsBefore = 0;
int32_t g_redrawsBefore = 0;
int32_t g_drawsCompleteBefore = 0;
TierMark g_cancelTier{};
TierMark g_stallTier{};
TierMark g_drawStallTier{};

TierMark markTier() {
	return {deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

/// AB-PREVIEW-TIER: since @p before, no sample may have been read from the card on the interaction tier.
void checkTier(const char* stage, const TierMark& before) {
	const TierMark now = markTier();
	const int32_t sampleIo = now.sampleIo - before.sampleIo;
	printf("AB-PREVIEW-TIER stage=%s sampleIo=%d\n", stage, sampleIo);
	if (sampleIo != 0) {
		g_report.fail("[audioLoadAB/%s]: %d preview sample read(s) went to the card on the interaction tier", stage,
		              sampleIo);
	}
}

/// Plain CRC32 (IEEE 802.3, reflected) — a stable fingerprint of the bytes read back through the
/// Sample's own stream.
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
		g_report.fail("[audioLoadAB/%s]: the published Sample cannot be read -- its stream was never bound, so it "
		              "would load without error and play silence",
		              label);
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
/// Runs from the card step (see deluge_scenario_audio_load_ab_dispatch_step()), where a blocking card read is legal. It
/// loads under a copy of the load's context: the context is per-load state that each load moves, and the async load has
/// to start from the state this one started from, or the two would be proposed different candidates.
std::string referenceLine(const char* tag, const char* label, const char* path) {
	deluge::storage::AudioLoadContext referenceContext = g_loadContext;
	std::string fileName = path;
	Error error = Error::NONE;
	AudioFile* file = audioFileManager.getAudioFileFromFilename(
	    fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	    /*makeWaveTableWorkAtAllCosts=*/false, deluge::storage::AsyncLoadMode::Blocking, &referenceContext);
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
		g_report.fail("[audioLoadAB/%s]: the async load differs from the in-process synchronous load of the same file "
		              "(AB-REF above is the reference)",
		              label);
	}
}

/// Print the per-run facts the reference has no counterpart for, and assert that the loads really went through the
/// async loader: that every case answered LOAD_IN_PROGRESS at least once before settling, rather than being served
/// synchronously (or from memory) with the loader never consulted.
void printSummary() {
	printf("AB-SUMMARY cases=%zu deferrals=%d failures=%d\n", kCases.size(), g_deferralsTotal, g_report.failures());

	printf("AB-DEFER");
	for (size_t i = 0; i < kCases.size(); i++) {
		printf(" %s=%d", kCases[i].label, g_deferralsByCase[i]);
	}
	printf("\n");

	// EVERY case, not merely some case. See g_deferralsByCase's comment for the fall-through this
	// catches and the run-wide total misses.
	for (size_t i = 0; i < kCases.size(); i++) {
		if (g_deferralsByCase[i] == 0) {
			g_report.fail("[audioLoadAB]: case '%s' never deferred -- it was answered on the synchronous path, so it "
			              "is not under test at all",
			              kCases[i].label);
		}
	}
	// Absolute, not comparative: see g_selfdirAlternatePath. The reference and the async load both
	// resolve this candidate positionally, and a regression in the shared resolver would take the two
	// of them down together, agreeing perfectly.
	// The VALUE, not merely non-emptiness: a case that happens to resolve from some other alternate
	// candidate would satisfy "not empty" while proving nothing about this one.
	if (g_selfdirAlternatePath != kSelfdirAlternate) {
		g_report.fail("[audioLoadAB/%s]: the sample living IN the alternate dir recorded "
		              "loadedFromAlternatePath \"%s\", expected \"%.*s\" -- the winning candidate was read by "
		              "comparing paths rather than by slot, so a collect-media save would leave it behind",
		              kCases[kSelfdirIndex].label, g_selfdirAlternatePath.c_str(),
		              static_cast<int>(kSelfdirAlternate.size()), kSelfdirAlternate.data());
	}
	if (g_bothStatusAfter != AlternateLoadDirStatus::MIGHT_EXIST) {
		g_report.fail("[audioLoadAB/%s]: a hit on the regular path changed the alternate-dir status to %d; it must "
		              "leave it unprobed (MIGHT_EXIST)",
		              kCases[kBothIndex].label, static_cast<int>(g_bothStatusAfter));
	}
	if (g_badRegLoaded || g_badRegError != Error::FILE_UNSUPPORTED
	    || g_badRegStatusAfter != AlternateLoadDirStatus::MIGHT_EXIST) {
		g_report.fail(
		    "[audioLoadAB/%s]: garbage at the regular path loaded=%d error=%d status=%d; a parse verdict must "
		    "fail as FILE_UNSUPPORTED without retrying in the alternate dir, which must stay unprobed",
		    kCases[kBadRegIndex].label, g_badRegLoaded ? 1 : 0, static_cast<int>(g_badRegError),
		    static_cast<int>(g_badRegStatusAfter));
	}
	// Reprint with the summary's own failures folded in, so a reader of the last line sees the real
	// count rather than the count as it stood before these checks ran.
	g_report.printResult();
	fflush(stdout);
}

/// One load attempt for the current case, run as the card step.
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
			g_report.count();
		}
		g_referenceTaken = true;
	}

	// getAudioFileFromFilename may REWRITE fileName to the location the file was actually found at,
	// so it takes a fresh copy each attempt and the rewritten value is printed as `asked=`.
	std::string fileName = c.path;
	Error error = Error::NONE;
	AudioFile* file = audioFileManager.getAudioFileFromFilename(
	    fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	    /*makeWaveTableWorkAtAllCosts=*/false, deluge::storage::AsyncLoadMode::AsyncOrBlocking, &g_loadContext);

	if (error == Error::LOAD_IN_PROGRESS) {
		g_deferralsThisCase++;
		g_deferralsTotal++;
		if (g_deferralsThisCase <= kMaxDeferrals) {
			return; // Ask again next tick — the contract LOAD_IN_PROGRESS imposes on its caller.
		}
		g_report.fail("[audioLoadAB/%s]: still LOAD_IN_PROGRESS after %d attempts -- the load never completed", c.label,
		              g_deferralsThisCase);
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
	if (g_caseIndex == kBothIndex) {
		g_bothStatusAfter = g_loadContext.alternateDirStatus;
	}
	if (g_caseIndex == kBadRegIndex) {
		g_badRegStatusAfter = g_loadContext.alternateDirStatus;
		g_badRegError = error;
		g_badRegLoaded = file != nullptr;
	}

	if (!checkLoadInvariants(c.label)) {
		g_report.count();
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
		// The ejection stage next, not the browser ones. It runs as the same card step, and it has
		// to run BEFORE the preview stage: a previewed sample stays leased for as long as it is on
		// screen, so checkLoadInvariants' leaked-reason half would report that legitimate lease as a
		// failure for every load attempted afterwards.
		g_stage = Stage::Eject;
	}
}

/// The resident sample at @p path, or null.
Sample* residentSample(const char* path) {
	for (AudioFile* file : audioFileManager.audioFiles) {
		if (file != nullptr && file->type == AudioFileType::SAMPLE && file->filePath == path) {
			return static_cast<Sample*>(file);
		}
	}
	return nullptr;
}

/// The resident sample at @p path when the scenes browser has drawn its waveform, else null. The pointer is compared,
/// never read through, by the browser.
Sample* drawnSample(const char* path) {
	Sample* sample = residentSample(path);
	return sample != nullptr
	               && deluge::ui::scenes::browser::browser_scene()
	                      .shown_waveform(deluge::gui::shell_host::waveform_identity(*sample))
	                      .has_value()
	           ? sample
	           : nullptr;
}

/// Open the scenes browser on the boot song's first synth clip, as a user opens a source's file selector, and let its
/// own opening listing and preview settle: the stages below then drive only what they mean to.
/// @return Whether the browser is on top and idle.
bool openGuestBrowser() {
	Output* synth = nullptr;
	for (Output* output = currentSong->live.media.first_output; output != nullptr && synth == nullptr;
	     output = output->next) {
		if (output->type == OutputType::SYNTH) {
			synth = output;
		}
	}
	Clip* clip = synth != nullptr ? currentSong->getClipWithOutput(synth) : nullptr;
	if (clip == nullptr) {
		return false;
	}
	deluge::harness::guestClose();
	currentSong->setCurrentClip(clip);
	changeRootUI(&instrumentClipView);
	const bool opened = deluge::gui::ui::openMenu(clip, /*sourceIndex=*/0, /*performanceView=*/false,
	                                              /*sessionOrArrangerRoot=*/false, /*sessionViewRoot=*/false,
	                                              /*kitGlobalFx=*/false, deluge::gui::ui::MenuNode::Osc0FileSelector);
	for (int32_t t = 0; opened && t < 16 && !deluge::harness::browserOnTop(); t++) {
		deluge::harness::guestTick();
	}
	int32_t idleTicks = 0;
	int32_t openTicks = 0;
	for (; deluge::harness::browserOnTop() && openTicks < kMaxOpenTicks && idleTicks < kOpenIdleTicks; openTicks++) {
		deluge::harness::guestTick();
		const bool busy = deluge::ui::scenes::browser::browser_scene().listing_in_flight()
		                  || deluge::gui::ui::shellPreviewer().pending();
		idleTicks = busy ? 0 : idleTicks + 1;
	}
	if (openTicks == kMaxOpenTicks && idleTicks < kOpenIdleTicks) {
		g_report.fail("[audioLoadAB/preview]: the browser's opening did not settle within %d guest ticks",
		              kMaxOpenTicks);
	}
	printf("AB-PROBE guest opened=%d browserOnTop=%d\n", opened ? 1 : 0, deluge::harness::browserOnTop() ? 1 : 0);
	return deluge::harness::browserOnTop();
}

/// Has the preview actually DRAWN its waveform for `kPreviewPath`?
///
/// The scenes browser keeps the view it drew a sample's waveform through (`shown_waveform`) only on the
/// path that renders the waveform, so a view naming this file's sample is direct evidence that
/// the preview reached the screen -- not merely that the file became resident somewhere. That
/// distinction is the whole point here: the defect this stage exists for left the file unloaded, the
/// waveform undrawn and no error shown, which from outside looks identical to "the user did not
/// scroll here".
bool previewHasDrawn() {
	return drawnSample(kPreviewPath) != nullptr;
}

/// One tick of the preview stage. See the Stage/kPreviewPath comments above.
/// @return Whether the tick was a step of the wait, not the request that starts it.
bool runOnePreviewTick() {
	if (!g_previewStarted) {
		// The ONE user-driven action of this stage: the browser is told which file to preview,
		// exactly as a cursor move tells it. Nothing below ever calls this again.
		g_previewStarted = true;
		// The guest's browser, opened before the first preview; it draws the waveform until a main pad is pressed.
		if (!openGuestBrowser()) {
			g_report.fail("[audioLoadAB/preview]: the browser scene could not be opened on the boot song");
			g_previewAborted = true;
			return false;
		}
		g_previewTier = markTier();
		// The scenes browser draws a frame after the load lands, by which time the host's fill has already delivered
		// this small file's cluster; a short hold keeps the first draw ahead of the card, as the stage needs.
		deluge_streaming_set_fill_latency_ms(kPreviewFillHoldMs);
		g_drawsBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().draws;
		g_redrawsBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().redraws;
		g_drawsCompleteBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().complete;
		deluge::gui::ui::shellPreviewer().request(kPreviewPath, /*movementDirection=*/0);
		g_previewWait = Wait{g_report, "the preview drawing", kMaxPreviewTicks};
		return false;
	}

	g_previewTicks++;
	if (deluge::gui::ui::shellPreviewer().pending()) {
		g_previewRedrives++;
	}
	// The per-tick surface, and the ONLY thing driven from here on.
	deluge::harness::guestTick();
	return true;
}

/// Print the preview stage's outcome and assert it.
///
/// Not compared against a reference: the synchronous path never defers, so its re-drive counts have nothing to say.
void finishPreviewStage(bool drawn) {
	deluge_streaming_set_fill_latency_ms(0);
	printf("AB-PREVIEW drawn=%d ticks=%d redrives=%d pending=%d\n", drawn ? 1 : 0, g_previewTicks, g_previewRedrives,
	       deluge::gui::ui::shellPreviewer().pending() ? 1 : 0);

	if (!drawn) {
		g_report.fail("[audioLoadAB/preview]: after ONE request() and %d guestTick() tick(s) with no "
		              "further input, the preview never sounded or drew '%s'. A deferred preview that nothing asks "
		              "for again is silent, blank and errorless -- indistinguishable to the user from a file that "
		              "does not exist.",
		              g_previewTicks, kPreviewPath);
	}
	if (deluge::gui::ui::shellPreviewer().pending()) {
		g_report.fail("[audioLoadAB/preview]: a deferral is STILL pending after this stage -- either it was never "
		              "answered (see above), or the re-drive state is not cleared once it lands and will fire again "
		              "over whatever the user looks at next");
	}
	// The preview must have gone through the deferred path at least once, or this stage tested
	// nothing: a preview that landed synchronously exercises no re-drive at all, and the check would
	// pass with the surface it exists to guard removed entirely.
	if (g_previewRedrives == 0) {
		g_report.fail("[audioLoadAB/preview]: the preview never deferred, so the re-drive was never under "
		              "test");
	}
	const int32_t draws = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().draws - g_drawsBefore;
	const int32_t redraws =
	    deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().redraws - g_redrawsBefore;
	const int32_t complete =
	    deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().complete - g_drawsCompleteBefore;
	printf("AB-PREVIEW-DRAW draws=%d redraws=%d complete=%d redrawPending=%d\n", draws, redraws, complete,
	       deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending() ? 1 : 0);
	// redraws >= 1 is measured, not assumed: the preview's first draw finds its cluster enqueued but not yet filled,
	// and one redraw finishes it (measured redraws=1). A draw that completed at once would be a draw that waited for
	// the card.
	if (draws != 1 || redraws < 1 || complete < 1
	    || deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending()) {
		g_report.fail(
		    "[audioLoadAB/preview-draw]: the preview must be drawn once, without waiting for its clusters, and "
		    "end on a complete render with nothing left to redraw");
	}
	checkTier("preview", g_previewTier);
}

/// The previewed sample, as the pre-scan stage began.
struct PrescanMark {
	Sample* sample = nullptr;
};
PrescanMark g_prescanBefore{};
/// The previewed sample's first unscanned frame before any peak scan ran: what the fill alone folded.
uint64_t g_peaksFilledBefore = 0;
/// Scan ticks made, the one that ended the stage included.
int32_t g_prescanTicks = 0;
/// Bound on the pre-scan stage, in ticks: one block per tick at most, and the stage's files hold far fewer.
constexpr int32_t kMaxPrescanTicks = 8000;
Wait g_prescanWait{g_report, "the peak scan finishing", kMaxPrescanTicks};

/// One tick of the pre-scan stage: the background waveform scan, as the idle loop runs it.
void runOnePrescanTick() {
	if (g_prescanTicks == 0) {
		Sample* sample = drawnSample(kPreviewPath);
		g_prescanBefore = PrescanMark{.sample = sample};
		audioFileManager.peakScanAllDone = false;
		g_peaksFilledBefore = sample != nullptr ? deluge_peaks_next_unscanned(sample->peaks(), 0) : uint64_t{0};
		// The scan must build the index on its own: the clusters the preview filled are resident, so no fill
		// folds them again.
		if (sample != nullptr) {
			sample->resetPeaks();
		}
	}
	g_prescanTicks++;
	audioFileManager.backgroundWaveformScan();
}

/// AB-PRESCAN-RANGE: the background peak scan, from an empty index, runs to completion over the previewed sample.
void finishPrescanStage() {
	Sample* sample = g_prescanBefore.sample;
	const bool sameSample = sample != nullptr && sample == drawnSample(kPreviewPath);
	const bool complete = sameSample && deluge_peaks_next_unscanned(sample->peaks(), 0) == UINT64_MAX;
	printf("AB-PRESCAN-RANGE done=%d ticks=%d complete=%d\n", audioFileManager.peakScanAllDone ? 1 : 0, g_prescanTicks,
	       complete ? 1 : 0);
	if (!sameSample || !audioFileManager.peakScanAllDone || !complete) {
		g_report.fail("[audioLoadAB/prescan-range]: the peak scan must finish over the previewed sample and leave its "
		              "index complete");
	}
}

/// The peak of every frame of @p sample, read back through the frame reader: the brute force the index must equal.
DelugeMaybePeak brutePeak(Sample& sample) {
	std::array<uint8_t, 4096> buf{};
	const uint32_t frameBytes = sample.numChannels * sample.byteDepth;
	const uint32_t chunk = static_cast<uint32_t>(buf.size()) / frameBytes;
	DelugeMaybePeak all{};
	for (uint64_t at = 0; at < sample.lengthInSamples;) {
		const auto want = static_cast<uint32_t>(std::min<uint64_t>(chunk, sample.lengthInSamples - at));
		const uint32_t got = deluge::sample::SampleFrameReader::read(deluge::sample::source_id_for(sample), at, want,
		                                                             buf.data(), buf.size());
		if (got == 0) {
			return DelugeMaybePeak{};
		}
		const DelugeMaybePeak p =
		    deluge_peaks_of_frames(buf.data(), got * frameBytes, static_cast<uint8_t>(sample.byteDepth));
		if (p.has) {
			all = all.has ? DelugeMaybePeak{.has = true,
			                                .peak = {.min = std::min(all.peak.min, p.peak.min),
			                                         .max = std::max(all.peak.max, p.peak.max)}}
			              : p;
		}
		at += got;
	}
	return all;
}

/// The varied sample the column check reads: loaded on the card step before the preview stage, and kept.
Sample* g_peaksSample = nullptr;
constexpr const char* kPeaksPath = "SAMPLES/PEAKS.WAV";

/// Load kPeaksPath through the synchronous card path and keep it loaded, for checkColumns. Runs as the card step.
void loadPeaksSample() {
	std::string fileName = kPeaksPath;
	Error error = Error::NONE;
	deluge::storage::AudioLoadContext context = g_loadContext;
	AudioFile* file = audioFileManager.getAudioFileFromFilename(
	    fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	    /*makeWaveTableWorkAtAllCosts=*/false, deluge::storage::AsyncLoadMode::Blocking, &context);
	if (file == nullptr || !AudioFile::isSample(file)) {
		g_report.fail("[audioLoadAB/columns]: %s did not load", kPeaksPath);
		return;
	}
	file->addReason(); // kept loaded through the scans
	g_peaksSample = static_cast<Sample*>(file);
}

/// The peak of frames [lo, hi) of @p sample read back through the frame reader, at the drawing's 32-bit scale.
std::optional<deluge::ui::views::waveform::Peak> brutePeakOf(Sample& sample, uint64_t lo, uint64_t hi) {
	std::array<uint8_t, 4096> buf{};
	const uint32_t frameBytes = sample.numChannels * sample.byteDepth;
	const uint32_t chunk = static_cast<uint32_t>(buf.size()) / frameBytes;
	DelugeMaybePeak all{};
	for (uint64_t at = lo; at < hi;) {
		const auto want = static_cast<uint32_t>(std::min<uint64_t>(chunk, hi - at));
		const uint32_t got = deluge::sample::SampleFrameReader::read(deluge::sample::source_id_for(sample), at, want,
		                                                             buf.data(), buf.size());
		if (got == 0) {
			return std::nullopt;
		}
		const DelugeMaybePeak p =
		    deluge_peaks_of_frames(buf.data(), got * frameBytes, static_cast<uint8_t>(sample.byteDepth));
		if (p.has) {
			all = all.has ? DelugeMaybePeak{.has = true,
			                                .peak = {.min = std::min(all.peak.min, p.peak.min),
			                                         .max = std::max(all.peak.max, p.peak.max)}}
			              : p;
		}
		at += got;
	}
	if (!all.has) {
		return std::nullopt;
	}
	return deluge::ui::views::waveform::Peak{.min = static_cast<int32_t>(all.peak.min) * 65536,
	                                         .max = static_cast<int32_t>(all.peak.max) * 65536};
}

/// AB-COLUMNS: every column of the waveform, across zooms from one frame to a sixteenth of the file and scrolls from
/// before its start to past its end, is the peak of exactly its frames, read back through the frame reader.
void checkColumns(Sample& sample) {
	const auto len = static_cast<int64_t>(sample.lengthInSamples);
	int32_t compared = 0;
	int32_t wrong = 0;
	int32_t pending = 0;
	for (int64_t zoom : {int64_t{1}, int64_t{7}, int64_t{255}, int64_t{256}, int64_t{4096}, int64_t{65537},
	                     std::max<int64_t>(len / 16, 1)}) {
		for (int64_t scroll : {-3 * zoom, int64_t{0}, int64_t{1}, len - 5 * zoom}) {
			const deluge::ui::views::waveform::PeakRead read =
			    deluge::sample::waveform_columns(sample, std::nullopt, {.scroll = scroll, .zoom = zoom});
			for (int32_t col = 0; col < deluge::ui::views::waveform::kColumns; col++) {
				const int64_t lo = std::max<int64_t>(scroll + col * zoom, 0);
				const int64_t hi = std::min<int64_t>(scroll + (col + 1) * zoom, len);
				const auto want = hi > lo ? brutePeakOf(sample, lo, hi) : std::nullopt;
				const deluge::ui::views::waveform::ColumnRead got = read.column(col);
				if (got.state() == deluge::ui::views::waveform::ColumnState::PENDING) {
					pending++;
					continue;
				}
				compared++;
				if (got.peak() != want) {
					wrong++;
				}
			}
		}
	}
	printf("AB-COLUMNS compared=%d wrong=%d pending=%d\n", compared, wrong, pending);
	if (wrong != 0 || pending != 0 || compared == 0) {
		g_report.fail("[audioLoadAB/columns]: a column is not the peak of exactly its frames");
	}
}

/// AB-PEAKS: the preview's own fills fold its first blocks into the sample's index before any peak scan runs; the
/// scan, starting over from an empty index, completes it; and the index's range is the whole file's.
void checkPeaks() {
	Sample* sample = g_prescanBefore.sample;
	if (sample == nullptr || sample != drawnSample(kPreviewPath)) {
		g_report.fail("[audioLoadAB/peaks]: no previewed sample");
		return;
	}
	const uint64_t unscanned = deluge_peaks_next_unscanned(sample->peaks(), 0);
	const DelugeMaybePeak range = deluge_peaks_range(sample->peaks());
	const DelugeMaybePeak brute = brutePeak(*sample);
	printf("AB-PEAKS filled_before_scan=%llu unscanned_after=%s range=%d/%d brute=%d/%d\n",
	       static_cast<unsigned long long>(g_peaksFilledBefore), unscanned == UINT64_MAX ? "none" : "some",
	       range.peak.min, range.peak.max, brute.peak.min, brute.peak.max);
	if (g_peaksFilledBefore == 0) {
		g_report.fail("[audioLoadAB/peaks]: the preview's fills folded no peaks before the scan ran");
	}
	if (unscanned != UINT64_MAX) {
		g_report.fail("[audioLoadAB/peaks]: the peak scan finished with blocks unscanned");
	}
	if (!range.has || !brute.has || range.peak.min != brute.peak.min || range.peak.max != brute.peak.max) {
		g_report.fail("[audioLoadAB/peaks]: the index's range is not the file's");
	}
	checkColumns(*sample);
	if (g_peaksSample != nullptr) {
		checkColumns(*g_peaksSample);
	}
}

/// Has anything drawn the cancelled file's waveform?
///
/// Same witness as previewHasDrawn(): the browser's view of a sample is set only on its waveform path. It still
/// names the PREVIOUS stage's file, which is exactly right -- what must not have happened is this file replacing
/// it.
bool cancelledHasDrawn() {
	return drawnSample(kCancelPath) != nullptr;
}

/// Was the cancelled file loaded (and, on a device, sounded) at all?
///
/// `lastFilePathLoaded` is assigned when the preview's tick starts the load, before anything is
/// sounded or drawn -- so it witnesses the load having started at all, which is the half
/// `cancelledHasDrawn()` misses while the load is still in flight. `AudioEngine::soundPreview`,
/// called when that load ends, is what starts the note.
bool cancelledWasLoaded() {
	return deluge::model::lastFilePathLoaded() == kCancelPath;
}

/// One tick of the cancellation stage. See the block comment above kCancelPath.
void runOneCancelTick() {
	if (!g_cancelStarted) {
		g_cancelStarted = true;
		g_cancelTier = markTier();
		// The user-driven preview, exactly as a cursor move issues it. This only records the file;
		// nothing starts its load until a tick.
		deluge::gui::ui::shellPreviewer().request(kCancelPath, /*movementDirection=*/0);
		// And then the cursor moves onto something with nothing to preview, before any tick has started the load.
		deluge::gui::ui::shellPreviewer().cancel();
		return;
	}

	g_cancelTicks++;
	// The per-tick surface, and the only thing driven from here on -- the same surface that
	// ticks a preview's load. If the cancelled preview re-arms itself, this is what keeps it alive.
	deluge::harness::guestTick();
}

/// Print the cancellation stage's outcome and assert it.
void finishCancelStage() {
	const bool drawn = cancelledHasDrawn();
	const bool loaded = cancelledWasLoaded();
	printf("AB-CANCEL drawn=%d loaded=%d ticks=%d pending=%d\n", drawn ? 1 : 0, loaded ? 1 : 0, g_cancelTicks,
	       deluge::gui::ui::shellPreviewer().pending() ? 1 : 0);

	if (drawn) {
		g_report.fail("[audioLoadAB/cancel]: the preview was cancelled before its load ran, and its waveform "
		              "was drawn anyway -- on the device that is a file the cursor has LEFT, rendered over the "
		              "folder the user moved onto, with its note-on no longer suppressed");
	}
	if (loaded) {
		g_report.fail("[audioLoadAB/cancel]: the preview was cancelled before its load ran, and the load ran "
		              "anyway (lastFilePathLoaded is '%s') -- the op acted on an abandoned target instead of "
		              "checking whether it was still wanted",
		              kCancelPath);
	}
	if (deluge::gui::ui::shellPreviewer().pending()) {
		g_report.fail("[audioLoadAB/cancel]: a cancelled preview is STILL pending after %d tick(s) -- it re-armed "
		              "itself and will be re-driven every frame until it lands, and then rendered",
		              g_cancelTicks);
	}
	checkTier("cancel", g_cancelTier);
}

/// Has anything drawn the stalled file's waveform? Same witness as previewHasDrawn().
bool stallHasDrawn() {
	return drawnSample(kStallPath) != nullptr;
}

/// One tick of the stall stage. See the block comment above kStallPath.
void runOneStallTick() {
	switch (g_stallStep) {
	case StallStep::Start:
		deluge_audio_load_set_latency_ms(kStallHoldMs);
		g_stallGiveUpsBefore = deluge::gui::ui::shellPreviewer().giveUps();
		g_stallTier = markTier();
		// The ONE user-driven action of this stage. The load it asks for is held from here on.
		deluge::gui::ui::shellPreviewer().request(kStallPath, /*movementDirection=*/0);
		g_stallStartUs = deluge::runtime::nowUs();
		g_stallStep = StallStep::Wait;
		return;

	case StallStep::Wait:
		// Read at the top of the tick, before guestTick() ticks the load again.
		if (deluge::gui::ui::shellPreviewer().pending() && deluge::runtime::nowUs() - g_stallStartUs < kStallWindowUs) {
			g_stallTicks++;
			deluge::harness::guestTick();
			return;
		}
		g_stallPendingAtEnd = deluge::gui::ui::shellPreviewer().pending();
		g_stallWaitedUs = deluge::runtime::nowUs() - g_stallStartUs;
		deluge_audio_load_set_latency_ms(0);
		g_stallStep = StallStep::Settle;
		return;

	case StallStep::Settle:
		if (g_stallSettleTicks < kStallSettleTicks) {
			g_stallSettleTicks++;
			deluge::harness::guestTick();
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
	const int32_t giveUps = deluge::gui::ui::shellPreviewer().giveUps() - g_stallGiveUpsBefore;
	const bool drawn = stallHasDrawn();
	const bool pendingAfter = deluge::gui::ui::shellPreviewer().pending();
	printf("AB-PREVIEW-STALL ticks=%d waitedUs=%llu boundUs=%llu giveUps=%d pendingAtEnd=%d drawn=%d pendingAfter=%d\n",
	       g_stallTicks, static_cast<unsigned long long>(g_stallWaitedUs),
	       static_cast<unsigned long long>(deluge::storage::AsyncSampleLoad::kMaxStalledUs), giveUps,
	       g_stallPendingAtEnd ? 1 : 0, drawn ? 1 : 0, pendingAfter ? 1 : 0);

	if (g_stallPendingAtEnd || giveUps != 1) {
		g_report.fail("[audioLoadAB/stall]: a preview whose load was held %u ms did not give up once (pending=%d after "
		              "%d guestTick() tick(s), %d give-up(s)) -- a load that never answers would be asked for on every "
		              "frame, silent and undrawn, for as long as the card takes",
		              static_cast<unsigned>(kStallHoldMs), g_stallPendingAtEnd ? 1 : 0, g_stallTicks, giveUps);
	}
	if (drawn) {
		g_report.fail("[audioLoadAB/stall]: '%s' was drawn, but its load was held for the whole stage", kStallPath);
	}
	if (pendingAfter) {
		g_report.fail("[audioLoadAB/stall]: a preview that gave up is pending again after %d tick(s) with the hold "
		              "lifted -- it re-armed itself",
		              g_stallSettleTicks);
	}
	checkTier("stall", g_stallTier);
}

/// One tick of the preview-draw stall stage: a preview whose clusters the streaming loader holds must draw what it
/// can, redraw, and give up once nothing more arrives -- then the browser must still preview another file.
void runOneDrawStallTick() {
	switch (g_drawStallStep) {
	case DrawStallStep::Start:
		g_drawStallTier = markTier();
		deluge_streaming_set_fill_latency_ms(kDrawStallHoldMs);
		deluge::gui::ui::shellPreviewer().request(kDrawStallPath, /*movementDirection=*/0);
		g_drawStallWait = Wait{g_report, "the stalled preview arming a redraw", kMaxDrawStallTicks};
		g_drawStallStep = DrawStallStep::Armed;
		return;
	case DrawStallStep::Armed:
		// First, a release with a redraw pending: Select (enterKeyPress) and a typed move release the preview's sample
		// through stopAnyPreviewing() without moving the generation. The redraw must not read the released sample.
		switch (g_drawStallWait.poll(deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending())) {
		case WaitState::Pending:
			deluge::harness::guestTick();
			return;
		case WaitState::Met:
			break;
		case WaitState::Expired:
			deluge_streaming_set_fill_latency_ms(0);
			g_drawStallExpired = true;
			g_drawStallStep = DrawStallStep::Finished;
			return;
		}
		g_dsArmed = deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending();
		g_drawStallStep = DrawStallStep::Release;
		return;
	case DrawStallStep::Release: {
		const int32_t redrawsBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().redraws;
		AudioEngine::stopAnyPreviewing();
		deluge::harness::guestTick();
		g_dsRedrawsAfterRelease =
		    deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().redraws - redrawsBefore;
		g_dsPendingAfterRelease = deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending();
		g_drawStallStep = DrawStallStep::Restart;
		return;
	}
	case DrawStallStep::Restart:
		// Then the give-up: the same file previewed again, its clusters still held.
		g_drawStallWait = Wait{g_report, "the stalled preview settling", kMaxDrawStallTicks};
		g_dsRedrawsBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().redraws;
		g_dsGiveUpsBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().give_ups;
		g_dsDrawsBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().draws;
		g_dsCompleteBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().complete;
		deluge::gui::ui::shellPreviewer().request(kDrawStallPath, /*movementDirection=*/0);
		g_drawStallStep = DrawStallStep::Wait;
		return;
	case DrawStallStep::Wait: {
		const bool drawnAtAll =
		    deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().draws > g_dsDrawsBefore;
		const bool settled = drawnAtAll && !deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending()
		                     && !deluge::gui::ui::shellPreviewer().pending();
		switch (g_drawStallWait.poll(settled)) {
		case WaitState::Pending:
			deluge::harness::guestTick();
			return;
		case WaitState::Met:
			break;
		case WaitState::Expired:
			deluge_streaming_set_fill_latency_ms(0);
			g_drawStallExpired = true;
			g_drawStallStep = DrawStallStep::Finished;
			return;
		}
		g_dsPendingAtEnd = deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending();
		g_dsStallDraws = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().draws - g_dsDrawsBefore;
		g_dsStallComplete =
		    deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().complete - g_dsCompleteBefore;
		g_dsStallTicks = g_drawStallWait.ticks();
		deluge_streaming_set_fill_latency_ms(0);
		g_drawStallStep = DrawStallStep::Move;
		return;
	}
	case DrawStallStep::Move:
		// A cursor move onto another file, with the hold lifted: the browser must still preview it.
		g_dsMoveDrawsBefore = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().draws;
		deluge::gui::ui::shellPreviewer().request(kPreviewPath, /*movementDirection=*/0);
		g_drawStallWait = Wait{g_report, "the moved preview drawing", kMaxPreviewTicks};
		g_drawStallStep = DrawStallStep::MoveWait;
		return;
	case DrawStallStep::MoveWait:
		g_dsMovedDrawn = deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().draws > g_dsMoveDrawsBefore
		                 && deluge::model::lastFilePathLoaded() == kPreviewPath
		                 && !deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending();
		if (g_drawStallWait.poll(g_dsMovedDrawn) == WaitState::Pending) {
			deluge::harness::guestTick();
			return;
		}
		g_drawStallExpired = g_drawStallWait.expired();
		g_drawStallStep = DrawStallStep::Finished;
		return;
	case DrawStallStep::Finished:
		return;
	}
}

void finishDrawStallStage() {
	const int32_t redraws =
	    deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().redraws - g_dsRedrawsBefore;
	const int32_t giveUps =
	    deluge::ui::scenes::browser::browser_scene().waveform_draw_stats().give_ups - g_dsGiveUpsBefore;
	printf("AB-PREVIEW-DRAW-STALL draws=%d complete=%d ticks=%d redraws=%d giveUps=%d pendingAtEnd=%d movedDrawn=%d\n",
	       g_dsStallDraws, g_dsStallComplete, g_dsStallTicks, redraws, giveUps, g_dsPendingAtEnd ? 1 : 0,
	       g_dsMovedDrawn ? 1 : 0);
	printf("AB-PREVIEW-DRAW-RELEASE armed=%d redrawsAfter=%d pendingAfter=%d\n", g_dsArmed ? 1 : 0,
	       g_dsRedrawsAfterRelease, g_dsPendingAfterRelease ? 1 : 0);
	if (!g_dsArmed || g_dsRedrawsAfterRelease != 0 || g_dsPendingAfterRelease) {
		g_report.fail(
		    "[audioLoadAB/preview-draw-release]: a redraw pending when the preview's sample is released must be "
		    "dropped, never drawn from the released sample");
	}
	checkTier("draw-stall", g_drawStallTier);
	if (redraws < 1 || giveUps != 1 || g_dsPendingAtEnd || !g_dsMovedDrawn) {
		g_report.fail("[audioLoadAB/preview-draw-stall]: a preview whose clusters never arrived must redraw, give up "
		              "exactly once with nothing left pending, and leave the browser previewing the next file");
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
	AudioFile* file = audioFileManager.getAudioFileFromFilename(
	    fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	    /*makeWaveTableWorkAtAllCosts=*/false, deluge::storage::AsyncLoadMode::AsyncOrBlocking, &g_loadContext);
	deferred = (error == Error::LOAD_IN_PROGRESS);
	return file;
}

/// Put the "other card's" file at kSwapPath: the same path, a different file.
///
/// See the block comment above kSwapPath for why this is an unlink+rename rather than a block-device
/// swap, and why the code under test cannot tell the difference.
bool performCardSwap() {
	if (!deluge::io::unlink(kSwapPath).has_value()) {
		g_report.fail("[audioLoadAB/eject]: could not remove '%s' to stage the swapped card", kSwapPath);
		return false;
	}
	if (!deluge::io::rename(kSwapReplacementPath, kSwapPath).has_value()) {
		g_report.fail("[audioLoadAB/eject]: could not move '%s' into place at '%s'", kSwapReplacementPath, kSwapPath);
		return false;
	}
	return true;
}

/// One tick of the ejection stage. See the block comment above kSwapPath for the sequence.
void runOneEjectTick() {
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
		g_ejectWait = Wait{g_report, "the witness file loading", kMaxEjectTicks};
		return;
	}

	case EjectStep::AwaitWitness: {
		Error error = Error::NONE;
		bool deferred = false;
		AudioFile* file = ejectAttempt(kWitnessPath, error, deferred);
		switch (g_ejectWait.poll(!deferred)) {
		case WaitState::Pending:
			return; // Still coming. Ask again next tick — never kSwapPath.
		case WaitState::Met:
			break;
		case WaitState::Expired:
			g_ejectStep = EjectStep::Finished;
			return;
		}
		if (file == nullptr) {
			g_report.fail("[audioLoadAB/eject]: the witness file '%s' did not load (error=%d), so this stage cannot "
			              "know whether '%s' was ever parsed",
			              kWitnessPath, static_cast<int>(error), kSwapPath);
			g_ejectStep = EjectStep::Finished;
			return;
		}
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kWitnessPath);
		g_ejectStep = EjectStep::Swap;
		return;
	}

	case EjectStep::Swap:
		// The ejection, through the production entry point. This is the call the flush lives in, and
		// it must run BEFORE the new file appears — flushing on re-insertion instead would leave the
		// window open for as long as the slot stayed empty.
		audioFileManager.setCardEjected();
		if (!performCardSwap()) {
			g_ejectSwapFailed = true;
			g_ejectStep = EjectStep::Finished;
			return;
		}
		// And the re-insertion: remount against the "new" card and re-validate residents, exactly as
		// slowRoutine() drives it on the device.
		audioFileManager.reinitEjectedCard();
		// The reference for the post-swap load: the file that is on the card NOW, read synchronously.
		g_ejectReferenceLine = referenceLine("AB-EJECT", "swap", kSwapPath);
		g_ejectStep = EjectStep::Collect;
		g_ejectWait = Wait{g_report, "the swapped file's load answering", kMaxEjectTicks};
		return;

	case EjectStep::Collect: {
		Error error = Error::NONE;
		bool deferred = false;
		AudioFile* file = ejectAttempt(kSwapPath, error, deferred);
		if (deferred) {
			g_ejectCollectDeferrals++;
		}
		switch (g_ejectWait.poll(!deferred)) {
		case WaitState::Pending:
			return;
		case WaitState::Met:
			break;
		case WaitState::Expired:
			g_ejectStep = EjectStep::Finished;
			return;
		}
		// The one line this stage compares with its reference, and the one that carries the whole property: the
		// geometry printed here must be the file that is on the card NOW.
		printAgainstReference(
		    caseLine("AB-EJECT", "swap", file, error, std::string{kSwapPath}, g_loadContext.alternateDirStatus),
		    g_ejectReferenceLine, "eject");
		if (!checkLoadInvariants("audioLoadAB/eject")) {
			g_report.count();
		}
		if (file == nullptr) {
			g_report.fail("[audioLoadAB/eject]: '%s' did not load after the card swap (error=%d)", kSwapPath,
			              static_cast<int>(error));
		}
		else {
			auto& s = static_cast<Sample&>(*file);
			if (s.numChannels != kSwapReplacementChannels || s.byteDepth != kSwapReplacementByteDepth
			    || s.audioDataLengthBytes != kSwapReplacementDataLength) {
				g_report.fail(
				    "[audioLoadAB/eject]: '%s' loaded with channels=%u byteDepth=%u dataLen=%llu, but the "
				    "file at that path after the swap is channels=%u byteDepth=%u dataLen=%u -- a header parsed "
				    "from the PREVIOUS card was applied to this one's file, which loads with no error and plays "
				    "the wrong thing",
				    kSwapPath, s.numChannels, s.byteDepth, static_cast<unsigned long long>(s.audioDataLengthBytes),
				    kSwapReplacementChannels, kSwapReplacementByteDepth, kSwapReplacementDataLength);
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
	printf("AB-EJECT-SUMMARY swapped=%d requestDeferrals=%d collectDeferrals=%d\n", g_ejectSwapFailed ? 0 : 1,
	       g_ejectRequestDeferrals, g_ejectCollectDeferrals);

	// Without this, the stage could "pass" having never created the hazard at all: a pre-eject ask
	// that was answered synchronously (a permanent request decline falls through to the sync path)
	// leaves no uncollected result behind, and the post-swap load is then trivially correct.
	if (g_ejectRequestDeferrals == 0) {
		g_report.fail("[audioLoadAB/eject]: the pre-ejection ask for '%s' never deferred, so no uncollected result was "
		              "left for the ejection to invalidate -- this stage tested nothing",
		              kSwapPath);
	}
	// The direct witness that the flush ran. A flushed path reports UNKNOWN, which makes the
	// collector request it again and therefore defer at least once. A STALE result is READY on the
	// very first ask and defers not at all -- so zero here is the bug, whatever the geometry check
	// happened to say.
	if (!g_ejectSwapFailed && g_ejectCollectDeferrals == 0) {
		g_report.fail("[audioLoadAB/eject]: the post-swap collect for '%s' was answered without a single deferral, so "
		              "it came from a result parsed BEFORE the ejection rather than from the card now in the slot",
		              kSwapPath);
	}
}

} // namespace

extern "C" {

void deluge_scenario_audio_load_ab_begin(void) {
	g_caseIndex = 0;
	g_deferralsThisCase = 0;
	g_deferralsTotal = 0;
	g_deferralsByCase.fill(0);
	g_report.reset();
	g_stepDone = true;
	g_summaryPrinted = false;
	g_stage = Stage::Cases;
	g_previewStarted = false;
	g_previewTicks = 0;
	g_previewAborted = false;
	g_previewWait = Wait{g_report, "the preview drawing", kMaxPreviewTicks};
	g_previewRedrives = 0;
	g_cancelStarted = false;
	g_cancelTicks = 0;
	g_stallStep = StallStep::Start;
	g_stallTicks = 0;
	g_stallStartUs = 0;
	g_stallWaitedUs = 0;
	g_stallSettleTicks = 0;
	g_stallGiveUpsBefore = 0;
	g_stallPendingAtEnd = false;
	g_drawStallStep = DrawStallStep::Start;
	g_drawStallExpired = false;
	g_dsRedrawsBefore = 0;
	g_dsGiveUpsBefore = 0;
	g_dsPendingAtEnd = false;
	g_dsMovedDrawn = false;
	g_dsDrawsBefore = 0;
	g_dsMoveDrawsBefore = 0;
	g_dsCompleteBefore = 0;
	g_dsArmed = false;
	g_dsRedrawsAfterRelease = -1;
	g_dsPendingAfterRelease = true;
	g_drawStallTier = {};
	g_dsStallDraws = 0;
	g_dsStallComplete = 0;
	g_dsStallTicks = 0;
	g_prescanWait = Wait{g_report, "the peak scan finishing", kMaxPrescanTicks};
	g_drawStallWait = Wait{g_report, "the stalled preview arming a redraw", kMaxDrawStallTicks};
	g_ejectStep = EjectStep::Request;
	g_ejectWait = Wait{g_report, "the witness file loading", kMaxEjectTicks};
	g_ejectCollectDeferrals = 0;
	g_ejectRequestDeferrals = 0;
	g_ejectSwapFailed = false;
	g_selfdirAlternatePath.clear();
	g_bothStatusAfter = AlternateLoadDirStatus::MIGHT_EXIST;
	g_badRegStatusAfter = AlternateLoadDirStatus::MIGHT_EXIST;
	g_badRegError = Error::NONE;
	g_badRegLoaded = false;
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
	printf("AB-BEGIN cases=%zu\n", kCases.size());
}

bool deluge_scenario_audio_load_ab_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_audio_load_ab_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}

	if (g_stage == Stage::Preview || g_stage == Stage::PrescanRange || g_stage == Stage::Cancel
	    || g_stage == Stage::Stall || g_stage == Stage::DrawStall) {
		// The browser-driven stages run on the UI tier, not as the card step -- that is where the
		// real browser calls these from. guestTick() ticks the preview's load here and
		// draws it on this tier. The cancellation stage's window needs no ordering: request() only
		// records the file, so cancelPreview() lands before any tick has started its load.
		g_stepDone = false;
		if (g_stage == Stage::Preview) {
			const bool waited = runOnePreviewTick();
			const bool drawn =
			    previewHasDrawn() && !deluge::ui::scenes::browser::browser_scene().waveform_redraw_pending();
			if (g_previewAborted) {
				finishPreviewStage(false);
				g_stage = Stage::PrescanRange;
			}
			else if (waited || drawn) {
				switch (g_previewWait.poll(drawn)) {
				case WaitState::Pending:
					break;
				case WaitState::Met:
					finishPreviewStage(true);
					g_stage = Stage::PrescanRange;
					break;
				case WaitState::Expired:
					deluge_streaming_set_fill_latency_ms(0);
					g_stage = Stage::PrescanRange;
					break;
				}
			}
		}
		else if (g_stage == Stage::PrescanRange) {
			runOnePrescanTick();
			switch (g_prescanWait.poll(audioFileManager.peakScanAllDone)) {
			case WaitState::Pending:
				break;
			case WaitState::Met:
				finishPrescanStage();
				checkPeaks();
				g_stage = Stage::Cancel;
				break;
			case WaitState::Expired:
				g_stage = Stage::Cancel;
				break;
			}
		}
		else if (g_stage == Stage::Cancel) {
			runOneCancelTick();
			if (g_cancelTicks >= kCancelTicks) {
				finishCancelStage();
				g_stage = Stage::DrawStall;
			}
		}
		else if (g_stage == Stage::DrawStall) {
			runOneDrawStallTick();
			if (g_drawStallStep == DrawStallStep::Finished) {
				if (!g_drawStallExpired) {
					finishDrawStallStage();
				}
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
		// Nothing this tick queued runs anywhere else: the step is done when the tick returns.
		g_stepDone = true;
		return;
	}

	// The attempt opens files and binds a read stream, and the ejection stage renames a file and remounts the
	// volume: card I/O that runs from any context. It runs when the runner next asks whether the step is done --
	// one virtual millisecond after this dispatch; the stages are paced to that.
	g_stepDone = false;
	g_cardStepPending = true;
}

bool deluge_scenario_audio_load_ab_step_done(void) {
	if (g_cardStepPending) {
		g_cardStepPending = false;
		if (g_stage == Stage::Eject) {
			runOneEjectTick();
			if (g_ejectStep == EjectStep::Finished) {
				finishEjectStage();
				loadPeaksSample();
				g_stage = Stage::Preview;
			}
		}
		else {
			runOneStep();
		}
		g_stepDone = true;
	}
	return g_stepDone;
}

int32_t deluge_scenario_audio_load_ab_failures(void) {
	return g_report.failures();
}

} // extern "C"
