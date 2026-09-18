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
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/reversion.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_song_ui.h"
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
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "lib/printf.h" // IWYU pragma: keep this over rides printf with a non allocating version
#include "libdeluge/app.h"
#include "libdeluge/board.h"
#include "libdeluge/control_surface.h"
#include "libdeluge/display.h"
#include "libdeluge/file_io.h"
#include "libdeluge/signals.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/favourite/favourite_manager.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/stem_export/stem_export.h"
#include "scheduler_api.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
#include "storage/flash_storage.h"
#include "storage/smsysex.h"
#include "storage/startup_song.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include "util/pack.h"
#include <stdlib.h>

namespace encoders = deluge::hid::encoders;

Song* currentSong = nullptr;
Song* preLoadedSong = nullptr;

extern "C" void midiAndGateTimerGoneOff(void) {
	cvEngine.updateGateOutputs();
	midiEngine.flushMIDI();
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
		renderUIsForOled();
	}

	bool speakerOn = (!AudioEngine::headphonesPluggedIn && !outputPluggedInR && !outputPluggedInL);
	deluge_signal_write(DELUGE_SIGNAL_SPEAKER_ENABLE, speakerOn);

	AudioEngine::renderInStereo =
	    (AudioEngine::headphonesPluggedIn || outputPluggedInR || AudioEngine::isAnyInternalRecordingHappening());

	bool lineInNow = deluge_signal_read(DELUGE_SIGNAL_LINE_IN_DETECT);
	if (lineInNow != AudioEngine::lineInPluggedIn) {
		D_PRINTLN("line in %d", lineInNow);
		AudioEngine::lineInPluggedIn = lineInNow;
		renderUIsForOled();
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
bool waitingForSDRoutineToEnd = false;

// An input event whose action returned REMIND_ME_OUTSIDE_CARD_ROUTINE: held here
// and re-dispatched once the SD routine ends (the pull-model replacement for the
// old uartPutCharBack back-pressure).
static DelugeInputEvent heldInputEvent;

extern uint8_t anythingInitiallyAttachedAsUSBHost;

bool closedPeripheral = false;

extern "C" {
/// Wait for one second
uint32_t timeUSBInitializationEnds = kSampleRate;
uint8_t usbInitializationPeriodComplete = 0;

void setTimeUSBInitializationEnds(int32_t timeFromNow) {
	timeUSBInitializationEnds = AudioEngine::audioSampleTimer + timeFromNow;
	usbInitializationPeriodComplete = 0;
}
}

extern "C" void openUSBHost(void);
extern "C" void closeUSBHost();
extern "C" void openUSBPeripheral();
extern "C" void closeUSBPeripheral(void);

uint32_t picFirmwareVersion = 0;
bool picSaysOLEDPresent = false;

namespace Deluge {
void factoryReset() {
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_FACTORY_RESET));
	FlashStorage::factoryReset(false);
	runtimeFeatureSettings.factoryReset(false);
	midiFollow.factoryReset(false);
	MIDIDeviceManager::factoryReset(false);
}
} // namespace Deluge

bool isShortPress(uint32_t pressTime) {
	return ((int32_t)(AudioEngine::audioSampleTimer - pressTime) < FlashStorage::holdTime);
}

// Dispatch one decoded input event. Returns false if it must be retried once the
// SD routine ends (the event is stashed in heldInputEvent and
// waitingForSDRoutineToEnd is set); true once handled.
static bool dispatchInputEvent(const DelugeInputEvent& ev) {
	switch (ev.kind) {
	case DELUGE_EVENT_PAD: {
		deluge::hid::display::Screensaver::noteActivity();
		// value is the velocity; 255 means "use the instrument default", 0 a release.
		ActionResult result = matrixDriver.padAction(ev.x, ev.y, ev.value);
		if (ev.value) {
			Buttons::ignoreCurrentShiftForSticky();
		}
		if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
			heldInputEvent = ev;
			waitingForSDRoutineToEnd = true;
			return false;
		}
		break;
	}
	case DELUGE_EVENT_BUTTON: {
		deluge::hid::display::Screensaver::noteActivity();
		auto b = deluge::hid::button::fromXY(ev.x, ev.y);
		ActionResult result = Buttons::buttonAction(b, ev.value, isSDRoutineActive());
		if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
			heldInputEvent = ev;
			waitingForSDRoutineToEnd = true;
			return false;
		}
		break;
	}
	case DELUGE_EVENT_NO_PRESSES:
		// This is the safety net that releases any pad the firmware still thinks is held (e.g. because a
		// release event was lost under load). It must not be dropped while the SD routine holds the lock -
		// otherwise stuck notes persist until the pad is pressed again (see issue #3168). Defer it, exactly
		// like pad/button events above, so it runs once the routine ends.
		if (isSDRoutineActive()) {
			heldInputEvent = ev;
			waitingForSDRoutineToEnd = true;
			return false;
		}
		matrixDriver.noPressesHappening(isSDRoutineActive());
		Buttons::noPressesHappening(isSDRoutineActive());
		break;
	default:
		break; // ENCODER events do not arrive on this channel
	}
	return true;
}

bool readButtonsAndPads() {

	if (!usbInitializationPeriodComplete && (int32_t)(AudioEngine::audioSampleTimer - timeUSBInitializationEnds) >= 0) {
		usbInitializationPeriodComplete = 1;
	}

	/*
	if (!inSDRoutine && !closedPeripheral && !anythingInitiallyAttachedAsUSBHost && AudioEngine::audioSampleTimer >=
	(44100 << 1)) { D_PRINTLN("closing peripheral"); closeUSBPeripheral(); D_PRINTLN("switching back to host");
	    openUSBHost();
	    closedPeripheral = true;
	}
	*/

	if (waitingForSDRoutineToEnd) {
		if (isSDRoutineActive()) {
			return false;
		}
		D_PRINTLN("got to end of sd routine");
		waitingForSDRoutineToEnd = false;
		// Re-dispatch the event we deferred; it may defer again.
		if (!dispatchInputEvent(heldInputEvent)) {
			return false;
		}
	}

	DelugeInputEvent ev;
	bool anything = deluge_control_poll_event(&ev);
	if (anything) {
		if (!dispatchInputEvent(ev)) {
			return false;
		}
	}

	// OLED transfer-ack: the input decode consumed the PIC ack, so pump the next
	// low-level OLED transfer here rather than off a raw byte.
	if (deluge_display_consume_transfer_ack()) {
		uiTimerManager.setTimer(TimerName::OLED_LOW_LEVEL, 3);
	}

	if (!isSDRoutineActive() && Buttons::shiftHasChanged()
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
	if (currentClip && song->inClipMinderViewOnLoad) {
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
		if (song->lastClipInstanceEnteredStartPos != -1) {
			newUI = &arrangerView;
		}
		else {
			newUI = &sessionView;
		}
	}

	setRootUILowLevel(newUI);

	getCurrentUI()->opened();
	renderUIsForOled();
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

	currentSong = preLoadedSong;
	preLoadedSong = nullptr;

	AudioEngine::getReverbParamsFromSong(currentSong);

	setUIForLoadedSong(currentSong);
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

// Which conditional task fires once the SD card is ready. Normally the startup-song
// loader above; the headless stem-render utility (src/bsp/host/host_render_main.cpp)
// overrides this with its own driver before deluge_main() runs. A plain firmware or
// interactive-host build never touches it, so behaviour is unchanged on-device.
TaskHandle startupConditionalTask = deluge::storage::armStartupSong;

extern "C" void usb_pstd_pcd_task(void);
extern "C" void usb_cstd_usb_task(void);

extern "C" volatile uint32_t usbLock;

extern "C" void usb_main_host(void);

void registerTasks() {
	// addRepeatingTask arguments are:
	//
	// - priority (lower = more important)
	// - min time between calls
	// - target time between calls
	// - max time between calls
	//
	// Scheduling algorithm is in chooseBestTask(), briefly:
	// - keep track of average time a task takes
	// - run the least important task that can finish before
	//   a more important task needs to start
	//
	// Explicit gaps at 10, 20, 30, 40 for dynamic tasks.
	//
	// p++ for priorities, so we can be sure the lexical order
	// here is always priority order.

	// 0-9: High priority (10 for dyn tasks)
	uint8_t p = 0;
	AudioEngine::routine_task_id = addRepeatingTask(&(AudioEngine::routine_task), p++, 8 / 44100., 64 / 44100.,
	                                                128 / 44100., "audio  routine", RESOURCE_NONE);
	addRepeatingTask(MidiEngine::check_incoming_usb, p++, 0.0005, 0.0005, 0.001, "check usb midi", RESOURCE_USB);

	// this will block itself unless an encoder is actually moved so can have a fast rate
	encoders::EncoderTaskID = addRepeatingTask(&(encoders::interpretEncodersTask), p++, 0.001, 0.001, 0.002,
	                                           "interpret encoders fast", RESOURCE_NONE);
	// formerly part of audio routine, updates midi and clock
	addRepeatingTask([]() { playbackHandler.routine(); }, p++, 0.0005, 0.001, 0.002, "playback routine", RESOURCE_NONE);
	midiEngine.routine_task_id = addRepeatingTask([]() { playbackHandler.midiRoutine(); }, p++, 0.0005, 0.001, 0.002,
	                                              "midi routine", RESOURCE_SD | RESOURCE_USB);
	// handles sd card recorders
	// named "slow" but isn't actually, it handles audio recording setup
	addRepeatingTask(&AudioEngine::slowRoutine, p++, 0.001, 0.005, 0.05, "audio slow", RESOURCE_NONE);
	addRepeatingTask(&(readButtonsAndPadsOnce), p++, 0.005, 0.005, 0.01, "buttons and pads", RESOURCE_NONE);

	// 11-19: Medium priority (20 for dyn tasks)
	p = 11;

	// 30 Hz update desired?
	addRepeatingTask(&doAnyPendingUIRendering, p++, 0.01, 0.01, 0.03, "pending UI", RESOURCE_NONE);

	// Advances a song load past a wait it has finished. Cheap when no load is in flight, and it only ever
	// queues the next stretch of the load onto the storage owner, so it holds no SD resource itself.
	addRepeatingTask([]() { loadSongUI.loadRoutine(); }, p++, 0.001, 0.005, 0.02, "song load", RESOURCE_NONE);

	// A staged document save builds itself on this tier and then stores it in one operation. Like the
	// song load above, it parks between steps and needs something to bring it back -- and returning to
	// the run loop is the only reason the storage driver can make progress at all.
	addRepeatingTask([]() { SaveUI::saveRoutine(); }, p++, 0.001, 0.005, 0.02, "document save", RESOURCE_NONE);
	// The sample browser's claim is a phase machine on this tier, and like the save above it waits between steps and
	// needs something to bring it back, whichever UI is on top. At the graphics routine's 15 ms, so the stall bound on
	// its load keeps its length.
	addRepeatingTask([]() { deluge::gui::sampleClaim.routine(); }, p++, 0.015, 0.015, 0.03, "sample claim",
	                 RESOURCE_NONE);
	// The browser's folder import is the same kind of machine, and its context menu is on top while it runs, so it has
	// no graphics routine of its own to be ticked from either.
	addRepeatingTask([]() { deluge::gui::folderImport.routine(); }, p++, 0.015, 0.015, 0.03, "folder import",
	                 RESOURCE_NONE);
	// The preset browser's own loads are the same kind of machine; the clone menu can be on top while one is out.
	// Undo and redo are the same kind of machine: the reversion's samples are loaded here before it runs.
	addRepeatingTask([]() { deluge::gui::reversion.routine(); }, p++, 0.015, 0.015, 0.03, "reversion", RESOURCE_NONE);
	addRepeatingTask([]() { deluge::gui::instrumentLoad.routine(); }, p++, 0.015, 0.015, 0.03, "instrument load",
	                 RESOURCE_NONE);
	// Drives whatever a settings read or write handed to the byte channel. Beside the song load for
	// the same reason: both are request/collect state machines whose caller returned long ago, and
	// neither has a UI tick of its own to resume from.
	addRepeatingTask(
	    []() {
		    deluge::storage::bytes::serviceRoutine();
		    // The favourites bank rides the same channel and applies what an operation produced on THIS tier, so
		    // it runs straight after the channel has been driven.
		    favouritesManager.serviceRoutine();
	    },
	    p++, 0.001, 0.005, 0.02, "settings io", RESOURCE_NONE);

	// Advances the startup fail-safe's phases. Beside the two above for the same reason: it is a
	// request/collect state machine whose caller returned long ago, and it has no UI tick of its own
	// to resume from. Cheap and reentrant once the machine reaches Done.
	addRepeatingTask([]() { deluge::storage::startupSongRoutine(); }, p++, 0.001, 0.005, 0.02, "startup failsafe",
	                 RESOURCE_NONE);

	// Same shape for stem export: each pass queues one step onto the storage owner and returns, so the
	// UI keeps running for the whole export - which is what makes cancelling it possible at all.
	addRepeatingTask([]() { stemExport.exportRoutine(); }, p++, 0.001, 0.005, 0.02, "stem export", RESOURCE_NONE);

	// Check for and handle queued SysEx traffic
	addRepeatingTask([]() { smSysex::handleNextSysEx(); }, p++, 0.0002, 0.0002, 0.01, "Handle pending SysEx traffic.",
	                 RESOURCE_SD);

	// 21-29: Low priority (30 for dyn tasks)
	p = 21;
	// these ones are actually "slow" -> file manager just checks if an sd card has been inserted, audio recorder checks
	// if recordings are finished
	addRepeatingTask([]() { audioFileManager.slowRoutine(); }, p++, 0.1, 0.1, 0.2, "audio file slow", RESOURCE_SD);
	// Needs the SD resources: it can call finishRecording(), which frees the SampleRecorder, and that must not happen
	// while the card routine is part-way through using it.
	addRepeatingTask([]() { audioRecorder.slowRoutine(); }, p++, 0.01, 0.09, 0.1, "audio recorder slow",
	                 RESOURCE_SD | RESOURCE_SD_ROUTINE);
	// formerly part of cluster loading (why? no idea), actions undo/redo midi commands
	addRepeatingTask([]() { playbackHandler.slowRoutine(); }, p++, 0.01, 0.09, 0.1, "playback slow routine",
	                 RESOURCE_SD);
	// 31-39: Idle priority (40 for dyn tasks)
	p = 31;
	addRepeatingTask(&deluge_control_flush, p++, 0.001, 0.001, 0.02, "PIC flush", RESOURCE_NONE);
	addRepeatingTask(&deluge_display_service, p++, 0.01, 0.01, 0.02, "oled routine", RESOURCE_NONE);
	// needs to be called very frequently,
	// handles animations and checks on the timers for any infrequent actions
	// long term this should probably be made into an idle task
	addRepeatingTask([]() { uiTimerManager.routine(); }, p++, 0.0001, 0.0007, 0.01, "ui routine", RESOURCE_NONE);

	// addRepeatingTask([]() { AudioEngine::routineWithClusterLoading(); }, 0, 1 / 44100., 16 / 44100., 32 / 44100.,
	// true); addRepeatingTask(&(AudioEngine::routine), 0, 16 / 44100., 64 / 44100., true);
}
// One cooperative slice of the run loop. The libdeluge platform calls this
// repeatedly (app.h: deluge_app_tick) — under Embassy it runs inside a task that
// yields between ticks, so the BSP's async tasks can run. mainLoop() below is the
// equivalent bare-metal superloop (host / task-manager-free builds).
extern "C" void deluge_app_tick(void) {
	uiTimerManager.routine();

	// Flush stuff - we just have to do this, regularly
	deluge_display_service();
	deluge_control_flush();

	AudioEngine::routineWithClusterLoading();

	int32_t count = 0;
	while (readButtonsAndPads() && count < 16) {
		if (!(count & 3)) {
			AudioEngine::routineWithClusterLoading();
		}
		count++;
	}

	bool anything = encoders::interpretEncoders();
	if (anything) {
		AudioEngine::routineWithClusterLoading();
	}

	doAnyPendingUIRendering();

	AudioEngine::routineWithClusterLoading();

	// Only actually needs calling a couple of times per second, but we can't put it in uiTimerManager cos that gets
	// called in card routine
	audioFileManager.slowRoutine();
	AudioEngine::slowRoutine();

	audioRecorder.slowRoutine();
}

void mainLoop() {
	while (1) {
		deluge_app_tick();
	}
}
// Shared one-time bring-up: everything up to (but not including) starting the
// run loop / task manager. Factored out so both the bare-metal entry
// (deluge_main, below) and the libdeluge cooperative entry (deluge_app_init)
// reuse it without duplicating boot or reordering encoder init.
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

	// One-time board bring-up: GPIO directions + initial state, the CV DAC SPI,
	// and (when an OLED is fitted) its shared-SPI plumbing.
	deluge_board_init_early(true);

	deluge_display_init(); // Set up OLED now
	display = new deluge::hid::display::OLED;

	// Setup audio output on SSI0
	deluge_board_init_audio();

#if RECORD_TEST_MODE == 1
	makeTestRecording();
#endif

	// encoders::init() is deferred until after registerTasks() below: it hands the encoder
	// edge source the scheduler id of the encoder task to wake on movement (the task blocks
	// itself between rotations), and that id isn't assigned until the task is registered.

#if TEST_GENERAL_MEMORY_ALLOCATION
	GeneralMemoryAllocator::get().test();
#endif

	init_crc_table();

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

	usbLock = 1;
	openUSBHost();

	// If nothing was plugged in to us as host, we'll go peripheral
	// Ideally I'd like to repeatedly switch between host and peripheral mode anytime there's no USB connection.
	// To do that, I'd really need to know at any point in time whether the user had just made a connection, just then,
	// that hadn't fully initialized yet. I think I sorta have that for host, but not for peripheral yet.
	if (!anythingInitiallyAttachedAsUSBHost) {
		D_PRINTLN("switching from host to peripheral");
		closeUSBHost();
		openUSBPeripheral();
	}

	usbLock = 0;

	// Hopefully we can read these files now
	runtimeFeatureSettings.readSettingsFromFile();
	deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled =
	    runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners);
	MIDIDeviceManager::readDevicesFromFile();
	midiFollow.readDefaultsFromFile();
	PadLEDs::setBrightnessLevel(FlashStorage::defaultPadBrightness);
	setupBlankSong(); // we always need to do this
	addConditionalTask(startupConditionalTask, 100, isCardReady, "startup/render", RESOURCE_SD | RESOURCE_SD_ROUTINE);

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

	// Settings have been read and the UI is up: start the idle countdown.
	deluge::hid::display::Screensaver::settingsChanged();

	D_PRINTLN("going into main loop");
	deluge_board_unlock_data_cache();
	// (The SD-routine reentrancy flag is scheduler-owned and starts clear; no reset needed here.)
}

// libdeluge cooperative entry (app.h), Embassy BSP only (host / bare-metal use
// deluge_main below). The platform calls deluge_app_init() once at startup; on
// the Embassy BSP the scheduler_api.h C ABI is backed by the Embassy executor
// (one task per registered task — see src/bsp/rust/src/scheduler.rs), so we
// registerTasks() here and the runners drive the app. There is no inline
// deluge_app_tick() loop: addRepeatingTask spawns an Embassy task immediately.
// registerTasks() must precede encoders::init() — the encoder edge source needs
// the (now-assigned) encoder task id to wake the self-blocking encoder task.
extern "C" void deluge_app_init(const DelugeBoard* board) {
	deluge_boot(board);
	registerTasks();
	encoders::init();
}

// Bare-metal / host entry: boot once, then run forever — either under the
// OSLikeStuff task manager or the inline superloop. (The Embassy BSP uses
// deluge_app_init()/deluge_app_tick() above instead of this.)
extern "C" int32_t deluge_main(void) {
	deluge_boot(deluge_board());
#ifdef USE_TASK_MANAGER
	registerTasks();
	encoders::init(); // after registerTasks(): the encoder edge source needs the (now-assigned) encoder task id to wake
	startTaskManager();
#else
	encoders::init(); // mainLoop() reads encoders inline, so the wake id is unused, but the IO still needs init
	mainLoop();
#endif
	return 0;
}

// The storage-wait hooks (yieldingRoutineForSD / yieldingRoutineWithTimeoutForSD) and the
// old conditionless routineForSD() pump have all moved down into the scheduler foundation
// (OSLikeStuff/task_scheduler/task_scheduler_c_api.cpp) or been retired: they are pure
// concurrency logic and named no app code beyond the manual audio/UI pump, which the task
// manager makes redundant (audio/UI run as registered tasks). The HAL now yields *down* to
// the scheduler during storage busy-waits rather than calling *up* into the application.

// Card-detect is now pull-based: AudioFileManager::slowRoutine() polls
// deluge_block_poll_card_event() instead of the BSP calling up into the app.

extern "C" void setNumeric(char* text) {
}

extern "C" void setNumericNumber(int32_t number) {
}

extern "C" void routineWithClusterLoading() {
	// Sean: don't use YieldToAudio here to be safe
	AudioEngine::routineWithClusterLoading();
}

void deleteOldSongBeforeLoadingNew() {

	currentSong->stopAllAuditioning();

	AudioEngine::killAllVoices(true); // Need to do this now that we're not bothering getting the old Song's
	                                  // Instruments detached and everything on delete

	view.activeModControllableModelStack.modControllable = nullptr;
	view.activeModControllableModelStack.setTimelineCounter(nullptr);
	view.activeModControllableModelStack.paramManager = nullptr;

	Song* toDelete = currentSong;
	currentSong = nullptr;
	void* toDealloc = dynamic_cast<void*>(toDelete);
	toDelete->~Song();
	delugeDealloc(toDelete);
}
