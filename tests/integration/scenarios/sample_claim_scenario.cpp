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

#include "scenarios/sample_claim_scenario.h"

#include "definitions_cxx.hpp"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/browser/sample_preview.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/slice_core.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/randomize_drums.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/audio_load.h" // deluge_audio_load_set_latency_ms
#include "libdeluge/control_surface.h"
#include "libdeluge/file_directory.h" // deluge_file_directory_set_latency_ms
#include "libdeluge/sample_source.h"
#include "libdeluge/streaming_fill.h"
#include "memory/fast_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "scenarios/guest_drive.h"
#include "scenarios/preset_fixture_loader.h"
#include "scenarios/slot_coherence.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h" // bytes::interactionTierIo
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table.h"

#include "../../dsp/wavetable_band_crc.h"

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_slice_ops.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/sample.h"
#include "scenes/menu/nodes/sound_root.h"
#include "scenes/registry.h"
#include "scenes/slicing/slicer_scene.h"
#include "util/shell_menu_entry.h"

#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases printf to an implementation this harness has no
// `putchar_` for -- every line below would become an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

namespace {

/// Each leg's file, asked for by that leg only (see pack_sample_claim_image in the harness).
constexpr const char* kSynthSample = "SAMPLES/SCSYN.WAV";

extern "C" void deluge_streaming_set_fill_latency_ms(uint32_t ms); // host harness only; streaming_loader.rs

/// The resume leg's file, the supersede legs' first files, and the pitch stall leg's file: see the Rust fixture.
constexpr const char* kPitchSample = "SAMPLES/SCPITCH.WAV";
constexpr const char* kSupersedeFirstLanded = "SAMPLES/SCSUPA.WAV";
constexpr const char* kSupersedeFirstLoading = "SAMPLES/SCSUPB.WAV";
constexpr const char* kPitchStallSample = "SAMPLES/SCPSTALL.WAV";
/// How long the streaming loader holds each drain pass in the resume and supersede legs: long enough that a detection
/// finds its cluster not yet filled, well inside the 300 ms bound, so it waits and resumes.
constexpr uint32_t kPitchHoldMs = 100;
/// ...and in the pitch stall leg: far past the bound.
constexpr uint32_t kPitchStallHoldMs = 2000;
/// Set on the synth's holder before a pitch leg: only a detection that found the note moves it.
constexpr int32_t kTransposeSentinel = 99;
/// SCSYN.WAV's measured transpose (SC-SYNTH transpose=3): the same sine must give the same one.
constexpr int32_t kPitchTranspose = 3;
/// IM-MULTI's measured transposes (SC-PROBE leg=import-multi): 110, 220 and 440 Hz.
constexpr int32_t kImportMultiTransposes[] = {15, 3, -9};
constexpr const char* kCycleSample = "SAMPLES/SCCYCLE.WAV";
/// The resident-wavetable leg's file: previewed (resident as a Sample, most clusters not yet filled) then claimed as a
/// wavetable, which builds it from the card: only a fully cached Sample is converted. See the Rust fixture.
constexpr const char* kResidentWaveTableSample = "SAMPLES/SCWTBIG.WAV";
/// The converted-wavetable leg's file: 32768 frames, three clusters (its 44-byte header pushes the last 44 bytes of
/// audio into a third), made fully resident as a Sample, then claimed as a wavetable,
/// which converts it from memory. Its twin holds the same bytes and is only ever built from the card.
constexpr const char* kFullWaveTableSample = "SAMPLES/SCWTFULL.WAV";
constexpr const char* kFullWaveTableTwin = "SAMPLES/SCWTFUL2.WAV";
/// The clusters the converted leg's file spans: 44 + 65536 bytes.
constexpr int32_t kFullWaveTableClusters = 3;
constexpr const char* kStereoSample = "SAMPLES/SCSTER.WAV";
/// The gate and stall legs' files: held by the loader from the moment their browser opens, so neither is in memory when
/// its claim begins.
constexpr const char* kGateSample = "SAMPLES/SCGATE.WAV";
constexpr const char* kStallSample = "SAMPLES/SCSTALL.WAV";
/// How long the loader holds each load in the gate leg: long enough that the claim is still loading when the encoder
/// turns, and over well inside the leg's bound.
constexpr uint32_t kGateHoldMs = 400;
/// How long the loader holds each load in the stall leg: far past the claim's stall bound.
constexpr uint32_t kStallHoldMs = 30000;
/// The randomizer's stall leg holds its load just past the walk's stall bound (`AsyncSampleLoad::kMaxStalledUs`, 18 s),
/// so the held load lands soon after the walk gives up on it and leaves the single-flight loader to the next leg.
constexpr uint32_t kRandomizeStallHoldMs = 19000;
/// The supersede legs' second files. The landed leg's is in memory from the gate leg, so its preview lands in the tick
/// the cursor reaches it; the loading leg's is not, and the loader holds its preview.
constexpr const char* kSupersedeLanded = "SAMPLES/SCGATE.WAV";
constexpr const char* kSupersedeLoading = "SAMPLES/SCKICK.WAV";
/// How long the loader holds the loading leg's second preview: long enough that the claim the cursor left would finish
/// first, were it not cancelled.
constexpr uint32_t kSupersedeHoldMs = 400;
/// The synth context menu's WaveTable option: Multisamples 0, Basic 1, Single-cycle 2, WaveTable 3.
constexpr int32_t kWaveTableOption = 3;
constexpr const char* kKitPresetSample = "SAMPLES/SCKITD.WAV";
constexpr const char* kKickSample = "SAMPLES/SCKICK.WAV";
constexpr const char* kClipSample = "SAMPLES/SCAUD.WAV";
constexpr const char* kSliceSample = "SAMPLES/SCSLICE.WAV";
/// The ported slicer's stalled open: a file nothing has loaded, so its load reaches the held card.
constexpr const char* kShellStallSample = "SAMPLES/SCSLCF.WAV";

/// The kit leg's one-drum kit preset, which the new-kit-track gesture loads.
constexpr const char* kKitPresetPath = "KITS/SCKIT.XML";
/// The ported slicer's legs' one-drum kits, one each: every slice leaves its kit with the drums it made.
constexpr const char* kShellKitPresetPaths[] = {"KITS/SCSKA.XML", "KITS/SCSKB.XML", "KITS/SCSKC.XML", "KITS/SCSKD.XML"};
constexpr const char* kKitDrumName = "SCD1";

/// The randomizer legs' kit preset, the third the new-kit-track gesture picks up, and the folder its drums name. The
/// randomizer picks from the folder a drum's current sample sits in, so these files are the randomizer's alone: every
/// other fixture is flat in SAMPLES/, where a pick from this folder can never reach it.
constexpr const char* kRandomizeKitPresetPath = "KITS/SCKIT3.XML";
constexpr const char* kRandomizeFolder = "SAMPLES/RD";
constexpr const char* kRandomizeFirstSample = "SAMPLES/RD/RD1.WAV";

/// The randomizer kit's drums, in the order the preset writes them: two rows that get a note, one muted row with a
/// note, and one row with no notes. Only the first two are eligible for an affect-entire randomize.
struct RandomizeDrumSpec {
	const char* name;
	bool note;
	bool muted;
};
constexpr RandomizeDrumSpec kRandomizeDrums[] = {
    {.name = "RDA", .note = true, .muted = false},
    {.name = "RDB", .note = true, .muted = false},
    {.name = "RDC", .note = true, .muted = true},
    {.name = "RDD", .note = false, .muted = false},
};
constexpr int32_t kRandomizeDrumCount = 4;
/// How many of those rows an affect-entire randomize may touch: the unmuted ones that have notes.
constexpr int32_t kRandomizeEligible = 2;

/// The gate leg's selected drum is pointed into a folder of files nothing has loaded, so its randomized load reaches
/// the loader, whose hold keeps the walk out while the second press and the edit pad arrive.
constexpr const char* kRandomizeGateSample = "SAMPLES/RDG/G1.WAV";
/// The stall leg's first row: alone in its folder, and loaded before the hold, so the walk lands it without the loader.
constexpr const char* kRandomizeStallKept = "SAMPLES/RDK/K1.WAV";
constexpr const char* kRandomizeStallKeptName = "K1";
/// The stall leg's second row: a folder of files nothing has loaded, so its load is held past the stall bound.
constexpr const char* kRandomizeStallSample = "SAMPLES/RDS/S1.WAV";
/// The column the gate leg's edit pad presses: empty on every row, inside the clip.
/// The cancel leg's hold on the DIRECTORY channel: the row's open is still out when the walk is cancelled, so the
/// cancel has to drain rather than end. Shorter than RandomizeDrums' own 300 ms give-up bound, or the drain would
/// give up before the handle it is waiting for ever arrived -- and it is the arrival that has to be collected.
constexpr uint32_t kRandomizeCancelHoldMs = 100;
/// Passes let through before each cancel. The first one puts the row's open on the channel (the walk requests from
/// its own await step, not from the press), so cancelling on it would find nothing outstanding and never drain.
constexpr int32_t kRandomizeCancelPasses = 2;
/// Cancels made, one per round: one more than the eight directory handles libdeluge/file_directory.h documents. A
/// drain that collected its handle without adopting it would have burned every slot before the last round, so the
/// ordinary randomize the leg ends with could not open its folder at all. That walk landing is the claim: counting
/// the collects cannot make it, since the collect is what happens either way.
constexpr int32_t kRandomizeCancelRounds = 9;

constexpr int32_t kRandomizeGateEditX = 1;

/// The folder import legs' folders, each its own: the kit import's three files share the prefix "IMK_", which the kit's
/// drum names drop; the multisample import's three sines sit an octave apart, a second long each.
constexpr const char* kImportKitFolder = "SAMPLES/IMK";
constexpr const char* kImportKitFile = "SAMPLES/IMK/IMK_HAT.WAV";
constexpr const char* kImportMultiFolder = "SAMPLES/IMM";
constexpr const char* kImportMultiFile = "SAMPLES/IMM/IMM_A2.WAV";
/// The kit import's drums, in filename order: the first reuses the kit's one drum.
constexpr const char* kImportKitNames[] = {"HAT", "KICK", "SNARE"};
constexpr int32_t kImportKitCount = 3;
/// The multisample import's files, in pitch order.
constexpr const char* kImportMultiFiles[] = {"SAMPLES/IMM/IMM_A2.WAV", "SAMPLES/IMM/IMM_A3.WAV",
                                             "SAMPLES/IMM/IMM_A4.WAV"};
constexpr int32_t kImportMultiCount = 3;
/// Each range's top note as today's importer sets it, measured: the rounded-down midpoint between detected pitches, and
/// the top range open-ended.
constexpr int32_t kImportMultiTopNotes[] = {50, 62, 32767};
/// The kit import leg's one-drum preset, the fourth the new-kit-track gesture picks up.
constexpr const char* kImportKitPresetPath = "KITS/SCKIT4.XML";
constexpr const char* kImportKitDrumName = "IMD1";
/// The multisample import leg's blank synth preset, the one synth preset on the card.
constexpr const char* kImportSynthPresetPath = "SYNTHS/SCSYN1.XML";
/// Both context menus' first option: the kit menu's "Load All", the synth menu's "Multisamples".
constexpr int32_t kImportOption = 0;

/// The import gate leg's folder and its one-drum kit's preset: files nothing loads before the leg, so the loader's hold
/// keeps the import out while BACK and Select arrive.
constexpr const char* kImportGateFile = "SAMPLES/IMG/IMG_A.WAV";
constexpr const char* kImportGatePresetPath = "KITS/SCKIT5.XML";
constexpr const char* kImportGateDrumName = "IMD2";
constexpr int32_t kImportGateCount = 3;
/// The empty import leg's folder: one subfolder and no audio file of its own, as `main`'s card's `SAMPLES/Artists`.
/// Its one-drum kit's preset is the sixth the new-kit-track gesture picks up.
constexpr const char* kImportEmptyFolder = "SAMPLES/IME";
constexpr const char* kImportEmptyPresetPath = "KITS/SCKIT6.XML";
constexpr const char* kImportEmptyDrumName = "IMD3";
/// The import stall leg's folder: its first and last files are loaded in the leg's preparation and land at once; the
/// middle one is loaded by nothing, so its load is held past the stall bound. The card lists a folder in the order its
/// packer found the files, so with the held file in the middle one in memory is always listed before it.
constexpr const char* kImportStallKept = "SAMPLES/IMS/IMS_A.WAV";
constexpr const char* kImportStallKeptLast = "SAMPLES/IMS/IMS_C.WAV";

/// How many regions the slice leg cuts the sample into.
constexpr int32_t kSliceRegions = 4;

/// The manual slice leg's slices, each a start frame and a transpose, as pad presses on a playing voice would add them.
constexpr SliceItem kManualSlices[] = {
    {.startPos = 0, .transpose = 1}, {.startPos = 11025, .transpose = 2}, {.startPos = 33075, .transpose = 3}};
constexpr int32_t kManualSliceCount = 3;

/// Scenario ticks a browser listing may take before it is called wedged.
constexpr int32_t kMaxListingTicks = 20000;
/// Steps a leg may take to bring the browser to its item (a favourite recalled, a folder left, a turn) before the
/// browser is called lost.
constexpr int32_t kMaxBrowseSteps = 4;
/// Scenario ticks a claim or a slice may take before it is called wedged.
constexpr int32_t kMaxLegTicks = 2000;
/// Scenario ticks the stall leg's claim may take to give up before it is called wedged: far past its stall bound.
constexpr int32_t kMaxStallTicks = 20000;

enum class Leg : uint8_t {
	Synth,
	WaveTable,
	WaveTableResident,
	WaveTableConverted,
	Fallback,
	Gate,
	Supersede,
	SupersedeLoading,
	CancelLoading,
	Pitch,
	Kit,
	AudioClip,
	// The ported slicer's host, through the same SliceCore: a region slice, a manual slice, and BACK out of manual
	// mode on a first drum that holds no file. Its held open is ShellSliceFail, with the other loader-hold legs.
	ShellSlice,
	ShellSliceManual,
	ShellSliceBack,
	RandomizeOne,
	RandomizeAll,
	RandomizeGate,
	ImportKit,
	ImportMulti,
	ImportGate,
	ImportEmpty,
	// Before the loader-hold legs: the audio loader is single-flight, and a claim asked for behind a held load stalls.
	PitchStall,
	Stall,
	RandomizeStall,
	RandomizeCancelScan,
	ImportStall,
	ShellSliceFail,
	Done
};
enum class Step : uint8_t { Prepare, Open, Listing, Select, PreviewWait, ResidentWait, Act, Wait };

Leg g_leg = Leg::Synth;
Step g_step = Step::Prepare;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
/// The steps the current leg has taken to bring the browser to its item.
int32_t g_browseSteps = 0;
SoundInstrument* g_synth = nullptr;
Clip* g_kitClip = nullptr;
Clip* g_audioClip = nullptr;
/// The folder import legs' clips: a one-drum kit and a synth, each of its own, so no other leg's clip is changed.
Clip* g_importKitClip = nullptr;
Clip* g_importSynthClip = nullptr;
/// The import gate leg's one-drum kit.
Clip* g_importGateClip = nullptr;
/// The empty import leg's one-drum kit, and how its import ended: told at all, and whether it succeeded.
Clip* g_importEmptyClip = nullptr;
bool g_imEmptyTold = false;
bool g_imEmptySucceeded = false;

/// The ported slicer's legs: whether the commit was begun, whether the leg has ended, and the BACK leg's verdict.
bool g_shellCommitBegun = false;
bool g_shellEnded = false;
bool g_shellBackKept = false;

/// The region leg's song-load checks. A teardown of every UI (a song load) must drop the slicer's held audition, whose
/// load names the outgoing kit's first drum; and a MIDI next-song that arrives while the slice is out must wait for it.
bool g_teardownHeld = false;
bool g_teardownRootKept = false;
bool g_teardownDropped = false;
bool g_nextHeld = false;
bool g_nextHeldThrough = true;
/// The channel and note the next-song command is learned on for the check, a channel nothing else here listens on,
/// and the command as the scenario found it.
constexpr uint8_t kNextSongChannel = 15;
constexpr uint8_t kNextSongNote = 0x31;
LearnedMIDI g_nextCommandBefore{};

bool isShellSlice() {
	return g_leg == Leg::ShellSlice || g_leg == Leg::ShellSliceManual || g_leg == Leg::ShellSliceBack
	       || g_leg == Leg::ShellSliceFail;
}

size_t shellKitIndex() {
	switch (g_leg) {
	case Leg::ShellSliceManual:
		return 1;
	case Leg::ShellSliceBack:
		return 2;
	case Leg::ShellSliceFail:
		return 3;
	default:
		return 0;
	}
}

/// What the import gate leg saw: whether its presses were tried, whether the import was still out when they were, and
/// whether each was refused.
bool g_imGateTried = false;
bool g_imGateWhileRunning = false;
bool g_imGateSecondRefused = false;
bool g_imGateBackRefused = false;

/// The import stall leg's reference: the two files in memory and their reason counts, and the synth's range paths,
/// before the import.
Sample* g_imStallKept[2]{};
uint32_t g_imStallKeptLeases[2]{};
std::vector<std::string> g_imStallRangePaths{};

/// The interaction tier's blocking sample reads when an import leg acts.
struct ImportMark {
	int32_t sampleIo = 0;
};
ImportMark g_importMark{};

/// The interaction tier's blocking sample reads when a leg acts.
struct TierMark {
	int32_t sampleIo = 0;
};
TierMark g_tierMark{};

/// The interaction tier's blocking sample reads when a randomizer leg acts.
struct RandomizeMark {
	int32_t sampleIo = 0;
};
RandomizeMark g_randomizeMark{};

/// Each randomizer row's path when the leg acted: a row the gesture must not touch keeps it.
std::string g_randomizeRowPaths[kRandomizeDrumCount];

/// Whether the gate leg's select-encoder turn moved the browser's selection, and whether its claim was out when the
/// encoder turned.
bool g_gateMoved = false;
bool g_gateClaimOut = false;

/// Whether the supersede leg has moved the cursor on, which it does the moment the first auto-load claim is waiting
/// on its pitch detection.
bool g_supersedeFired = false;

/// The claim's counts when a leg acted.
int32_t g_giveUpsBefore = 0;
int32_t g_cancelsBefore = 0;
int32_t g_finishesBefore = 0;

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::Synth:
		return "synth";
	case Leg::WaveTable:
		return "wavetable";
	case Leg::Fallback:
		return "fallback";
	case Leg::Gate:
		return "gate";
	case Leg::Stall:
		return "stall";
	case Leg::Supersede:
		return "supersede";
	case Leg::SupersedeLoading:
		return "supersede-loading";
	case Leg::CancelLoading:
		return "cancel-loading";
	case Leg::Kit:
		return "kit";
	case Leg::AudioClip:
		return "audioclip";
	case Leg::ShellSlice:
		return "shell-slice";
	case Leg::ShellSliceManual:
		return "shell-slice-manual";
	case Leg::ShellSliceBack:
		return "shell-slice-back";
	case Leg::ShellSliceFail:
		return "shell-slice-fail";
	case Leg::RandomizeOne:
		return "randomize-one";
	case Leg::RandomizeAll:
		return "randomize-all";
	case Leg::RandomizeGate:
		return "randomize-gate";
	case Leg::ImportKit:
		return "import-kit";
	case Leg::ImportMulti:
		return "import-multi";
	case Leg::ImportGate:
		return "import-gate";
	case Leg::ImportEmpty:
		return "import-empty";
	case Leg::ImportStall:
		return "import-stall";
	case Leg::RandomizeStall:
		return "randomize-stall";
	case Leg::RandomizeCancelScan:
		return "randomize-cancelscan";
	case Leg::WaveTableResident:
		return "wavetable-resident";
	case Leg::WaveTableConverted:
		return "wavetable-converted";
	case Leg::Pitch:
		return "pitch";
	case Leg::PitchStall:
		return "pitch-stall";
	case Leg::Done:
		return "done";
	}
	return "?";
}

bool fail(const char* leg, const char* what) {
	printf("SC-SETUP FAIL leg=%s %s\n", leg, what);
	g_failures++;
	return false;
}

/// Fixture builds a tick asked for, run when the runner next asks whether the step is done: one virtual
/// millisecond after the tick, matching the pacing the gates are already tuned to. Builds are queued here
/// rather than run inside the tick, to keep that order.
std::vector<void (*)()> g_pendingFixtures{};

/// Queue a fixture build for the end of this step. See g_pendingFixtures.
void buildFixture(void (*fn)()) {
	g_pendingFixtures.push_back(fn);
}

/// Run the next build this step queued, if any: one per call, so each build gets a poll of its own.
/// @return Whether a build ran -- the step is not done on a call that ran one.
bool runPendingFixture() {
	if (g_pendingFixtures.empty()) {
		return false;
	}
	void (*fn)() = g_pendingFixtures.front();
	g_pendingFixtures.erase(g_pendingFixtures.begin());
	fn();
	return true;
}

ImportMark markImport() {
	return {deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

RandomizeMark markRandomize() {
	return {deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

TierMark markTier() {
	return {deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::SampleLoad)};
}

/// SC-TIER: since the leg acted, no sample may have been read from the card on the interaction tier.
void checkTier() {
	const TierMark now = markTier();
	const int32_t sampleIo = now.sampleIo - g_tierMark.sampleIo;
	printf("SC-TIER leg=%s sampleIo=%d\n", legName(g_leg), sampleIo);
	if (sampleIo != 0) {
		printf("SC-TIER FAIL leg=%s %d sample read(s) went to the card on the interaction tier\n", legName(g_leg),
		       sampleIo);
		g_failures++;
	}
}

AudioFileHolder* firstHolder(Source& source) {
	MultiRange* range = source.getOrCreateFirstRange();
	return (range != nullptr) ? range->getAudioFileHolder() : nullptr;
}

int32_t g_pitchTicksBefore = 0;
int32_t g_pitchGiveUpsBefore = 0;

/// Arm a pitch leg: the streaming loader held for @p holdMs, the synth's transpose set to the sentinel, the claim's
/// pitch counts marked.
void armPitchLeg(uint32_t holdMs) {
	deluge_streaming_set_fill_latency_ms(holdMs);
	if (AudioFileHolder* holder = firstHolder(g_synth->sources[0]); holder != nullptr) {
		static_cast<SampleHolderForVoice*>(holder)->transpose = kTransposeSentinel;
	}
	g_pitchTicksBefore = deluge::gui::sampleClaim.pitchScanTicks();
	g_pitchGiveUpsBefore = deluge::gui::sampleClaim.pitchGiveUps();
}

// ── Fixture construction ─────────────────────────────────────────────────────

/// A sample drum, the way the slicer builds one, added to @p kit.
SoundDrum* addSampleDrum(Kit& kit, const char* name, const char* samplePath) {
	ParamManagerForTimeline paramManager;
	if (paramManager.setupWithPatching() != Error::NONE) {
		fail("kit", "a drum's ParamManager could not be set up");
		return nullptr;
	}
	void* memory = deluge::memory::alloc_fast(sizeof(SoundDrum));
	if (memory == nullptr) {
		fail("kit", "no memory for a drum");
		return nullptr;
	}
	auto* drum = new (memory) SoundDrum();
	Sound::initParams(&paramManager);
	kit.addDrum(drum);
	drum->setName(name);
	drum->setupAsSample(&paramManager);
	drum->sources[0].setOscType(OscType::SAMPLE);
	AudioFileHolder* holder = firstHolder(drum->sources[0]);
	if (holder == nullptr) {
		fail("kit", "a drum has no sample range");
		return nullptr;
	}
	holder->filePath = samplePath;
	// The fixture stands in for a claim, and records the drum's ranges as a claim does.
	deluge::edits::recordRanges(*drum, 0);
	currentSong->live.param_backup.backUp(drum, nullptr, &paramManager, true);
	return drum;
}

/// The one-drum kit preset, written through the firmware serialiser. The kit is leaked: it is never added to the song.
/// Write a kit preset of @p count drums to @p path through the firmware's own serialiser, each drum named and
/// pointed at a sample by @p drums.
struct KitPresetDrum {
	const char* name;
	const char* samplePath;
};
bool writeKitPreset(const char* path, const KitPresetDrum* drums, int32_t count) {
	ParamManagerForTimeline kitParams;
	Instrument* kitBase = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (kitBase == nullptr) {
		return fail("kit", "no kit for the preset");
	}
	auto* kit = static_cast<Kit*>(kitBase);
	currentSong->live.param_backup.backUp(*kit, nullptr, &kitParams, true);
	for (int32_t i = 0; i < count; i++) {
		if (addSampleDrum(*kit, drums[i].name, drums[i].samplePath) == nullptr) {
			return false;
		}
	}
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("kit", "the kit preset file could not be created");
	}
	kit->writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</kit>\n");
	return error == Error::NONE || fail("kit", "the kit preset file could not be closed");
}

/// A blank synth preset, written through the firmware's own new-synth and save paths (never hand-written: a
/// content-empty preset corrupts the heap on the load that rejects it). The synth is leaked: it is never added to the
/// song.
bool writeSynthPreset(const char* path) {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		return fail("import-multi", "no synth for the preset");
	}
	static_cast<SoundInstrument*>(instrument)->setupAsBlankSynth(&paramManager, false);
	currentSong->live.param_backup.backUp(*instrument, nullptr, &paramManager, true);
	Error error = StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false);
	if (error != Error::NONE) {
		return fail("import-multi", "the synth preset file could not be created");
	}
	instrument->writeToFile(nullptr, currentSong);
	error = GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</sound>\n");
	return error == Error::NONE || fail("import-multi", "the synth preset file could not be closed");
}

// ── Driving the browser ──────────────────────────────────────────────────────

using deluge::harness::browserOnTop;
using deluge::harness::guestBack;
using deluge::harness::guestClose;
using deluge::harness::guestLongPressSelect;
using deluge::harness::guestPickOption;
using deluge::harness::guestSelect;
using deluge::harness::guestSelectedName;
using deluge::harness::guestSelectedPath;
using deluge::harness::guestTick;
using deluge::harness::guestTop;

/// Tick the guest while a browser listing runs, and say whether it still is.
bool listingBusy() {
	guestTick();
	return deluge::scenes::browser::browser_scene().listing_in_flight();
}

/// Step a pad scroll the browser started (a preview's entry animation) to its end, as PadLEDs::timerRoutine() does on
/// the device, where the scroll ends in the current UI's scrollFinished() within a few milliseconds. Nothing steps it
/// headless, and until it ends the browser's Select refuses a press.
void finishScroll() {
	int32_t steps = 0;
	while (isUIModeActive(UI_MODE_HORIZONTAL_SCROLL) && steps <= kDisplayWidth + kSideBarWidth) {
		PadLEDs::horizontal::renderScroll();
		steps++;
	}
	printf("SC-PROBE leg=%s scroll steps=%d modeAfter=%d\n", legName(g_leg), steps, currentUIMode);
}

/// Make @p clip current under @p rootUI alone, with @p drum selected when one is given; a guest a leg before left open
/// is closed first, as its user would close it.
void frontClip(const char* leg, Clip* clip, UI* rootUI, SoundDrum* drum) {
	guestClose();
	currentSong->setCurrentClip(clip);
	changeRootUI(rootUI);
	if (drum != nullptr) {
		// Selecting a drum is refused while the sound editor or the browser is the current UI, so it happens here, with
		// only the clip view open, as a user auditions a row before opening its file selector.
		auto* kit = static_cast<Kit*>(clip->output);
		static_cast<InstrumentClip*>(clip)->affectEntire = false;
		instrumentClipView.setSelectedDrum(drum, /*shouldRedrawStuff=*/false, kit, /*shouldSendMidiFeedback=*/false);
		printf("SC-PROBE leg=%s rootOnTop=%d selected=%d\n", leg, getCurrentUI() == rootUI ? 1 : 0,
		       kit->selectedDrum == drum ? 1 : 0);
	}
}

/// Make @p clip current under @p rootUI and open the menu on its first source's file selector, whose hook pushes the
/// browser: the gesture a user makes to pick a sample. The browser opens beside the file the source holds, or the
/// last one loaded; the leg's Select step brings it to the leg's item from there.
bool openBrowser(const char* leg, Clip* clip, UI* rootUI, SoundDrum* drum = nullptr) {
	frontClip(leg, clip, rootUI, drum);
	const bool opened =
	    deluge::gui::ui::openShellMenu(clip, /*sourceIndex=*/0, /*performanceView=*/false,
	                                   /*sessionOrArrangerRoot=*/false, /*sessionViewRoot=*/false,
	                                   /*kitGlobalFx=*/false, deluge::gui::ui::ShellMenuNode::Osc0FileSelector);
	for (int32_t t = 0; opened && t < 16 && !browserOnTop(); t++) {
		guestTick();
		if (t > 0 && guestTop(deluge::scenes::menu::kMenuSceneKind)) {
			// A sound with several ranges sends its file selector to the range menu first; a user picks a range
			// there before the browser opens, and the browser reads the range it opens on: the first row, range 0.
			guestSelect();
			printf("SC-PROBE leg=%s rangePicked=0\n", leg);
		}
	}
	printf("SC-SETUP leg=%s opened=%d browserOnTop=%d\n", leg, opened ? 1 : 0, browserOnTop() ? 1 : 0);
	return browserOnTop() || fail(leg, "the sample browser could not be opened");
}

/// The sample the browser's preview holds once its load has landed, or null while it loads or holds none.
Sample* previewedSample() {
	if (deluge::gui::ui::shellPreviewer().pending()) {
		return nullptr;
	}
	Source& source = AudioEngine::sampleForPreview->sources[0];
	if (source.ranges.empty()) {
		return nullptr;
	}
	AudioFile* file = static_cast<MultisampleRange*>(source.ranges.getElement(0))->sampleHolder.audioFile;
	return (file != nullptr && file->type == AudioFileType::SAMPLE) ? static_cast<Sample*>(file) : nullptr;
}

/// Remember every randomizer row's path, so a row the gesture must leave alone can be shown to still hold it.
void recordRandomizeRows() {
	int32_t index = 0;
	for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum; drum != nullptr && index < kRandomizeDrumCount;
	     drum = drum->next, index++) {
		AudioFileHolder* holder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
		g_randomizeRowPaths[index] = (holder != nullptr) ? holder->filePath : "";
	}
}

SoundDrum* kitDrum() {
	if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
		return nullptr;
	}
	Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum;
	return (drum != nullptr && drum->type == DrumType::SOUND) ? static_cast<SoundDrum*>(drum) : nullptr;
}

/// The randomizer kit's clip current under the clip view, with @p drum selected when one is given. No browser: the
/// gesture is a pad press in the clip view itself.
bool openRandomizeLeg(const char* leg, SoundDrum* drum) {
	if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
		return fail(leg, "the randomizer kit clip is missing");
	}
	guestClose();
	currentSong->setCurrentClip(g_kitClip);
	changeRootUI(&instrumentClipView);
	auto* kit = static_cast<Kit*>(g_kitClip->output);
	static_cast<InstrumentClip*>(g_kitClip)->affectEntire = false;
	if (drum != nullptr) {
		instrumentClipView.setSelectedDrum(drum, /*shouldRedrawStuff=*/false, kit, /*shouldSendMidiFeedback=*/false);
	}
	const bool onTop = getCurrentUI() == &instrumentClipView;
	printf("SC-SETUP leg=%s clipViewOnTop=%d selected=%d mode=%d\n", leg, onTop ? 1 : 0,
	       (drum != nullptr && kit->selectedDrum == drum) ? 1 : 0, (int)currentUIMode);
	return onTop || fail(leg, "the clip view could not be made current");
}

/// The kit clip current, its first drum selected, and the browser opened on that drum.
bool openBrowserOnKitDrum(const char* leg) {
	SoundDrum* drum = kitDrum();
	if (drum == nullptr) {
		return fail(leg, "the kit clip has no sound drum");
	}
	return openBrowser(leg, g_kitClip, &instrumentClipView, drum);
}

/// The boot song's synth clip current, its first oscillator set to @p osc, and the browser opened on it.
bool openBrowserOnSynth(const char* leg, OscType osc) {
	for (Output* output = currentSong->live.media.first_output; output != nullptr && g_synth == nullptr;
	     output = output->next) {
		if (output->type == OutputType::SYNTH) {
			g_synth = static_cast<SoundInstrument*>(output);
		}
	}
	Clip* clip = (g_synth != nullptr) ? currentSong->getClipWithOutput(g_synth) : nullptr;
	if (clip == nullptr) {
		return fail(leg, "the boot song has no synth clip");
	}
	g_synth->sources[0].setOscType(osc);
	return openBrowser(leg, clip, &instrumentClipView);
}

// ── The legs ─────────────────────────────────────────────────────────────────

/// The conversion counters, taken before a wavetable claim so a leg reads only its own.
struct ConversionMark {
	int32_t converted;
	int32_t fromCard;
	int32_t pinned;
};
ConversionMark g_convBefore{};

ConversionMark markConversions() {
	return {audioFileManager.waveTablesConverted(), audioFileManager.waveTablesBuiltFromCard(),
	        audioFileManager.conversionPinnedClusters()};
}

ConversionMark conversionsSince(const ConversionMark& before) {
	const ConversionMark now = markConversions();
	return {now.converted - before.converted, now.fromCard - before.fromCard, now.pinned - before.pinned};
}

/// Whether every audio cluster of @p sample is resident, asking for any that is not. Pins nothing beyond the call.
bool acquireAllClusters(Sample& sample) {
	DelugeSampleGeometry geometry{};
	geometry.audio_data_start_bytes = sample.audioDataStartPosBytes;
	geometry.audio_data_length_bytes = sample.audioDataLengthBytes;
	geometry.cluster_size_bytes = static_cast<uint32_t>(Cluster::size);
	geometry.byte_depth = sample.byteDepth;
	geometry.num_channels = sample.numChannels;
	geometry.raw_data_format = static_cast<uint8_t>(sample.rawDataFormat);
	DelugeSampleSource* source = deluge_sample_source_open(deluge::audio::stream::toAbi(sample.stream()), geometry);
	if (source == nullptr) {
		return false;
	}
	const uint32_t first = sample.audioDataStartPosBytes >> Cluster::size_magnitude;
	const uint32_t last = (sample.audioDataStartPosBytes + static_cast<uint32_t>(sample.audioDataLengthBytes) - 1)
	                      >> Cluster::size_magnitude;
	bool all = true;
	for (uint32_t i = first; i <= last; i++) {
		DelugeSampleRegion region{};
		if (deluge_sample_region_acquire_ex(source, i, +1, 0, &region) != DELUGE_REGION_READY) {
			all = false;
		}
	}
	deluge_sample_source_close(source); // releases every lease the cursor took
	if (!all) {
		// A fill is queued, not run: ask the fill task for a drain, which runs between this tick and the next.
		deluge_streaming_request_drain();
	}
	return all;
}

void prepareLeg() {
	switch (g_leg) {
	case Leg::Kit:
		buildFixture([] {
			const KitPresetDrum presetDrum{.name = kKitDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kKitPresetPath, &presetDrum, 1)) {
				// The new-kit-track gesture, which loads kKitPresetPath.
				g_kitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 1, kKitPresetPath);
			}
			printf("SC-PROBE leg=kit prepared kitClip=%d\n", g_kitClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::AudioClip:
		buildFixture([] { g_audioClip = sessionView.createNewClip(OutputType::AUDIO, 2); });
		break;
	case Leg::RandomizeOne:
		buildFixture([] {
			// A kit of its own, whose drums all name the randomizer folder's first file: the randomizer picks from the
			// folder a drum's sample sits in, and every other fixture is outside it.
			KitPresetDrum drums[kRandomizeDrumCount];
			for (int32_t i = 0; i < kRandomizeDrumCount; i++) {
				drums[i] = {.name = kRandomizeDrums[i].name, .samplePath = kRandomizeFirstSample};
			}
			if (!writeKitPreset(kRandomizeKitPresetPath, drums, kRandomizeDrumCount)) {
				return;
			}
			g_kitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 4, kRandomizeKitPresetPath);
			if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
				fail("randomize-one", "no kit clip could be created from the randomizer preset");
				return;
			}
			// The rows a user would have: notes on three, one of them muted, and one row left empty. An affect-entire
			// randomize walks unmuted rows that have notes, so only the first two are eligible.
			auto* kitClip = static_cast<InstrumentClip*>(g_kitClip);
			alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, kitClip);
			int32_t drumIndex = 0;
			int32_t rowsWithNotes = 0;
			for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum;
			     drum != nullptr && drumIndex < kRandomizeDrumCount; drum = drum->next, drumIndex++) {
				ModelStackWithNoteRow* rowStack = kitClip->getNoteRowForDrum(modelStack, drum);
				NoteRow* row = (rowStack != nullptr) ? rowStack->getNoteRowAllowNull() : nullptr;
				if (row == nullptr) {
					fail("randomize-one", "a randomizer kit drum has no note row");
					return;
				}
				if (kRandomizeDrums[drumIndex].note) {
					if (row->attemptNoteAdd(0, 1, 64, row->getDefaultProbability(), row->getDefaultIterance(),
					                        row->getDefaultFill(rowStack), rowStack, nullptr)
					    == 0) {
						fail("randomize-one", "a note could not be added to a randomizer kit row");
						return;
					}
					rowsWithNotes++;
				}
				row->setMuted(kRandomizeDrums[drumIndex].muted);
			}
			// The gesture is refused outright while the community feature is off.
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::DrumRandomizer, RuntimeFeatureStateToggle::On);
			printf("SC-PROBE leg=randomize-one prepared kitClip=%d drums=%d rowsWithNotes=%d feature=%d\n",
			       g_kitClip != nullptr ? 1 : 0, drumIndex, rowsWithNotes,
			       runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::DrumRandomizer) ? 1 : 0);
		});
		break;
	case Leg::RandomizeCancelScan:
		buildFixture([] {
			// The legs before this one left the first drum on the stall folder. Back to the randomizer's own folder,
			// unbound: a cancelled walk must leave both exactly as they are, so both are what the check reads.
			SoundDrum* drum = kitDrum();
			AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
			if (holder == nullptr) {
				fail("randomize-cancelscan", "the randomizer kit's first drum has no holder");
				return;
			}
			holder->setAudioFile(nullptr);
			holder->filePath = kRandomizeFirstSample;
			deluge::edits::recordRanges(*drum, 0);
			drum->setName(kRandomizeDrums[0].name);
		});
		break;
	case Leg::RandomizeGate:
		buildFixture([] {
			SoundDrum* drum = kitDrum();
			AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
			if (holder == nullptr) {
				fail("randomize-gate", "the randomizer kit's first drum has no holder");
				return;
			}
			holder->setAudioFile(nullptr);
			holder->filePath = kRandomizeGateSample;
			deluge::edits::recordRanges(*drum, 0);
		});
		break;
	case Leg::RandomizeStall:
		buildFixture([] {
			if (g_kitClip == nullptr || g_kitClip->output == nullptr || g_kitClip->output->type != OutputType::KIT) {
				fail("randomize-stall", "the randomizer kit clip is missing");
				return;
			}
			Drum* first = static_cast<Kit*>(g_kitClip->output)->firstDrum;
			Drum* second = (first != nullptr) ? first->next : nullptr;
			Drum* third = (second != nullptr) ? second->next : nullptr;
			AudioFileHolder* kept =
			    (first != nullptr) ? firstHolder(static_cast<SoundDrum*>(first)->sources[0]) : nullptr;
			AudioFileHolder* held =
			    (second != nullptr) ? firstHolder(static_cast<SoundDrum*>(second)->sources[0]) : nullptr;
			AudioFileHolder* after =
			    (third != nullptr) ? firstHolder(static_cast<SoundDrum*>(third)->sources[0]) : nullptr;
			NoteRow* afterRow =
			    (third != nullptr) ? static_cast<InstrumentClip*>(g_kitClip)->getNoteRowForDrum(third) : nullptr;
			if (kept == nullptr || held == nullptr || after == nullptr || afterRow == nullptr) {
				fail("randomize-stall", "the randomizer kit's first three drums have no holders or rows");
				return;
			}
			kept->setAudioFile(nullptr);
			kept->filePath = kRandomizeStallKept;
			// Loaded now, before the hold: the first row's only choice is already in memory when the walk reaches it.
			const Error error =
			    kept->loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
			held->setAudioFile(nullptr);
			held->filePath = kRandomizeStallSample;
			// The third row: eligible now, and pointing at the first row's file, already in memory.
			after->setAudioFile(nullptr);
			after->filePath = kRandomizeStallKept;
			const Error afterError =
			    after->loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
			// The fixture stands in for three claims, and records each row's ranges as a claim does.
			for (Drum* row : {first, second, third}) {
				deluge::edits::recordRanges(*static_cast<SoundDrum*>(row), 0);
			}
			afterRow->setMuted(false);
			printf("SC-PROBE leg=randomize-stall prepared keptBound=%d error=%d afterBound=%d afterError=%d\n",
			       kept->audioFile != nullptr ? 1 : 0, static_cast<int>(error), after->audioFile != nullptr ? 1 : 0,
			       static_cast<int>(afterError));
		});
		break;
	case Leg::ImportKit:
		buildFixture([] {
			// A one-drum kit of its own: the kit menu offers "Load All" only into a kit with one drum.
			const KitPresetDrum presetDrum{.name = kImportKitDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kImportKitPresetPath, &presetDrum, 1)) {
				g_importKitClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 5, kImportKitPresetPath);
			}
			printf("SC-PROBE leg=import-kit prepared kitClip=%d\n", g_importKitClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::ImportMulti:
		buildFixture([] {
			// A synth of its own, from a preset of its own: the multisample import replaces its source's ranges, and
			// the new-synth-track gesture loads it.
			if (writeSynthPreset(kImportSynthPresetPath)) {
				g_importSynthClip =
				    deluge::harness::createNewInstrumentClip(OutputType::SYNTH, 6, kImportSynthPresetPath);
			}
			printf("SC-PROBE leg=import-multi prepared synthClip=%d type=%d\n", g_importSynthClip != nullptr ? 1 : 0,
			       (g_importSynthClip != nullptr && g_importSynthClip->output != nullptr)
			           ? static_cast<int>(g_importSynthClip->output->type)
			           : -1);
		});
		break;
	case Leg::ImportGate:
		buildFixture([] {
			// A one-drum kit of its own: the import kit leg's kit holds three drums by now.
			const KitPresetDrum presetDrum{.name = kImportGateDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kImportGatePresetPath, &presetDrum, 1)) {
				g_importGateClip = deluge::harness::createNewInstrumentClip(OutputType::KIT, 7, kImportGatePresetPath);
			}
			printf("SC-PROBE leg=import-gate prepared kitClip=%d\n", g_importGateClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::ImportEmpty:
		buildFixture([] {
			const KitPresetDrum presetDrum{.name = kImportEmptyDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kImportEmptyPresetPath, &presetDrum, 1)) {
				g_importEmptyClip =
				    deluge::harness::createNewInstrumentClip(OutputType::KIT, 8, kImportEmptyPresetPath);
			}
			printf("SC-PROBE leg=import-empty prepared kitClip=%d\n", g_importEmptyClip != nullptr ? 1 : 0);
		});
		break;
	case Leg::ImportStall:
		buildFixture([] {
			// The first and last files in memory before the hold, and the synth's ranges as the multisample leg left
			// them.
			Error error = Error::NONE;
			const char* keptPaths[2] = {kImportStallKept, kImportStallKeptLast};
			for (int32_t i = 0; i < 2; i++) {
				std::string keptPath{keptPaths[i]};
				g_imStallKept[i] = static_cast<Sample*>(
				    audioFileManager.getAudioFileFromFilename(keptPath, true, &error, AudioFileType::SAMPLE));
				g_imStallKeptLeases[i] = (g_imStallKept[i] != nullptr) ? g_imStallKept[i]->leaseCount() : 0;
			}
			g_imStallRangePaths.clear();
			if (g_importSynthClip != nullptr && g_importSynthClip->output != nullptr) {
				Source& source = static_cast<SoundInstrument*>(g_importSynthClip->output)->sources[0];
				for (int32_t i = 0; i < std::ssize(source.ranges); i++) {
					g_imStallRangePaths.push_back(
					    static_cast<MultisampleRange*>(source.ranges.getElement(i))->sampleHolder.filePath);
				}
			}
			printf("SC-PROBE leg=import-stall prepared kept=%d leases=%u,%u ranges=%d error=%d\n",
			       (g_imStallKept[0] != nullptr && g_imStallKept[1] != nullptr) ? 1 : 0, g_imStallKeptLeases[0],
			       g_imStallKeptLeases[1], static_cast<int>(g_imStallRangePaths.size()), static_cast<int>(error));
		});
		break;
	case Leg::ShellSlice:
	case Leg::ShellSliceManual:
	case Leg::ShellSliceBack:
	case Leg::ShellSliceFail:
		buildFixture([] {
			const KitPresetDrum presetDrum{.name = kKitDrumName, .samplePath = kKitPresetSample};
			if (writeKitPreset(kShellKitPresetPaths[shellKitIndex()], &presetDrum, 1)) {
				g_kitClip =
				    deluge::harness::createNewInstrumentClip(OutputType::KIT, 3, kShellKitPresetPaths[shellKitIndex()]);
			}
			printf("SC-PROBE leg=%s prepared kitClip=%d\n", legName(g_leg), g_kitClip != nullptr ? 1 : 0);
		});
		break;
	default:
		break;
	}
}

bool openLeg() {
	switch (g_leg) {
	case Leg::Synth:
		return openBrowserOnSynth("synth", OscType::SAMPLE);
	case Leg::WaveTable:
		return openBrowserOnSynth("wavetable", OscType::SAMPLE);
	case Leg::WaveTableResident:
		// Fills held briefly, so the Sample the preview leaves resident still lacks most of its clusters at the claim.
		deluge_streaming_set_fill_latency_ms(kPitchHoldMs);
		return openBrowserOnSynth("wavetable-resident", OscType::SAMPLE);
	case Leg::WaveTableConverted:
		return openBrowserOnSynth("wavetable-converted", OscType::SAMPLE);
	case Leg::Fallback:
		// A wavetable oscillator: the claim tries the file as a wavetable first.
		return openBrowserOnSynth("fallback", OscType::WAVETABLE);
	case Leg::Gate:
		deluge_audio_load_set_latency_ms(kGateHoldMs);
		return openBrowserOnSynth("gate", OscType::SAMPLE);
	case Leg::Stall:
		deluge_audio_load_set_latency_ms(kStallHoldMs);
		return openBrowserOnSynth("stall", OscType::SAMPLE);
	case Leg::Supersede:
		armPitchLeg(kPitchHoldMs);
		return openBrowserOnSynth("supersede", OscType::SAMPLE);
	case Leg::SupersedeLoading:
		armPitchLeg(kPitchHoldMs);
		return openBrowserOnSynth("supersede-loading", OscType::SAMPLE);
	case Leg::CancelLoading:
		deluge_audio_load_set_latency_ms(kGateHoldMs);
		return openBrowserOnSynth("cancel-loading", OscType::SAMPLE);
	case Leg::Pitch:
		armPitchLeg(kPitchHoldMs);
		return openBrowserOnSynth("pitch", OscType::SAMPLE);
	case Leg::PitchStall:
		armPitchLeg(kPitchStallHoldMs);
		return openBrowserOnSynth("pitch-stall", OscType::SAMPLE);
	case Leg::Kit:
		return openBrowserOnKitDrum(legName(g_leg));
	case Leg::ShellSlice:
	case Leg::ShellSliceManual:
	case Leg::ShellSliceBack:
	case Leg::ShellSliceFail: {
		// No browser: the ported slicer's host opens the slicer on the file itself, for the kit's first drum.
		SoundDrum* drum = kitDrum();
		if (drum == nullptr) {
			return fail(legName(g_leg), "the kit clip has no sound drum");
		}
		frontClip(legName(g_leg), g_kitClip, &instrumentClipView, drum);
		return true;
	}
	case Leg::RandomizeOne:
		// The first drum, as a user selects a row before randomizing just that one.
		return openRandomizeLeg("randomize-one", kitDrum());
	case Leg::RandomizeAll:
		return openRandomizeLeg("randomize-all", nullptr);
	case Leg::RandomizeGate:
		deluge_audio_load_set_latency_ms(kGateHoldMs);
		return openRandomizeLeg("randomize-gate", kitDrum());
	case Leg::RandomizeStall:
		deluge_audio_load_set_latency_ms(kRandomizeStallHoldMs);
		return openRandomizeLeg("randomize-stall", nullptr);
	case Leg::RandomizeCancelScan:
		// The DIRECTORY channel held, not the loader: this leg never reaches a load.
		deluge_file_directory_set_latency_ms(kRandomizeCancelHoldMs);
		return openRandomizeLeg("randomize-cancelscan", kitDrum());
	case Leg::ImportKit: {
		if (g_importKitClip == nullptr || g_importKitClip->output == nullptr
		    || g_importKitClip->output->type != OutputType::KIT) {
			return fail("import-kit", "no kit clip could be created from the import preset");
		}
		Drum* drum = static_cast<Kit*>(g_importKitClip->output)->firstDrum;
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return fail("import-kit", "the import kit has no sound drum");
		}
		return openBrowser("import-kit", g_importKitClip, &instrumentClipView, static_cast<SoundDrum*>(drum));
	}
	case Leg::ImportGate: {
		if (g_importGateClip == nullptr || g_importGateClip->output == nullptr
		    || g_importGateClip->output->type != OutputType::KIT) {
			return fail("import-gate", "no kit clip could be created from the import gate preset");
		}
		Drum* drum = static_cast<Kit*>(g_importGateClip->output)->firstDrum;
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return fail("import-gate", "the import gate kit has no sound drum");
		}
		return openBrowser("import-gate", g_importGateClip, &instrumentClipView, static_cast<SoundDrum*>(drum));
	}
	case Leg::ImportEmpty: {
		if (g_importEmptyClip == nullptr || g_importEmptyClip->output == nullptr
		    || g_importEmptyClip->output->type != OutputType::KIT) {
			return fail("import-empty", "no kit clip could be created from the empty import preset");
		}
		Drum* drum = static_cast<Kit*>(g_importEmptyClip->output)->firstDrum;
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return fail("import-empty", "the empty import kit has no sound drum");
		}
		return openBrowser("import-empty", g_importEmptyClip, &instrumentClipView, static_cast<SoundDrum*>(drum));
	}
	case Leg::ImportStall:
		if (g_importSynthClip == nullptr || g_importSynthClip->output == nullptr) {
			return fail("import-stall", "the multisample leg's synth clip is missing");
		}
		return openBrowser("import-stall", g_importSynthClip, &instrumentClipView);
	case Leg::ImportMulti:
		if (g_importSynthClip == nullptr || g_importSynthClip->output == nullptr
		    || g_importSynthClip->output->type != OutputType::SYNTH) {
			return fail("import-multi", "no synth clip could be created");
		}
		// A wavetable oscillator: the import makes it a sample one, and finds the range it fills again.
		static_cast<SoundInstrument*>(g_importSynthClip->output)->sources[0].setOscType(OscType::WAVETABLE);
		return openBrowser("import-multi", g_importSynthClip, &instrumentClipView);
	case Leg::AudioClip:
		if (g_audioClip == nullptr || g_audioClip->type != ClipType::AUDIO) {
			return fail("audioclip", "no audio clip could be created");
		}
		return openBrowser("audioclip", g_audioClip, &audioClipView);
	case Leg::Done:
		break;
	}
	return false;
}

const char* legFile() {
	switch (g_leg) {
	case Leg::Synth:
		return kSynthSample;
	case Leg::WaveTable:
		return kCycleSample;
	case Leg::WaveTableResident:
		return kResidentWaveTableSample;
	case Leg::WaveTableConverted:
		return kFullWaveTableSample;
	case Leg::Fallback:
		return kStereoSample;
	case Leg::Gate:
		return kGateSample;
	case Leg::Stall:
		return kStallSample;
	case Leg::Supersede:
		return kSupersedeFirstLanded;
	case Leg::SupersedeLoading:
		return kSupersedeFirstLoading;
	case Leg::CancelLoading:
		return kStallSample; // not in memory yet: the stall leg is the first to ask for it
	case Leg::Pitch:
		return kPitchSample;
	case Leg::PitchStall:
		return kPitchStallSample;
	case Leg::Kit:
		return kKickSample;
	case Leg::ImportKit:
		return kImportKitFile;
	case Leg::ImportMulti:
		return kImportMultiFile;
	case Leg::ImportGate:
		return kImportGateFile;
	case Leg::ImportEmpty:
		return kImportEmptyFolder; // the folder item itself, which the kit menu's Load All imports
	case Leg::ImportStall:
		return kImportStallKept;
	case Leg::AudioClip:
		return kClipSample;
	case Leg::ShellSlice:
	case Leg::ShellSliceManual:
	case Leg::ShellSliceBack:
		return kSliceSample;
	case Leg::ShellSliceFail:
		return kShellStallSample;
	case Leg::RandomizeOne:
	case Leg::RandomizeAll:
	case Leg::RandomizeGate:
	case Leg::RandomizeStall:
	case Leg::RandomizeCancelScan:
		// No browser listing: the randomizer picks the file itself, from the folder the drum's sample sits in.
		return kRandomizeFirstSample;
	case Leg::Done:
		break;
	}
	return "";
}

/// Whether the current leg is one of the randomizer legs.
bool isRandomize() {
	return g_leg == Leg::RandomizeOne || g_leg == Leg::RandomizeAll || g_leg == Leg::RandomizeGate
	       || g_leg == Leg::RandomizeStall || g_leg == Leg::RandomizeCancelScan;
}

/// What the cancel-loading leg saw: whether the exit was pressed, whether the claim was still loading then, and the
/// ticks before it.
bool g_cancelLoadingFired = false;
bool g_cancelLoadingInFlight = false;
int32_t g_cancelLoadingTicks = 0;

/// Whether the current leg is one of the two supersede legs.
bool isSupersede() {
	return g_leg == Leg::Supersede || g_leg == Leg::SupersedeLoading;
}

/// The file a supersede leg's cursor moves on to.
const char* supersedeSecond() {
	return (g_leg == Leg::Supersede) ? kSupersedeLanded : kSupersedeLoading;
}

/// What the gate leg saw: whether its presses were tried, whether the walk was still out when they were, whether each
/// was refused, and whether the edit pad was taken once the walk had ended.
bool g_rdGateTried = false;
bool g_rdGateWhileRunning = false;
bool g_rdGateSecondWalkRefused = false;
bool g_rdGateEditRefused = false;
bool g_rdGateEditTakenAfter = false;

/// What the cancel leg saw: the round it is on, how many passes that round has let through, whether its cancel has
/// been made, whether the ordinary randomize that follows the rounds has been pressed, and what the rounds left
/// behind -- all read before that last walk touches anything.
int32_t g_rdCancelRound = 0;
int32_t g_rdCancelPasses = 0;
bool g_rdCancelCancelled = false;
bool g_rdCancelFinalPressed = false;
int32_t g_rdCancelDrainsEnteredBefore = 0;
int32_t g_rdCancelDrainsCollectedBefore = 0;
int32_t g_rdCancelScansBefore = 0;
int32_t g_rdCancelDrained = 0;
bool g_rdCancelRowUntouched = false;

/// Every note in the randomizer kit clip's rows.
int32_t kitClipNotes() {
	auto* clip = static_cast<InstrumentClip*>(g_kitClip);
	int32_t notes = 0;
	for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
		notes += static_cast<int32_t>(clip->noteRows[i].notes.size());
	}
	return notes;
}

/// The display row showing the selected drum, or -1.
int32_t selectedDrumY() {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		NoteRow* row = static_cast<InstrumentClip*>(g_kitClip)->getNoteRowOnScreen(y, currentSong);
		if (row != nullptr && row->drum == kitDrum()) {
			return y;
		}
	}
	return -1;
}

/// One press and release of the gate leg's edit pad; whether it changed the clip's notes.
bool pressGateEditPad() {
	const int32_t y = selectedDrumY();
	if (y < 0) {
		return false;
	}
	const int32_t before = kitClipNotes();
	(void)instrumentClipView.padAction(kRandomizeGateEditX, y, 1);
	(void)instrumentClipView.padAction(kRandomizeGateEditX, y, 0);
	return kitClipNotes() != before;
}

/// The gate leg's first wait tick: while the walk is still out, the randomizer pressed again and the edit pad.
void tickRandomizeGate() {
	if (g_rdGateTried) {
		return;
	}
	g_rdGateTried = true;
	g_rdGateWhileRunning = currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	if (!g_rdGateWhileRunning) {
		return;
	}
	using namespace deluge::hid::button;
	// Requests that reached the randomizer: what the gate stops. (A scan count cannot tell -- a second walk would not
	// have scanned yet in this window.)
	const int32_t requestsBefore = deluge::gui::randomizeDrums.requestsReceived();
	(void)Buttons::buttonAction(LOAD, true);
	(void)instrumentClipView.padAction(15, 2, 1);
	(void)instrumentClipView.padAction(15, 2, 0);
	(void)Buttons::buttonAction(LOAD, false);
	g_rdGateSecondWalkRefused = deluge::gui::randomizeDrums.requestsReceived() == requestsBefore;
	g_rdGateEditRefused = !pressGateEditPad();
}

/// Whether the current leg is one of the folder import legs.
bool isImport() {
	return g_leg == Leg::ImportKit || g_leg == Leg::ImportMulti || g_leg == Leg::ImportGate
	       || g_leg == Leg::ImportStall;
}

/// The import gate leg's first wait tick: while the import is still out, BACK and Select on the kit menu.
void tickImportGate() {
	if (g_imGateTried) {
		return;
	}
	g_imGateTried = true;
	g_imGateWhileRunning = currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	if (!g_imGateWhileRunning) {
		return;
	}
	// Requests that reached the import: what the gate stops. (A listing count cannot tell -- a second import would not
	// have listed yet in this window.)
	const int32_t requestsBefore = deluge::gui::folderImport.requestsReceived();
	guestBack();
	g_imGateBackRefused = guestTop(deluge::scenes::context_menu::kContextMenuSceneKind);
	guestSelect();
	g_imGateSecondRefused = deluge::gui::folderImport.requestsReceived() == requestsBefore;
}

/// The randomizer's gesture: LOAD held (which the randomizer reads through Buttons::isButtonPressed), AFFECT_ENTIRE
/// held too when @p affectEntire, then the pad. Buttons::buttonAction is what sets the held state a press reads.
void pressRandomize(bool affectEntire) {
	using namespace deluge::hid::button;
	(void)Buttons::buttonAction(LOAD, true);
	if (affectEntire) {
		(void)Buttons::buttonAction(AFFECT_ENTIRE, true);
	}
	(void)instrumentClipView.padAction(15, 2, 1);
	(void)instrumentClipView.padAction(15, 2, 0);
	if (affectEntire) {
		(void)Buttons::buttonAction(AFFECT_ENTIRE, false);
	}
	(void)Buttons::buttonAction(LOAD, false);
}

/// The completion the empty import leg's request came with, which the leg's own completion passes the end on to.
void (*g_imEmptyCompletion)(bool success) = nullptr;

void emptyImportDone(bool success) {
	g_imEmptyTold = true;
	g_imEmptySucceeded = success;
	if (g_imEmptyCompletion != nullptr) {
		g_imEmptyCompletion(success);
	}
}

/// The empty import leg's folder import, requested as the browser asked for it with the leg's completion in front.
void observeEmptyImport(deluge::gui::FolderImportRequest request) {
	g_imEmptyCompletion = request.completion;
	request.completion = &emptyImportDone;
	printf("SC-PROBE leg=import-empty requested folder=%s\n", request.folder.c_str());
	deluge::gui::folderImport.request(std::move(request));
}

void actShellSlice() {
	g_shellCommitBegun = false;
	g_shellEnded = false;
	g_shellBackKept = false;
	g_teardownHeld = g_teardownRootKept = g_teardownDropped = g_nextHeld = false;
	g_nextHeldThrough = true;
	// What the ported menu edits: the kit's drum, as the clip view selected it.
	const deluge::scenes::menu::MenuContext ctx = deluge::scenes::menu::context_for(
	    *currentSong, deluge::gui::menuViewOf(g_kitClip), 0, false, false, false, /*kit_global_fx=*/false);
	deluge::scenes::menu_scene().open(deluge::scenes::menu::nodes::sound_root, ctx);
	auto& scene = deluge::scenes::slicing::slicer_scene();
	scene.open(nullptr);
	if (g_leg == Leg::ShellSliceBack) {
		// A first drum that holds no file, as a brand-new kit's has: main dereferences its missing sample here.
		auto* drum = static_cast<SoundDrum*>(static_cast<Kit*>(g_kitClip->output)->firstDrum);
		auto* range = static_cast<MultisampleRange*>(drum->sources[0].getOrCreateFirstRange());
		range->sampleHolder.setAudioFile(nullptr);
		const uint64_t start = range->sampleHolder.startPos;
		const uint64_t end = range->sampleHolder.endPos;
		if (scene.ops() != nullptr) {
			scene.ops()->restore_full_range();
		}
		g_shellBackKept = range->sampleHolder.audioFile == nullptr && range->sampleHolder.startPos == start
		                  && range->sampleHolder.endPos == end;
		// The guest closing with the slicer's open still out drops it, so it cannot land in the next guest.
		deluge::gui::ui::openShellSlice(legFile());
		deluge::gui::ui::releaseShellSlicerBeforeClose();
		if (deluge::gui::ui::shellSlicerBusy()) {
			fail(legName(g_leg), "the guest's close left the slicer's open out");
		}
		g_shellEnded = true;
		return;
	}
	if (g_leg == Leg::ShellSliceFail) {
		deluge_audio_load_set_latency_ms(kStallHoldMs);
	}
	deluge::gui::ui::openShellSlice(legFile());
}

/// A press on the slicer before the slice, which holds a load for the first drum, then every UI taken down as a song
/// load takes them, and the root and the menu's context (which the teardown forgets) put back for the rest of the leg,
/// which goes on in the same song.
void checkTeardownDropsAudition(deluge::scenes::slicing::SlicerScene& scene) {
	if (scene.ops() == nullptr) {
		return;
	}
	scene.ops()->audition({.start_point = 0, .end_point = -1, .transpose = 0, .on = 64});
	g_teardownHeld = deluge::gui::ui::shellSlicerBusy();
	UI* const root = getRootUI();
	g_teardownRootKept = getCurrentUI() == root;
	const deluge::scenes::menu::MenuContext ctx = deluge::scenes::menu_scene().menu_context();
	nullifyUIs();
	g_teardownDropped = !deluge::gui::ui::shellSlicerBusy();
	if (g_teardownRootKept) {
		setRootUILowLevel(root);
	}
	deluge::scenes::menu_scene().open(deluge::scenes::menu::nodes::sound_root, ctx);
}

/// A slice auditioned and released before its load lands: the press names the slicer's file for the first drum, and
/// once the release drops the load, that drum's record and slot must say what it holds.
void checkAuditionReleaseCoherent(deluge::scenes::slicing::SlicerScene& scene) {
	if (scene.ops() == nullptr) {
		return;
	}
	const int32_t failuresBefore = g_failures;
	deluge_audio_load_set_latency_ms(kGateHoldMs);
	scene.ops()->audition({.start_point = 0, .end_point = -1, .transpose = 0, .on = 64});
	const bool held = deluge::gui::ui::shellSlicerBusy();
	scene.ops()->audition({.start_point = 0, .end_point = -1, .transpose = 0, .on = 0});
	deluge_audio_load_set_latency_ms(0);
	g_failures += deluge::harness::expectSongCoherent(*currentSong, "slice-audition-release");
	printf("SC-PROBE leg=%s auditionReleased held=%d coherent=%d\n", legName(g_leg), held ? 1 : 0,
	       g_failures == failuresBefore ? 1 : 0);
	if (!held) {
		fail(legName(g_leg), "the slice's audition load was not out when it was released, so nothing was tested");
	}
}

/// A learned MIDI next-song while the slice is out, the way a DIN note reaches it; the command is put back after.
void sendNextSongDuringSlice() {
	if (!deluge::gui::ui::shellSliceOut()) {
		return;
	}
	auto& command = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::NEXT_SONG)];
	g_nextCommandBefore = command;
	command.cable = nullptr;
	command.channelOrZone = kNextSongChannel;
	command.noteOrCC = kNextSongNote;
	bool thru = false;
	playbackHandler.noteMessageReceived(MIDIDeviceManager::dinMIDIPorts, true, kNextSongChannel, kNextSongNote, 64,
	                                    &thru);
	g_nextHeld = playbackHandler.nextSongPending_;
	// The job that lets it go, run while the slice is still out: it must keep holding it.
	playbackHandler.slowRoutine();
	g_nextHeldThrough = playbackHandler.nextSongPending_;
	playbackHandler.noteMessageReceived(MIDIDeviceManager::dinMIDIPorts, false, kNextSongChannel, kNextSongNote, 0,
	                                    &thru);
	command = g_nextCommandBefore;
}

/// The ported slicer's legs, a tick at a time: once the host has opened the slicer on the file, begin the slice; the
/// leg ends when nothing of the slicer's is out.
void tickShellSlice() {
	deluge::gui::ui::pollShellSlicer();
	auto& scene = deluge::scenes::slicing::slicer_scene();
	if (g_shellEnded) {
		return;
	}
	if (g_leg == Leg::ShellSliceFail) {
		// The open never lands: the leg ends when it has given up.
		g_shellEnded = !deluge::gui::ui::shellSlicerBusy();
		return;
	}
	if (!g_shellCommitBegun) {
		if (!scene.has_sample()) {
			return;
		}
		if (g_leg == Leg::ShellSlice) {
			checkTeardownDropsAudition(scene);
		}
		if (g_leg == Leg::ShellSliceManual) {
			checkAuditionReleaseCoherent(scene);
		}
		deluge::scenes::slicing::SliceCommit commit{};
		commit.is_manual = g_leg == Leg::ShellSliceManual;
		commit.slice_count = commit.is_manual ? kManualSliceCount : kSliceRegions;
		for (int32_t i = 0; i < kManualSliceCount; i++) {
			commit.points[i] = {.start_pos = kManualSlices[i].startPos, .transpose = kManualSlices[i].transpose};
		}
		g_shellCommitBegun = scene.ops() != nullptr && scene.ops()->begin_commit(commit);
		if (!g_shellCommitBegun) {
			fail(legName(g_leg), "the host refused the slice");
			g_shellEnded = true;
			return;
		}
		if (g_leg == Leg::ShellSlice) {
			sendNextSongDuringSlice();
		}
		// The slicer stays open on its sample if the slice fails, so the preview's holder has to keep it loaded.
		const auto* range =
		    static_cast<const MultisampleRange*>(AudioEngine::sampleForPreview->sources[0].ranges.getElement(0));
		if (range->sampleHolder.audioFile == nullptr || range->sampleHolder.filePath != legFile()) {
			fail(legName(g_leg), "the slice let go of the sample the slicer shows");
		}
		return;
	}
	if (g_leg == Leg::ShellSlice) {
		// Held for as long as the slice is out, and loaded (here: let go, nothing playing) once it is in.
		g_nextHeldThrough =
		    g_nextHeldThrough && (!deluge::gui::ui::shellSliceOut() || playbackHandler.nextSongPending_);
		g_shellEnded = !deluge::gui::ui::shellSlicerBusy() && !playbackHandler.nextSongPending_;
		return;
	}
	g_shellEnded = !deluge::gui::ui::shellSlicerBusy();
}

void actLeg() {
	if (isShellSlice()) {
		actShellSlice();
		return;
	}
	if (g_leg == Leg::WaveTableResident || g_leg == Leg::WaveTableConverted) {
		g_convBefore = markConversions();
	}
	if (isRandomize()) {
		g_randomizeMark = markRandomize();
		recordRandomizeRows();
		g_rdGateTried = false;
		g_rdGateWhileRunning = false;
		g_rdGateSecondWalkRefused = false;
		g_rdGateEditRefused = false;
		g_rdGateEditTakenAfter = false;
		g_rdCancelRound = 0;
		g_rdCancelPasses = 0;
		g_rdCancelCancelled = false;
		g_rdCancelFinalPressed = false;
		g_rdCancelDrained = 0;
		g_rdCancelRowUntouched = false;
		g_rdCancelDrainsEnteredBefore = deluge::gui::randomizeDrums.drainsEntered();
		g_rdCancelDrainsCollectedBefore = deluge::gui::randomizeDrums.drainsCollected();
		g_rdCancelScansBefore = deluge::gui::randomizeDrums.scansRun();
		pressRandomize(g_leg == Leg::RandomizeAll || g_leg == Leg::RandomizeStall);
		return;
	}
	finishScroll();
	g_tierMark = markTier();
	g_giveUpsBefore = deluge::gui::sampleClaim.giveUps();
	g_cancelsBefore = deluge::gui::sampleClaim.cancels();
	g_finishesBefore = deluge::gui::sampleClaim.finishes();
	if (isSupersede()) {
		using namespace deluge::hid::button;
		// Auto-load on (the Load button toggles it as it is released), then the cursor brought onto the file again by
		// its favourite: the preview of it that lands asks for the auto-load claim.
		g_supersedeFired = false;
		deluge::harness::guestPress(LOAD);
		const bool recalled = deluge::harness::guestRecallFavourite(legFile());
		printf("SC-PROBE leg=%s autoLoad=%d recalled=%d\n", legName(g_leg),
		       deluge::scenes::browser::browser_scene().auto_load() ? 1 : 0, recalled ? 1 : 0);
		if (!recalled) {
			fail(legName(g_leg), "the leg's file could not be recalled with auto-load on");
		}
		return;
	}
	if (g_leg == Leg::CancelLoading) {
		// An auto-load claim of a file not in memory, held by the loader: the browser's exit cancels it mid-load.
		g_cancelLoadingFired = false;
		g_cancelLoadingInFlight = false;
		g_cancelLoadingTicks = 0;
		// The ported browser's auto-load request (`dispatchShellChoices`): the sound its menu resolved, no completion.
		deluge::gui::sampleClaim.request({.path = legFile(),
		                                  .origin = deluge::gui::ClaimOrigin::AutoLoad,
		                                  .target = deluge::gui::ui::shellMenuTarget()});
		return;
	}
	if (g_leg == Leg::Gate) {
		const std::string before = guestSelectedName();
		guestSelect();
		// Browser input while the claim is in flight: the select encoder, turned back one file.
		shellHostUI.selectEncoderAction(-1);
		guestTick();
		const std::string after = guestSelectedName();
		g_gateMoved = (after != before);
		g_gateClaimOut = deluge::gui::sampleClaim.explicitInFlight();
		printf("SC-PROBE leg=gate before=%s after=%s claimOut=%d\n", before.c_str(), after.c_str(),
		       g_gateClaimOut ? 1 : 0);
		return;
	}
	if (g_leg == Leg::ImportEmpty) {
		// The kit menu's Load All on the folder item the cursor is on, as a user asks for it; the request goes out
		// through the scenario, which hears how it ended.
		g_imEmptyTold = false;
		g_imEmptySucceeded = false;
		deluge::gui::ui::setFolderImportDispatch(&observeEmptyImport);
		guestLongPressSelect();
		const bool menuOnTop = guestTop(deluge::scenes::context_menu::kContextMenuSceneKind);
		const bool picked = guestPickOption(kImportOption);
		deluge::gui::ui::setFolderImportDispatch(nullptr);
		printf("SC-SETUP leg=import-empty menuOnTop=%d picked=%d inFlight=%d\n", menuOnTop ? 1 : 0, picked ? 1 : 0,
		       deluge::gui::folderImport.inFlight() ? 1 : 0);
		return;
	}
	if (isImport()) {
		g_importMark = markImport();
		g_imGateTried = false;
		g_imGateWhileRunning = false;
		g_imGateSecondRefused = false;
		g_imGateBackRefused = false;
		// The held legs hold the loader only now: the browser's own preview of the selected file is not held.
		if (g_leg == Leg::ImportGate) {
			deluge_audio_load_set_latency_ms(kGateHoldMs);
		}
		else if (g_leg == Leg::ImportStall) {
			deluge_audio_load_set_latency_ms(kStallHoldMs);
		}
		// A long press on Select opens the context menu for the kind of instrument the browser is choosing for.
		guestLongPressSelect();
		const bool menuOnTop = guestTop(deluge::scenes::context_menu::kContextMenuSceneKind);
		printf("SC-SETUP leg=%s menuOnTop=%d\n", legName(g_leg), menuOnTop ? 1 : 0);
		(void)guestPickOption(kImportOption);
		return;
	}
	if (g_leg == Leg::WaveTable || g_leg == Leg::WaveTableResident || g_leg == Leg::WaveTableConverted) {
		guestLongPressSelect();
		const bool menuOnTop = guestTop(deluge::scenes::context_menu::kContextMenuSceneKind);
		printf("SC-SETUP leg=%s menuOnTop=%d\n", legName(g_leg), menuOnTop ? 1 : 0);
		(void)guestPickOption(kWaveTableOption);
		return;
	}
	guestSelect();
}

/// The file the randomizer's stall leg's second row was given, whose load the loader holds: any file of its folder,
/// as the randomizer picks one at random.
std::string randomizeStallHeldPath() {
	Drum* first = static_cast<Kit*>(g_kitClip->output)->firstDrum;
	Drum* second = (first != nullptr) ? first->next : nullptr;
	AudioFileHolder* holder = (second != nullptr) ? firstHolder(static_cast<SoundDrum*>(second)->sources[0]) : nullptr;
	return (holder != nullptr) ? holder->filePath : std::string{kRandomizeStallSample};
}

/// Whether the leg's claim or slice has ended: a claim that succeeds closes the browser, and a slice lets go of the
/// slicer's loads.
bool legEnded() {
	if (g_leg == Leg::CancelLoading) {
		return g_cancelLoadingFired && !deluge::gui::sampleClaim.inFlight();
	}
	if (g_leg == Leg::ImportGate) {
		return g_imGateTried && currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	}
	if (g_leg == Leg::ImportEmpty) {
		// Told either way: an abort tells the completion too, and leaves the browser open. A success closes the guest
		// onto the clip view.
		return g_imEmptyTold && currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES
		       && (!g_imEmptySucceeded || !isUIOpen(&shellHostUI));
	}
	if (g_leg == Leg::ImportStall) {
		// An import that aborts closes just the menu and releases the gate; one that finishes closes the browser too.
		return currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES
		       && !guestTop(deluge::scenes::context_menu::kContextMenuSceneKind);
	}
	if (g_leg == Leg::RandomizeCancelScan) {
		// Every round releases the gate as it cancels, so the mode says nothing until the ordinary randomize the
		// rounds are there to set up: the leg ends when THAT one has ended.
		return g_rdCancelFinalPressed && currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	}
	if (isRandomize()) {
		// The gesture closes the gate and the walk releases it: the leg ends when the mode is gone -- for the gate leg,
		// once its presses have been tried. The stall leg's walk gives up on a load the loader still holds under the
		// leg's latency; the loader is single-flight, so that load has to finish before the next leg's own can start.
		return (g_leg != Leg::RandomizeGate || g_rdGateTried) && currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES
		       && (g_leg != Leg::RandomizeStall || deluge_audio_load_can_progress(randomizeStallHeldPath().c_str()));
	}
	if (isSupersede()) {
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		return g_supersedeFired && !deluge::gui::sampleClaim.inFlight() && holder != nullptr
		       && holder->audioFile != nullptr && holder->filePath == supersedeSecond();
	}
	if (isShellSlice()) {
		return g_shellEnded;
	}
	if (g_leg == Leg::Stall) {
		// A claim that gives up leaves the browser open and releases the gate; one that binds closes the browser.
		return !browserOnTop() || currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	}
	return !browserOnTop();
}

/// The region slice's end: @p marker reports the kit's drums, how many are bound to the sliced sample, and the
/// NoteRows.
void checkRegionSlice(const char* marker) {
	int32_t drums = 0;
	int32_t boundToSlice = 0;
	for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum; drum != nullptr; drum = drum->next) {
		drums++;
		if (drum->type != DrumType::SOUND) {
			continue;
		}
		AudioFileHolder* holder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
		if (holder != nullptr && holder->audioFile != nullptr && holder->filePath == kSliceSample) {
			boundToSlice++;
		}
	}
	auto* clip = static_cast<InstrumentClip*>(g_kitClip);
	int32_t rowsWithDrum = 0;
	for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
		if (clip->noteRows[i].drum != nullptr) {
			rowsWithDrum++;
		}
	}
	printf("%s drums=%d boundToSlice=%d rowsWithDrum=%d ticks=%d\n", marker, drums, boundToSlice, rowsWithDrum,
	       g_waitTicks);
	if (drums != kSliceRegions || boundToSlice != kSliceRegions || rowsWithDrum < kSliceRegions) {
		printf("%s FAIL the slice did not end with %d drums on the sliced sample, each with a NoteRow\n", marker,
		       kSliceRegions);
		g_failures++;
	}
}

/// The manual slice's end: as `checkRegionSlice`, and each drum painted from the slice points.
void checkManualSlice(const char* marker) {
	auto* kit = static_cast<Kit*>(g_kitClip->output);
	int32_t drums = 0;
	for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
		drums++;
	}
	int32_t boundToSlice = 0;
	int32_t painted = 0;
	for (int32_t i = 0; i < kManualSliceCount && i < drums; i++) {
		Drum* drum = kit->getDrumFromIndex(i);
		if (drum->type != DrumType::SOUND) {
			continue;
		}
		auto* range = static_cast<MultisampleRange*>(static_cast<SoundDrum*>(drum)->sources[0].getOrCreateFirstRange());
		if (range == nullptr || range->sampleHolder.audioFile == nullptr
		    || !(range->sampleHolder.filePath == kSliceSample)) {
			continue;
		}
		boundToSlice++;
		const int64_t length = static_cast<Sample*>(range->sampleHolder.audioFile)->lengthInSamples;
		const int64_t wantEnd = (i == kManualSliceCount - 1) ? length : kManualSlices[i + 1].startPos;
		const bool match = range->sampleHolder.startPos == static_cast<uint64_t>(kManualSlices[i].startPos)
		                   && range->sampleHolder.endPos == static_cast<uint64_t>(wantEnd)
		                   && range->sampleHolder.transpose == kManualSlices[i].transpose;
		printf("SC-PROBE leg=%s drum=%d start=%llu end=%llu transpose=%d\n", legName(g_leg), i,
		       static_cast<unsigned long long>(range->sampleHolder.startPos),
		       static_cast<unsigned long long>(range->sampleHolder.endPos), range->sampleHolder.transpose);
		painted += match ? 1 : 0;
	}
	auto* clip = static_cast<InstrumentClip*>(g_kitClip);
	int32_t rowsWithDrum = 0;
	for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
		if (clip->noteRows[i].drum != nullptr) {
			rowsWithDrum++;
		}
	}
	printf("%s drums=%d boundToSlice=%d painted=%d rowsWithDrum=%d ticks=%d\n", marker, drums, boundToSlice, painted,
	       rowsWithDrum, g_waitTicks);
	if (drums != kManualSliceCount || boundToSlice != kManualSliceCount || painted != kManualSliceCount
	    || rowsWithDrum < kManualSliceCount) {
		printf("%s FAIL the manual slice did not end with %d drums on the sliced sample, each with a "
		       "NoteRow and painted from the slice points as they were when Select was pressed\n",
		       marker, kManualSliceCount);
		g_failures++;
	}
}

void checkLeg() {
	checkTier();
	switch (g_leg) {
	case Leg::Synth: {
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		// The browser closes itself once its claim lands (`shellSampleClaimDone`).
		const bool browserClosed = !browserOnTop();
		// ...and the menu beneath re-enters on the page that lists the file selector, focused on it, as main's claim
		// re-enters on `parentMenuHeadingTo` with `menuItemHeadingTo` focused: with horizontal menus on, a page of the
		// source's group; with them off, the file selector the menu was opened on.
		const auto& menu = deluge::scenes::menu_scene();
		const bool horizontal = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus);
		const bool onPage = guestTop(deluge::scenes::menu::kMenuSceneKind)
		                    && menu.focused_value() == &deluge::scenes::menu::nodes::file0_selector
		                    && (menu.state().path[0] != &deluge::scenes::menu::nodes::file0_selector) == horizontal;
		printf("SC-SYNTH bound=%d path=%s osc=%d repeat=%d transpose=%d browserClosed=%d onPage=%d horizontal=%d "
		       "ticks=%d\n",
		       bound ? 1 : 0, holder != nullptr ? holder->filePath.c_str() : "",
		       static_cast<int>(g_synth->sources[0].oscType), static_cast<int>(g_synth->sources[0].repeatMode),
		       holder != nullptr ? static_cast<SampleHolderForVoice*>(holder)->transpose : 0, browserClosed ? 1 : 0,
		       onPage ? 1 : 0, horizontal ? 1 : 0, g_waitTicks);
		if (!onPage) {
			printf("SC-SYNTH FAIL the menu did not re-enter on the page that lists the file selector\n");
			g_failures++;
		}
		if (!bound) {
			printf("SC-SYNTH FAIL the claim bound no file\n");
			g_failures++;
		}
		if (!browserClosed) {
			printf("SC-SYNTH FAIL the browser stayed open after its claim\n");
			g_failures++;
		}
		break;
	}
	case Leg::Gate: {
		deluge_audio_load_set_latency_ms(0);
		// Released is the gate's own mode gone: a preview's scroll may hold a mode of its own.
		const bool released = (currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES);
		printf("SC-GATE input=selectEncoder claimOut=%d refused=%d released=%d ticks=%d\n", g_gateClaimOut ? 1 : 0,
		       g_gateMoved ? 0 : 1, released ? 1 : 0, g_waitTicks);
		if (!g_gateClaimOut) {
			printf("SC-GATE FAIL the claim was not out when the encoder turned, so nothing was tested\n");
			g_failures++;
		}
		if (g_gateMoved) {
			printf("SC-GATE FAIL the select encoder moved the browser's selection while an explicit claim was in "
			       "flight\n");
			g_failures++;
		}
		if (!released) {
			printf("SC-GATE FAIL the gate was still closed once the claim had ended\n");
			g_failures++;
		}
		break;
	}
	case Leg::Stall: {
		deluge_audio_load_set_latency_ms(0);
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const bool browserOpen = browserOnTop();
		const bool released = (currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES);
		const int32_t giveUps = deluge::gui::sampleClaim.giveUps() - g_giveUpsBefore;
		printf("SC-STALL bound=%d browserOpen=%d released=%d giveUps=%d ticks=%d\n", bound ? 1 : 0, browserOpen ? 1 : 0,
		       released ? 1 : 0, giveUps, g_waitTicks);
		if (bound || !browserOpen || !released || giveUps != 1) {
			printf("SC-STALL FAIL a claim whose load never answered did not give up: it must leave the holder unbound, "
			       "the browser open and the gate released\n");
			g_failures++;
		}
		break;
	}
	case Leg::WaveTable:
	case Leg::Fallback: {
		const bool wantWaveTable = (g_leg == Leg::WaveTable);
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const OscType osc = g_synth->sources[0].oscType;
		printf("SC-%s bound=%d path=%s osc=%d modKnobMode=%d ticks=%d\n", wantWaveTable ? "WAVETABLE" : "FALLBACK",
		       bound ? 1 : 0, holder != nullptr ? holder->filePath.c_str() : "", static_cast<int>(osc),
		       static_cast<int>(g_synth->modKnobMode), g_waitTicks);
		if (!bound || osc != (wantWaveTable ? OscType::WAVETABLE : OscType::SAMPLE)) {
			printf("SC-%s FAIL the claim did not bind the file as a %s\n", wantWaveTable ? "WAVETABLE" : "FALLBACK",
			       wantWaveTable ? "wavetable" : "sample");
			g_failures++;
		}
		break;
	}
	case Leg::CancelLoading:
		deluge_audio_load_set_latency_ms(0);
		printf("SC-CANCEL-LOADING inFlightWhenCancelled=%d ticks=%d\n", g_cancelLoadingInFlight ? 1 : 0, g_waitTicks);
		if (!g_cancelLoadingInFlight) {
			printf("SC-CANCEL-LOADING FAIL the auto-load claim had ended before the browser's exit could cancel it\n");
			g_failures++;
		}
		guestClose();
		break;
	case Leg::Supersede:
	case Leg::SupersedeLoading: {
		deluge_audio_load_set_latency_ms(0);
		deluge_streaming_set_fill_latency_ms(0);
		const auto& claim = deluge::gui::sampleClaim;
		const char* supersedeCase = (g_leg == Leg::Supersede) ? "landed" : "loading";
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const int32_t cancels = claim.cancels() - g_cancelsBefore;
		const int32_t finishes = claim.finishes() - g_finishesBefore;
		const bool boundSecond =
		    holder != nullptr && holder->audioFile != nullptr && holder->filePath == supersedeSecond();
		printf("SC-SUPERSEDE case=%s underOp=%d cancels=%d finishes=%d boundSecond=%d ticks=%d\n", supersedeCase,
		       g_supersedeFired ? 1 : 0, cancels, finishes, boundSecond ? 1 : 0, g_waitTicks);
		if (!g_supersedeFired || cancels != 1 || finishes != 1 || !boundSecond) {
			printf(
			    "SC-SUPERSEDE FAIL case=%s an auto-load claim the cursor left with its pitch detection waiting must be "
			    "cancelled once, and only the next file's claim may finish\n",
			    supersedeCase);
			g_failures++;
		}
		// Auto-load off again, and out of the browser, which auto-load leaves open.
		deluge::harness::guestHideKeyboard();
		if (browserOnTop()) {
			deluge::harness::guestPress(deluge::hid::button::LOAD);
		}
		guestClose();
		break;
	}
	case Leg::WaveTableConverted: {
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const bool isWaveTable = bound && holder->audioFile->type == AudioFileType::WAVETABLE;
		const ConversionMark conv = conversionsSince(g_convBefore);
		const uint32_t crc = isWaveTable ? wavetable_band_crc(static_cast<WaveTable*>(holder->audioFile)->view()) : 0;
		// The reference: the twin, never previewed, so built from the card. Measured after the deltas above, which
		// its own card build would otherwise count.
		uint32_t twinCrc = 0;
		{
			std::string twin = kFullWaveTableTwin;
			Error error = Error::NONE;
			AudioFile* twinFile = audioFileManager.getAudioFileFromFilename(
			    twin, /*mayReadCard=*/true, &error, AudioFileType::WAVETABLE, /*makeWaveTableWorkAtAllCosts=*/true);
			if (twinFile != nullptr && twinFile->type == AudioFileType::WAVETABLE) {
				twinCrc = wavetable_band_crc(static_cast<WaveTable*>(twinFile)->view());
			}
		}
		printf("SC-WAVETABLE-CONVERTED bound=%d waveTable=%d converted=%d fromCard=%d pinned=%d crc=%08X twinCrc=%08X "
		       "ticks=%d\n",
		       bound ? 1 : 0, isWaveTable ? 1 : 0, conv.converted, conv.fromCard, conv.pinned, (unsigned)crc,
		       (unsigned)twinCrc, g_waitTicks);
		if (!isWaveTable || conv.converted != 1 || conv.fromCard != 0 || conv.pinned != kFullWaveTableClusters
		    || crc == 0 || crc != twinCrc) {
			printf("SC-WAVETABLE-CONVERTED FAIL a fully cached Sample claimed as a wavetable must be converted from "
			       "memory, with every cluster pinned, into the WaveTable the card would have built\n");
			g_failures++;
		}
		break;
	}
	case Leg::WaveTableResident: {
		deluge_streaming_set_fill_latency_ms(0);
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const bool isWaveTable = bound && holder->audioFile->type == AudioFileType::WAVETABLE;
		const OscType osc = g_synth->sources[0].oscType;
		const ConversionMark conv = conversionsSince(g_convBefore);
		printf("SC-WAVETABLE-RESIDENT bound=%d waveTable=%d osc=%d path=%s ticks=%d converted=%d fromCard=%d\n",
		       bound ? 1 : 0, isWaveTable ? 1 : 0, static_cast<int>(osc),
		       holder != nullptr ? holder->filePath.c_str() : "", g_waitTicks, conv.converted, conv.fromCard);
		if (conv.converted != 0 || conv.fromCard != 1) {
			printf("SC-WAVETABLE-RESIDENT FAIL a Sample with clusters not yet cached must be built from the card, not "
			       "converted\n");
			g_failures++;
		}
		if (!bound || !isWaveTable || osc != OscType::WAVETABLE) {
			printf("SC-WAVETABLE-RESIDENT FAIL a file resident as a Sample, most of its clusters not yet filled, must "
			       "still be claimed as a wavetable\n");
			g_failures++;
		}
		break;
	}
	case Leg::Pitch: {
		deluge_streaming_set_fill_latency_ms(0);
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const int32_t transpose = holder != nullptr ? static_cast<SampleHolderForVoice*>(holder)->transpose : 0;
		const int32_t scanTicks = deluge::gui::sampleClaim.pitchScanTicks() - g_pitchTicksBefore;
		const int32_t giveUps = deluge::gui::sampleClaim.pitchGiveUps() - g_pitchGiveUpsBefore;
		printf("SC-PITCH bound=%d transpose=%d scanTicks=%d giveUps=%d ticks=%d\n", bound ? 1 : 0, transpose, scanTicks,
		       giveUps, g_waitTicks);
		if (!bound || transpose != kPitchTranspose || scanTicks < 1 || giveUps != 0) {
			printf("SC-PITCH FAIL a detection whose clusters arrive late must wait for them and find the same note\n");
			g_failures++;
		}
		break;
	}
	case Leg::PitchStall: {
		deluge_streaming_set_fill_latency_ms(0);
		AudioFileHolder* holder = firstHolder(g_synth->sources[0]);
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		const bool noteError = bound && static_cast<Sample*>(holder->audioFile)->midiNote == MIDI_NOTE_ERROR;
		const int32_t transpose = holder != nullptr ? static_cast<SampleHolderForVoice*>(holder)->transpose : 0;
		const int32_t giveUps = deluge::gui::sampleClaim.pitchGiveUps() - g_pitchGiveUpsBefore;
		const bool inFlight = deluge::gui::sampleClaim.inFlight();
		const bool released = (currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES);
		printf("SC-PITCH-STALL bound=%d noteError=%d transpose=%d giveUps=%d inFlight=%d released=%d ticks=%d\n",
		       bound ? 1 : 0, noteError ? 1 : 0, transpose, giveUps, inFlight ? 1 : 0, released ? 1 : 0, g_waitTicks);
		// inFlight()==false with the gate released is the claim machine able to take the next claim: routine() starts
		// a queued claim only from Idle.
		if (!bound || !noteError || transpose != kTransposeSentinel || giveUps != 1 || inFlight || !released) {
			printf("SC-PITCH-STALL FAIL a detection whose clusters never arrive must give up once, leave the note an "
			       "error and the transpose alone, and still finish the claim\n");
			g_failures++;
		}
		break;
	}
	case Leg::Kit: {
		SoundDrum* drum = kitDrum();
		AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
		const bool bound = holder != nullptr && holder->audioFile != nullptr;
		printf("SC-KIT bound=%d path=%s repeat=%d name=%s sideChain=%d ticks=%d\n", bound ? 1 : 0,
		       holder != nullptr ? holder->filePath.c_str() : "",
		       drum != nullptr ? static_cast<int>(drum->sources[0].repeatMode) : -1,
		       drum != nullptr ? drum->name().c_str() : "", drum != nullptr ? drum->sideChainSendLevel : 0,
		       g_waitTicks);
		if (!bound) {
			printf("SC-KIT FAIL the claim bound no file\n");
			g_failures++;
		}
		break;
	}
	case Leg::AudioClip: {
		auto* clip = static_cast<AudioClip*>(g_audioClip);
		const bool bound = clip->sampleHolder.audioFile != nullptr;
		printf("SC-AUDIOCLIP bound=%d path=%s loopLength=%d attack=%d ticks=%d\n", bound ? 1 : 0,
		       clip->sampleHolder.filePath.c_str(), clip->loopLength(), clip->attack(), g_waitTicks);
		if (!bound) {
			printf("SC-AUDIOCLIP FAIL the claim bound no file\n");
			g_failures++;
		}
		break;
	}
	case Leg::ShellSlice:
		checkRegionSlice("SC-SHELL-SLICE");
		printf("SC-SHELL-SLICE teardown held=%d rootOnly=%d dropped=%d next held=%d heldThrough=%d pending=%d\n",
		       g_teardownHeld ? 1 : 0, g_teardownRootKept ? 1 : 0, g_teardownDropped ? 1 : 0, g_nextHeld ? 1 : 0,
		       g_nextHeldThrough ? 1 : 0, playbackHandler.nextSongPending_ ? 1 : 0);
		if (!g_teardownHeld || !g_teardownRootKept) {
			printf("SC-SHELL-SLICE FAIL no audition load was out under the root alone at the teardown, so nothing was "
			       "tested\n");
			g_failures++;
		}
		else if (!g_teardownDropped) {
			printf("SC-SHELL-SLICE FAIL a teardown of every UI left the slicer's held audition out\n");
			g_failures++;
		}
		if (!g_nextHeld) {
			printf("SC-SHELL-SLICE FAIL a MIDI next-song during the slice was not held back for it\n");
			g_failures++;
		}
		else if (!g_nextHeldThrough || playbackHandler.nextSongPending_) {
			printf("SC-SHELL-SLICE FAIL a next-song held back for the slice did not wait for it to land, then go\n");
			g_failures++;
		}
		playbackHandler.nextSongPending_ = false;
		break;
	case Leg::ShellSliceManual:
		checkManualSlice("SC-SHELL-SLICE-MANUAL");
		break;
	case Leg::ShellSliceBack:
		printf("SC-SHELL-SLICE-BACK kept=%d\n", g_shellBackKept ? 1 : 0);
		if (!g_shellBackKept) {
			printf("SC-SHELL-SLICE-BACK FAIL BACK out of manual mode changed a first drum that holds no file\n");
			g_failures++;
		}
		break;
	case Leg::ShellSliceFail: {
		deluge_audio_load_set_latency_ms(0);
		int32_t drums = 0;
		for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum; drum != nullptr; drum = drum->next) {
			drums++;
		}
		const bool ended = g_shellEnded && !deluge::gui::ui::shellSlicerBusy();
		const bool opened = deluge::scenes::slicing::slicer_scene().has_sample();
		printf("SC-SHELL-SLICE-FAIL drums=%d ended=%d opened=%d ticks=%d\n", drums, ended ? 1 : 0, opened ? 1 : 0,
		       g_waitTicks);
		if (drums != 1 || !ended || opened) {
			printf("SC-SHELL-SLICE-FAIL FAIL a slicer whose open stalled did not give up with the kit untouched\n");
			g_failures++;
		}
		break;
	}
	case Leg::ImportGate: {
		deluge_audio_load_set_latency_ms(0);
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		int32_t drums = 0;
		for (Drum* drum = static_cast<Kit*>(g_importGateClip->output)->firstDrum; drum != nullptr; drum = drum->next) {
			drums++;
		}
		const bool completed = drums == kImportGateCount && !browserOnTop();
		printf("IM-GATE whileRunning=%d secondRefused=%d backRefused=%d released=%d completed=%d ticks=%d\n",
		       g_imGateWhileRunning ? 1 : 0, g_imGateSecondRefused ? 1 : 0, g_imGateBackRefused ? 1 : 0,
		       released ? 1 : 0, completed ? 1 : 0, g_waitTicks);
		if (!g_imGateWhileRunning) {
			printf("IM-GATE FAIL the import had ended before its held loads could land: an import must wait on its "
			       "loads with the gate closed\n");
			g_failures++;
		}
		else if (!g_imGateSecondRefused || !g_imGateBackRefused || !released || !completed) {
			printf(
			    "IM-GATE FAIL BACK or a second Select was taken while the import was out, or the gate stayed closed, "
			    "or the import did not complete once its loads landed\n");
			g_failures++;
		}
		break;
	}
	case Leg::ImportEmpty: {
		// `main` lands a Load All of a folder with no audio file of its own as an import of nothing: the kit keeps
		// its one drum, the browser closes onto the clip view, and no error shows. Its sort area of no samples must not
		// be a zero-byte allocation, which our allocator answers with null (INSUFFICIENT_RAM).
		int32_t drums = 0;
		for (Drum* drum = static_cast<Kit*>(g_importEmptyClip->output)->firstDrum; drum != nullptr; drum = drum->next) {
			drums++;
		}
		const bool browserClosed = !browserOnTop();
		const bool onClipView = getCurrentUI() == &instrumentClipView;
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		printf("IM-EMPTY succeeded=%d drums=%d browserClosed=%d onClipView=%d released=%d listings=%d\n",
		       g_imEmptySucceeded ? 1 : 0, drums, browserClosed ? 1 : 0, onClipView ? 1 : 0, released ? 1 : 0,
		       deluge::gui::folderImport.listingsRun());
		if (!g_imEmptySucceeded || drums != 1 || !browserClosed || !onClipView || !released) {
			printf("IM-EMPTY FAIL Load All on a folder with no audio file of its own did not land as main's: an "
			       "import of nothing, the kit's one drum kept, the browser closed onto the clip view\n");
			g_failures++;
		}
		break;
	}
	case Leg::ImportStall: {
		deluge_audio_load_set_latency_ms(0);
		Source& source = static_cast<SoundInstrument*>(g_importSynthClip->output)->sources[0];
		bool rangesKept = std::ssize(source.ranges) == std::ssize(g_imStallRangePaths);
		for (int32_t i = 0; rangesKept && i < std::ssize(source.ranges); i++) {
			rangesKept = static_cast<MultisampleRange*>(source.ranges.getElement(i))->sampleHolder.filePath
			             == g_imStallRangePaths[i];
		}
		bool reasonsKept = true;
		for (int32_t i = 0; i < 2; i++) {
			reasonsKept =
			    reasonsKept && g_imStallKept[i] != nullptr && g_imStallKept[i]->leaseCount() == g_imStallKeptLeases[i];
		}
		const bool menuClosed = !guestTop(deluge::scenes::context_menu::kContextMenuSceneKind);
		const bool browserOpen = browserOnTop();
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		// How the import ended: on the async sample load's stall bound, not on some other failure.
		printf("SC-PROBE leg=import-stall giveUps=%d\n", deluge::gui::folderImport.giveUps());
		printf("IM-STALL rangesKept=%d reasonsKept=%d menuClosed=%d browserOpen=%d released=%d\n", rangesKept ? 1 : 0,
		       reasonsKept ? 1 : 0, menuClosed ? 1 : 0, browserOpen ? 1 : 0, released ? 1 : 0);
		if (!rangesKept || !reasonsKept || !menuClosed || !browserOpen || !released) {
			printf("IM-STALL FAIL an import whose second file's load never answered did not abort cleanly: the synth's "
			       "ranges and the loaded files' reasons must be as they were, just the menu closed, the browser open, "
			       "and "
			       "the gate released\n");
			g_failures++;
		}
		break;
	}
	case Leg::ImportKit:
	case Leg::ImportMulti: {
		const ImportMark now = markImport();
		const int32_t sampleIo = now.sampleIo - g_importMark.sampleIo;
		const int32_t listingsRun = deluge::gui::folderImport.listingsRun();
		printf("IM-TIER leg=%s listingsRun=%d sampleIo=%d\n", legName(g_leg), listingsRun, sampleIo);
		if (sampleIo != 0) {
			printf("IM-TIER FAIL leg=%s %d sample read(s) went to the card on the interaction tier -- a folder "
			       "import's loads belong on the interaction tier\n",
			       legName(g_leg), sampleIo);
			g_failures++;
		}
		if (listingsRun == 0) {
			printf("IM-TIER FAIL leg=%s no folder listing ran at all\n", legName(g_leg));
			g_failures++;
		}
		const bool browserClosed = !browserOnTop();
		if (g_leg == Leg::ImportKit) {
			auto* kit = static_cast<Kit*>(g_importKitClip->output);
			int32_t drums = 0;
			int32_t matched = 0;
			for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next, drums++) {
				if (drum->type != DrumType::SOUND) {
					continue;
				}
				auto* soundDrum = static_cast<SoundDrum*>(drum);
				AudioFileHolder* holder = firstHolder(soundDrum->sources[0]);
				printf("SC-PROBE leg=import-kit drum=%d name=%s path=%s bound=%d repeat=%d sideChain=%d\n", drums,
				       soundDrum->name().c_str(), holder != nullptr ? holder->filePath.c_str() : "",
				       (holder != nullptr && holder->audioFile != nullptr) ? 1 : 0,
				       static_cast<int>(soundDrum->sources[0].repeatMode), soundDrum->sideChainSendLevel);
				if (drums < kImportKitCount && holder != nullptr && holder->audioFile != nullptr
				    && soundDrum->name() == kImportKitNames[drums]
				    && holder->filePath == std::string{kImportKitFolder} + "/IMK_" + kImportKitNames[drums] + ".WAV") {
					matched++;
				}
			}
			auto* clip = static_cast<InstrumentClip*>(g_importKitClip);
			int32_t rowsWithDrum = 0;
			for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
				if (clip->noteRows[i].drum != nullptr) {
					rowsWithDrum++;
				}
			}
			printf("IM-KIT drums=%d matched=%d rowsWithDrum=%d edited=%d browserClosed=%d ticks=%d\n", drums, matched,
			       rowsWithDrum, kit->editedByUser ? 1 : 0, browserClosed ? 1 : 0, g_waitTicks);
			if (drums != kImportKitCount || matched != kImportKitCount || rowsWithDrum < kImportKitCount
			    || !kit->editedByUser || !browserClosed) {
				printf(
				    "IM-KIT FAIL the kit import did not end with one drum per file in filename order, each named after "
				    "its file without the prefix, bound to it and with a NoteRow, the kit edited and the browser "
				    "closed\n");
				g_failures++;
			}
		}
		else {
			auto* synth = static_cast<SoundInstrument*>(g_importSynthClip->output);
			Source& source = synth->sources[0];
			const int32_t ranges = std::ssize(source.ranges);
			int32_t matched = 0;
			for (int32_t i = 0; i < ranges; i++) {
				auto* range = static_cast<MultisampleRange*>(source.ranges.getElement(i));
				const bool bound = range->sampleHolder.audioFile != nullptr;
				printf("SC-PROBE leg=import-multi range=%d top=%d path=%s bound=%d transpose=%d\n", i, range->topNote,
				       range->sampleHolder.filePath.c_str(), bound ? 1 : 0, range->sampleHolder.transpose);
				if (i < kImportMultiCount && bound && range->sampleHolder.filePath == kImportMultiFiles[i]
				    && range->topNote == kImportMultiTopNotes[i]
				    && range->sampleHolder.transpose == kImportMultiTransposes[i]) {
					matched++;
				}
			}
			// The range the import made current (`rangeChosen`), as the menu context keeps it: the guest has closed, so
			// no menu is open on it.
			const int32_t menuRange = deluge::scenes::menu_scene().menu_context().range_index;
			const int32_t editorRange = menuRange >= 0 && menuRange < ranges ? menuRange : -1;
			printf(
			    "IM-MULTI ranges=%d matched=%d repeat=%d osc=%d edited=%d browserClosed=%d editorRange=%d ticks=%d\n",
			    ranges, matched, static_cast<int>(source.repeatMode), static_cast<int>(source.oscType),
			    synth->editedByUser ? 1 : 0, browserClosed ? 1 : 0, editorRange, g_waitTicks);
			if (editorRange != kImportMultiCount / 2) {
				printf("IM-MULTI FAIL the menu context was not left on the middle range\n");
				g_failures++;
			}
			if (ranges != kImportMultiCount || matched != kImportMultiCount
			    || source.repeatMode != SampleRepeatMode::ONCE || source.oscType != OscType::SAMPLE || !browserClosed) {
				printf("IM-MULTI FAIL the multisample import did not end with one range per file in pitch order, each "
				       "bound to its file with the top note today's importer gives it and the transpose its detection "
				       "gives "
				       "it, one-second files playing once, and "
				       "the browser closed\n");
				g_failures++;
			}
		}
		break;
	}
	case Leg::RandomizeGate: {
		deluge_audio_load_set_latency_ms(0);
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		if (released && g_rdGateWhileRunning) {
			// The same pad, now the gate is open: taken, then taken away again so the clip is as it was.
			g_rdGateEditTakenAfter = pressGateEditPad();
			(void)pressGateEditPad();
		}
		printf("RD-GATE whileRunning=%d secondWalkRefused=%d editPadRefused=%d editPadTakenAfter=%d released=%d "
		       "ticks=%d\n",
		       g_rdGateWhileRunning ? 1 : 0, g_rdGateSecondWalkRefused ? 1 : 0, g_rdGateEditRefused ? 1 : 0,
		       g_rdGateEditTakenAfter ? 1 : 0, released ? 1 : 0, g_waitTicks);
		if (!g_rdGateWhileRunning) {
			printf("RD-GATE FAIL the walk had ended before its held load could land: a randomize must wait on its load "
			       "with the gate closed\n");
			g_failures++;
		}
		else if (!g_rdGateSecondWalkRefused || !g_rdGateEditRefused || !g_rdGateEditTakenAfter || !released) {
			printf(
			    "RD-GATE FAIL a second randomize or the edit pad was taken while the walk was out, or the gate stayed "
			    "closed once it ended\n");
			g_failures++;
		}
		break;
	}
	case Leg::RandomizeStall: {
		deluge_audio_load_set_latency_ms(0);
		auto* first = static_cast<SoundDrum*>(static_cast<Kit*>(g_kitClip->output)->firstDrum);
		auto* second = static_cast<SoundDrum*>(first->next);
		auto* third = static_cast<SoundDrum*>(second->next);
		AudioFileHolder* kept = firstHolder(first->sources[0]);
		AudioFileHolder* held = firstHolder(second->sources[0]);
		const bool keptFirst = kept != nullptr && kept->audioFile != nullptr && kept->filePath == kRandomizeStallKept
		                       && first->name() == kRandomizeStallKeptName;
		const bool secondUnbound = held != nullptr && held->audioFile == nullptr;
		// Untouched: still named as the preset named it, which a randomize would have replaced with its file's name.
		const bool thirdUntouched = third->name() == kRandomizeDrums[2].name;
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		// How the walk ended: on the async sample load's stall bound, not on some other failure.
		printf("SC-PROBE leg=randomize-stall giveUps=%d\n", deluge::gui::randomizeDrums.giveUps());
		printf("RD-STALL keptFirst=%d secondUnbound=%d thirdUntouched=%d released=%d\n", keptFirst ? 1 : 0,
		       secondUnbound ? 1 : 0, thirdUntouched ? 1 : 0, released ? 1 : 0);
		if (!keptFirst || !secondUnbound || !thirdUntouched || !released) {
			printf("RD-STALL FAIL a walk whose second row's load never answered did not stop there: the first row must "
			       "keep its file, the second must be left unbound, the third untouched, and the gate released\n");
			g_failures++;
		}
		break;
	}
	case Leg::RandomizeCancelScan: {
		deluge_file_directory_set_latency_ms(0);
		SoundDrum* drum = kitDrum();
		AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
		const int32_t entered = deluge::gui::randomizeDrums.drainsEntered() - g_rdCancelDrainsEnteredBefore;
		const int32_t collected = deluge::gui::randomizeDrums.drainsCollected() - g_rdCancelDrainsCollectedBefore;
		// The claim the rounds are for: a randomize made after them still gets a directory handle, so every handle
		// their cancelled opens produced went back. Nothing else in this leg can see a burned handle -- a drain that
		// collects and drops one looks identical from the outside until the slots run out.
		const bool landed =
		    holder != nullptr && holder->audioFile != nullptr && holder->filePath.rfind(kRandomizeFolder, 0) == 0;
		const bool released = currentUIMode != UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		printf("RD-CANCELSCAN rounds=%d entered=%d collected=%d drained=%d rowUntouched=%d landed=%d released=%d "
		       "ticks=%d\n",
		       g_rdCancelRound, entered, collected, g_rdCancelDrained, g_rdCancelRowUntouched ? 1 : 0, landed ? 1 : 0,
		       released ? 1 : 0, g_waitTicks);
		if (g_rdCancelRound != kRandomizeCancelRounds || entered != kRandomizeCancelRounds) {
			// Without this the leg proves nothing: every claim below is about what a drain does, and a cancel that
			// found nothing outstanding never drains, so a leg whose cancels all come too late goes quietly green.
			printf("RD-CANCELSCAN FAIL only %d of %d walks were cancelled with the row's open still out, so the "
			       "drain ran %d time(s)\n",
			       g_rdCancelRound, kRandomizeCancelRounds, entered);
			g_failures++;
		}
		else if (collected != kRandomizeCancelRounds) {
			printf("RD-CANCELSCAN FAIL %d of %d drains never collected the handle their cancelled open produced\n",
			       kRandomizeCancelRounds - collected, kRandomizeCancelRounds);
			g_failures++;
		}
		else if (!landed) {
			printf("RD-CANCELSCAN FAIL a randomize made after %d cancelled opens did not bind a file from its folder: "
			       "either the handles those opens produced were never given back, or its load is queued behind one "
			       "an earlier leg left in the loader\n",
			       kRandomizeCancelRounds);
			g_failures++;
		}
		else if (g_rdCancelDrained != 0 || !g_rdCancelRowUntouched || !released) {
			printf("RD-CANCELSCAN FAIL a cancelled walk did more than discard: it must read no entries and leave the "
			       "row's holder and name as they were, and the gate must be released at the end\n");
			g_failures++;
		}
		break;
	}
	case Leg::RandomizeOne:
	case Leg::RandomizeAll: {
		const RandomizeMark now = markRandomize();
		const int32_t sampleIo = now.sampleIo - g_randomizeMark.sampleIo;
		const int32_t scansRun = deluge::gui::randomizeDrums.scansRun();
		printf("RD-TIER leg=%s scansRun=%d sampleIo=%d\n", legName(g_leg), scansRun, sampleIo);
		if (sampleIo != 0) {
			printf("RD-TIER FAIL leg=%s %d sample read(s) went to the card on the interaction tier -- a randomized "
			       "drum's load belongs on the interaction tier\n",
			       legName(g_leg), sampleIo);
			g_failures++;
		}
		if (scansRun == 0) {
			printf("RD-TIER FAIL leg=%s no randomizer scan ran at all\n", legName(g_leg));
			g_failures++;
		}

		int32_t randomized = 0;
		int32_t untouched = 0;
		int32_t index = 0;
		for (Drum* drum = static_cast<Kit*>(g_kitClip->output)->firstDrum;
		     drum != nullptr && index < kRandomizeDrumCount; drum = drum->next, index++) {
			AudioFileHolder* holder = firstHolder(static_cast<SoundDrum*>(drum)->sources[0]);
			const bool eligible = (g_leg == Leg::RandomizeAll) ? (index < kRandomizeEligible) : (index == 0);
			const bool inFolder =
			    holder != nullptr && holder->audioFile != nullptr && holder->filePath.rfind(kRandomizeFolder, 0) == 0;
			const std::string& name = static_cast<SoundDrum*>(drum)->name();
			const bool namedAfterFile =
			    inFolder && holder->filePath.rfind(name.c_str()) != std::string::npos && !name.empty();
			printf("SC-PROBE leg=%s drum=%d path=%s name=%s\n", legName(g_leg), index,
			       holder != nullptr ? holder->filePath.c_str() : "", name.c_str());
			if (eligible) {
				randomized += (inFolder && namedAfterFile) ? 1 : 0;
			}
			else {
				untouched += (holder != nullptr && holder->filePath == g_randomizeRowPaths[index]) ? 1 : 0;
			}
		}
		const int32_t wantRandomized = (g_leg == Leg::RandomizeAll) ? kRandomizeEligible : 1;
		const int32_t wantUntouched = kRandomizeDrumCount - wantRandomized;
		if (g_leg == Leg::RandomizeOne) {
			printf("RD-ONE randomized=%d untouched=%d scans=%d ticks=%d\n", randomized, untouched,
			       (int)deluge::gui::randomizeDrums.scansRun(), g_waitTicks);
			if (randomized != wantRandomized || untouched != wantUntouched) {
				printf("RD-ONE FAIL the selected drum did not end on a file from its own folder, named after it, with "
				       "every other row untouched\n");
				g_failures++;
			}
		}
		else {
			printf("RD-ALL randomized=%d untouched=%d scans=%d ticks=%d\n", randomized, untouched,
			       (int)deluge::gui::randomizeDrums.scansRun(), g_waitTicks);
			if (randomized != wantRandomized || untouched != wantUntouched) {
				printf("RD-ALL FAIL an affect-entire randomize did not end with every unmuted row that has notes on a "
				       "file from its own folder, named after it, leaving the muted and empty rows alone\n");
				g_failures++;
			}
		}
		break;
	}
	case Leg::Done:
		break;
	}
}

/// One tick of a supersede leg: the browser ticked, and the moment the first auto-load claim's pitch detection is out,
/// the cursor moved on to the second file, which cancels that claim.
void tickSupersede() {
	if (g_supersedeFired || !deluge::gui::sampleClaim.pitchPending()) {
		return;
	}
	g_supersedeFired = true;
	if (g_leg == Leg::SupersedeLoading) {
		// Its preview held: the claim the cursor left must go now, not when this preview lands.
		deluge_audio_load_set_latency_ms(kSupersedeHoldMs);
	}
	// The landed leg's second file is in memory, so its preview lands in the turn's own tick with the detection still
	// waiting for its cluster -- as on the device, where detection reads the card for longer than a graphics tick --
	// and asks for the next claim at once.
	const std::string_view second = supersedeSecond();
	if (!deluge::harness::guestTurnTo(second.substr(second.rfind('/') + 1))) {
		fail(legName(g_leg), "the cursor could not be moved onto the second file");
	}
}

/// One tick of the cancel-loading leg: a few ticks into the held load, the browser's exit cancels the auto-load claim.
void tickCancelLoading() {
	if (g_cancelLoadingFired || ++g_cancelLoadingTicks < 3) {
		return;
	}
	g_cancelLoadingInFlight = deluge::gui::sampleClaim.inFlight();
	guestBack();
	g_cancelLoadingFired = true;
}

/// One tick of the cancel leg.
///
/// A round lets the row's open reach the held directory channel, cancels the walk through the real user action --
/// returning to the clip view, the only caller of cancelWalk() -- and waits for the drain that cancel starts. After
/// the last round the hold comes off and the leg randomizes for real: that walk needs a directory handle, and the
/// rounds have asked for one more than there are.
void tickRandomizeCancel() {
	auto& walk = deluge::gui::randomizeDrums;
	if (g_rdCancelFinalPressed) {
		return; // the real walk is running; legEnded() waits for it
	}
	if (!g_rdCancelCancelled) {
		if (++g_rdCancelPasses < kRandomizeCancelPasses) {
			return; // this round's open has not reached the channel yet
		}
		instrumentClipView.focusRegained();
		g_rdCancelCancelled = true;
		return;
	}
	if (walk.draining()) {
		return; // waiting for the handle the cancelled open still produces
	}
	g_rdCancelRound++;
	g_rdCancelCancelled = false;
	g_rdCancelPasses = 0;
	if (g_rdCancelRound < kRandomizeCancelRounds) {
		pressRandomize(/*affectEntire=*/false);
		return;
	}
	// Read before the real walk runs: it reads a folder and rebinds the row, which is exactly what a cancelled one
	// must not have done.
	g_rdCancelDrained = walk.scansRun() - g_rdCancelScansBefore;
	SoundDrum* drum = kitDrum();
	AudioFileHolder* holder = (drum != nullptr) ? firstHolder(drum->sources[0]) : nullptr;
	g_rdCancelRowUntouched = holder != nullptr && holder->audioFile == nullptr
	                         && holder->filePath == kRandomizeFirstSample && drum->name() == kRandomizeDrums[0].name;
	deluge_file_directory_set_latency_ms(0);
	pressRandomize(/*affectEntire=*/false);
	g_rdCancelFinalPressed = true;
}

/// Whether the leg's item is a folder, which no favourite can hold (a favourite names a file).
bool legItemIsFolder() {
	return g_leg == Leg::ImportEmpty;
}

/// The file a leg's favourite holds: the leg's own, or for a folder item a file in the folder beside it, whose own
/// folder BACK leaves for the one the item is in.
const char* recallFile() {
	return legItemIsFolder() ? kImportKitFile : legFile();
}

/// One step of bringing the browser to the leg's item, which it opened beside the file the sound holds: a favourite
/// holding the file recalled, and for a folder item BACK out of the file's folder and a turn onto the item.
/// @return Whether the leg is done with it, the browser on the item or never to get there; false while a step waits, or
///         after one that started a listing (the step goes back to the listing wait).
bool landStep() {
	const std::string target = legFile();
	if (guestSelectedPath() == target || g_browseSteps >= kMaxBrowseSteps) {
		return true;
	}
	const std::string_view dir = std::string_view{target}.substr(0, target.rfind('/'));
	const std::string_view current = deluge::scenes::browser::browser_scene().current_dir();
	if (legItemIsFolder() && current == dir
	    && deluge::harness::guestTurnTo(std::string_view{target}.substr(dir.size() + 1))) {
		g_browseSteps++;
		return true;
	}
	if (legItemIsFolder() && current.size() > dir.size() && current.starts_with(dir) && current[dir.size()] == '/') {
		g_browseSteps++;
		guestBack();
		g_step = Step::Listing;
		return false;
	}
	if (!deluge::harness::guestRecallFavourite(recallFile())) {
		// The favourites bank is still loading.
		return ++g_waitTicks > kMaxLegTicks;
	}
	g_browseSteps++;
	g_waitTicks = 0;
	g_step = Step::Listing;
	return false;
}

void nextLeg() {
	// Whatever the leg did to a sound, its record, slot and snapshot say what it plays.
	g_failures += deluge::harness::expectSongCoherent(*currentSong, legName(g_leg));
	g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
	g_step = Step::Prepare;
	g_waitTicks = 0;
}

/// Whether the blank song's own preset request has landed. The boot song starts on the built-in default synth and asks
/// InstrumentLoad for the first SYNTHS preset on the interaction tier; a scenario that acted on the boot clip before
/// that landed would be working on an Instrument about to be replaced, which no user can do.
bool bootSettled() {
	static int32_t waited = 0;
	if (currentSong != nullptr && (currentSong->blankPresetPending || deluge::gui::instrumentLoad.inFlight())
	    && ++waited < 2000) {
		return false;
	}
	return true;
}

void runTick() {
	if (!bootSettled()) {
		return;
	}
	if (g_leg == Leg::Done) {
		printf("SC-RESULT failures=%d\n", g_failures);
		fflush(stdout);
		g_summaryPrinted = true;
		return;
	}
	switch (g_step) {
	case Step::Prepare:
		prepareLeg();
		g_step = Step::Open;
		break;
	case Step::Open:
		// A claim still in flight from the leg before -- one that timed out -- would act on this leg's clip.
		if (deluge::gui::sampleClaim.inFlight()) {
			if (++g_waitTicks > kMaxLegTicks) {
				fail(legName(g_leg), "a claim from the leg before never ended");
				nextLeg();
			}
			break;
		}
		g_waitTicks = 0;
		g_browseSteps = 0;
		if (openLeg()) {
			// The randomizer legs press a pad in the clip view, and the ported slicer's host opens the file itself: no
			// listing, no file to select, no preview to draw.
			g_step = (isRandomize() || isShellSlice()) ? Step::Act : Step::Listing;
		}
		else {
			nextLeg();
		}
		break;
	case Step::Listing:
		if (listingBusy()) {
			if (++g_waitTicks > kMaxListingTicks) {
				fail(legName(g_leg), "a browser listing never finished");
				nextLeg();
			}
			break;
		}
		g_waitTicks = 0;
		g_step = Step::Select;
		break;
	case Step::Select: {
		if (!landStep()) {
			break;
		}
		deluge::harness::guestHideKeyboard();
		const std::string selected = guestSelectedPath();
		printf("SC-SELECT leg=%s file=%s steps=%d\n", legName(g_leg), selected.c_str(), g_browseSteps);
		if (selected != legFile()) {
			fail(legName(g_leg), "the browser could not be brought to the leg's item");
			nextLeg();
			break;
		}
		g_waitTicks = 0;
		// The resident-wavetable leg claims a file the preview has made resident as a Sample.
		g_step = (isSupersede() || g_leg == Leg::WaveTableResident || g_leg == Leg::WaveTableConverted)
		             ? Step::PreviewWait
		             : Step::Act;
		break;
	}
	case Step::PreviewWait: {
		// The legs that claim a previewed file wait for the browser's preview of it to land.
		const Sample* landed = previewedSample();
		if (landed != nullptr && landed->filePath == legFile()) {
			printf("SC-PROBE leg=%s preview landed ticks=%d\n", legName(g_leg), g_waitTicks);
			g_step = (g_leg == Leg::WaveTableConverted) ? Step::ResidentWait : Step::Act;
			g_waitTicks = 0;
			break;
		}
		guestTick();
		if (++g_waitTicks > kMaxLegTicks) {
			fail(legName(g_leg), "the browser's preview never landed on the file");
			nextLeg();
		}
		break;
	}
	case Step::ResidentWait: {
		// The converted leg claims a file every cluster of which is cached: ask for each until all are there at once.
		Sample* previewed = previewedSample();
		if (previewed != nullptr && previewed->filePath == legFile() && acquireAllClusters(*previewed)) {
			printf("SC-PROBE leg=%s fully resident ticks=%d\n", legName(g_leg), g_waitTicks);
			g_step = Step::Act;
			break;
		}
		if (++g_waitTicks > kMaxLegTicks) {
			fail(legName(g_leg), "the file never became fully resident");
			nextLeg();
		}
		break;
	}
	case Step::Act:
		actLeg();
		g_waitTicks = 0;
		g_step = Step::Wait;
		break;
	case Step::Wait:
		// The guest's own clock: a scene files what its load's end asks for (a browser closing) from its render, which
		// a frozen clock never asks for.
		if (isUIOpen(&shellHostUI)) {
			guestTick();
		}
		if (isSupersede()) {
			tickSupersede();
		}
		if (g_leg == Leg::CancelLoading) {
			tickCancelLoading();
		}
		if (g_leg == Leg::ImportGate) {
			tickImportGate();
		}
		if (isRandomize() && getCurrentUI() == &instrumentClipView) {
			// A randomize may walk on the clip view's graphics routine: ticked here, as the slice legs tick the
			// slicer's.
			instrumentClipView.graphicsRoutine();
			if (g_leg == Leg::RandomizeGate) {
				tickRandomizeGate();
			}
			if (g_leg == Leg::RandomizeCancelScan) {
				tickRandomizeCancel();
			}
		}
		if (isShellSlice()) {
			tickShellSlice();
		}
		if (legEnded()
		    || ++g_waitTicks > ((g_leg == Leg::Stall || g_leg == Leg::RandomizeStall || g_leg == Leg::ImportStall
		                         || g_leg == Leg::PitchStall || g_leg == Leg::ShellSliceFail)
		                            ? kMaxStallTicks
		                            : kMaxLegTicks)) {
			checkLeg();
			nextLeg();
		}
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_sample_claim_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_leg = Leg::Synth;
	g_step = Step::Prepare;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_synth = nullptr;
	g_kitClip = nullptr;
	g_audioClip = nullptr;
	g_tierMark = {};
	g_gateMoved = false;
	g_gateClaimOut = false;
	g_supersedeFired = false;
}

bool deluge_scenario_sample_claim_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_sample_claim_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// Nothing this tick queued runs anywhere else: the step is done when the tick returns.
	g_stepDone = true;
}

bool deluge_scenario_sample_claim_step_done(void) {
	if (runPendingFixture()) {
		return false;
	}
	return g_stepDone;
}

int32_t deluge_scenario_sample_claim_failures(void) {
	return g_failures;
}

} // extern "C"
