/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

// host_debug.cpp — host-sim developer triggers for the reason-leak tooling.
//
// The reason-leak workflow is a round-trip diff: snapshot at idle, drive the suspect operation, return to idle, report
// what's still pinned. On the device that's a sysex command (Debug subcommand 4); on the host-sim it's two signals:
//
//     kill -USR1 <pid>     # snapshot the current reason state
//     ...drive the suspect operation, then return to idle...
//     kill -USR2 <pid>     # report Clusters pinned since the snapshot (with the addReason call-site)
//
// The signal handlers only set a flag (async-signal-safe); the actual table walk runs from deluge_host_debug_poll(),
// which the host audio backend calls once per rendered block on the app thread, so it always sees a consistent table.

#include "definitions_cxx.hpp"                     // OutputType, MIDI_CHANNEL_NONE, kDefaultLiftValue
#include "gui/ui/load/load_instrument_preset_ui.h" // loadInstrumentPresetUI
#include "gui/ui/load/load_song_ui.h"              // loadSongUI
#include "gui/ui/ui.h"                             // openUI
#include "gui/views/session_view.h"                // sessionView, gridCreateInstrumentClipWithNewTrack
#include "memory/reason_check.h"
#include "model/clip/instrument_clip.h"            // InstrumentClip
#include "model/instrument/melodic_instrument.h"   // MelodicInstrument
#include "model/model_stack.h"                     // MODEL_STACK_MAX_SIZE, setupModelStackWithSong
#include "model/song/song.h"                       // currentSong, getCurrentInstrumentClip/Instrument
#include "processing/engines/audio_engine.h"       // AudioEngine::getNumVoices
#include "processing/sound/sound_instrument.h"     // SoundInstrument
#include "processing/source.h"                     // Source, oscType, ranges
#include "storage/audio/audio_file_holder.h"       // AudioFileHolder
#include "storage/multi_range/multi_range.h"       // MultiRange
#include "storage/multi_range/multi_range_array.h" // MultiRangeArray
#include "util/d_string.h"

extern int16_t zeroMPEValues[]; // audio_engine.cpp — all-zero MPE vector for programmatic note-on

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" {
#include "fatfs/ff.h"
}
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#include <sanitizer/lsan_interface.h>
#endif

#if REASON_CHECK

namespace {
volatile std::sig_atomic_t g_request = 0; // 0 = nothing, 1 = snapshot, 2 = report
reason_check::Snapshot g_baseline = 0;

void onSignal(int sig) {
	g_request = (sig == SIGUSR1) ? 1 : 2;
}

// Kit-copy reproduction harness. With DELUGE_HOST_KIT_COPY_REPS=N, once the app has booted, create N new KIT tracks -
// each calls the path from the bug report (createNewTrackForInstrumentClip -> setPresetOrNextUnlaunchedOne, which loads
// the *next* unlaunched kit preset from KITS/ plus all its samples). The first loads 000 TR-808, then 001, 002 SDS-5,
// ... exactly as the device log showed. We snapshot reason_check around it; the FULL-internal pressure shows in the
// D_PRINTLN log, and any kit leaked on the INSUFFICIENT_RAM error path shows in the closing reportOutstanding().
int g_kitCopyReps = -1; // -1 = not yet read, 0 = disabled
uint32_t g_pollCount = 0;
bool g_kitCopyDone = false;

void runKitCopyHarnessIfDue() {
	if (g_kitCopyReps < 0) {
		const char* env = std::getenv("DELUGE_HOST_KIT_COPY_REPS");
		g_kitCopyReps = env ? std::atoi(env) : 0;
	}
	if (g_kitCopyReps <= 0 || g_kitCopyDone) {
		return;
	}
	// Let the app finish booting (song + browser + UI ready) before driving the operation.
	if (++g_pollCount < 200) {
		return;
	}
	g_kitCopyDone = true;

	printf("[kit-copy] creating %d new KIT tracks (each loads the next kit preset + its samples)\n", g_kitCopyReps);
	std::fflush(stdout);
	reason_check::Snapshot snap = reason_check::snapshot();

	for (int i = 0; i < g_kitCopyReps; i++) {
		Clip* clip = sessionView.reproCreateNewInstrumentClip(OutputType::KIT, 0);
		printf("[kit-copy] rep %d: clip=%p, pinned stealables now %u\n", i, (void*)clip,
		       (unsigned)reason_check::liveClusters());
		std::fflush(stdout);
		if (clip == nullptr) {
			printf("[kit-copy] rep %d returned null (INSUFFICIENT_RAM) - the leak-on-error path\n", i);
			std::fflush(stdout);
		}
	}

	printf("[kit-copy] done - reasons still outstanding across the run (leaks):\n");
	std::fflush(stdout);
	reason_check::reportOutstanding(snap);

#if defined(__SANITIZE_ADDRESS__)
	// The real leak is the raw Instrument structure leaked on the INSUFFICIENT_RAM error path (not a reason leak), so
	// ask LeakSanitizer to report unreachable allocations now (the host quick_exit()s, bypassing the at-exit check).
	printf("[kit-copy] LeakSanitizer check:\n");
	std::fflush(stdout);
	__lsan_do_recoverable_leak_check();
#endif
}

// ---- Song-load leak harness -------------------------------------------------
// With DELUGE_HOST_SONG_LOAD_DIR=<dir on card> (e.g. SONGS/problem_songs) and DELUGE_HOST_SONG_LOAD_REPS=N, once the
// app has booted, recursively find every .XML under that dir and load each one (full performLoad: read song -> swap in
// -> delete the old song), looping over the whole set N times. reason_check is snapshotted around the run and
// LeakSanitizer is asked to report at the end; any per-load leak (something not freed when a song is replaced) shows up
// as growth in the pinned-cluster count and as LSan findings. This is the "loading lots of projects" leak repro.
constexpr int kMaxSongPaths = 512;
char g_songPaths[kMaxSongPaths][256];
int g_numSongPaths = 0;
int g_songLoadReps = -1; // -1 = not yet read, 0 = disabled
uint32_t g_songPollCount = 0;
bool g_songLoadDone = false;

void collectXmlPaths(const char* dir, int depth) {
	if (depth > 6 || g_numSongPaths >= kMaxSongPaths) {
		return;
	}
	DIR dp;
	if (f_opendir(&dp, dir) != FR_OK) {
		return;
	}
	FILINFO fno;
	while (f_readdir(&dp, &fno) == FR_OK && fno.fname[0] != 0) {
		if (fno.fname[0] == '.' || (fno.fattrib & AM_SYS) || (fno.fattrib & AM_HID)) {
			continue;
		}
		char child[256];
		snprintf(child, sizeof(child), "%s/%s", dir, fno.fname);
		if (fno.fattrib & AM_DIR) {
			collectXmlPaths(child, depth + 1);
		}
		else {
			size_t n = strlen(fno.fname);
			if (n > 4 && strcasecmp(fno.fname + n - 4, ".XML") == 0 && g_numSongPaths < kMaxSongPaths) {
				strncpy(g_songPaths[g_numSongPaths], child, sizeof(g_songPaths[0]) - 1);
				g_numSongPaths++;
			}
		}
	}
	f_closedir(&dp);
}

void loadOneSong(const char* fullPath) {
#if defined(__SANITIZE_ADDRESS__)
	if (currentSong && __asan_address_is_poisoned((void*)currentSong)) {
		printf("[song-load] BEFORE load: currentSong %p is POISONED (dangling freed Song)\n", (void*)currentSong);
		std::fflush(stdout);
	}
#endif
	// Drive the same path the boot loader / enter-key uses: make loadSongUI the active UI (so the session view doesn't
	// try to render - and dereference the transiently-null currentSong - during performLoad's yields), point the
	// browser at this song, then perform the full load (read -> swap in -> delete the old song).
	openUI(&loadSongUI);
	Error e = loadSongUI.setFileByFullPath(OutputType::NONE, fullPath);
	if (e != Error::NONE) {
		printf("  [song-load] setFileByFullPath failed (%d) for %s\n", (int)e, fullPath);
		std::fflush(stdout);
		return;
	}
	loadSongUI.performLoad();
}

void runSongLoadHarnessIfDue() {
	if (g_songLoadReps < 0) {
		const char* reps = std::getenv("DELUGE_HOST_SONG_LOAD_REPS");
		g_songLoadReps = std::getenv("DELUGE_HOST_SONG_LOAD_DIR") ? (reps ? std::atoi(reps) : 1) : 0;
	}
	if (g_songLoadReps <= 0 || g_songLoadDone) {
		return;
	}
	if (++g_songPollCount < 200) { // let the app finish booting
		return;
	}
	g_songLoadDone = true;

	const char* dir = std::getenv("DELUGE_HOST_SONG_LOAD_DIR");
	collectXmlPaths(dir, 0);
	printf("[song-load] found %d .XML files under %s; loading each x%d\n", g_numSongPaths, dir, g_songLoadReps);
	std::fflush(stdout);

	// Optional cap on total loads, so we can stop short of memory exhaustion and get a clean LeakSanitizer report.
	const char* maxEnv = std::getenv("DELUGE_HOST_SONG_LOAD_MAX");
	int maxLoads = maxEnv ? std::atoi(maxEnv) : 0;
	int totalLoads = 0;

	reason_check::Snapshot snap = reason_check::snapshot();
	for (int rep = 0; rep < g_songLoadReps; rep++) {
		for (int i = 0; i < g_numSongPaths; i++) {
			if (maxLoads > 0 && totalLoads >= maxLoads) {
				goto doneLoading;
			}
			loadOneSong(g_songPaths[i]);
			totalLoads++;
			printf("[song-load] rep %d song %d/%d: %s | pinned stealables now %u\n", rep, i + 1, g_numSongPaths,
			       g_songPaths[i], (unsigned)reason_check::liveClusters());
			std::fflush(stdout);
		}
	}
doneLoading:;

	printf("[song-load] done - reasons still outstanding across the run (leaks):\n");
	std::fflush(stdout);
	reason_check::reportOutstanding(snap);

#if defined(__SANITIZE_ADDRESS__)
	printf("[song-load] LeakSanitizer check:\n");
	std::fflush(stdout);
	__lsan_do_recoverable_leak_check();
#endif

	// Loading many songs generates more than the default audio-capture window, so don't depend on the host's natural
	// exit (which could fire mid-run). We've done the leak checks above; leave now.
	printf("[song-load] complete, exiting\n");
	std::fflush(stdout);
	std::quick_exit(0);
}
// ---- Preset-switch leak harness (issue #4515) ------------------------------
// Reproduces the E078 crash from #4515: load a SYNTH preset whose oscillator is a time-stretched, looping sample
// (e.g. "Busted Mf"/"Kirk" from the 1.2 community B-Sides), play a note so voices spin up the sample/time-stretcher
// (which take Cluster reasons), then switch to another preset. Switching away deletes/hibernates the old sound, which
// removes the Sample's holder reason; if any voice-held Cluster reason leaked, Sample::numReasonsDecreasedToZero()
// FREEZE_WITH_ERROR("E078"). reason_check is snapshotted around the run so reportOutstanding() names the leaking
// addReason call-site.
//
//   DELUGE_HOST_PRESET_SWITCH=1                       enable
//   DELUGE_HOST_PRESET_A="SYNTHS/Busted Mf.XML"       preset to load + play (the suspect)
//   DELUGE_HOST_PRESET_B="SYNTHS/000 Rich Saw Bass.XML" preset to switch to (the trigger)
//   DELUGE_HOST_PRESET_NOTE=60                        MIDI note to audition
//   DELUGE_HOST_PRESET_RENDER=400                     audio blocks (poll ticks) to hold the note before switching
int g_psEnabled = -1; // -1 = not read, 0 = disabled
uint32_t g_psPoll = 0;
const char* g_psA = nullptr;
const char* g_psB = nullptr;
int g_psNote = 60;
int g_psRender = 400;
reason_check::Snapshot g_psSnap = 0;

MelodicInstrument* currentSynth() {
	Instrument* inst = getCurrentInstrument();
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (!inst || !clip || inst->type != OutputType::SYNTH) {
		return nullptr;
	}
	return static_cast<MelodicInstrument*>(inst);
}

Error loadPreset(const char* fullPath) {
	Instrument* inst = getCurrentInstrument();
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (!inst || !clip) {
		printf("[preset] no current instrument/clip to load onto\n");
		return Error::UNSPECIFIED;
	}
	loadInstrumentPresetUI.setupLoadInstrument(OutputType::SYNTH, inst, clip);
	// Open the browser UI only once - the real select-encoder scroll stays inside the open browser and re-loads via
	// currentFileChanged. Re-opening every step would push onto the bounded UI stack until it overflows.
	if (getCurrentUI() != &loadInstrumentPresetUI) {
		openUI(&loadInstrumentPresetUI);
	}
	Error e = loadInstrumentPresetUI.setFileByFullPath(OutputType::SYNTH, fullPath);
	if (e != Error::NONE) {
		printf("[preset] setFileByFullPath(%s) failed (%d)\n", fullPath, (int)e);
		return e;
	}
	e = loadInstrumentPresetUI.performLoad();
	// performLoad alone leaves the oscillator sample detached in this host path (on-device it streams in via the audio
	// engine's cluster loading). Force the actual attach so the auditioned voice really reads the sample and pins
	// Cluster reasons - i.e. so we exercise the sample teardown the bug is about.
	Instrument* nowInst = getCurrentInstrument();
	if (nowInst && nowInst->type == OutputType::SYNTH) {
		static_cast<SoundInstrument*>(nowInst)->loadAllAudioFiles(true);
	}
	printf("[preset] performLoad(%s) -> %d, pinned clusters now %u\n", fullPath, (int)e,
	       (unsigned)reason_check::liveClusters());
	return e;
}

void probeSources() {
	Instrument* inst = getCurrentInstrument();
	if (!inst || inst->type != OutputType::SYNTH) {
		printf("[preset]   probe: current instrument is not a SYNTH\n");
		return;
	}
	Sound* sound = static_cast<SoundInstrument*>(inst);
	Error le = sound->loadAllAudioFiles(true);
	printf("[preset]   probe: loadAllAudioFiles(true) -> Error %d\n", (int)le);
	for (int32_t s = 0; s < kNumSources; s++) {
		Source& src = sound->sources[s];
		int32_t nRanges = src.ranges.getNumElements();
		AudioFile* af = nullptr;
		if (nRanges > 0) {
			MultiRange* r = src.ranges.getElement(0);
			if (r && r->getAudioFileHolder()) {
				af = r->getAudioFileHolder()->audioFile;
			}
		}
		printf("[preset]   probe src%d: oscType=%d ranges=%d sampleAttached=%s\n", s, (int)src.oscType, nRanges,
		       af ? "YES" : "no");
	}
}

void auditionNote(bool on) {
	MelodicInstrument* synth = currentSynth();
	if (!synth) {
		printf("[preset] auditionNote: current instrument is not a SYNTH\n");
		return;
	}
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	if (on) {
		synth->beginAuditioningForNote(modelStack, g_psNote, 127, zeroMPEValues, MIDI_CHANNEL_NONE, 0);
	}
	else {
		synth->endAuditioningForNote(modelStack, g_psNote);
	}
}

void runPresetSwitchHarnessIfDue() {
	if (g_psEnabled < 0) {
		g_psEnabled = std::getenv("DELUGE_HOST_PRESET_SWITCH") ? 1 : 0;
		const char* a = std::getenv("DELUGE_HOST_PRESET_A");
		const char* b = std::getenv("DELUGE_HOST_PRESET_B");
		const char* n = std::getenv("DELUGE_HOST_PRESET_NOTE");
		const char* r = std::getenv("DELUGE_HOST_PRESET_RENDER");
		g_psA = a ? a : "SYNTHS/Busted Mf.XML";
		g_psB = b ? b : "SYNTHS/000 Rich Saw Bass.XML";
		g_psNote = n ? std::atoi(n) : 60;
		g_psRender = r ? std::atoi(r) : 400;
	}
	if (g_psEnabled <= 0) {
		return;
	}
	uint32_t t = g_psPoll++;
	const uint32_t kBoot = 200;
	const uint32_t kLoadA = kBoot + 5;
	const uint32_t kNoteOn = kLoadA + 5;
	const uint32_t kSwitch = kNoteOn + (uint32_t)g_psRender;
	// Optionally note-off `release` blocks before the switch, so the switch lands mid-release (Busted Mf has a
	// near-maximum env1 release — the "rings out for ~10s" from the report).
	static int g_psRelease = -2;
	if (g_psRelease == -2) {
		const char* rr = std::getenv("DELUGE_HOST_PRESET_RELEASE");
		g_psRelease = rr ? std::atoi(rr) : 0;
	}
	const uint32_t kNoteOff = (g_psRelease > 0 && (uint32_t)g_psRelease < g_psRender) ? (kSwitch - g_psRelease) : 0;
	const uint32_t kReport = kSwitch + 20;

	// The offline host render measures "DSP direness" from wall-clock task time, which is inflated in non-realtime
	// rendering, so the engine spuriously force-culls every voice the instant it starts (audio_engine.cpp setDireness
	// -> cullVoices). Suppress that for the whole harness window so the auditioned voice actually sustains and reads
	// the sample. bypassCulling self-resets each render (audio_engine.cpp:625), so re-assert it every poll.
	if (t >= kBoot && t <= kReport) {
		AudioEngine::bypassCulling = true;
	}

	if (t == kBoot) {
		printf("[preset] === #4515 preset-switch harness ===\n");
		printf("[preset] A=%s  B=%s  note=%d  render=%d blocks\n", g_psA, g_psB, g_psNote, g_psRender);
		g_psSnap = reason_check::snapshot();
	}
	else if (t == kLoadA) {
		printf("[preset] loading suspect preset A\n");
		loadPreset(g_psA);
		probeSources();
	}
	else if (t == kNoteOn) {
		printf("[preset] note-on %d (hold through switch)\n", g_psNote);
		auditionNote(true);
	}
	else if (kNoteOff != 0 && t == kNoteOff) {
		printf("[preset] note-off %d (%d blocks before switch -> switch lands mid-release)\n", g_psNote, g_psRelease);
		auditionNote(false);
	}
	else if (t > kNoteOn && t < kSwitch) {
		// Track the pinned-cluster trajectory finely and print only on CHANGE, so a brief loop-crossfade spike (the
		// time-stretcher spawning olderPartReader) is visible instead of averaged away by coarse sampling.
		static unsigned lastPinned = 0;
		unsigned p = (unsigned)reason_check::liveClusters();
		if (p != lastPinned) {
			printf("[preset]   +%u blocks: voices=%d, pinned clusters=%u\n", (unsigned)(t - kNoteOn),
			       AudioEngine::getNumVoices(), p);
			lastPinned = p;
		}
	}
	else if (t == kSwitch) {
		printf("[preset] switching to B (trigger) while note still held...\n");
		std::fflush(stdout);
		loadPreset(g_psB); // <-- deletes/hibernates A; removeReason on A's Sample -> E078 if a cluster reason leaked
		printf("[preset] switch survived (no E078 freeze)\n");
	}
	else if (t == kReport) {
		printf("[preset] reasons still outstanding since snapshot (leaks):\n");
		std::fflush(stdout);
		reason_check::reportOutstanding(g_psSnap);
#if defined(__SANITIZE_ADDRESS__)
		printf("[preset] LeakSanitizer check:\n");
		std::fflush(stdout);
		__lsan_do_recoverable_leak_check();
#endif
		printf("[preset] complete, exiting\n");
		std::fflush(stdout);
		std::quick_exit(0);
	}
	std::fflush(stdout);
}
// ---- Preset-scroll leak harness (issue #4515, browser-scroll mimic) --------
// The report's gesture is scrolling the select encoder through the 1.2 B-Side presets while notes are ringing. Each
// scroll loads the next preset (tearing down the previous, sample and all) with voices still active. This harness
// cycles through a preset list many times, auditioning a (varying) note on each and rendering a short window before
// scrolling on - reproducing the repeated load+play+teardown churn without pad input.
//   DELUGE_HOST_PRESET_SCROLL=N     enable, do N scroll steps
//   DELUGE_HOST_PRESET_STEP=120     audio blocks per preset before scrolling on
int g_scEnabled = -1;
uint32_t g_scPoll = 0;
int g_scSteps = 0;
int g_scStep = 120;
int g_scDone = 0;
reason_check::Snapshot g_scSnap = 0;

void runScrollHarnessIfDue() {
	if (g_scEnabled < 0) {
		const char* n = std::getenv("DELUGE_HOST_PRESET_SCROLL");
		g_scEnabled = n ? 1 : 0;
		g_scSteps = n ? std::atoi(n) : 0;
		const char* st = std::getenv("DELUGE_HOST_PRESET_STEP");
		g_scStep = st ? std::atoi(st) : 120;
	}
	if (g_scEnabled <= 0 || g_scDone) {
		return;
	}
	uint32_t t = g_scPoll++;
	const uint32_t kBoot = 200;
	if (t < kBoot) {
		return;
	}
	AudioEngine::bypassCulling = true; // keep offline voices alive (see preset-switch harness note)

	// Preset rotation and note pattern - both sample-osc B-side presets, plus notes that keep the voice sustaining.
	static const char* kPresets[] = {"SYNTHS/Busted Mf.XML", "SYNTHS/Kirk.XML"};
	static const int kNotes[] = {60, 55, 62, 58, 64};

	uint32_t rel = t - kBoot;
	if (rel == 0) {
		printf("[scroll] === #4515 browser-scroll harness: %d steps, %d blocks each ===\n", g_scSteps, g_scStep);
		g_scSnap = reason_check::snapshot();
	}
	if (rel % (uint32_t)g_scStep == 0) {
		int step = (int)(rel / (uint32_t)g_scStep);
		if (step >= g_scSteps) {
			printf("[scroll] done %d steps. reasons outstanding since snapshot (leaks):\n", g_scSteps);
			std::fflush(stdout);
			reason_check::reportOutstanding(g_scSnap);
#if defined(__SANITIZE_ADDRESS__)
			__lsan_do_recoverable_leak_check();
#endif
			g_scDone = 1;
			printf("[scroll] complete, exiting\n");
			std::fflush(stdout);
			std::quick_exit(0);
		}
		// Note-off the previous audition, scroll to the next preset, and audition a new note - all while the previous
		// preset's voice may still be ringing (the teardown-with-active-voice condition the bug needs).
		const char* preset = kPresets[step % 2];
		int note = kNotes[step % 5];
		printf("[scroll] step %d: load %s, note %d (pinned before=%u, voices=%d)\n", step, preset, note,
		       (unsigned)reason_check::liveClusters(), AudioEngine::getNumVoices());
		std::fflush(stdout);
		loadPreset(preset);
		auditionNote(true);
	}
}
} // namespace

extern "C" void deluge_host_debug_install(void) {
	std::signal(SIGUSR1, onSignal);
	std::signal(SIGUSR2, onSignal);
	printf("[reason-leak] pid %ld: kill -USR1 to snapshot, kill -USR2 to report pinned clusters since\n",
	       (long)getpid());
	std::fflush(stdout);
}

extern "C" void deluge_host_debug_poll(void) {
	runKitCopyHarnessIfDue();
	runSongLoadHarnessIfDue();
	runPresetSwitchHarnessIfDue();
	runScrollHarnessIfDue();

	int req = g_request;
	if (req == 0) {
		return;
	}
	g_request = 0;

	if (req == 1) {
		g_baseline = reason_check::snapshot();
		printf("[reason-leak] snapshot at epoch %u (%u clusters pinned)\n", (unsigned)g_baseline,
		       (unsigned)reason_check::liveClusters());
		std::fflush(stdout);
	}
	else {
		printf("[reason-leak] pinned since snapshot %u:\n", (unsigned)g_baseline);
		std::fflush(stdout);
		reason_check::reportOutstanding(g_baseline);
	}
}

#else // no reason tracking in this build

extern "C" void deluge_host_debug_install(void) {
}
extern "C" void deluge_host_debug_poll(void) {
}

#endif
