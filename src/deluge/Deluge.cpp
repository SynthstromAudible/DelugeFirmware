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

#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <contextmenuclearsong.h>
#include <ContextMenuSampleBrowserKit.h>
#include <ContextMenuSampleBrowserSynth.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include <ParamManager.h>
#include <samplebrowser.h>
#include "iodefine.h"

#include <stdlib.h>
#include <new>

#include "UI.h"
#include "song.h"
#include "soundeditor.h"
#include "AudioSample.h"
#include "saveinstrumentpresetui.h"
#include "CVEngine.h"
#include "numericdriver.h"
#include "KeyboardScreen.h"
#include "View.h"
#include "AudioRecorder.h"
#include "midiengine.h"
#include "matrixdriver.h"
#include "uitimermanager.h"
#include "Note.h"
#include "ActionLogger.h"
#include "Slicer.h"
#include "encoder.h"
#include <string.h>
#include <SaveSongOrInstrumentContextMenu.h>
#include <SessionView.h>
#include <WaveformRenderer.h>
#include "Arrangement.h"
#include "Session.h"
#include "storagemanager.h"
#include "LoadInstrumentPresetUI.h"
#include "RenameDrumUI.h"
#include "ContextMenuOverwriteFile.h"
#include "ContextMenuDeleteFile.h"
#include "AudioInputSelector.h"
#include "ContextMenuLoadInstrumentPreset.h"
#include "SampleMarkerEditor.h"
#include "WaveformBasicNavigator.h"
#include "AudioClipView.h"
#include "RenameOutputUI.h"
#include "Output.h"
#include "OpenAddressingHashTable.h"
#include "GeneralMemoryAllocator.h"
#include "NE10.h"
#include "IndicatorLEDs.h"
#include "Encoders.h"
#include "FlashStorage.h"
#include "HardwareTesting.h"
#include "Buttons.h"
#include "uart.h"
#include "MIDIDeviceManager.h"
#include "InstrumentClip.h"
#include "FileItem.h"
#include "loadsongui.h"
#include "SaveSongUI.h"
#include "oled.h"
#include "ContextMenuOverwriteBootloader.h"
#include "RuntimeFeatureSettings.h"
#include "Deluge.h"

#if AUTOMATED_TESTER_ENABLED
#include "AutomatedTester.h"
#endif

extern "C" {
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "uart_all_cpus.h"
#include "sio_char.h"
#include "r_usb_basic_if.h"
#include <drivers/RZA1/system/rza_io_regrw.h>
#include <drivers/RZA1/gpio/gpio.h>
#include "ssi_all_cpus.h"
#include "mtu_all_cpus.h"
#include "oled_low_level_all_cpus.h"
#include "oled_low_level.h"

#include "rspi.h"
#include "spibsc_Deluge_setup.h"
#include "r_usb_pmidi_config.h"
#include "bsc_userdef.h"
#include "ssi.h"
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

int voltageReadingLastTime = 65535 * 3300;
uint8_t batteryCurrentRegion = 2;
uint16_t batteryMV;
bool batteryLEDState = false;

void batteryLEDBlink() {
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	setOutputState(BATTERY_LED_1, BATTERY_LED_2, batteryLEDState);
	int blinkPeriod = ((int)batteryMV - 2630) * 3;
	blinkPeriod = getMin(blinkPeriod, 500);
	blinkPeriod = getMax(blinkPeriod, 60);
	uiTimerManager.setTimer(TIMER_BATT_LED_BLINK, blinkPeriod);
	batteryLEDState = !batteryLEDState;
#endif
}

void inputRoutine() {
	disk_timerproc(UI_MS_PER_REFRESH);

	// Check if mono output cable plugged in
	bool outputPluggedInL = readInput(LINE_OUT_DETECT_L_1, LINE_OUT_DETECT_L_2);
	bool outputPluggedInR = readInput(LINE_OUT_DETECT_R_1, LINE_OUT_DETECT_R_2);

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	bool headphoneNow = readInput(HEADPHONE_DETECT_1, HEADPHONE_DETECT_2);
	if (headphoneNow != AudioEngine::headphonesPluggedIn) {
		Uart::print("headphone ");
		Uart::println(headphoneNow);
		AudioEngine::headphonesPluggedIn = headphoneNow;
	}

	bool micNow = !readInput(7, 9);
	if (micNow != AudioEngine::micPluggedIn) {
		Uart::print("mic ");
		Uart::println(micNow);
		AudioEngine::micPluggedIn = micNow;
	}

	if (!ALLOW_SPAM_MODE) {
		bool speakerOn = (!AudioEngine::headphonesPluggedIn && !outputPluggedInL && !outputPluggedInR);
		setOutputState(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2, speakerOn);
	}

	AudioEngine::renderInStereo =
	    (AudioEngine::headphonesPluggedIn || outputPluggedInR || AudioEngine::isAnyInternalRecordingHappening());

	bool lineInNow = readInput(6, 6);
	if (lineInNow != AudioEngine::lineInPluggedIn) {
		Uart::print("line in ");
		Uart::println(lineInNow);
		AudioEngine::lineInPluggedIn = lineInNow;
	}

#else
	AudioEngine::renderInStereo = !(outputPluggedInL && !outputPluggedInR);
#endif

	// Battery voltage
#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	// If analog read is ready...

	if (ADC.ADCSR & (1 << 15)) {
		int numericReading = ADC.ADDRF;
		// Apply LPF
		int voltageReading = numericReading * 3300;
		int distanceToGo = voltageReading - voltageReadingLastTime;
		voltageReadingLastTime += distanceToGo >> 4;
		batteryMV =
		    (voltageReadingLastTime)
		    >> 15; // We only >> by 15 so that we intentionally double the value, because the incoming voltage is halved by a resistive divider already
		//uartPrint("batt mV: ");
		//uartPrintNumber(batteryMV);

		// See if we've reached threshold to change verdict on battery level

		if (batteryCurrentRegion == 0) {
			if (batteryMV > 2950) {
makeBattLEDSolid:
				batteryCurrentRegion = 1;
				setOutputState(BATTERY_LED_1, BATTERY_LED_2, false);
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
				setOutputState(BATTERY_LED_1, BATTERY_LED_2, true);
				uiTimerManager.unsetTimer(TIMER_BATT_LED_BLINK);
			}
		}
		else {
			if (batteryMV < 3200) goto makeBattLEDSolid;
		}
	}

	// Set up for next analog read
	ADC.ADCSR = (1 << 13) | (0b011 << 6) | SYS_VOLT_SENSE_PIN;
#endif

	MIDIDeviceManager::slowRoutine();

	uiTimerManager.setTimer(TIMER_READ_INPUTS, 100);
}

bool nextPadPressIsOn = true; // Not actually used for 40-pad
bool alreadyDoneScroll = false;
bool waitingForSDRoutineToEnd = false;

extern uint8_t anythingInitiallyAttachedAsUSBHost;

bool closedPeripheral = false;

extern "C" {
uint32_t timeUSBInitializationEnds = 44100;
uint8_t usbInitializationPeriodComplete = 0;

void setTimeUSBInitializationEnds(int timeFromNow) {
	timeUSBInitializationEnds = AudioEngine::audioSampleTimer + timeFromNow;
	usbInitializationPeriodComplete = 0;
}
}

extern "C" void openUSBHost(void);
extern "C" void closeUSBHost();
extern "C" void openUSBPeripheral();
extern "C" void closeUSBPeripheral(void);

int picFirmwareVersion = 0;
bool picSaysOLEDPresent = false;

bool readButtonsAndPads() {

	if (!usbInitializationPeriodComplete && (int32_t)(AudioEngine::audioSampleTimer - timeUSBInitializationEnds) >= 0) {
		usbInitializationPeriodComplete = 1;
	}

	/*
	if (!inSDRoutine && !closedPeripheral && !anythingInitiallyAttachedAsUSBHost && AudioEngine::audioSampleTimer >= (44100 << 1)) {
		Uart::println("closing peripheral");
		closeUSBPeripheral();
		Uart::println("switching back to host");
		openUSBHost();
		closedPeripheral = true;
	}
	*/

	if (waitingForSDRoutineToEnd) {
		if (sdRoutineLock) return false;
		else {
			Uart::println("got to end of sd routine");
			waitingForSDRoutineToEnd = false;
		}
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

			Uart::println("");
			Uart::println("undoing");
			Buttons::buttonAction(backButtonX, backButtonY, true, sdRoutineLock);
		}
		else {
			Uart::println("");
			Uart::println("beginning playback");
			Buttons::buttonAction(playButtonX, playButtonY, true, sdRoutineLock);
		}

		int random = getRandom255();

		timeNextSDTestAction = AudioEngine::audioSampleTimer + ((random) << 9);
	}
#endif

	uint8_t value;
	bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
	if (anything) {

		if (value < PAD_AND_BUTTON_MESSAGES_END) {

			int result;

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
			bool thisPadPressIsOn = (value >= 70);
			int x = (unsigned int)value % 10;
			int y = ((unsigned int)value % 70) / 10;

			if (y < displayHeight) result = matrixDriver.padAction(x, y, thisPadPressIsOn, inSDRoutine);
			else result = Buttons::buttonAction(x, y - displayHeight, thisPadPressIsOn, sdRoutineLock);

#else
			bool thisPadPressIsOn = nextPadPressIsOn;
			nextPadPressIsOn = true;

			int y = (unsigned int)value / 9;
			int x = value - y * 9;

			// If pad...
			if (y < displayHeight * 2) {
				x <<= 1;
				if (y >= displayHeight) {
					y -= displayHeight;
					x++;
				}

				result = matrixDriver.padAction(x, y, thisPadPressIsOn);
			}

			// Or if button...
			else {
				result = Buttons::buttonAction(x, y - displayHeight * 2, thisPadPressIsOn, sdRoutineLock);
			}
#endif

			if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) {
				nextPadPressIsOn = thisPadPressIsOn;
				Uart::println("putCharBack ---------");
				uartPutCharBack(UART_ITEM_PIC);
				waitingForSDRoutineToEnd = true;
				return false;
			}
		}
		else if (value == 252) {
			nextPadPressIsOn = false;
		}

		// "No presses happening" message
		else if (value == NO_PRESSES_HAPPENING_MESSAGE) {
			if (!sdRoutineLock) {
				matrixDriver.noPressesHappening(sdRoutineLock);
				Buttons::noPressesHappening(sdRoutineLock);
			}
		}
#if HAVE_OLED
		else if (value == oledWaitingForMessage) {
			uiTimerManager.setTimer(TIMER_OLED_LOW_LEVEL, 3);
		}
#endif
	}

#if SD_TEST_MODE_ENABLED_LOAD_SONGS

	if (playbackHandler.currentlyPlaying) {
		if (getCurrentUI()->isViewScreen()) {
			Buttons::buttonAction(loadButtonX, loadButtonY, true);
			Buttons::buttonAction(loadButtonX, loadButtonY, false);
			alreadyDoneScroll = false;
		}
		else if (getCurrentUI() == &loadSongUI && currentUIMode == noSubMode) {
			if (!alreadyDoneScroll) {
				getCurrentUI()->selectEncoderAction(1);
				alreadyDoneScroll = true;
			}
			else {
				Buttons::buttonAction(loadButtonX, loadButtonY, true);
				Buttons::buttonAction(loadButtonX, loadButtonY, false);
			}
		}
	}
#endif

#if UNDO_REDO_TEST_ENABLED
	if (playbackHandler.currentlyPlaying && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {

		int random0 = getRandom255();
		preLoadedSong = NULL;

		if (random0 < 64 && getCurrentUI() == &instrumentClipView) {
			Buttons::buttonAction(songViewButtonX, songViewButtonY, true);
		}

		else if (random0 < 120) actionLogger.revert(BEFORE);
		else actionLogger.revert(AFTER);

		int random = getRandom255();
		timeNextSDTestAction = AudioEngine::audioSampleTimer + ((random) << 4); // * 44 / 13;
		anything = true;
	}
#endif

#if LAUNCH_CLIP_TEST_ENABLED
	if (playbackHandler.playbackState && (int32_t)(audioDriver.audioSampleTimer - timeNextSDTestAction) >= 0) {
		matrixDriver.buttonStates[shiftButtonX][shiftButtonY] = true;
		matrixDriver.padAction(displayWidth, getRandom255() & 7, true, inSDRoutine);
		matrixDriver.buttonStates[shiftButtonX][shiftButtonY] = false;
		int random = getRandom255();
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
	void* songMemory = generalMemoryAllocator.alloc(sizeof(Song), NULL, false, true); // TODO: error checking
	preLoadedSong = new (songMemory) Song();

	preLoadedSong->paramManager.setupUnpatched(); // TODO: error checking
	GlobalEffectable::initParams(&preLoadedSong->paramManager);
	preLoadedSong->setupDefault();

	setRootUILowLevel(&instrumentClipView); // Prevents crash. (Wait, I'd like to know more about what crash...)
	preLoadedSong->ensureAtLeastOneSessionClip();

	currentSong = preLoadedSong;
	preLoadedSong = NULL;

	AudioEngine::getReverbParamsFromSong(currentSong);

	setUIForLoadedSong(currentSong);
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

extern "C" void usb_pstd_pcd_task(void);
extern "C" void usb_cstd_usb_task(void);

extern "C" volatile uint32_t usbLock;

extern "C" void usb_main_host(void);

extern "C" int deluge_main(void) {

	// Give the PIC some startup instructions

#if HAVE_OLED
	bufferPICUart(247); // Enable OLED
#endif

	bufferPICUart(18); // Set debounce time (mS) to...
	bufferPICUart(20);

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	setRefreshTime(12);
	bufferPICUart(20); // Set flash length
	bufferPICUart(6);
#else
	setRefreshTime(23);

#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	bufferPICUart(244); // Set min interrupt interval
	bufferPICUart(8);
#endif

	bufferPICUart(23); // Set flash length
	bufferPICUart(6);

#endif

#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	int newSpeedNumber = 4000000.0f / UART_FULL_SPEED_PIC_PADS_HZ - 0.5f;
	bufferPICPadsUart(225);            // Set UART speed
	bufferPICPadsUart(newSpeedNumber); // Speed is 4MHz / (x + 1)
	uartFlushIfNotSending(UART_ITEM_PIC_PADS);
#endif

	// Setup SDRAM. Have to do this before setting up AudioEngine
	userdef_bsc_cs2_init(0); // 64MB, hardcoded

	functionsInit();

	currentPlaybackMode = &session;

	setOutputState(BATTERY_LED_1, BATTERY_LED_2, 1); // Switch it off (1 is off for open-drain)
	setPinAsOutput(BATTERY_LED_1, BATTERY_LED_2);    // Battery LED control

	setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, 0); // Switch it off
	setPinAsOutput(SYNCED_LED_PORT, SYNCED_LED_PIN);    // Synced LED

#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD

	// Codec control
	setPinAsOutput(6, 12);
	setOutputState(6, 12, 0); // Switch it off

	// Speaker / amp control
	setPinAsOutput(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2);
	setOutputState(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2, 0); // Switch it off

	setPinAsInput(HEADPHONE_DETECT_1, HEADPHONE_DETECT_2); // Headphone detect
	setPinAsInput(6, 6);                                   // Line in detect
	setPinAsInput(7, 9);                                   // Mic detect

	setPinMux(1, 8 + SYS_VOLT_SENSE_PIN, 1); // Analog input for voltage sense

#else
	// SD CD pin
	setPinAsInput(6, 7);

	// SPI 0 for SD
	R_RSPI_Create(0, 400000, 1, 8); // 400000
	R_RSPI_Start(0);
	setPinMux(6, 0, 3);
	setPinMux(6, 2, 3);
	setPinMux(6, 3, 3);
	// Non-automatic SSL pin for SD SPI
	//setPinMux(6, 1, 3);
	setPinAsOutput(6, 1);
	setOutputState(6, 1, 1);
#endif

	// Trigger clock input
	setPinMux(ANALOG_CLOCK_IN_1, ANALOG_CLOCK_IN_2, 2);

	// Line out detect pins
	setPinAsInput(LINE_OUT_DETECT_L_1, LINE_OUT_DETECT_L_2);
	setPinAsInput(LINE_OUT_DETECT_R_1, LINE_OUT_DETECT_R_2);

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
#if SPI_CHANNEL_CV == 1
	setPinMux(6, 12, 3);
	setPinMux(6, 14, 3);
	setPinMux(6, 13, 3);
#elif SPI_CHANNEL_CV == 0
	setPinMux(6, 0, 3); // CLK
	setPinMux(6, 2, 3); // MOSI
#if !HAVE_OLED
	setPinMux(6, 1, 3); // SSL
#else
	// If OLED sharing SPI channel, have to manually control SSL pin.
	setOutputState(6, 1, true);
	setPinAsOutput(6, 1);

	setupSPIInterrupts();
	oledDMAInit();

#endif
#endif

	// Setup audio output on SSI0
	ssiInit(0, 1);

#if RECORD_TEST_MODE == 1
	makeTestRecording();
#endif

	Encoders::init();

#ifdef TEST_GENERAL_MEMORY_ALLOCATION
	generalMemoryAllocator.test();
#endif

	// Setup for gate output
	cvEngine.init();

#if DELUGE_MODEL == DELUGE_MODEL_144_PAD

	// Wait for PIC Uart to flush out. Could this help Ron R with his Deluge sometimes not booting? (No probably wasn't that.) Otherwise didn't seem necessary.
	while (!(DMACn(PIC_TX_DMA_CHANNEL).CHSTAT_n & (1 << 6))) {}

	uartSetBaudRate(UART_CHANNEL_PIC, UART_FULL_SPEED_PIC_PADS_HZ);
	setOutputState(6, 12, 1); // Enable codec
#endif

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

	bufferPICUart(250); // D/C low
	bufferPICUart(247); // Enable OLED
	bufferPICUart(248); // Select OLED
	uartFlushIfNotSending(UART_ITEM_PIC);

	delayMS(5);

	oledMainInit();

	//delayMS(5);

	bufferPICUart(249); // Unselect OLED
	uartFlushIfNotSending(UART_ITEM_PIC);
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

	bufferPICIndicatorsUart(245);                          // Request PIC firmware version
	bufferPICIndicatorsUart(RESEND_BUTTON_STATES_MESSAGE); // Tell PIC to re-send button states
	uartFlushIfNotSending(UART_ITEM_PIC_INDICATORS);

	// Check if the user is holding down the select knob to do a factory reset
	uint16_t timeWaitBegan = *TCNT[TIMER_SYSTEM_FAST];
	bool readingFirmwareVersion = false;
	bool looksOk = true;

	while ((uint16_t)(*TCNT[TIMER_SYSTEM_FAST] - timeWaitBegan)
	       < 32768) { // Safety timer, in case we don't receive anything
		uint8_t value;
		if (!uartGetChar(UART_ITEM_PIC, (char*)&value)) continue;

		if (readingFirmwareVersion) {
			readingFirmwareVersion = false;
			picFirmwareVersion = value & 127;
			picSaysOLEDPresent = value & 128;
			Uart::print("PIC firmware version reported: ");
			Uart::println(value);
		}
		else {
			if (value == 245) readingFirmwareVersion = true; // Message means "here comes the firmware version next".
			else if (value == 253) break;
			else if (value ==
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
			         (110 + selectEncButtonY * 10 + selectEncButtonX)
#else
			         175
#endif
			) {
				if (looksOk) goto resetSettings;
			}
			else if (value >= 246 && value <= 251) {} // OLED D/C low ack
			else { // If any hint of another button being held, don't do anything. (Unless we already did in which case, well it's probably ok.)
				looksOk = false;
			}
		}
	}

	FlashStorage::readSettings();

	if (false) {
resetSettings:
#if HAVE_OLED
		OLED::consoleText("Factory reset");
#else
		numericDriver.displayPopup("RESET");
#endif
		FlashStorage::resetSettings();
		FlashStorage::writeSettings();
	}

	new (&runtimeFeatureSettings) RuntimeFeatureSettings;
	runtimeFeatureSettings.readSettingsFromFile();

	usbLock = 1;
	openUSBHost();

	// If nothing was plugged in to us as host, we'll go peripheral
	if (!anythingInitiallyAttachedAsUSBHost) {
		Uart::println("switching from host to peripheral");
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
	int sdTotalBytesWritten;

	int count = 0;

	while (true) {

		numericDriver.setTextAsNumber(count);

		int fileNumber = (uint32_t)getNoise() % 10000;
		int fileSize = (uint32_t)getNoise() % 1000000;

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

	Uart::println("going into main loop");
	sdRoutineLock = false; // Allow SD routine to start happening

	while (1) {

		uiTimerManager.routine();

		// Flush stuff - we just have to do this, regularly
#if HAVE_OLED
		oledRoutine();
#endif
		uartFlushIfNotSending(UART_ITEM_PIC);

		AudioEngine::routineWithClusterLoading(true); // -----------------------------------

		int count = 0;
		while (readButtonsAndPads() && count < 16) {
			if (!(count & 3)) AudioEngine::routineWithClusterLoading(true); // -----------------------------------
			count++;
		}

		Encoders::readEncoders();
		bool anything = Encoders::interpretEncoders();
		if (anything) {
			AudioEngine::routineWithClusterLoading(true); // -----------------------------------
		}

		doAnyPendingUIRendering();

		AudioEngine::routineWithClusterLoading(true); // -----------------------------------

		audioFileManager
		    .slowRoutine(); // Only actually needs calling a couple of times per second, but we can't put it in uiTimerManager cos that gets called in card routine
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

	if (inInterrupt) return;

	// We lock this to prevent multiple entry. Otherwise we could get SD -> routineForSD() -> AudioEngine::routine() -> USB -> routineForSD()
	if (sdRoutineLock) return;

	sdRoutineLock = true;

	AudioEngine::logAction("from routineForSD()");
	AudioEngine::routine();

	uiTimerManager.routine();

#if HAVE_OLED
	oledRoutine();
#endif
	uartFlushIfNotSending(UART_ITEM_PIC);

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

extern "C" void setNumericNumber(int number) {
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
	generalMemoryAllocator.dealloc(toDelete);
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
int currentSpamThing = 0;

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

	int cvChannel = 0;

	bool sdReading = false;
	bool sdFileCurrentlyOpen = false;
	int sdTotalBytesWritten = 0;

	uint16_t timeLastCV = 0;
	uint16_t timeLastPIC = 0;
	uint16_t timeLastMIDI = 0;
	uint16_t timeLastUSB = 0;
	int lastCol = 0;

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
			if (++ramReadAddress == (uint32_t*)0x10000000) ramReadAddress = (uint32_t*)0x0C000000;
			if (++ramWriteAddress == (uint32_t*)0x10000000) ramWriteAddress = (uint32_t*)0x0C000000;
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
						//Uart::println("couldn't create");
					}
					else {
						if (spamStates[SPAM_MIDI]) Uart::println("writing");
						sdFileCurrentlyOpen = true;
					}
				}

				// Write to open file
				else {
					UINT bytesWritten = 0;
					char thisByte = getRandom255();
					result = f_write(&fil, &thisByte, 1, &bytesWritten);
					//if (result) Uart::println("couldn't write");

					sdTotalBytesWritten++;

					if (sdTotalBytesWritten > 1000 * 5) {
						f_close(&fil);
						//Uart::println("finished writing");
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
						//Uart::println("file not found");
					}

					else {
						if (spamStates[SPAM_MIDI]) Uart::println("reading");
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
						//Uart::println("finished file");
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

				Uart::putChar(UART_CHANNEL_PIC, lastCol + 1);
				for (int i = 0; i < 16; i++) {
					int whichColour = getRandom255() % 3;
					for (int colour = 0; colour < 3; colour++) {
						if (colour == whichColour && (getRandom255() % 3) == 0)
							Uart::putChar(UART_CHANNEL_PIC, getRandom255());
						else Uart::putChar(UART_CHANNEL_PIC, 0);
					}
				}

				lastCol = (lastCol + 1) % 9;
			}
		}

		// Selector
		readEncoders();

		// Select encoder
		int limitedDetentPos = encoders[ENCODER_SELECT].detentPos;
		encoders[ENCODER_SELECT].detentPos = 0; // Reset. Crucial that this happens before we call selectEncoderAction()

		if (limitedDetentPos != 0) {
			currentSpamThing += limitedDetentPos;
			if (currentSpamThing == NUM_SPAM_THINGS) currentSpamThing = 0;
			else if (currentSpamThing == -1) currentSpamThing = NUM_SPAM_THINGS - 1;

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
					setPinMux(7, 11, 6); // AUDIO_XOUT
					setPinMux(6, 9, 3);  // SSI0 word select
					setPinMux(6, 10, 3); // SSI0 tx
					setPinMux(6, 8, 3);  // SSI0 serial clock
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
					setPinMux(6, 11, 3); // SSI0 rx
#endif
					setOutputState(6, 12, 1); // Switch codec on

					setOutputState(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2, 1); // Speaker on
				}
			}

			// PIC
			else if (currentSpamThing == SPAM_PIC) {

				// Disable
				if (!spamStates[currentSpamThing]) {
					Uart::putChar(UART_CHANNEL_PIC, 227);
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
