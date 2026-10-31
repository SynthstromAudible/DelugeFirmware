/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "deluge.h"
#include "definitions_cxx.hpp"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/reversion.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/screensaver.h"
#include "hid/display/seven_segment_tombstone.h"
#include "hid/encoder_input.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "io/midi/mapping_index_stale.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "io/usb/usb_audio.h"
#include "lib/printf.h" // IWYU pragma: keep this over rides printf with a non allocating version
#include "libdeluge/app.h"
#if DELUGE_STREAM_BENCH
#include "bench/stream_bench.h"
#endif
#include "libdeluge/board.h"
#include "libdeluge/control_surface.h"
#include "libdeluge/display.h"
#include "libdeluge/file_io.h"
#include "libdeluge/signals.h"
#include "memory/general_memory_allocator.h"
#include "memory/memory_report.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/favourite/favourite_manager.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/output.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/render_requests.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/stem_export/stem_export.h"
#include "runtime/app_loop.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
#include "storage/flash_storage.h"
#include "storage/smsysex.h"
#include "storage/startup_song.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include "util/notify.h"
#include <libdeluge/scheduler.h>
#include <stdlib.h>

#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_note_bridge.h"
#include "gui/ui/shell_notification_sink.h"
#include "scenes/registry.h"

namespace encoders = deluge::hid::encoders;

Song* currentSong = nullptr;
Song* preLoadedSong = nullptr;

extern "C" void midiAndGateTimerGoneOff(void) noexcept {
	try {
		// Sends only what's due now, and arms the timer again for anything still to come
		AudioEngine::midiGateOutputTimerFired();
	} catch (...) {
		// An interrupt has no caller to report to. What was not sent stays pending for the next flush.
	}
}

uint32_t timeNextGraphicsTick = 0;

int32_t voltageReadingLastTime = 65535 * 3300;
uint8_t batteryCurrentRegion = 2;
uint16_t batteryMV;
bool batteryLEDState = false;

void batteryLEDBlink() {
	deluge_signal_write(DELUGE_SIGNAL_BATTERY_LED, batteryLEDState);
	int32_t blinkPeriod = ((int32_t)batteryMV - 2630) * 3;
	blinkPeriod = std::min(blinkPeriod, 500_i32);
	blinkPeriod = std::max(blinkPeriod, 60_i32);
	uiTimerManager.setTimer(TimerName::BATT_LED_BLINK, blinkPeriod);
	batteryLEDState = !batteryLEDState;
}

void inputRoutine() {
	// Check if mono output cable plugged in
	bool outputPluggedInL = deluge_signal_read(DELUGE_SIGNAL_LINE_OUT_DETECT_L);
	bool outputPluggedInR = deluge_signal_read(DELUGE_SIGNAL_LINE_OUT_DETECT_R);

	bool headphoneNow = deluge_signal_read(DELUGE_SIGNAL_HEADPHONE_DETECT);
	if (headphoneNow != AudioEngine::headphonesPluggedIn) {
		D_PRINT("headphone %d", headphoneNow);
		AudioEngine::headphonesPluggedIn = headphoneNow;
	}

	// Mic detect is active-low on this board.
	bool micNow = !deluge_signal_read(DELUGE_SIGNAL_MIC_DETECT);
	if (micNow != AudioEngine::micPluggedIn) {
		D_PRINT("mic %d", micNow);
		AudioEngine::micPluggedIn = micNow;
		deluge::notify::raise(deluge::notify::Dirty::Oled);
	}

	bool speakerOn = (!AudioEngine::headphonesPluggedIn && !outputPluggedInR && !outputPluggedInL);
	deluge_signal_write(DELUGE_SIGNAL_SPEAKER_ENABLE, speakerOn);

	AudioEngine::renderInStereo =
	    (AudioEngine::headphonesPluggedIn || outputPluggedInR || AudioEngine::isAnyInternalRecordingHappening());

	bool lineInNow = deluge_signal_read(DELUGE_SIGNAL_LINE_IN_DETECT);
	if (lineInNow != AudioEngine::lineInPluggedIn) {
		D_PRINTLN("line in %d", lineInNow);
		AudioEngine::lineInPluggedIn = lineInNow;
		deluge::notify::raise(deluge::notify::Dirty::Oled);
	}

	// Battery voltage
	// If analog read is ready...

	uint16_t batteryADCReading;
	if (deluge_battery_read_raw(&batteryADCReading)) {
		int32_t numericReading = batteryADCReading;
		// Apply LPF
		int32_t voltageReading = numericReading * 3300;
		int32_t distanceToGo = voltageReading - voltageReadingLastTime;
		voltageReadingLastTime += distanceToGo >> 4;

		// We only >> by 15 so that we intentionally double the value, because the incoming voltage is halved by a
		// resistive divider already
		batteryMV = (voltageReadingLastTime) >> 15;
		// D_PRINT("batt mV: ");
		// D_PRINTLN(batteryMV);

		// See if we've reached threshold to change verdict on battery level

		if (batteryCurrentRegion == 0) {
			if (batteryMV > 2950) {
makeBattLEDSolid:
				batteryCurrentRegion = 1;
				deluge_signal_write(DELUGE_SIGNAL_BATTERY_LED, true); // solid on
				uiTimerManager.unsetTimer(TimerName::BATT_LED_BLINK);
			}
		}
		else if (batteryCurrentRegion == 1) {
			if (batteryMV < 2900) {
				batteryCurrentRegion = 0;
				batteryLEDBlink();
			}

			else if (batteryMV > 3300) {
				batteryCurrentRegion = 2;
				deluge_signal_write(DELUGE_SIGNAL_BATTERY_LED, false); // off (charged)
				uiTimerManager.unsetTimer(TimerName::BATT_LED_BLINK);
			}
		}
		else {
			if (batteryMV < 3200) {
				goto makeBattLEDSolid;
			}
		}
	}

	// Set up for next analog read
	deluge_battery_start_conversion();

	MIDIDeviceManager::slowRoutine();

	uiTimerManager.setTimer(TimerName::READ_INPUTS, 100);
}

bool alreadyDoneScroll = false;

namespace {
/// When the console-text quiet window ends, in µs on the BSP clock: one second after boot, then
/// wherever setTimeUSBInitializationEnds moves it.
deluge::runtime::Micros usbInitializationEndsUs = 1'000'000;
} // namespace

extern "C" {
void setTimeUSBInitializationEnds(int32_t timeFromNow) {
	usbInitializationEndsUs =
	    deluge::runtime::nowUs() + static_cast<deluge::runtime::Micros>(timeFromNow) * 1'000'000 / kSampleRate;
}

bool usbInitializationPeriodComplete() {
	return deluge::runtime::nowUs() >= usbInitializationEndsUs;
}
}

uint32_t picFirmwareVersion = 0;
bool picSaysOLEDPresent = false;

namespace Deluge {
void factoryReset() {
	deluge::notify::notice(deluge::l10n::String::STRING_FOR_FACTORY_RESET);
	FlashStorage::factoryReset(false);
	runtimeFeatureSettings.factoryReset(false);
	midiFollow.factoryReset(false);
	MIDIDeviceManager::factoryReset(false);
}
} // namespace Deluge

bool isShortPress(uint32_t pressTime) {
	return ((int32_t)(AudioEngine::audioSampleTimer - pressTime) < FlashStorage::holdTime);
}

// Dispatch one decoded input event.
static void dispatchInputEvent(const DelugeInputEvent& ev) {
	switch (ev.kind) {
	case DELUGE_EVENT_PAD: {
		deluge::hid::display::Screensaver::noteActivity();
		// value is the velocity; 255 means "use the instrument default", 0 a release.
		matrixDriver.padAction(ev.x, ev.y, ev.value);
		if (ev.value) {
			Buttons::ignoreCurrentShiftForSticky();
		}
		break;
	}
	case DELUGE_EVENT_BUTTON: {
		deluge::hid::display::Screensaver::noteActivity();
		Buttons::buttonAction(deluge::hid::button::fromXY(ev.x, ev.y), ev.value);
		break;
	}
	case DELUGE_EVENT_NO_PRESSES:
		// The safety net that releases any pad the firmware still thinks is held (e.g. because a release
		// event was lost under load); see issue #3168.
		matrixDriver.noPressesHappening();
		Buttons::noPressesHappening();
		break;
	default:
		break; // ENCODER events do not arrive on this channel
	}
}

bool readButtonsAndPads() {
	bool anything = false;

	// Drain the queue: the BSP wakes this job once for a burst.
	DelugeInputEvent ev;
	while (deluge_control_poll_event(&ev)) {
		anything = true;
		dispatchInputEvent(ev);
	}

	if (Buttons::shiftHasChanged()
	    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed) == RuntimeFeatureStateToggle::On) {
		indicator_leds::setLedState(indicator_leds::LED::SHIFT, Buttons::isShiftButtonPressed());
	}

	return anything;
}
void readButtonsAndPadsOnce() {
	// returns a bool: this is just a type wrapper
	readButtonsAndPads();
}

void setUIForLoadedSong(Song* song) {

	UI* newUI;
	Clip* currentClip = song->getCurrentClip();
	// If in a Clip-minder view
	if (currentClip && song->runtimeView.inClipMinderViewOnLoad) {
		if (currentClip->onAutomationClipView) {
			newUI = &automationView;
		}
		else if (currentClip->type == ClipType::INSTRUMENT) {
			if (((InstrumentClip*)currentClip)->onKeyboardScreen) {
				newUI = &keyboardScreen;
			}
			else {
				newUI = &instrumentClipView;
			}
		}
		else {
			newUI = &audioClipView;
		}
	}

	// Otherwise we're in session or arranger view
	else {
		if (song->persistentView.lastClipInstanceEnteredStartPos != -1) {
			newUI = &arrangerView;
		}
		else {
			newUI = &sessionView;
		}
	}

	setRootUILowLevel(newUI);

	getCurrentUI()->opened();
	deluge::notify::raise(deluge::notify::Dirty::Oled);
}

void setupBlankSong() {
	void* songMemory = deluge::memory::alloc_fast(sizeof(Song));
	if (songMemory == nullptr) {
		// No RAM for even a blank Song. This is the last-resort fallback (we get here when we have no song at all), so
		// there's nothing left to do but fail loudly with a code rather than placement-new a Song into a null pointer.
		FREEZE_WITH_ERROR("E454");
		return;
	}
	preLoadedSong = new (songMemory) Song();

	preLoadedSong->paramManager.setupUnpatched(); // TODO: error checking
	GlobalEffectable::initParams(&preLoadedSong->paramManager);
	preLoadedSong->setupDefault();

	setRootUILowLevel(&instrumentClipView); // Prevents crash. (Wait, I'd like to know more about what crash...)
	preLoadedSong->ensureAtLeastOneSessionClip();

	deluge::bridge::discardPendingMappingOps(); // the table writes the render held were for the song going
	currentSong = preLoadedSong;
	preLoadedSong = nullptr;
	deluge::markMappingIndexStale(); // the index is the current song's

	AudioEngine::getReverbParamsFromSong(currentSong);

	setUIForLoadedSong(currentSong);
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	deluge::gui::instrumentLoad.songMadeCurrent();
}

// Which conditional task fires once the SD card is ready. Normally the settings prelude, which
// reads the three settings files through the channel and THEN arms the startup-song loader above;
// headless hosts override this before deluge_app_init() runs (see deluge_startup_skip_settings_prelude()).
// A plain firmware or interactive-host build never touches it, so behaviour is unchanged on-device.
void (*startupConditionalTask)() = deluge::storage::armStartupSettings;

/// @brief Opt a non-Deluge host out of the boot-settings prelude, leaving only the startup song.
///
/// A host that drives its own scenario and never services the byte channel cannot complete work
/// submitted to it: the prelude's reads (and, when MIDI_FOLLOW.XML is absent, the write its
/// `onAbsent` performs) sit in the single-slot channel forever and wedge whatever else needs it.
/// Measured: the offline stem drain hung for 900s against a cold card image, and finished in 37s
/// with nothing submitted.

extern "C" void deluge_startup_skip_settings_prelude() noexcept {
	startupConditionalTask = deluge::storage::armStartupSong;
}

namespace {
using deluge::runtime::appLoop;
using deluge::runtime::kNever;
using deluge::runtime::Micros;

constexpr Micros us(double seconds) {
	return static_cast<Micros>(seconds * 1'000'000.0);
}

deluge::runtime::JobId startupJob = deluge::runtime::kNoJob;

#if ENABLE_TEXT_OUTPUT && !DELUGE_STREAM_BENCH
/// Jobs whose worst run is at least this long are reported: this long is how late a single run
/// can make every other job, the playback and MIDI routines included.
constexpr Micros kReportRunAtLeast = 1000;

/// Prints each job whose worst run since the last report reached kReportRunAtLeast or that threw,
/// then starts a new window. Debug builds only; its output goes wherever D_PRINTLN does (RTT on the
/// device).
void reportJobStats() {
	for (deluge::runtime::JobId id = 0; id < appLoop.size(); ++id) {
		const auto s = appLoop.stats(id);
		if (s.maxRun >= kReportRunAtLeast || s.failures != 0) {
			D_PRINTLN("job %s: worst %llu us over %lu runs, %lu failed (last %llu us)", s.name,
			          static_cast<unsigned long long>(s.maxRun), static_cast<unsigned long>(s.runs),
			          static_cast<unsigned long>(s.failures), static_cast<unsigned long long>(s.lastRun));
		}
	}
	appLoop.resetStats();
}
#endif

/// Polls for the card, then runs the startup task once.
void startupWhenCardReady() {
	if (isCardReady()) {
		startupConditionalTask();
		appLoop.cancel(startupJob);
	}
}
} // namespace

// The app's jobs, which the BSP's app loop runs (runtime/app_loop.h): in registration order, one at a
// time. A job runs when its gap has passed since its last run ended, or when a reason it listens for
// arrives. The render is not a job: the BSP's audio tier drives it (deluge_app_audio_tick).
void registerJobs() {
	appLoop.add("interpret encoders", &encoders::interpretEncodersTask, kNever, DELUGE_WAKE_ENCODERS);
	// Updates MIDI and clock.
	appLoop.add("playback routine", [] { playbackHandler.routine(); }, us(0.0015), 0);
	appLoop.add("midi routine", [] { playbackHandler.midiRoutine(); }, us(0.0015), 0);
	// Named "slow" but isn't: it handles audio recording setup.
	appLoop.add("audio slow", &AudioEngine::slowRoutine, us(0.006), 0);
	// Frees what deleted outputs and drums handed to the render, once it has run two blocks past them. Runs when
	// something is handed over, and again while anything is still waiting -- see AudioEngine::reclaimRoutine.
	AudioEngine::reclaimJob = appLoop.add("reclaim retired", &AudioEngine::reclaimRoutine, kNever, 0);
	// Anything handed over before the job existed asked for nobody's run.
	appLoop.request(AudioEngine::reclaimJob);
	// Lends the delays' arena a region when a tape the render took has used its margin, and returns the regions it no
	// longer needs. Runs when the render takes or gives back a tape.
	deluge::model::registerDelayArenaJob();
	// Runs only when the BSP has queued pad or button events, and drains them all.
	appLoop.add("buttons and pads", &readButtonsAndPadsOnce, kNever, DELUGE_WAKE_INPUT);
	appLoop.add("pending UI", &doAnyPendingUIRendering, us(0.02), 0);
	// Logs heap and stack occupancy on a run whose debugger painted the stack (tools/device_check.py); a plain run
	// pays one compare.
	appLoop.add("memory report", &deluge::memory::reportMemoryIfPainted, us(2.0), 0);

	// Advances a song load past a wait it has finished. Cheap when no load is in flight. It runs one phase of the
	// load and returns; the load's card I/O goes through the channels. It runs when a load begins, when a storage
	// channel answers, and at the time the phase it is parked in asks for -- see LoadSongUI::armNextRun.
	loadSongUI.jobId = appLoop.add("song load", [] { loadSongUI.loadRoutine(); }, kNever, DELUGE_WAKE_STORAGE);
	// The startup song's load can begin before the job exists.
	appLoop.request(loadSongUI.jobId);

	// A staged document save builds itself on this tier and then stores it in one operation. Like the
	// song load above, it parks between steps and needs something to bring it back -- and returning to
	// the run loop is the only reason the storage driver can make progress at all.
	// It runs when a save starts, when the channel answers, and at the stall bound's deadline of the wait it is on --
	// see SaveUI::saveJob.
	SaveUI::saveJob = appLoop.add("document save", [] { SaveUI::saveRoutine(); }, kNever, DELUGE_WAKE_STORAGE);
	// The sample browser's claim is a phase machine on this tier, and like the save above it waits between steps and
	// needs something to bring it back, whichever UI is on top. It runs when a claim is asked for, when a storage
	// channel answers, and at the time its wait asks for -- see SampleClaim::armNextRun.
	deluge::gui::sampleClaim.jobId =
	    appLoop.add("sample claim", [] { deluge::gui::sampleClaim.routine(); }, kNever, DELUGE_WAKE_STORAGE);
	// The browser's folder import is the same kind of machine, and its context menu is on top while it runs, so it has
	// no graphics routine of its own to be ticked from either. It runs when an import is asked for, when a storage
	// channel answers, and at the time its wait asks for -- see FolderImport::armNextRun.
	deluge::gui::folderImport.jobId =
	    appLoop.add("folder import", [] { deluge::gui::folderImport.routine(); }, kNever, DELUGE_WAKE_STORAGE);
	// The preset browser's own loads are the same kind of machine; the clone menu can be on top while one is out.
	// Undo and redo are the same kind of machine: the reversion's samples are loaded here before it runs. It runs when
	// a reversion is asked for, when a storage channel answers, and at its pre-load's next tick -- see
	// Reversion::routine.
	deluge::gui::reversion.jobId =
	    appLoop.add("reversion", [] { deluge::gui::reversion.routine(); }, kNever, DELUGE_WAKE_STORAGE);
	// The pattern browser's reads are the same kind of machine, and a load from Enter lands after the browser has
	// closed. They share this job: one job per UI surface. It runs when either is asked for a load, when a storage
	// channel answers, and at the time their waits ask for -- see InstrumentLoad::armNextRun and
	// LoadPatternUI::loadRoutine.
	deluge::gui::instrumentLoad.jobId = appLoop.add(
	    "instrument load",
	    [] {
		    deluge::gui::instrumentLoad.routine();
		    loadPatternUI.loadRoutine();
	    },
	    kNever, DELUGE_WAKE_STORAGE);
	loadPatternUI.jobId = deluge::gui::instrumentLoad.jobId;
	// The boot song is built before the job exists, and a blank one asks for its first preset from the job's run.
	deluge::gui::instrumentLoad.songMadeCurrent();
	// Drives whatever a settings read or write handed to the byte channel: a request/collect state machine whose
	// caller returned long ago, with no UI tick of its own to resume from. It runs when a submission asks for it, when
	// the channel answers, and at its stall bound's deadline -- see deluge::storage::bytes::serviceJob.
	deluge::storage::bytes::serviceJob = appLoop.add(
	    "settings io",
	    [] {
		    deluge::storage::bytes::serviceRoutine();
		    // The favourites bank rides the same channel and applies what an operation produced on THIS tier, so
		    // it runs straight after the channel has been driven.
		    favouritesManager.serviceRoutine();
		    // A staged document released mid-read is parked until the byte channel lets go of it, which wakes
		    // this job; free it then.
		    StorageManager::reapOrphanedStaging();
	    },
	    kNever, DELUGE_WAKE_STORAGE);
	// A submission made before the job existed asked for nobody's run.
	appLoop.request(deluge::storage::bytes::serviceJob);

	// Advances the startup fail-safe's phases. Beside the two above for the same reason: it is a
	// request/collect state machine whose caller returned long ago, and it has no UI tick of its own
	// to resume from. Cheap and reentrant once the machine reaches Done.
	deluge::storage::startupSongJob =
	    appLoop.add("startup failsafe", [] { deluge::storage::startupSongRoutine(); }, kNever, DELUGE_WAKE_STORAGE);

	// Same shape for stem export: each pass runs one phase of the export and returns, so the
	// UI keeps running for the whole export - which is what makes cancelling it possible at all.
	// It runs when an export starts or is aborted, when a storage channel answers or the streaming loader drains, and
	// at the time the phase it is parked in asks for: at once while an offline render has blocks to go and is not
	// waiting on the card -- see StemExport::armNextRun.
	stemExport.jobId = appLoop.add("stem export", [] { stemExport.exportRoutine(); }, kNever, DELUGE_WAKE_STORAGE);

	// Runs only when a SysEx message is queued, when a storage channel answers (a path operation or directory open
	// it waits on), or when the job itself asks for another run (a back-off, or the deadline of the wait it is on): see
	// smSysex::sysexReceived and smSysex::handleNextSysEx.
	smSysex::jobId =
	    appLoop.add("Handle pending SysEx traffic.", [] { smSysex::handleNextSysEx(); }, kNever, DELUGE_WAKE_STORAGE);

	// these ones are actually "slow" -> file manager just checks if an sd card has been inserted, audio recorder checks
	// if recordings are finished
	appLoop.add("audio file slow", [] { audioFileManager.slowRoutine(); }, us(0.2), 0);
	// It can call finishRecording(), which frees the SampleRecorder. Jobs run one at a time on this tier, so no other
	// job is part-way through using that recorder when it does.
	appLoop.add("audio recorder slow", [] { audioRecorder.slowRoutine(); }, us(0.1), 0);
	// An audio-input take: ends it on a card error, attaches its sample once it settles, and keeps the recorder's
	// status line current. Requested when a take starts, and asks for its own next run while the take is in progress
	// -- see AudioRecorder::tick. It and the job above never finish the same take: each owns one kind of source.
	audioRecorder.jobId = appLoop.add("audio recorder", [] { audioRecorder.tick(); }, kNever, 0);
	// Actions undo/redo MIDI commands that had to wait for the card. Requested where a command is made to wait, and
	// again while the card is busy -- see PlaybackHandler::slowRoutine.
	playbackHandler.slowJob = appLoop.add("playback slow routine", [] { playbackHandler.slowRoutine(); }, kNever, 0);
	// Runs the UI timers (animations, blinks, the graphics routine) at the next one's time: routine() and
	// setTimer() ask for the job's next run themselves.
	uiTimerManager.jobId = appLoop.add("ui routine", [] { uiTimerManager.routine(); }, kNever, 0);
	// Boot armed its timers before the job existed, so nothing has asked for its first run yet.
	appLoop.request(uiTimerManager.jobId);
	startupJob = appLoop.add("startup/render", &startupWhenCardReady, us(0.004), 0);
#if DELUGE_STREAM_BENCH
	// The benchmark zeroes and reads the job stats itself, per phase.
	deluge::bench::registerStreamBench();
#elif ENABLE_TEXT_OUTPUT
	appLoop.add("job stats", &reportJobStats, us(10.0), 0);
#endif
}
// One-time bring-up: everything up to (but not including) registering the tasks.
static void deluge_boot(const DelugeBoard* board) {
	(void)board;

	if (!deluge_board_probe_oled()) {
		// This firmware does not support 7-segment hardware. Tell the user and stop,
		// before we bring up audio, SD, or the task manager.
		deluge_control_init();
		deluge::hid::display::tombstoneAndHalt();
	}

	// Give the control surface its startup configuration.
	deluge_control_enable_oled();

	deluge_control_init();

	PadLEDs::setRefreshTime(23);

	deluge_control_flush();

	functionsInit();

	currentPlaybackMode = &session;

	// One-time board bring-up: GPIO directions + initial state, and (on the
	// 7-segment variant) the SPI chip-select. The CV DAC and its shared SPI bus are
	// already up: the Rust runtime starts them before the executor runs.
	deluge_board_init_early(true);

	deluge_display_init(); // Set up OLED now
	display = new deluge::hid::display::OLED;

	// Setup audio output on SSI0
	deluge_board_init_audio();

#if RECORD_TEST_MODE == 1
	makeTestRecording();
#endif

#if TEST_GENERAL_MEMORY_ALLOCATION
	GeneralMemoryAllocator::get().test();
#endif

	// Setup for gate output
	cvEngine.init();

	// Wait for PIC Uart to flush out. Could this help Ron R with his Deluge sometimes not booting? (No probably wasn't
	// that.) Otherwise didn't seem necessary.
	deluge_control_wait_for_flush();

	deluge_control_setup_for_pads();
	deluge_signal_write(DELUGE_SIGNAL_CODEC_ENABLE, true); // Enable codec

	AudioEngine::init();

	audioFileManager.init();

	// Storage-phase board bring-up (SPIBSC). Must run only now that everything
	// else is up, because the graphics and audio routines are injected into the
	// SPIBSC wait routines, so those have to be running.
	deluge_board_init_storage();

	// Read the PIC firmware version + OLED-present bit, and check whether the user
	// is holding the select knob for a factory reset. The BSP drains the boot
	// response burst; the app reacts.
	DelugeBootInfo bootInfo;
	deluge_control_read_boot_info(&bootInfo);
	picFirmwareVersion = bootInfo.pic_firmware_version;
	picSaysOLEDPresent = bootInfo.oled_present;
	D_PRINTLN("PIC firmware version reported: %d", picFirmwareVersion);

	if (bootInfo.factory_reset_requested) {
		Deluge::factoryReset();
	}

	FlashStorage::readSettings();

	runtimeFeatureSettings.init();

	// Assign each ConnectedUSBMIDIDevice its boundary MIDI port and initialise the
	// MIDI transport before the USB stack can drive any MIDI transfers.
	MIDIDeviceManager::init();

	// Only now does the device go on the bus: whether it offers USB audio is a setting, and the host reads the
	// configuration once, when it enumerates us.
	deluge::usb_audio::startDevice();

	// The three settings files are NOT read here. They are read by the settings prelude
	// (storage/startup_song.h) behind the same card-ready trigger as the startup song, through the
	// byte channel. Boot therefore touches the card not at all, and the UI starts without waiting
	// for it -- which is what lets the reads' blocking arms be deleted, and with them the boot
	// window that `deluge::boot::finished()` names.
	PadLEDs::setBrightnessLevel(FlashStorage::defaultPadBrightness);
	setupBlankSong(); // we always need to do this

#ifdef TEST_VECTOR
	NoteVector noteVector;
	noteVector.test();
#endif

#ifdef TEST_VECTOR_SEARCH_MULTIPLE
	NoteVector noteVector;
	noteVector.testSearchMultiple();
#endif

#ifdef TEST_VECTOR_DUPLICATES
	NoteVector noteVector;
	noteVector.testDuplicates();
#endif

	inputRoutine();

	uiTimerManager.setTimer(TimerName::GRAPHICS_ROUTINE, 50);

	// ---- Boot wiring for the ported UI (`src/scenes/registry.h`'s boot-wiring note) -------------
	//
	// Settings the ported tree and the guest's `Shell` cannot read for themselves. Each is pushed beside legacy's own
	// writer, since a boot call alone goes stale the moment the user changes the setting; where the writer is the flash
	// read itself, the push is also below.
	//
	//   * **The screensaver timeout** is pushed from `Screensaver::settingsChanged()`
	//     (`hid/display/screensaver.cpp`), where all its writers funnel, including the call below.
	//   * **The shortcut layout** (flash byte 60) is pushed from inside
	//     `SoundEditor::setShortcutsVersion`, the sole writer of `shortcutsVersion`, which every
	//     change path goes through: boot, the legacy settings item, and both factory-reset menus via
	//     `FlashStorage::resetSettings`.
	//   * **The menu highlighting** (flash byte 175) is pushed by `gui::ui::pushShellHighlighting`, from the
	//     settings item, a factory reset, the ported menu's `MenuAction::HighlightingChanged` and the call below.
	//   * **The play-cursor flash** (flash byte 36, `AudioClipScene::set_flash_cursor`) is unwired in
	//     either direction: `FlashStorage::readSettings` writes `PadLEDs::flashCursor` directly and
	//     the scene keeps its own copy defaulting to the factory value, so a user who turns the
	//     cursor off still sees it in the ported audio clip view. See `src/scenes/README.md`'s
	//     boot-wiring section; the shortcut-layout wiring above is the shape to copy.

	// Number the ported menu tree. Idempotent (a function-local static), and normally already done by
	// `shellHostUI`'s construction through `scenes::scene_registry()`; called anyway because that is
	// static-initialisation order rather than a guarantee, and `MenuScene::save_state`,
	// `restore_state` and the shortcut-pad jump silently do nothing on an unnumbered tree.
	(void)deluge::scenes::assigned_node_ids();

	// Install the note bridge: function pointers onto `instrumentClipView`'s note and note-row
	// state, so `src/scenes` reaches it without an include. It holds no `Song`, so it has no
	// ordering constraint of its own (see `shell_note_bridge.h`).
	deluge::gui::ui::wireShellNoteBridge();

	// Install the notification sink: the shell's `Notice::Kind::Notification` consumer, pointed at
	// the same `display->displayNotification` call legacy's `drain_notifications` makes for that
	// kind, so whichever of the two pops a horizontal-menu notification, legacy's popup slot holds it.
	deluge::gui::ui::wireShellNotificationSink();

	// Build the three real services over the guest's `Shell` and install them. After
	// `setupBlankSong()` above: all three take `currentSong`, and `ModKnobs` also mirrors `View`'s
	// active mod controllable, neither of which exists earlier. `ShellHostUI::opened()` calls it
	// again on every open, to re-read both.
	deluge::gui::ui::wireShellGuestServices();

	// The UI is up: start the idle countdown. This also supplies the ported screensaver service's
	// timeout for the first time (see `settingsChanged` itself).
	deluge::hid::display::Screensaver::settingsChanged();

	// The guest's panels take the card's menu highlighting; its writers push it again on every change.
	deluge::gui::ui::pushShellHighlighting();

	D_PRINTLN("going into main loop");
	deluge_board_unlock_data_cache();
	// (The SD-routine reentrancy flag is scheduler-owned and starts clear; no reset needed here.)
}

// libdeluge entry (app.h). The platform calls deluge_app_init() once at startup. When it returns, the
// BSP's audio tier starts calling deluge_app_audio_tick() and its app loop deluge_app_service(),
// which runs the jobs registerJobs() added.
extern "C" void deluge_app_init(const DelugeBoard* board) noexcept {
	try {
		deluge_boot(board);
		registerJobs();
		// The BSP's audio tier starts rendering once this returns.
		AudioEngine::renderDrivenByTier.store(true, std::memory_order_release);
	} catch (...) {
		// A boot that ran out of memory cannot go on; freeze with a code rather than terminate silently. The throw may
		// come before the display exists, which freezeWithError() handles.
		FREEZE_WITH_ERROR("BOOT");
	}
}

extern "C" uint64_t deluge_app_service(uint64_t now_us, DelugeWake reasons) noexcept {
	try {
		// Before any job reads interaction-owned state the render's work changes: do what it asked for since the last
		// job.
		deluge::playback::applyRenderRequests();
		return appLoop.service(now_us, reasons);
	} catch (...) {
		// appLoop.service() already catches a throwing job's own body, so this is something else --
		// e.g. a bad reasons value hitting undefined behaviour above the job loop. There is no job to
		// charge the failure to, so just keep the clock moving: ask for another pass right away rather
		// than leave the app loop stalled.
		return now_us;
	}
}

// Card-detect is now pull-based: AudioFileManager::slowRoutine() polls
// deluge_block_poll_card_event() instead of the BSP calling up into the app.

extern "C" void setNumeric(char* text) {
}

extern "C" void setNumericNumber(int32_t number) {
}

void deleteOldSongBeforeLoadingNew() {

	currentSong->stopAllAuditioning();

	// Kept although the render's let-go of the song below ends its Sounds' voices: this also cuts the sound of the
	// song's audio outputs (AudioOutput::cutAllSound()), which that let-go does not touch.
	AudioEngine::killAllVoices(true);

	view.releaseActiveModControllable();

	// The old song's stutter ends and its tape goes back before the song is let go.
	if (currentSong != nullptr) {
		currentSong->live.stutter.end();
		exitUIMode(UI_MODE_STUTTERING);
	}

	// Hand the old song to the render, then wait -- bounded, on the render -- until it has been freed, so the new song
	// is built in the memory it gave back. The wait runs no other interaction task, so none sees currentSong null.
	Song* toDelete = currentSong;
	deluge::bridge::discardPendingMappingOps(); // the table writes the render held were for the song going
	currentSong = nullptr;
	deluge::markMappingIndexStale(); // the index is the current song's
	AudioEngine::retire(toDelete);
	AudioEngine::awaitRetired();
}
