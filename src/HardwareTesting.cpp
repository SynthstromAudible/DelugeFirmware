/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "HardwareTesting.h"
#include "definitions.h"
#include "uart.h"
#include "IndicatorLEDs.h"
#include "CVEngine.h"
#include <string.h>
#include "midiengine.h"
#include "Encoders.h"
#include "oled.h"
#include "AudioEngine.h"
#include "RootUI.h"
#include "functions.h"
#include "matrixdriver.h"
#include "Buttons.h"
#include "loadsongui.h"

extern "C" {
#include "cfunctions.h"
#include "gpio.h"
#include "uart_all_cpus.h"
#include "sio_char.h"
#include "ssi_all_cpus.h"
#include "oled_low_level.h"
}

void ramTestUart() {
	// Test the RAM
	uint32_t lastErrorAt = 0;
	uint32_t* address;

	while (1) {

		//while (1) {
		Uart::println("writing to ram");
		address = (uint32_t*)EXTERNAL_MEMORY_BEGIN;
		while (address != (uint32_t*)EXTERNAL_MEMORY_END) {
			*address = (uint32_t)address;
			address++;
		}
		//}

		//while (1) {
		Uart::println("reading back from ram. Checking for errors every megabyte");
		address = (uint32_t*)EXTERNAL_MEMORY_BEGIN;
		while (address != (uint32_t*)EXTERNAL_MEMORY_END) {
			if (*address != (uint32_t)address) {

				uint32_t errorAtBlockNow = ((uint32_t)address) & (0xFFF00000);
				if (errorAtBlockNow != lastErrorAt) {
					while (uartGetTxBufferFullnessByItem(UART_ITEM_MIDI) > 100)
						;
					Uart::print("error at ");
					Uart::print((uint32_t)address);
					Uart::print(". got ");
					Uart::println(*address);
					//while(1);
					lastErrorAt = errorAtBlockNow;
				}
			}
			address++;
		}
		//}
		Uart::println("finished checking ram");
	}
}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
bool inputStateLastTime = false;

bool nextIsDepress = false;
int16_t encoderTestPos = 128;

void setupSquareWave() {
	// Send square wave
	int count = 0;
	for (int32_t* address = getTxBufferStart(); address < getTxBufferEnd(); address++) {
		if (count < SSI_TX_BUFFER_NUM_SAMPLES) {
			*address = 2147483647;
		}
		else {
			*address = -2147483648;
		}

		count++;
	}
}

int hardwareTestWhichColour = 0;

void sendColoursForHardwareTest(bool testButtonStates[9][16]) {
	for (int x = 0; x < 9; x++) {
		bufferPICPadsUart(x + 1);
		for (int y = 0; y < 16; y++) {
			for (int c = 0; c < 3; c++) {
				int value;
				if (testButtonStates[x][y]) value = 255;
				else value = (c == hardwareTestWhichColour) ? 64 : 0;
				bufferPICPadsUart(value);
			}
		}
	}

	uartFlushIfNotSending(UART_ITEM_PIC_PADS);
}

bool anythingProbablyPressed = false;

void readInputsForHardwareTest(bool testButtonStates[9][16]) {
	bool outputPluggedInL = readInput(LINE_OUT_DETECT_L_1, LINE_OUT_DETECT_L_2);
	bool outputPluggedInR = readInput(LINE_OUT_DETECT_R_1, LINE_OUT_DETECT_R_2);
	bool headphoneNow = readInput(HEADPHONE_DETECT_1, HEADPHONE_DETECT_2);
	bool micNow = !readInput(7, 9);
	bool lineInNow = readInput(6, 6);
	bool gateInNow = readInput(ANALOG_CLOCK_IN_1, ANALOG_CLOCK_IN_2);

	bool inputStateNow = (outputPluggedInL == outputPluggedInR == headphoneNow == micNow == lineInNow == gateInNow);

	if (inputStateNow != inputStateLastTime) {
		IndicatorLEDs::setLedState(tapTempoLedX, tapTempoLedY, !inputStateNow);
		inputStateLastTime = inputStateNow;
	}

	uint8_t value;
	bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
	if (anything) {
		if (value == 252) {
			nextIsDepress = true;
		}
		else if (value < 180) {

			int y = (unsigned int)value / 9;
			int x = value - y * 9;
			if (y < displayHeight * 2) {

				testButtonStates[x][y] = !nextIsDepress;
				sendColoursForHardwareTest(testButtonStates);
			}

			if (nextIsDepress) {

				// Send silence
				if (!HARDWARE_TEST_MODE) {
					int count = 0;
					for (int32_t* address = getTxBufferStart(); address < getTxBufferEnd(); address++) {
						*address = 1024;
						count++;
					}
				}

				nextIsDepress = false;
				anythingProbablyPressed = false;
			}
			else {
				if (!HARDWARE_TEST_MODE) setupSquareWave();

				anythingProbablyPressed = true;
			}
		}
#if HAVE_OLED
		else if (value == oledWaitingForMessage) {
			//delayUS(2500); // TODO: fix
			if (value == 248) oledSelectingComplete();
			else oledDeselectionComplete();
		}
#endif
	}

	midiEngine.checkIncomingSerialMidi();
	midiEngine.flushMIDI();

	Encoders::readEncoders();

	anything = false;
	for (int e = 0; e < 4; e++) {
		if (Encoders::encoders[e].detentPos != 0) {
			encoderTestPos += Encoders::encoders[e].detentPos;
			Encoders::encoders[e].detentPos = 0;
			anything = true;
		}
	}
	for (int e = 0; e < 2; e++) {
		if (Encoders::encoders[e + 4].encPos != 0) {
			encoderTestPos += Encoders::encoders[e + 4].encPos;
			Encoders::encoders[e + 4].encPos = 0;
			anything = true;
		}
	}

	if (anything) {
		if (encoderTestPos > 128) encoderTestPos = 128;
		else if (encoderTestPos < 0) encoderTestPos = 0;

		IndicatorLEDs::setKnobIndicatorLevel(1, encoderTestPos);
	}

#if HAVE_OLED
	oledRoutine();
#endif
	uartFlushIfNotSending(UART_ITEM_PIC);
	uartFlushIfNotSending(UART_ITEM_MIDI);
}

void ramTestLED(bool stuffAlreadySetUp) {
	bool testButtonStates[9][16];
	memset(testButtonStates, 0, sizeof(testButtonStates));

	// Send CV 10V
	cvEngine.sendVoltageOut(0, 65520);
	cvEngine.sendVoltageOut(1, 65520);

#if HAVE_OLED
	OLED::clearMainImage();
	OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_TOPMOST_PIXEL, OLED_MAIN_HEIGHT_PIXELS - 1,
	                 OLED::oledMainImage);
	OLED::sendMainImage();
#endif

	midiEngine.midiThru = true;

	if (!HARDWARE_TEST_MODE) setupSquareWave();

	bufferPICPadsUart(23); // Set flash length
	bufferPICPadsUart(100);

	// Switch on numeric display
	bufferPICUart(224);
	bufferPICUart(0xFF);
	bufferPICUart(0xFF);
	bufferPICUart(0xFF);
	bufferPICUart(0xFF);

	// Switch on level indicator LEDs
	IndicatorLEDs::setKnobIndicatorLevel(0, 128);
	IndicatorLEDs::setKnobIndicatorLevel(1, 128);

	// Switch on all round-button LEDs
	for (int x = 1; x < 9; x++) {
		if (x == 4) continue; // Skip icecube LEDs
		for (int y = 0; y < 4; y++) {
			bufferPICIndicatorsUart(152 + x + y * 9 + 36);
		}
	}

	uartFlushIfNotSending(UART_ITEM_PIC);

	// Codec
	setPinAsOutput(6, 12);
	setOutputState(6, 12, 1); // Switch it on

	// Speaker / amp control
	setPinAsOutput(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2);
	setOutputState(SPEAKER_ENABLE_1, SPEAKER_ENABLE_2, 1); // Switch it on

	setPinAsInput(HEADPHONE_DETECT_1, HEADPHONE_DETECT_2); // Headphone detect
	setPinAsInput(6, 6);                                   // Line in detect
	setPinAsInput(7, 9);                                   // Mic detect

	setPinAsOutput(BATTERY_LED_1, BATTERY_LED_2);    // Battery LED control
	setOutputState(BATTERY_LED_1, BATTERY_LED_2, 1); // Switch it off (1 is off for open-drain)

	setPinMux(1, 8 + SYS_VOLT_SENSE_PIN, 1); // Analog input for voltage sense

	setPinAsInput(ANALOG_CLOCK_IN_1, ANALOG_CLOCK_IN_2); // Gate input

	setPinAsOutput(SYNCED_LED_PORT, SYNCED_LED_PIN);    // Synced LED
	setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, 0); // Switch it off

	// Line out detect pins
	setPinAsInput(LINE_OUT_DETECT_L_1, LINE_OUT_DETECT_L_2);
	setPinAsInput(LINE_OUT_DETECT_R_1, LINE_OUT_DETECT_R_2);

	// Test the RAM
	uint32_t* address;
	bool ledState = true;

	while (1) {

		sendColoursForHardwareTest(testButtonStates);

		hardwareTestWhichColour = (hardwareTestWhichColour + 1) % 3;

		// Write Synced LED
		setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, true);

		// Write gate outputs
		for (int i = 0; i < NUM_GATE_CHANNELS; i++) {
			cvEngine.gateChannels[i].on = ledState;
			cvEngine.physicallySwitchGate(i);
		}

		// Send MIDI
		//midiEngine.sendNote(ledState, 50, 64, 0, true);

		ledState = !ledState;

		address = (uint32_t*)0x0C000000;
		while (address != (uint32_t*)0x10000000) {

			if (((uint32_t)address & 4095) == 0) {
				readInputsForHardwareTest(testButtonStates);
				//AudioEngine::routine(false);
			}
			*address = (uint32_t)address;
			address++;
		}

		setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, false);

		address = (uint32_t*)0x0C000000;
		while (address != (uint32_t*)0x10000000) {
			if (((uint32_t)address & 4095) == 0) {
				readInputsForHardwareTest(testButtonStates);
				//AudioEngine::routine(false);
			}

			if (*address != (uint32_t)address) {

				// Error!!!
				while (1) {
					readInputsForHardwareTest(testButtonStates);

					setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, true);
					delayMS(50);
					delayMS(50);
					setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, false);
					delayMS(50);
					delayMS(50);
					setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, true);
					delayMS(50);
					delayMS(50);
					setOutputState(SYNCED_LED_PORT, SYNCED_LED_PIN, false);
					delayMS(50);
					delayMS(50);
					delayMS(50);
					delayMS(50);
					delayMS(50);
					delayMS(50);
					delayMS(50);
					delayMS(50);
					delayMS(50);
					delayMS(50);
				}
			}
			address++;
		}
	}
}
#endif

#if AUTOPILOT_TEST_ENABLED
#define AUTOPILOT_NONE 0
#define AUTOPILOT_HOLDING_EDIT_PAD 1
#define AUTOPILOT_HOLDING_AUDITION_PAD 2
#define AUTOPILOT_IN_MENU 3
#define AUTOPILOT_IN_SONG_SAVER 4
#define AUTOPILOT_IN_SONG_LOADER 5

int autoPilotMode = 0;
int autoPilotX;
int autoPilotY;

uint32_t timeNextAutoPilotAction = 0;

void autoPilotStuff() {

	if (!playbackHandler.recording) return;

	int timeTilNextAction = timeNextAutoPilotAction - AudioEngine::audioSampleTimer;
	if (timeTilNextAction > 0) return;

	int randThing;

	switch (autoPilotMode) {

	case 0:

		if (true) { //getCurrentUI() == &instrumentClipView && currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
			if (!currentUIMode) {
				randThing = getRandom255();

				// Maybe press an edit pad?
				if (randThing < 70) {
					autoPilotMode = AUTOPILOT_HOLDING_EDIT_PAD;
					autoPilotX = getRandom255() % displayWidth;
					autoPilotY = getRandom255() % displayHeight;

					matrixDriver.padAction(autoPilotX, autoPilotY, true);
				}

				// Or an audition pad?
				else if (randThing < 180) {
					autoPilotMode = AUTOPILOT_HOLDING_AUDITION_PAD;
					autoPilotY = getRandom255() % displayHeight;
					matrixDriver.padAction(displayWidth + 1, autoPilotY, true);
				}

				// Or change sample mode
				else if (randThing < 220) {
					Buttons::buttonAction(shiftButtonX, shiftButtonY, true, false);
					matrixDriver.padAction(0, getRandom255() % 4, true);
					Buttons::buttonAction(shiftButtonX, shiftButtonY, false, false);

					autoPilotMode = AUTOPILOT_IN_MENU;
				}

				// Or toggle playback
				else if (randThing < 230) {
					Buttons::buttonAction(playButtonX, playButtonY, true, false);
				}

				// Or save song
				/*
				else {
					autoPilotMode = AUTOPILOT_IN_SONG_SAVER;
					Buttons::buttonAction(saveButtonX, saveButtonY, true, false);
					Buttons::buttonAction(saveButtonX, saveButtonY, false, false);

					QwertyUI::enteredText.set("T001");

					//saveSongUI.performSave(true);
				}
				*/

				// Or load song
				else {
					autoPilotMode = AUTOPILOT_IN_SONG_LOADER;
					openUI(&loadSongUI);
				}
			}
		}
		break;

	case AUTOPILOT_HOLDING_EDIT_PAD:
		autoPilotMode = AUTOPILOT_NONE;
		matrixDriver.padAction(autoPilotX, autoPilotY, false);
		break;

	case AUTOPILOT_HOLDING_AUDITION_PAD:
		randThing = getRandom255();

		// Maybe just release it
		if (randThing < 128) {
			autoPilotMode = AUTOPILOT_NONE;
			matrixDriver.padAction(displayWidth + 1, autoPilotY, false);
		}

		// Or maybe load a sample
		else {
			autoPilotMode = AUTOPILOT_IN_MENU;
			Buttons::buttonAction(kitButtonX, kitButtonY, true, false);
		}

		break;

	case AUTOPILOT_IN_MENU:

		// Maybe we already actually exited
		if (getCurrentUI() == getRootUI()) {
			autoPilotMode = AUTOPILOT_NONE;
			break;
		}

		randThing = getRandom255();

		// Maybe turn knob
		if (randThing < 200) {
			randThing = getRandom255();
			int direction = (randThing >= 128) ? 1 : -1;
			getCurrentUI()->selectEncoderAction(direction);
		}

		// Maybe press back
		else if (randThing < 220) {
			Buttons::buttonAction(backButtonX, backButtonY, true, false);
		}

		// Maybe press encoder button
		else {
			Buttons::buttonAction(selectEncButtonX, selectEncButtonY, true, false);
		}

		break;

	case AUTOPILOT_IN_SONG_SAVER:

		// Maybe we already actually exited
		if (getCurrentUI() == getRootUI()) {
			autoPilotMode = AUTOPILOT_NONE;
			break;
		}

		Buttons::buttonAction(saveButtonX, saveButtonY, true, false);
		Buttons::buttonAction(saveButtonX, saveButtonY, false, false);

		break;

	case AUTOPILOT_IN_SONG_LOADER:

		if (currentUIMode) break;

		// Maybe we already actually exited
		if (getCurrentUI() == getRootUI()) {
			autoPilotMode = AUTOPILOT_NONE;
			break;
		}

		randThing = getRandom255();

		// Maybe turn knob
		if (randThing < 200) {
			randThing = getRandom255();
			int direction = (randThing >= 128) ? 1 : -1;
			getCurrentUI()->selectEncoderAction(direction);
		}

		// Maybe press back
		else if (randThing < 220) {
			Buttons::buttonAction(backButtonX, backButtonY, true, false);
		}

		// Maybe press load button
		else {
			//matrixDriver.buttonAction(loadButtonX, loadButtonY, true, false);
			//matrixDriver.buttonAction(loadButtonX, loadButtonY, false, false);

			loadSongUI.performLoad();
		}

		break;
	}

	timeNextAutoPilotAction = AudioEngine::audioSampleTimer + getRandom255() * 100;
}

#endif
