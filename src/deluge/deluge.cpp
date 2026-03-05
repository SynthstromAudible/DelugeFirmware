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

#include "RZA1/sdhi/inc/sdif.h"
#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
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
#include "hid/display/seven_segment.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "lib/printf.h" // IWYU pragma: keep this over rides printf with a non allocating version
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "scheduler_api.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/smsysex.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include "util/pack.h"
#include <stdlib.h>

#if AUTOMATED_TESTER_ENABLED
#include "testing/automated_tester.h"
#endif

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/oled/oled_low_level.h"
#include "drivers/oled/oled.h"
#include "drivers/ssi/ssi.h"
#include "drivers/uart/uart.h"
#include "fatfs/ff.h"

#include "RZA1/rspi/rspi.h"
#include "RZA1/spibsc/spibsc_Deluge_setup.h"
}

namespace encoders = deluge::hid::encoders;

extern uint8_t currentlyAccessingCard;

extern "C" void disk_timerproc(UINT msPassed);

Song* currentSong = nullptr;
Song* preLoadedSong = nullptr;

bool sdRoutineLock = false;

bool allowSomeUserActionsEvenWhenInCardRoutine = false;

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
	setOutputState(BATTERY_LED.port, BATTERY_LED.pin, batteryLEDState);
	int32_t blinkPeriod = ((int32_t)batteryMV - 2630) * 3;
	blinkPeriod = std::min(blinkPeriod, 500_i32);
	blinkPeriod = std::max(blinkPeriod, 60_i32);
	uiTimerManager.setTimer(TimerName::BATT_LED_BLINK, blinkPeriod);
	batteryLEDState = !batteryLEDState;
}

void inputRoutine() {
	disk_timerproc(UI_MS_PER_REFRESH);

	// Check if mono output cable plugged in
	bool outputPluggedInL = readInput(LINE_OUT_DETECT_L.port, LINE_OUT_DETECT_L.pin) != 0u;
	bool outputPluggedInR = readInput(LINE_OUT_DETECT_R.port, LINE_OUT_DETECT_R.pin) != 0u;

	bool headphoneNow = readInput(HEADPHONE_DETECT.port, HEADPHONE_DETECT.pin) != 0u;
	if (headphoneNow != AudioEngine::headphonesPluggedIn) {
		D_PRINT("headphone %d", headphoneNow);
		AudioEngine::headphonesPluggedIn = headphoneNow;
	}

	bool micNow = readInput(MIC_DETECT.port, MIC_DETECT.pin) == 0u;
	if (micNow != AudioEngine::micPluggedIn) {
		D_PRINT("mic %d", micNow);
		AudioEngine::micPluggedIn = micNow;
		renderUIsForOled();
	}

	if (!ALLOW_SPAM_MODE) {
		bool speakerOn = (!AudioEngine::headphonesPluggedIn && !outputPluggedInL && !outputPluggedInR);
		setOutputState(SPEAKER_ENABLE.port, SPEAKER_ENABLE.pin, speakerOn);
	}

	AudioEngine::renderInStereo =
	    (AudioEngine::headphonesPluggedIn || outputPluggedInR || AudioEngine::isAnyInternalRecordingHappening());

	bool lineInNow = readInput(LINE_IN_DETECT.port, LINE_IN_DETECT.pin) != 0u;
	if (lineInNow != AudioEngine::lineInPluggedIn) {
		D_PRINTLN("line in %d", lineInNow);
		AudioEngine::lineInPluggedIn = lineInNow;
		renderUIsForOled();
	}

	// Battery voltage
	// If analog read is ready...

	if (ADC.ADCSR & (1 << 15)) {
		int32_t numericReading = ADC.ADDRF;
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
				setOutputState(BATTERY_LED.port, BATTERY_LED.pin, false);
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
				setOutputState(BATTERY_LED.port, BATTERY_LED.pin, true);
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
	ADC.ADCSR = (1 << 13) | (0b011 << 6) | SYS_VOLT_SENSE_PIN;

	MIDIDeviceManager::slowRoutine();

	uiTimerManager.setTimer(TimerName::READ_INPUTS, 100);
}

int32_t nextPadPressIsOn = USE_DEFAULT_VELOCITY; // Not actually used for 40-pad
bool alreadyDoneScroll = false;
bool waitingForSDRoutineToEnd = false;

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

bool isShortPress(uint32_t pressTime) {
	return ((int32_t)(AudioEngine::audioSampleTimer - pressTime) < FlashStorage::holdTime);
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
		if (sdRoutineLock) {
			return false;
		}
		D_PRINTLN("got to end of sd routine");
		waitingForSDRoutineToEnd = false;
	}

#if SD_TEST_MODE_ENABLED_SAVE_SONGS
	if (!inSDRoutine && playbackHandler.playbackState && getCurrentUI() == &instrumentClipView) {
		openUI(&saveSongUI);

		QwertyUI::enteredText.set("T001");

		saveSongUI.performSave(true);
	}
#endif

#if RECORDING_TEST_ENABLED
	if (!inSDRoutine && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {
		if (playbackHandler.playbackState) {

			D_PRINTLN("");
			D_PRINTLN("undoing");
			Buttons::buttonAction(deluge::hid::button::BACK, true, sdRoutineLock);
		}
		else {
			D_PRINTLN("");
			D_PRINTLN("beginning playback");
			Buttons::buttonAction(deluge::hid::button::PLAY, true, sdRoutineLock);
		}

		int32_t random = getRandom255();

		timeNextSDTestAction = AudioEngine::audioSampleTimer + ((random) << 9);
	}
#endif

	PIC::Response value{0};
	bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
	if (anything) {

		if (value < PIC::kPadAndButtonMessagesEnd) {

			int32_t thisPadPressIsOn = nextPadPressIsOn;
			nextPadPressIsOn = USE_DEFAULT_VELOCITY;

			ActionResult result;
			if (Pad::isPad(util::to_underlying(value))) {
				auto p = Pad(util::to_underlying(value));
				/* while this function takes an int32_t for velocity, 255 indicates to the downstream audition pad
				 * function that it should use the default velocity for the instrument
				 */
				result = matrixDriver.padAction(p.x, p.y, thisPadPressIsOn);
				if (thisPadPressIsOn) {
					Buttons::ignoreCurrentShiftForSticky();
				}
			}
			else {
				auto b = deluge::hid::Button(value);
				result = Buttons::buttonAction(b, thisPadPressIsOn, sdRoutineLock);
			}

			if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
				nextPadPressIsOn = thisPadPressIsOn;
				D_PRINTLN("putCharBack ---------");
				uartPutCharBack(UART_ITEM_PIC);
				waitingForSDRoutineToEnd = true;
				return false;
			}
		}
		else if (value == PIC::Response::NEXT_PAD_OFF) {
			nextPadPressIsOn = false;
		}

		// "No presses happening" message
		else if (value == PIC::Response::NO_PRESSES_HAPPENING) {
			if (!sdRoutineLock) {
				matrixDriver.noPressesHappening(sdRoutineLock);
				Buttons::noPressesHappening(sdRoutineLock);
			}
		}
		else if (util::to_underlying(value) == oledWaitingForMessage && deluge::hid::display::have_oled_screen) {
			uiTimerManager.setTimer(TimerName::OLED_LOW_LEVEL, 3);
		}
	}

	if (!sdRoutineLock && Buttons::shiftHasChanged()
	    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed) == RuntimeFeatureStateToggle::On) {
		indicator_leds::setLedState(indicator_leds::LED::SHIFT, Buttons::isShiftButtonPressed());
	}

#if SD_TEST_MODE_ENABLED_LOAD_SONGS

	if (playbackHandler.currentlyPlaying) {
		if (getCurrentUI()->isViewScreen()) {
			Buttons::buttonAction(deluge::hid::button::LOAD, true);
			Buttons::buttonAction(deluge::hid::button::LOAD, false);
			alreadyDoneScroll = false;
		}
		else if (getCurrentUI() == &loadSongUI && currentUIMode == noSubMode) {
			if (!alreadyDoneScroll) {
				getCurrentUI()->selectEncoderAction(1);
				alreadyDoneScroll = true;
			}
			else {
				Buttons::buttonAction(deluge::hid::button::LOAD, true);
				Buttons::buttonAction(deluge::hid::button::LOAD, false);
			}
		}
	}
#endif

#if UNDO_REDO_TEST_ENABLED
	if (playbackHandler.currentlyPlaying && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {

		int32_t random0 = getRandom255();
		preLoadedSong = NULL;

		if (random0 < 64 && getCurrentUI() == &instrumentClipView) {
			Buttons::buttonAction(deluge::hid::button::song, true);
		}

		else if (random0 < 120)
			actionLogger.revert(BEFORE);
		else
			actionLogger.revert(AFTER);

		int32_t random = getRandom255();
		timeNextSDTestAction = AudioEngine::audioSampleTimer + ((random) << 4); // * 44 / 13;
		anything = true;
	}
#endif

#if LAUNCH_CLIP_TEST_ENABLED
	if (playbackHandler.playbackState && (int32_t)(audioDriver.audioSampleTimer - timeNextSDTestAction) >= 0) {
		Buttons::buttonAction(SHIFT, true, false);
		matrixDriver.padAction(kDisplayWidth, getRandom255() & 7, true, inSDRoutine);
		Buttons::buttonAction(SHIFT, false, false);
		int32_t random = getRandom255();
		timeNextSDTestAction = audioDriver.audioSampleTimer + ((random) << 4); // * 44 / 13;
		anything = true;
	}

#endif

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
	if (display->haveOLED()) {
		renderUIsForOled();
	}
}

void setupBlankSong() {
	void* songMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Song)); // TODO: error checking
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

/// Can only happen after settings, which includes default settings, have been read
void setupStartupSong() {

	auto startupSongMode = FlashStorage::defaultStartupSongMode;
	auto defaultSongFullPath = "SONGS/DEFAULT.XML";
	auto filename =
	    startupSongMode == StartupSongMode::TEMPLATE ? defaultSongFullPath : runtimeFeatureSettings.getStartupSong();
	String failSafePath;
	failSafePath.concatenate("SONGS/__STARTUP_OFF_CHECK_");
	auto size = strlen(filename) + 1;
	// The messages we show to the user are mostly plain language, but
	// for non-user facing errors we use codes. All messages are < 20 chars.
	if (size > 2048) {
		display->consoleText("Startup path too long");
		return;
	}

	char replaced[sizeof(char) * strlen(filename) + 1]; // +1 for NULL terminator
	replace_char(replaced, filename, '/', '_');
	failSafePath.concatenate(replaced);

	if (StorageManager::fileExists(failSafePath.get())) {
		// canary exists, previous boot failed?
		display->consoleText("Startup fault F1");
		return; // no cleanup, keep canary!
	}
	switch (startupSongMode) {
	case StartupSongMode::TEMPLATE: {
		if (!StorageManager::fileExists(defaultSongFullPath)) {
			display->consoleText("Creating template");
			currentSong->writeTemplateSong(defaultSongFullPath);
		}
	}
		[[fallthrough]];
	case StartupSongMode::LASTOPENED:
		[[fallthrough]];
	case StartupSongMode::LASTSAVED: {
		// Create canary
		FIL f;
		if (f_open(&f, failSafePath.get(), FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
			f_close(&f);
		}
		else {
			// Could not create canary, not a user-facing error so code is fine.
			// We're going to skip the startup song to avoid any issues.
			display->consoleText("Startup fault F2");
			return; // no canary, no cleanup
		}
		// Handle missing song
		if (!StorageManager::fileExists(filename)) {
			if (startupSongMode == StartupSongMode::TEMPLATE) {
				// we tried to create it earlier, but didn't happen?
				display->consoleText("Startup fault F3");
				// cleanup, this wasn't a crash
				f_unlink(failSafePath.get());
				return;
			}
			display->consoleText("Song missing");
			// user didn't ask for the template, but if it exists let's use it instead
			if (StorageManager::fileExists(defaultSongFullPath)) {
				display->consoleText("Using template");
				filename = defaultSongFullPath;
				startupSongMode = StartupSongMode::TEMPLATE;
			}
			else {
				// cleanup, this wasn't a crash
				f_unlink(failSafePath.get());
				return;
			}
		}
		// Load song, if we got this far!
		void* songMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Song));
		currentSong->setSongFullPath(filename);
		if (openUI(&loadSongUI)) {
			loadSongUI.performLoad();
			if (startupSongMode == StartupSongMode::TEMPLATE) {
				// Wipe the name so the Save action asks you for a new song
				currentSong->name.clear();
			}
		}
		else {
			// what just failed??
			display->consoleText("Startup fault F4");
		}
		// ...but we got this far, cleanup
		f_unlink(failSafePath.get());
	} break;
	case StartupSongMode::BLANK:
		[[fallthrough]];
	default:
		return;
	}
}

void setupOLED() {
	// delayMS(10);

	// Set up 8-bit
	RSPI0.SPDCR = 0x20u;               // 8-bit
	RSPI0.SPCMD0 = 0b0000011100000010; // 8-bit
	RSPI0.SPBFCR.BYTE = 0b01100000;    // 0b00100000;

	PIC::setDCLow();
	PIC::enableOLED();
	PIC::selectOLED();
	PIC::flush();

	delayMS(5);

	oledMainInit();

	// delayMS(5);

	PIC::deselectOLED();
	PIC::flush();
}

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
	// this one runs quickly and frequently to check for encoder changes
	addRepeatingTask([]() { encoders::readEncoders(); }, p++, 0.0002, 0.0004, 0.0005, "read encoders", RESOURCE_NONE);
	// formerly part of audio routine, updates midi and clock
	addRepeatingTask([]() { playbackHandler.routine(); }, p++, 0.0005, 0.001, 0.002, "playback routine", RESOURCE_NONE);
	midiEngine.routine_task_id = addRepeatingTask([]() { playbackHandler.midiRoutine(); }, p++, 0.0005, 0.001, 0.002,
	                                              "midi routine", RESOURCE_SD | RESOURCE_USB);
	addRepeatingTask([]() { audioFileManager.loadAnyEnqueuedClusters(128, false); }, p++, 0.0001, 0.0001, 0.0002,
	                 "load clusters", RESOURCE_NONE);
	// handles sd card recorders
	// named "slow" but isn't actually, it handles audio recording setup
	addRepeatingTask(&AudioEngine::slowRoutine, p++, 0.001, 0.005, 0.05, "audio slow", RESOURCE_NONE);
	addRepeatingTask(&(readButtonsAndPadsOnce), p++, 0.005, 0.005, 0.01, "buttons and pads", RESOURCE_NONE);

	// 11-19: Medium priority (20 for dyn tasks)
	p = 11;
	addRepeatingTask([]() { encoders::interpretEncoders(true); }, p++, 0.002, 0.003, 0.006, "interpret encoders fast",
	                 RESOURCE_NONE);
	// 30 Hz update desired?
	addRepeatingTask(&doAnyPendingUIRendering, p++, 0.01, 0.01, 0.03, "pending UI", RESOURCE_NONE);
	// this one actually actions them
	addRepeatingTask([]() { encoders::interpretEncoders(false); }, p++, 0.005, 0.005, 0.01, "interpret encoders slow",
	                 RESOURCE_SD_ROUTINE);

	// Check for and handle queued SysEx traffic
	addRepeatingTask([]() { smSysex::handleNextSysEx(); }, p++, 0.0002, 0.0002, 0.01, "Handle pending SysEx traffic.",
	                 RESOURCE_SD);

	// 21-29: Low priority (30 for dyn tasks)
	p = 21;
	// these ones are actually "slow" -> file manager just checks if an sd card has been inserted, audio recorder checks
	// if recordings are finished
	addRepeatingTask([]() { audioFileManager.slowRoutine(); }, p++, 0.1, 0.1, 0.2, "audio file slow", RESOURCE_SD);
	addRepeatingTask([]() { audioRecorder.slowRoutine(); }, p++, 0.01, 0.09, 0.1, "audio recorder slow", RESOURCE_NONE);
	// formerly part of cluster loading (why? no idea), actions undo/redo midi commands
	addRepeatingTask([]() { playbackHandler.slowRoutine(); }, p++, 0.01, 0.09, 0.1, "playback slow routine",
	                 RESOURCE_SD);
	// 31-39: Idle priority (40 for dyn tasks)
	p = 31;
	addRepeatingTask(&(PIC::flush), p++, 0.001, 0.001, 0.02, "PIC flush", RESOURCE_NONE);
	if (hid::display::have_oled_screen) {
		addRepeatingTask(&(oledRoutine), p++, 0.01, 0.01, 0.02, "oled routine", RESOURCE_NONE);
	}
	// needs to be called very frequently,
	// handles animations and checks on the timers for any infrequent actions
	// long term this should probably be made into an idle task
	addRepeatingTask([]() { uiTimerManager.routine(); }, p++, 0.0001, 0.0007, 0.01, "ui routine", RESOURCE_NONE);

	// addRepeatingTask([]() { AudioEngine::routineWithClusterLoading(true); }, 0, 1 / 44100., 16 / 44100., 32 / 44100.,
	// true); addRepeatingTask(&(AudioEngine::routine), 0, 16 / 44100., 64 / 44100., true);
}
void mainLoop() {
	while (1) {

		uiTimerManager.routine();

		// Flush stuff - we just have to do this, regularly
		if (hid::display::have_oled_screen) {
			oledRoutine();
		}
		PIC::flush();

		AudioEngine::routineWithClusterLoading(true);

		int32_t count = 0;
		while (readButtonsAndPads() && count < 16) {
			if (!(count & 3)) {
				AudioEngine::routineWithClusterLoading(true);
			}
			count++;
		}

		encoders::readEncoders();
		bool anything = encoders::interpretEncoders();
		if (anything) {
			AudioEngine::routineWithClusterLoading(true);
		}

		doAnyPendingUIRendering();

		AudioEngine::routineWithClusterLoading(true);

		// Only actually needs calling a couple of times per second, but we can't put it in uiTimerManager cos that gets
		// called in card routine
		audioFileManager.slowRoutine();
		AudioEngine::slowRoutine();

		audioRecorder.slowRoutine();

#if AUTOPILOT_TEST_ENABLED
		autoPilotStuff();
#endif
	}
}
extern "C" int32_t deluge_main(void) {
	// Piggyback off of bootloader DMA setup.
	uint32_t oledSPIDMAConfig = (0b1101000 | (OLED_SPI_DMA_CHANNEL & 7));
	bool have_oled = ((DMACn(OLED_SPI_DMA_CHANNEL).CHCFG_n & oledSPIDMAConfig) == oledSPIDMAConfig);

	// Give the PIC some startup instructions
	if (have_oled) {
		PIC::enableOLED();
	}

	PIC::setDebounce(20); // Set debounce time (mS) to...

	PadLEDs::setRefreshTime(23);
	PIC::setMinInterruptInterval(8);
	PIC::setFlashLength(6);

	PIC::setUARTSpeed();
	PIC::flush();

	functionsInit();

#if AUTOMATED_TESTER_ENABLED
	AutomatedTester::init();
#endif

	currentPlaybackMode = &session;

	setOutputState(BATTERY_LED.port, BATTERY_LED.pin, 1); // Switch it off (1 is off for open-drain)
	setPinAsOutput(BATTERY_LED.port, BATTERY_LED.pin);    // Battery LED control

	setOutputState(SYNCED_LED.port, SYNCED_LED.pin, 0); // Switch it off
	setPinAsOutput(SYNCED_LED.port, SYNCED_LED.pin);    // Synced LED

	// Codec control
	setPinAsOutput(CODEC.port, CODEC.pin);
	setOutputState(CODEC.port, CODEC.pin, 0); // Switch it off

	// Speaker / amp control
	setPinAsOutput(SPEAKER_ENABLE.port, SPEAKER_ENABLE.pin);
	setOutputState(SPEAKER_ENABLE.port, SPEAKER_ENABLE.pin, 0); // Switch it off

	setPinAsInput(HEADPHONE_DETECT.port, HEADPHONE_DETECT.pin); // Headphone detect
	setPinAsInput(LINE_IN_DETECT.port, LINE_IN_DETECT.pin);     // Line in detect
	setPinAsInput(MIC_DETECT.port, MIC_DETECT.pin);             // Mic detect

	setPinMux(VOLT_SENSE.port, VOLT_SENSE.pin, 1); // Analog input for voltage sense

	// Trigger clock input
	setPinMux(ANALOG_CLOCK_IN.port, ANALOG_CLOCK_IN.pin, 2);

	// Line out detect pins
	setPinAsInput(LINE_OUT_DETECT_L.port, LINE_OUT_DETECT_L.pin);
	setPinAsInput(LINE_OUT_DETECT_R.port, LINE_OUT_DETECT_R.pin);

	// SPI for CV
	R_RSPI_Create(SPI_CHANNEL_CV,
	              have_oled ? 10000000 // Higher than this would probably work... but let's stick to the OLED
	                                   // datasheet's spec of 100ns (10MHz).
	                        : 30000000,
	              0, 32);
	R_RSPI_Start(SPI_CHANNEL_CV);
	setPinMux(SPI_CLK.port, SPI_CLK.pin, 3);   // CLK
	setPinMux(SPI_MOSI.port, SPI_MOSI.pin, 3); // MOSI

	if (have_oled) {
		// If OLED sharing SPI channel, have to manually control SSL pin.
		setOutputState(SPI_SSL.port, SPI_SSL.pin, true);
		setPinAsOutput(SPI_SSL.port, SPI_SSL.pin);

		setupSPIInterrupts();
		oledDMAInit();
		setupOLED(); // Set up OLED now
		display = new deluge::hid::display::OLED;
	}
	else {
		setPinMux(SPI_SSL.port, SPI_SSL.pin, 3); // SSL
		display = new deluge::hid::display::SevenSegment;
	}
	// remember the physical display type
	deluge::hid::display::have_oled_screen = have_oled;

	// Setup audio output on SSI0
	ssiInit(0, 1);

#if RECORD_TEST_MODE == 1
	makeTestRecording();
#endif

	encoders::init();

#if TEST_GENERAL_MEMORY_ALLOCATION
	GeneralMemoryAllocator::get().test();
#endif

	init_crc_table();

	// Setup for gate output
	cvEngine.init();

	// Wait for PIC Uart to flush out. Could this help Ron R with his Deluge sometimes not booting? (No probably wasn't
	// that.) Otherwise didn't seem necessary.
	PIC::waitForFlush();

	PIC::setupForPads();
	setOutputState(CODEC.port, CODEC.pin, 1); // Enable codec

	AudioEngine::init();

#if HARDWARE_TEST_MODE
	ramTestLED();
#endif

	audioFileManager.init();

	// Setup SPIBSC. Crucial that this only be done now once everything else is running, because I've injected graphics
	// and audio routines into the SPIBSC wait routines, so that has to be running
	setPinMux(4, 2, 2);
	setPinMux(4, 3, 2);
	setPinMux(4, 4, 2);
	setPinMux(4, 5, 2);
	setPinMux(4, 6, 2);
	setPinMux(4, 7, 2);
	initSPIBSC(); // This will run the audio routine! Ideally, have external RAM set up by now.

	PIC::requestFirmwareVersion(); // Request PIC firmware version
	PIC::resendButtonStates();     // Tell PIC to re-send button states
	PIC::flush();

	// Check if the user is holding down the select knob to do a factory reset
	bool readingFirmwareVersion = false;
	bool otherButtonsOrEvents = false;

	PIC::read(0x8000, [&readingFirmwareVersion, &otherButtonsOrEvents](auto response) {
		if (readingFirmwareVersion) {
			readingFirmwareVersion = false;
			uint8_t value = util::to_underlying(response);
			picFirmwareVersion = value & 127;
			picSaysOLEDPresent = value & 128;
			D_PRINTLN("PIC firmware version reported: %s", value);
			return 0;
		}

		using enum PIC::Response;
		switch (response) {
		case FIRMWARE_VERSION_NEXT:
			readingFirmwareVersion = true;
			return 0;

		case RESET_SETTINGS:
			if (!otherButtonsOrEvents) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_FACTORY_RESET));
				FlashStorage::resetSettings();
				FlashStorage::writeSettings();
			}
			return 0;

		case UNKNOWN_BREAK:
			return 1;

		case UNKNOWN_BOOT_RESPONSE: // value 129. Happens every boot. If you know what this is, please rename!
			return 0;

		default:
			if (response >= UNKNOWN_OLED_RELATED_COMMAND && response <= SET_DC_HIGH) {
				// OLED D/C low ack
				return 0;
			}
			// If any hint of another button being held, don't do anything.
			otherButtonsOrEvents = true;
			return 0;
		}
	});

	FlashStorage::readSettings();

	runtimeFeatureSettings.init();

	if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EmulatedDisplay)
	    == RuntimeFeatureStateEmulatedDisplay::OnBoot) {
		deluge::hid::display::swapDisplayType();
	}

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
	MIDIDeviceManager::readDevicesFromFile();
	midiFollow.readDefaultsFromFile();
	PadLEDs::setBrightnessLevel(FlashStorage::defaultPadBrightness);
	setupBlankSong(); // we always need to do this
	addConditionalTask(setupStartupSong, 100, isCardReady, "load startup song", RESOURCE_SD | RESOURCE_SD_ROUTINE);

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

#ifdef TEST_SD_WRITE

	FIL fil; // File object
	FATFS fs;
	DIR dp;
	FRESULT result; //	 FatFs return code
	int32_t sdTotalBytesWritten;

	int32_t count = 0;

	while (true) {

		display->setTextAsNumber(count);

		int32_t fileNumber = (uint32_t)getNoise() % 10000;
		int32_t fileSize = (uint32_t)getNoise() % 1000000;

		char fileName[20];
		strcpy(fineName, "TEST/") intToString(fileNumber, &fileName[5], 4);
		strcat(fileName, ".TXT");

		result = f_open(&fil, fileName, FA_CREATE_ALWAYS | FA_WRITE);
		if (result) {
			display->setText("AAAA");
			while (1) {}
		}

		sdTotalBytesWritten = 0;

		while (sdTotalBytesWritten < fileSize) {
			UINT bytesWritten = 0;
			result = f_write(&fil, &miscStringBuffer, 256, &bytesWritten);

			if (bytesWritten != 256) {
				display->setText("BBBB");
				while (1) {}
			}

			sdTotalBytesWritten += 256;
		}

		f_close(&fil);

		count++;
	}
#endif

	inputRoutine();

	uiTimerManager.setTimer(TimerName::GRAPHICS_ROUTINE, 50);

	D_PRINTLN("going into main loop");
	sdRoutineLock = false; // Allow SD routine to start happening

#ifdef USE_TASK_MANAGER
	registerTasks();
	startTaskManager();
#else
	mainLoop();
#endif
	return 0;
}

bool inSpamMode = false;

extern "C" void logAudioAction(char const* string) {
	AudioEngine::logAction(string);
}

extern "C" bool yieldingRoutineWithTimeoutForSD(RunCondition until, double timeoutSeconds) {
	if (intc_func_active != 0) {
		return false;
	}
	auto timeNow = getSystemTime();
	// We lock this to prevent multiple entry. Otherwise we could get SD -> routineForSD() -> AudioEngine::routine() ->
	// USB -> routineForSD()
	if (sdRoutineLock) {
		// busy wait - matches running sdroutine in a loop while checking for condition
		while (!until()) {
			if (getSystemTime() > timeNow + timeoutSeconds) {
				return false;
			}
		}
		return true;
	}
	sdRoutineLock = true;
	bool ret = yieldWithTimeout(until, timeoutSeconds);
	sdRoutineLock = false;
	return ret;
}

extern "C" void yieldingRoutineForSD(RunCondition until) {
	if (intc_func_active != 0) {
		return;
	}

	// We lock this to prevent multiple entry. Otherwise we could get SD -> routineForSD() -> AudioEngine::routine() ->
	// USB -> routineForSD()
	if (sdRoutineLock) {
		// busy wait - matches running sdroutine in a loop while checking for condition
		while (!until()) {
			asm volatile("nop");
		}
		return;
	}
	sdRoutineLock = true;
	yield(until);
	sdRoutineLock = false;
}
enum class UIStage { oled, readEnc, readButtons };

/// this function is used as a busy wait loop for long SD reads, and while swapping songs
extern "C" void routineForSD(void) {

	if (intc_func_active != 0) {
		return;
	}

	// We lock this to prevent multiple entry. Otherwise we could get SD -> routineForSD() -> AudioEngine::routine()
	// -> USB -> routineForSD()
	if (sdRoutineLock) {
		return;
	}

	sdRoutineLock = true;
	static UIStage step = UIStage::oled;
	AudioEngine::logAction("from routineForSD()");
	AudioEngine::runRoutine();
	switch (step) {
	case UIStage::oled:
		if (display->haveOLED()) {
			oledRoutine();
		}
		PIC::flush();
		step = UIStage::readEnc;
		break;
	case UIStage::readEnc:
		encoders::readEncoders();
		encoders::interpretEncoders(true);
		step = UIStage::readButtons;
		break;
	case UIStage::readButtons:
		readButtonsAndPads();
		step = UIStage::oled;
		break;
	}
	sdRoutineLock = false;
}

extern "C" void sdCardInserted(void) {
}

extern "C" void sdCardEjected(void) {
	audioFileManager.setCardEjected();
}

extern "C" void loadAnyEnqueuedClustersRoutine() {
	audioFileManager.loadAnyEnqueuedClusters();
}

extern "C" void setNumeric(char* text) {
	display->setText(text);
}

extern "C" void setNumericNumber(int32_t number) {
	display->setTextAsNumber(number);
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

#if ALLOW_SPAM_MODE

#define NUM_SPAM_THINGS 9

#define SPAM_RAM 0
#define SPAM_SD 1
#define SPAM_PIC 2
#define SPAM_USB 3
#define SPAM_MIDI 4
#define SPAM_AUDIO 5
#define SPAM_CV 6
#define SPAM_GATE 7
#define SPAM_CLOCK 8

bool spamStates[NUM_SPAM_THINGS];
int32_t currentSpamThing = 0;

void redrawSpamDisplay() {
	char* thingName;
	switch (currentSpamThing) {
	case SPAM_RAM:
		thingName = "RAM";
		break;

	case SPAM_SD:
		thingName = "SD";
		break;

	case SPAM_PIC:
		thingName = "PIC";
		break;

	case SPAM_USB:
		thingName = "USB";
		break;

	case SPAM_MIDI:
		thingName = "MIDI";
		break;

	case SPAM_AUDIO:
		thingName = "AUDIO";
		break;

	case SPAM_CV:
		thingName = "CV";
		break;

	case SPAM_GATE:
		thingName = "GATE";
		break;

	case SPAM_CLOCK:
		thingName = "CLOCK";
		break;
	}

	display->setText(thingName, false, spamStates[currentSpamThing] ? 3 : 255);
}

void spamMode() {
	inSpamMode = true;
	memset(spamStates, 1, sizeof(spamStates));

	uint32_t* ramReadAddress = (uint32_t*)0x0C000000;
	uint32_t* ramWriteAddress = (uint32_t*)0x0E000000;

	int32_t cvChannel = 0;

	bool sdReading = false;
	bool sdFileCurrentlyOpen = false;
	int32_t sdTotalBytesWritten = 0;

	uint16_t timeLastCV = 0;
	uint16_t timeLastPIC = 0;
	uint16_t timeLastMIDI = 0;
	uint16_t timeLastUSB = 0;
	int32_t lastCol = 0;

	FIL fil; // File object
	FATFS fs;
	DIR dp;
	FRESULT result; //	 FatFs return code

	redrawSpamDisplay();

	setOutputState(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2, 1); // Speaker on

	uint32_t thisPicTime = 0;

	while (true) {

		AudioEngine::routine();

		if (spamStates[SPAM_RAM]) {
			*ramWriteAddress = ~(uint32_t)ramWriteAddress - (*ramReadAddress >> 16);
			if (++ramReadAddress == (uint32_t*)0x10000000)
				ramReadAddress = (uint32_t*)0x0C000000;
			if (++ramWriteAddress == (uint32_t*)0x10000000)
				ramWriteAddress = (uint32_t*)0x0C000000;
		}

		if (spamStates[SPAM_USB]) {
			uint16_t timeSince = (uint16_t)MTU2.TCNT_0 - timeLastUSB;
			if (timeSince >= 4010) {
				timeLastUSB = MTU2.TCNT_0;

				midiEngine.sendUsbMidi(0x09, 5, getRandom255(), getRandom255());
				midiEngine.flushMIDI();
			}
		}

		if (spamStates[SPAM_MIDI]) {
			uint16_t timeSince = (uint16_t)MTU2.TCNT_0 - timeLastMIDI;
			if (timeSince >= 2994) {
				timeLastMIDI = MTU2.TCNT_0;

				midiEngine.sendSerialMidi(0x09, 5, getRandom255(), getRandom255());
				midiEngine.flushMIDI();
			}
		}

		if (spamStates[SPAM_CV]) {

			uint16_t timeSince = (uint16_t)MTU2.TCNT_0 - timeLastCV;
			if (timeSince >= 9001) {
				timeLastCV = MTU2.TCNT_0;

				uint16_t voltage = getRandom255();
				voltage <<= 8;
				voltage |= getRandom255();

				cvEngine.sendVoltageOut(cvChannel, voltage);
				cvChannel = (cvChannel + 1) % 2;
			}
		}

		if (spamStates[SPAM_SD]) {

			// Writing
			if (!sdReading) {
				// Open file for first time
				if (!sdFileCurrentlyOpen) {
					result = f_open(&fil, "written.txt", FA_CREATE_ALWAYS | FA_WRITE);
					if (result) {
						// D_PRINTLN("couldn't create");
					}
					else {
						if (spamStates[SPAM_MIDI])
							D_PRINTLN("writing");
						sdFileCurrentlyOpen = true;
					}
				}

				// Write to open file
				else {
					UINT bytesWritten = 0;
					char thisByte = getRandom255();
					result = f_write(&fil, &thisByte, 1, &bytesWritten);
					// if (result) D_PRINTLN("couldn't write");

					sdTotalBytesWritten++;

					if (sdTotalBytesWritten > 1000 * 5) {
						f_close(&fil);
						// D_PRINTLN("finished writing");
						sdReading = true;
						sdFileCurrentlyOpen = false;
					}
				}
			}

			// Reading
			else {

				// Open file for first time
				if (!sdFileCurrentlyOpen) {

					result = f_open(&fil, "written.txt", FA_READ);
					if (result) {
						// D_PRINTLN("file not found");
					}

					else {
						if (spamStates[SPAM_MIDI])
							D_PRINTLN("reading");
						sdFileCurrentlyOpen = true;
					}
				}

				// Read open file
				else {
					UINT bytesRead;

					char thisByte;
					result = f_read(&fil, &thisByte, 1, &bytesRead);

					if (bytesRead <= 0) {
						f_close(&fil);
						// D_PRINTLN("finished file");
						sdReading = false;
						sdFileCurrentlyOpen = false;
						sdTotalBytesWritten = 0;
					}
				}
			}
		}

		if (spamStates[SPAM_PIC]) {

			uint16_t timeSince = (uint16_t)(MTU2.TCNT_0 - timeLastPIC);
			if (timeSince >= 5000) {
				timeLastPIC = MTU2.TCNT_0;

				Debug::putChar(UART_CHANNEL_PIC, lastCol + 1);
				for (int32_t i = 0; i < 16; i++) {
					int32_t whichColour = getRandom255() % 3;
					for (int32_t colour = 0; colour < 3; colour++) {
						if (colour == whichColour && (getRandom255() % 3) == 0)
							Debug::putChar(UART_CHANNEL_PIC, getRandom255());
						else
							Debug::putChar(UART_CHANNEL_PIC, 0);
					}
				}

				lastCol = (lastCol + 1) % 9;
			}
		}

		// Selector
		readEncoders();

		// Select encoder
		int32_t limitedDetentPos = encoders[ENCODER_SELECT].detentPos;
		encoders[ENCODER_SELECT].detentPos = 0; // Reset. Crucial that this happens before we call selectEncoderAction()

		if (limitedDetentPos != 0) {
			currentSpamThing += limitedDetentPos;
			if (currentSpamThing == NUM_SPAM_THINGS)
				currentSpamThing = 0;
			else if (currentSpamThing == -1)
				currentSpamThing = NUM_SPAM_THINGS - 1;

			redrawSpamDisplay();
		}

		// Vertical encoder
		limitedDetentPos = encoders[ENCODER_SCROLL_Y].detentPos;
		encoders[ENCODER_SCROLL_Y].detentPos =
		    0; // Reset. Crucial that this happens before we call selectEncoderAction()
		if (limitedDetentPos != 0) {
			spamStates[currentSpamThing] = !spamStates[currentSpamThing];
			redrawSpamDisplay();

			// Audio
			if (currentSpamThing == SPAM_AUDIO) {

				// Disable
				if (!spamStates[currentSpamThing]) {
					setPinAsInput(7, 11);
					setPinAsInput(6, 9);
					setPinAsInput(6, 10);
					setPinAsInput(6, 8);
					setPinAsInput(6, 11);

					setOutputState(6, 12, 0); // Switch codec off

					setOutputState(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2, 0); // Speaker off
				}

				// Enable
				else {
					setPinMux(7, 11, 6);      // AUDIO_XOUT
					setPinMux(6, 9, 3);       // SSI0 word select
					setPinMux(6, 10, 3);      // SSI0 tx
					setPinMux(6, 8, 3);       // SSI0 serial clock
					setPinMux(6, 11, 3);      // SSI0 rx
					setOutputState(6, 12, 1); // Switch codec on

					setOutputState(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2, 1); // Speaker on
				}
			}

			// PIC
			else if (currentSpamThing == SPAM_PIC) {

				// Disable
				if (!spamStates[currentSpamThing]) {
					Debug::putChar(UART_CHANNEL_PIC, 227);
				}

				// Enable
				else {}
			}

			// Clock
			else if (currentSpamThing == SPAM_CLOCK) {

				// Disable
				if (!spamStates[currentSpamThing]) {
					CPG.FRQCR |= 0b0011000000000000;
				}

				// Enable
				else {
					CPG.FRQCR &= ~0b0011000000000000;
				}
			}
		}
	}
}

#endif
