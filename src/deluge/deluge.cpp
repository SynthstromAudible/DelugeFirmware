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
#include "NE10.h"
#include "RZA1/system/iodefine.h"
#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "dsp/stereo_sample.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/clear_song.h"
#include "gui/context_menu/delete_file.h"
#include "gui/context_menu/load_instrument_preset.h"
#include "gui/context_menu/overwrite_bootloader.h"
#include "gui/context_menu/overwrite_file.h"
#include "gui/context_menu/sample_browser/kit.h"
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/context_menu/save_song_or_instrument.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/rename/rename_output_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/slicer.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"
#include "hid/encoder.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/print.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/note/note.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "testing/hardware_testing.h"
#include "util/container/hashtable/open_addressing_hash_table.h"
#include "util/misc.h"
#include "util/pack.h"
#include <new>
#include <stdlib.h>
#include <string.h>

#if AUTOMATED_TESTER_ENABLED
#include "testing/automated_tester.h"
#endif

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/oled/oled_low_level.h"
#include "RZA1/system/rza_io_regrw.h"
#include "RZA1/uart/sio_char.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "drivers/mtu/mtu.h"
#include "drivers/oled/oled.h"
#include "drivers/ssi/ssi.h"
#include "drivers/uart/uart.h"
#include "fatfs/diskio.h"
#include "fatfs/ff.h"

#include "RZA1/bsc/bsc_userdef.h"
#include "RZA1/rspi/rspi.h"
#include "RZA1/spibsc/spibsc_Deluge_setup.h"
#include "RZA1/ssi/ssi.h"
#include "drivers/usb/userdef/r_usb_pmidi_config.h"
}

extern uint8_t currentlyAccessingCard;

extern "C" void disk_timerproc(UINT msPassed);

Song* currentSong = NULL;
Song* preLoadedSong = NULL;

bool sdRoutineLock = true;
bool inInterrupt = false;

bool allowSomeUserActionsEvenWhenInCardRoutine = false;

extern "C" void timerGoneOff(void) {
	inInterrupt = true;
	cvEngine.updateGateOutputs();
	midiEngine.flushMIDI();
	inInterrupt = false;
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
	uiTimerManager.setTimer(TIMER_BATT_LED_BLINK, blinkPeriod);
	batteryLEDState = !batteryLEDState;
}

void inputRoutine() {
	disk_timerproc(UI_MS_PER_REFRESH);

	// Check if mono output cable plugged in
	bool outputPluggedInL = readInput(LINE_OUT_DETECT_L.port, LINE_OUT_DETECT_L.pin) != 0u;
	bool outputPluggedInR = readInput(LINE_OUT_DETECT_R.port, LINE_OUT_DETECT_R.pin) != 0u;

	bool headphoneNow = readInput(HEADPHONE_DETECT.port, HEADPHONE_DETECT.pin) != 0u;
	if (headphoneNow != AudioEngine::headphonesPluggedIn) {
		Debug::print("headphone ");
		Debug::println(headphoneNow);
		AudioEngine::headphonesPluggedIn = headphoneNow;
	}

	bool micNow = readInput(MIC_DETECT.port, MIC_DETECT.pin) == 0u;
	if (micNow != AudioEngine::micPluggedIn) {
		Debug::print("mic ");
		Debug::println(micNow);
		AudioEngine::micPluggedIn = micNow;
	}

	if (!ALLOW_SPAM_MODE) {
		bool speakerOn = (!AudioEngine::headphonesPluggedIn && !outputPluggedInL && !outputPluggedInR);
		setOutputState(SPEAKER_ENABLE.port, SPEAKER_ENABLE.pin, speakerOn);
	}

	AudioEngine::renderInStereo =
	    (AudioEngine::headphonesPluggedIn || outputPluggedInR || AudioEngine::isAnyInternalRecordingHappening());

	bool lineInNow = readInput(LINE_IN_DETECT.port, LINE_IN_DETECT.pin) != 0u;
	if (lineInNow != AudioEngine::lineInPluggedIn) {
		Debug::print("line in ");
		Debug::println(lineInNow);
		AudioEngine::lineInPluggedIn = lineInNow;
	}

	// Battery voltage
	// If analog read is ready...

	if (ADC.ADCSR & (1 << 15)) {
		int32_t numericReading = ADC.ADDRF;
		// Apply LPF
		int32_t voltageReading = numericReading * 3300;
		int32_t distanceToGo = voltageReading - voltageReadingLastTime;
		voltageReadingLastTime += distanceToGo >> 4;

		// We only >> by 15 so that we intentionally double the value, because the incoming voltage is halved by a resistive divider already
		batteryMV = (voltageReadingLastTime) >> 15;
		//Debug::print("batt mV: ");
		//Debug::println(batteryMV);

		// See if we've reached threshold to change verdict on battery level

		if (batteryCurrentRegion == 0) {
			if (batteryMV > 2950) {
makeBattLEDSolid:
				batteryCurrentRegion = 1;
				setOutputState(BATTERY_LED.port, BATTERY_LED.pin, false);
				uiTimerManager.unsetTimer(TIMER_BATT_LED_BLINK);
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
				uiTimerManager.unsetTimer(TIMER_BATT_LED_BLINK);
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

	uiTimerManager.setTimer(TIMER_READ_INPUTS, 100);
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

int32_t picFirmwareVersion = 0;
bool picSaysOLEDPresent = false;

bool readButtonsAndPads() {

	if (!usbInitializationPeriodComplete && (int32_t)(AudioEngine::audioSampleTimer - timeUSBInitializationEnds) >= 0) {
		usbInitializationPeriodComplete = 1;
	}

	/*
	if (!inSDRoutine && !closedPeripheral && !anythingInitiallyAttachedAsUSBHost && AudioEngine::audioSampleTimer >= (44100 << 1)) {
		Debug::println("closing peripheral");
		closeUSBPeripheral();
		Debug::println("switching back to host");
		openUSBHost();
		closedPeripheral = true;
	}
	*/

	if (waitingForSDRoutineToEnd) {
		if (sdRoutineLock) {
			return false;
		}
		Debug::println("got to end of sd routine");
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

			Debug::println("");
			Debug::println("undoing");
			Buttons::buttonAction(hid::button::BACK, true, sdRoutineLock);
		}
		else {
			Debug::println("");
			Debug::println("beginning playback");
			Buttons::buttonAction(hid::button::PLAY, true, sdRoutineLock);
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
				result = matrixDriver.padAction(p.x, p.y, thisPadPressIsOn);
				/* while this function takes an int32_t for velocity, 255 indicates to the downstream audition pad
				 * function that it should use the default velocity for the instrument
				 */
			}
			else {
				auto b = hid::Button(value);
				result = Buttons::buttonAction(b, thisPadPressIsOn, sdRoutineLock);
			}

			if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
				nextPadPressIsOn = thisPadPressIsOn;
				Debug::println("putCharBack ---------");
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
#if HAVE_OLED
		else if (util::to_underlying(value) == oledWaitingForMessage) {
			uiTimerManager.setTimer(TIMER_OLED_LOW_LEVEL, 3);
		}
#endif
	}

#if SD_TEST_MODE_ENABLED_LOAD_SONGS

	if (playbackHandler.currentlyPlaying) {
		if (getCurrentUI()->isViewScreen()) {
			Buttons::buttonAction(hid::button::LOAD, true);
			Buttons::buttonAction(hid::button::LOAD, false);
			alreadyDoneScroll = false;
		}
		else if (getCurrentUI() == &loadSongUI && currentUIMode == noSubMode) {
			if (!alreadyDoneScroll) {
				getCurrentUI()->selectEncoderAction(1);
				alreadyDoneScroll = true;
			}
			else {
				Buttons::buttonAction(hid::button::LOAD, true);
				Buttons::buttonAction(hid::button::LOAD, false);
			}
		}
	}
#endif

#if UNDO_REDO_TEST_ENABLED
	if (playbackHandler.currentlyPlaying && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {

		int32_t random0 = getRandom255();
		preLoadedSong = NULL;

		if (random0 < 64 && getCurrentUI() == &instrumentClipView) {
			Buttons::buttonAction(hid::button::song, true);
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

void setUIForLoadedSong(Song* song) {

	UI* newUI;

	// If in a Clip-minder view
	if (song->currentClip && song->inClipMinderViewOnLoad) {
		if (song->currentClip->type == CLIP_TYPE_INSTRUMENT) {
			if (((InstrumentClip*)song->currentClip)->onKeyboardScreen) {
				newUI = &keyboardScreen;
			}
			else if (((InstrumentClip*)song->currentClip)->onAutomationInstrumentClipView) {
				newUI = &automationInstrumentClipView;
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
#if HAVE_OLED
	renderUIsForOled();
#endif
}

void setupBlankSong() {
	void* songMemory = GeneralMemoryAllocator::get().alloc(sizeof(Song), NULL, false, true); // TODO: error checking
	preLoadedSong = new (songMemory) Song();

	preLoadedSong->paramManager.setupUnpatched(); // TODO: error checking
	GlobalEffectable::initParams(&preLoadedSong->paramManager);
	preLoadedSong->setupDefault();

	setRootUILowLevel(&instrumentClipView); // Prevents crash. (Wait, I'd like to know more about what crash...)
	preLoadedSong->ensureAtLeastOneSessionClip();

	currentSong = preLoadedSong;
	preLoadedSong = NULL;

	AudioEngine::getReverbParamsFromSong(currentSong);
	AudioEngine::getMasterCompressorParamsFromSong(currentSong);

	setUIForLoadedSong(currentSong);
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

extern "C" void usb_pstd_pcd_task(void);
extern "C" void usb_cstd_usb_task(void);

extern "C" volatile uint32_t usbLock;

extern "C" void usb_main_host(void);

extern "C" int32_t deluge_main(void) {

	// Give the PIC some startup instructions

#if HAVE_OLED
	PIC::enableOLED();
#endif

	PIC::setDebounce(20); // Set debounce time (mS) to...

	PadLEDs::setRefreshTime(23);

	PIC::setMinInterruptInterval(8);
	PIC::setFlashLength(6);

	PIC::setUARTSpeed();
	PIC::flush();

	// Setup SDRAM. Have to do this before setting up AudioEngine
	userdef_bsc_cs2_init(0); // 64MB, hardcoded

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
	R_RSPI_Create(
	    SPI_CHANNEL_CV,
#if HAVE_OLED
	    10000000, // Higher than this would probably work... but let's stick to the OLED datasheet's spec of 100ns (10MHz).
#else
	    30000000,
#endif
	    0, 32);
	R_RSPI_Start(SPI_CHANNEL_CV);
	setPinMux(SPI_CLK.port, SPI_CLK.pin, 3);   // CLK
	setPinMux(SPI_MOSI.port, SPI_MOSI.pin, 3); // MOSI
#if !HAVE_OLED
	setPinMux(SPI_SSL.port, SPI_SSL.pin, 3); // SSL
#else
	// If OLED sharing SPI channel, have to manually control SSL pin.
	setOutputState(6, 1, true);
	setPinAsOutput(6, 1);

	setupSPIInterrupts();
	oledDMAInit();

#endif

	// Setup audio output on SSI0
	ssiInit(0, 1);

#if RECORD_TEST_MODE == 1
	makeTestRecording();
#endif

	Encoders::init();

#ifdef TEST_GENERAL_MEMORY_ALLOCATION
	GeneralMemoryAllocator::get().test();
#endif

	init_crc_table();

	// Setup for gate output
	cvEngine.init();

	// Wait for PIC Uart to flush out. Could this help Ron R with his Deluge sometimes not booting? (No probably wasn't that.) Otherwise didn't seem necessary.
	PIC::waitForFlush();

	PIC::setupForPads();
	setOutputState(CODEC.port, CODEC.pin, 1); // Enable codec

	AudioEngine::init();

#if HARDWARE_TEST_MODE
	ramTestLED();
#endif

	audioFileManager.init();

	// Set up OLED now
#if HAVE_OLED

	//delayMS(10);

	// Set up 8-bit
	RSPI0.SPDCR = 0x20u;               // 8-bit
	RSPI0.SPCMD0 = 0b0000011100000010; // 8-bit
	RSPI0.SPBFCR.BYTE = 0b01100000;    //0b00100000;

	PIC::setDCLow();
	PIC::enableOLED();
	PIC::selectOLED();
	PIC::flush();

	delayMS(5);

	oledMainInit();

	//delayMS(5);

	PIC::deselectOLED();
	PIC::flush();
#endif

	// Setup SPIBSC. Crucial that this only be done now once everything else is running, because I've injected graphics and audio routines into the SPIBSC wait routines, so that
	// has to be running
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
	bool looksOk = true;

	PIC::read(0x8000, [&readingFirmwareVersion, &looksOk](auto response) {
		if (readingFirmwareVersion) {
			readingFirmwareVersion = false;
			uint8_t value = util::to_underlying(response);

			picFirmwareVersion = value & 127;
			picSaysOLEDPresent = value & 128;
			Debug::print("PIC firmware version reported: ");
			Debug::println(value);
			return 0; // continue
		}

		using enum PIC::Response;
		switch (response) {
		case FIRMWARE_VERSION_NEXT:
			readingFirmwareVersion = true;
			return 0;

		case UNKNOWN_BREAK:
			return 1;

		case RESET_SETTINGS:
			if (looksOk) {
#if HAVE_OLED
				OLED::consoleText("Factory reset");
#else
				numericDriver.displayPopup("RESET");
#endif
				FlashStorage::resetSettings();
				FlashStorage::writeSettings();
			}
			return 0;

		default:
			if (response >= UNKNOWN_OLED_RELATED_COMMAND && response <= SET_DC_HIGH) {
				// OLED D/C low ack
				return 0;
			}
			// If any hint of another button being held, don't do anything.
			// (Unless we already did in which case, well it's probably ok.)
			looksOk = false;
			return 0;
		}
	});

	FlashStorage::readSettings();

	runtimeFeatureSettings.init();
	runtimeFeatureSettings.readSettingsFromFile();

	usbLock = 1;
	openUSBHost();

	// If nothing was plugged in to us as host, we'll go peripheral
	if (!anythingInitiallyAttachedAsUSBHost) {
		Debug::println("switching from host to peripheral");
		closeUSBHost();
		openUSBPeripheral();
	}

	usbLock = 0;

	// Ideally I'd like to repeatedly switch between host and peripheral mode anytime there's no USB connection.
	// To do that, I'd really need to know at any point in time whether the user had just made a connection, just then, that hadn't fully
	// initialized yet. I think I sorta have that for host, but not for peripheral yet.

	MIDIDeviceManager::readDevicesFromFile(); // Hopefully we can read this file now.

	setupBlankSong(); // Can only happen after settings, which includes default settings, have been read

#ifdef TEST_BST
	BST bst;
	bst.test();
#endif

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

#ifdef TEST_OPEN_ADDRESSING_HASH_TABLE
	OpenAddressingHashTableWith8bitKey table;
	table.test();
#endif

#ifdef TEST_SD_WRITE

	FIL fil; // File object
	FATFS fs;
	DIR dp;
	FRESULT result; //	 FatFs return code
	int32_t sdTotalBytesWritten;

	int32_t count = 0;

	while (true) {

		numericDriver.setTextAsNumber(count);

		int32_t fileNumber = (uint32_t)getNoise() % 10000;
		int32_t fileSize = (uint32_t)getNoise() % 1000000;

		char fileName[20];
		strcpy(fineName, "TEST/") intToString(fileNumber, &fileName[5], 4);
		strcat(fileName, ".TXT");

		result = f_open(&fil, fileName, FA_CREATE_ALWAYS | FA_WRITE);
		if (result) {
			numericDriver.setText("AAAA");
			while (1) {}
		}

		sdTotalBytesWritten = 0;

		while (sdTotalBytesWritten < fileSize) {
			UINT bytesWritten = 0;
			result = f_write(&fil, &miscStringBuffer, 256, &bytesWritten);

			if (bytesWritten != 256) {
				numericDriver.setText("BBBB");
				while (1) {}
			}

			sdTotalBytesWritten += 256;
		}

		f_close(&fil);

		count++;
	}
#endif

	inputRoutine();

	uiTimerManager.setTimer(TIMER_GRAPHICS_ROUTINE, 50);

	Debug::println("going into main loop");
	sdRoutineLock = false; // Allow SD routine to start happening

	while (1) {

		uiTimerManager.routine();

		// Flush stuff - we just have to do this, regularly
#if HAVE_OLED
		oledRoutine();
#endif
		PIC::flush();

		AudioEngine::routineWithClusterLoading(true); // -----------------------------------

		int32_t count = 0;
		while (readButtonsAndPads() && count < 16) {
			if (!(count & 3)) {
				AudioEngine::routineWithClusterLoading(true); // -----------------------------------
			}
			count++;
		}

		Encoders::readEncoders();
		bool anything = Encoders::interpretEncoders();
		if (anything) {
			AudioEngine::routineWithClusterLoading(true); // -----------------------------------
		}

		doAnyPendingUIRendering();

		AudioEngine::routineWithClusterLoading(true); // -----------------------------------

		// Only actually needs calling a couple of times per second, but we can't put it in uiTimerManager cos that gets called in card routine
		audioFileManager.slowRoutine();
		AudioEngine::slowRoutine();

		audioRecorder.slowRoutine();

#if AUTOPILOT_TEST_ENABLED
		autoPilotStuff();
#endif
	}

	return 0;
}

bool inSpamMode = false;

extern "C" void logAudioAction(char const* string) {
	AudioEngine::logAction(string);
}

extern "C" void routineForSD(void) {

	if (inInterrupt) {
		return;
	}

	// We lock this to prevent multiple entry. Otherwise we could get SD -> routineForSD() -> AudioEngine::routine() -> USB -> routineForSD()
	if (sdRoutineLock) {
		return;
	}

	sdRoutineLock = true;

	AudioEngine::logAction("from routineForSD()");
	AudioEngine::routine();

	uiTimerManager.routine();

#if HAVE_OLED
	oledRoutine();
#endif
	PIC::flush();

	Encoders::readEncoders();
	Encoders::interpretEncoders(true);
	readButtonsAndPads();
	doAnyPendingUIRendering();

	sdRoutineLock = false;
}

extern "C" void sdCardInserted(void) {
}

extern "C" void sdCardEjected(void) {
	audioFileManager.cardEjected = true;
}

extern "C" void loadAnyEnqueuedClustersRoutine() {
	audioFileManager.loadAnyEnqueuedClusters();
}

#if !HAVE_OLED
extern "C" void setNumeric(char* text) {
	numericDriver.setText(text);
}

extern "C" void setNumericNumber(int32_t number) {
	numericDriver.setTextAsNumber(number);
}
#endif

extern "C" void routineWithClusterLoading() {
	AudioEngine::routineWithClusterLoading(false);
}

void deleteOldSongBeforeLoadingNew() {

	currentSong->stopAllAuditioning();

	AudioEngine::unassignAllVoices(
	    true); // Need to do this now that we're not bothering getting the old Song's Instruments detached and everything on delete

	view.activeModControllableModelStack.modControllable = NULL;
	view.activeModControllableModelStack.setTimelineCounter(NULL);
	view.activeModControllableModelStack.paramManager = NULL;

	Song* toDelete = currentSong;
	currentSong = NULL;
	void* toDealloc = dynamic_cast<void*>(toDelete);
	toDelete->~Song();
	GeneralMemoryAllocator::get().dealloc(toDelete);
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

	numericDriver.setText(thingName, false, spamStates[currentSpamThing] ? 3 : 255);
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
						//Debug::println("couldn't create");
					}
					else {
						if (spamStates[SPAM_MIDI])
							Debug::println("writing");
						sdFileCurrentlyOpen = true;
					}
				}

				// Write to open file
				else {
					UINT bytesWritten = 0;
					char thisByte = getRandom255();
					result = f_write(&fil, &thisByte, 1, &bytesWritten);
					//if (result) Debug::println("couldn't write");

					sdTotalBytesWritten++;

					if (sdTotalBytesWritten > 1000 * 5) {
						f_close(&fil);
						//Debug::println("finished writing");
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
						//Debug::println("file not found");
					}

					else {
						if (spamStates[SPAM_MIDI])
							Debug::println("reading");
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
						//Debug::println("finished file");
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
