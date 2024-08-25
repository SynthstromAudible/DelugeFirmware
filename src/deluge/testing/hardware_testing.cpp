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

#include "testing/hardware_testing.h"
#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/root_ui.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <string.h>

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/oled/oled_low_level.h"
#include "RZA1/uart/sio_char.h"
#include "drivers/ssi/ssi.h"
#include "drivers/uart/uart.h"
}

namespace encoders = deluge::hid::encoders;

void ramTestUart() {
	// Test the RAM
	uint32_t lastErrorAt = 0;
	uint32_t* address;

	while (1) {

		// while (1) {
		D_PRINTLN("writing to ram");
		address = (uint32_t*)EXTERNAL_MEMORY_BEGIN;
		while (address != (uint32_t*)EXTERNAL_MEMORY_END) {
			*address = (uint32_t)address;
			address++;
		}
		//}

		// while (1) {
		D_PRINTLN("reading back from ram. Checking for errors every megabyte");
		address = (uint32_t*)EXTERNAL_MEMORY_BEGIN;
		while (address != (uint32_t*)EXTERNAL_MEMORY_END) {
			if (*address != (uint32_t)address) {

				uint32_t errorAtBlockNow = ((uint32_t)address) & (0xFFF00000);
				if (errorAtBlockNow != lastErrorAt) {
					while (uartGetTxBufferFullnessByItem(UART_ITEM_MIDI) > 100) {
						;
					}
					D_PRINTLN("error at  %d . got  %d", (uint32_t)address, *address);
					// while(1);
					lastErrorAt = errorAtBlockNow;
				}
			}
			address++;
		}
		//}
		D_PRINTLN("finished checking ram");
	}
}

bool inputStateLastTime = false;

bool nextIsDepress = false;
int16_t encoderTestPos = 128;

void setupSquareWave() {
	// Send square wave
	int32_t count = 0;
	for (int32_t* address = getTxBufferStart(); address < getTxBufferEnd(); address++) {
		if (count < SSI_TX_BUFFER_NUM_SAMPLES) {
			*address = std::numeric_limits<int32_t>::max();
		}
		else {
			*address = std::numeric_limits<int32_t>::min();
			;
		}

		count++;
	}
}

int32_t hardwareTestWhichColour = 0;

void sendColoursForHardwareTest(bool testButtonStates[9][16]) {
	for (int32_t x = 0; x < 9; x++) {

		std::array<RGB, 16> colours{};
		for (int32_t y = 0; y < 16; y++) {
			std::array<uint8_t, 3> raw_colour{};
			for (int32_t c = 0; c < 3; c++) {
				int32_t value = 0;
				if (testButtonStates[x][y]) {
					value = 255;
				}
				else if (c == hardwareTestWhichColour) {
					value = 64;
				}
				raw_colour[c] = value;
			}
			colours[y] = {raw_colour[0], raw_colour[1], raw_colour[2]};
		}

		PIC::setColourForTwoColumns(x, colours);
	}

	PIC::flush();
}

bool anythingProbablyPressed = false;

void readInputsForHardwareTest(bool testButtonStates[9][16]) {
	bool outputPluggedInL = readInput(LINE_OUT_DETECT_L.port, LINE_OUT_DETECT_L.pin);
	bool outputPluggedInR = readInput(LINE_OUT_DETECT_R.port, LINE_OUT_DETECT_R.pin);
	bool headphoneNow = readInput(HEADPHONE_DETECT.port, HEADPHONE_DETECT.pin);
	bool micNow = !readInput(MIC_DETECT.port, MIC_DETECT.pin);
	bool lineInNow = readInput(LINE_IN_DETECT.port, LINE_IN_DETECT.pin);
	bool gateInNow = readInput(ANALOG_CLOCK_IN.port, ANALOG_CLOCK_IN.pin);

	bool inputStateNow = (outputPluggedInL == outputPluggedInR == headphoneNow == micNow == lineInNow == gateInNow);

	if (inputStateNow != inputStateLastTime) {
		indicator_leds::setLedState(IndicatorLED::TAP_TEMPO, !inputStateNow);
		inputStateLastTime = inputStateNow;
	}

	uint8_t value;
	bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
	if (anything) {
		if (value == 252) {
			nextIsDepress = true;
		}
		else if (value < 180) {

			int32_t y = (uint32_t)value / 9;
			int32_t x = value - y * 9;
			if (y < kDisplayHeight * 2) {

				testButtonStates[x][y] = !nextIsDepress;
				sendColoursForHardwareTest(testButtonStates);
			}

			if (nextIsDepress) {

				// Send silence
				if (!HARDWARE_TEST_MODE) {
					int32_t count = 0;
					for (int32_t* address = getTxBufferStart(); address < getTxBufferEnd(); address++) {
						*address = 1024;
						count++;
					}
				}

				nextIsDepress = false;
				anythingProbablyPressed = false;
			}
			else {
				if (!HARDWARE_TEST_MODE) {
					setupSquareWave();
				}

				anythingProbablyPressed = true;
			}
		}
		else if (value == oledWaitingForMessage && display->haveOLED()) {
			// delayUS(2500); // TODO: fix
			if (value == 248) {
				oledSelectingComplete();
			}
			else {
				oledDeselectionComplete();
			}
		}
	}

	midiEngine.checkIncomingSerialMidi();
	midiEngine.flushMIDI();

	encoders::readEncoders();

	anything = false;
	for (int32_t e = 0; e < 4; e++) {
		auto& encoder = deluge::hid::encoders::getEncoder(static_cast<deluge::hid::encoders::EncoderName>(e));
		if (encoder.detentPos != 0) {
			encoderTestPos += encoder.detentPos;
			encoder.detentPos = 0;
			anything = true;
		}
	}
	for (int32_t e = 0; e < 2; e++) {
		auto& encoder = deluge::hid::encoders::getEncoder(static_cast<deluge::hid::encoders::EncoderName>(e + 4));
		if (encoder.encPos != 0) {
			encoderTestPos += encoder.encPos;
			encoder.encPos = 0;
			anything = true;
		}
	}

	if (anything) {
		if (encoderTestPos > 128) {
			encoderTestPos = 128;
		}
		else if (encoderTestPos < 0) {
			encoderTestPos = 0;
		}

		indicator_leds::setKnobIndicatorLevel(1, encoderTestPos);
	}

	if (display->haveOLED()) {
		oledRoutine();
	}
	PIC::flush();
	uartFlushIfNotSending(UART_ITEM_MIDI);
}

void ramTestLED(bool stuffAlreadySetUp) {
	bool testButtonStates[9][16];
	memset(testButtonStates, 0, sizeof(testButtonStates));

	// Send CV 10V
	cvEngine.sendVoltageOut(0, 65520);
	cvEngine.sendVoltageOut(1, 65520);

	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();
		deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;

		canvas.invertArea(0, OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_TOPMOST_PIXEL, OLED_MAIN_HEIGHT_PIXELS - 1);
		deluge::hid::display::OLED::sendMainImage();
	}

	midiEngine.midiThru = true;

	if (!HARDWARE_TEST_MODE) {
		setupSquareWave();
	}

	PIC::setFlashLength(100);

	// Switch on numeric display
	PIC::update7SEG({0xFF, 0xFF, 0xFF, 0xFF});

	// Switch on level indicator LEDs
	indicator_leds::setKnobIndicatorLevel(0, 128);
	indicator_leds::setKnobIndicatorLevel(1, 128);

	// Switch on all round-button LEDs
	for (int32_t x = 1; x < 9; x++) {
		if (x == 4) {
			continue; // Skip icecube LEDs
		}
		for (int32_t y = 0; y < 4; y++) {
			PIC::setLEDOn(x + y * 9);
		}
	}

	PIC::flush();

	// Codec
	setPinAsOutput(CODEC.port, CODEC.pin);
	setOutputState(CODEC.port, CODEC.pin, 1); // Switch it on

	// Speaker / amp control
	setPinAsOutput(SPEAKER_ENABLE.port, SPEAKER_ENABLE.pin);
	setOutputState(SPEAKER_ENABLE.port, SPEAKER_ENABLE.pin, 1); // Switch it on

	setPinAsInput(HEADPHONE_DETECT.port, HEADPHONE_DETECT.pin); // Headphone detect
	setPinAsInput(LINE_IN_DETECT.port, LINE_IN_DETECT.pin);     // Line in detect
	setPinAsInput(MIC_DETECT.port, MIC_DETECT.pin);             // Mic detect

	setPinAsOutput(BATTERY_LED.port, BATTERY_LED.pin);    // Battery LED control
	setOutputState(BATTERY_LED.port, BATTERY_LED.pin, 1); // Switch it off (1 is off for open-drain)

	setPinMux(VOLT_SENSE.port, VOLT_SENSE.pin, 1); // Analog input for voltage sense

	setPinAsInput(ANALOG_CLOCK_IN.port, ANALOG_CLOCK_IN.pin); // Gate input

	setPinAsOutput(SYNCED_LED.port, SYNCED_LED.pin);    // Synced LED
	setOutputState(SYNCED_LED.port, SYNCED_LED.pin, 0); // Switch it off

	// Line out detect pins
	setPinAsInput(LINE_OUT_DETECT_L.port, LINE_OUT_DETECT_L.pin);
	setPinAsInput(LINE_OUT_DETECT_R.port, LINE_OUT_DETECT_R.pin);

	// Test the RAM
	uint32_t* address;
	bool ledState = true;

	while (1) {

		sendColoursForHardwareTest(testButtonStates);

		hardwareTestWhichColour = (hardwareTestWhichColour + 1) % 3;

		// Write Synced LED
		setOutputState(SYNCED_LED.port, SYNCED_LED.pin, true);

		// Write gate outputs
		for (int32_t i = 0; i < NUM_GATE_CHANNELS; i++) {
			cvEngine.gateChannels[i].on = ledState;
			cvEngine.physicallySwitchGate(i);
		}

		// Send MIDI
		// midiEngine.sendNote(ledState, 50, 64, 0, true);

		ledState = !ledState;

		address = (uint32_t*)0x0C000000;
		while (address != (uint32_t*)0x10000000) {

			if (((uint32_t)address & 4095) == 0) {
				readInputsForHardwareTest(testButtonStates);
				// AudioEngine::routine(false);
			}
			*address = (uint32_t)address;
			address++;
		}

		setOutputState(SYNCED_LED.port, SYNCED_LED.pin, false);

		address = (uint32_t*)0x0C000000;
		while (address != (uint32_t*)0x10000000) {
			if (((uint32_t)address & 4095) == 0) {
				readInputsForHardwareTest(testButtonStates);
				// AudioEngine::routine(false);
			}

			if (*address != (uint32_t)address) {

				// Error!!!
				while (1) {
					readInputsForHardwareTest(testButtonStates);

					setOutputState(SYNCED_LED.port, SYNCED_LED.pin, true);
					delayMS(50);
					delayMS(50);
					setOutputState(SYNCED_LED.port, SYNCED_LED.pin, false);
					delayMS(50);
					delayMS(50);
					setOutputState(SYNCED_LED.port, SYNCED_LED.pin, true);
					delayMS(50);
					delayMS(50);
					setOutputState(SYNCED_LED.port, SYNCED_LED.pin, false);
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

#if AUTOPILOT_TEST_ENABLED
#define AUTOPILOT_NONE 0
#define AUTOPILOT_HOLDING_EDIT_PAD 1
#define AUTOPILOT_HOLDING_AUDITION_PAD 2
#define AUTOPILOT_IN_MENU 3
#define AUTOPILOT_IN_SONG_SAVER 4
#define AUTOPILOT_IN_SONG_LOADER 5

int32_t autoPilotMode = 0;
int32_t autoPilotX;
int32_t autoPilotY;

uint32_t timeNextAutoPilotAction = 0;

void autoPilotStuff() {
	using namespace deluge::hid::button;

	if (!playbackHandler.recording)
		return;

	int32_t timeTilNextAction = timeNextAutoPilotAction - AudioEngine::audioSampleTimer;
	if (timeTilNextAction > 0)
		return;

	int32_t randThing;

	switch (autoPilotMode) {

	case 0:

		if (true) { // getCurrentUI() == &instrumentClipView && getCurrentOutputType() == OutputType::KIT) {
			if (!currentUIMode) {
				randThing = getRandom255();

				// Maybe press an edit pad?
				if (randThing < 70) {
					autoPilotMode = AUTOPILOT_HOLDING_EDIT_PAD;
					autoPilotX = getRandom255() % kDisplayWidth;
					autoPilotY = getRandom255() % kDisplayHeight;

					matrixDriver.padAction(autoPilotX, autoPilotY, true);
				}

				// Or an audition pad?
				else if (randThing < 180) {
					autoPilotMode = AUTOPILOT_HOLDING_AUDITION_PAD;
					autoPilotY = getRandom255() % kDisplayHeight;
					matrixDriver.padAction(kDisplayWidth + 1, autoPilotY, true);
				}

				// Or change sample mode
				else if (randThing < 220) {
					Buttons::buttonAction(SHIFT, true, false);
					matrixDriver.padAction(0, getRandom255() % 4, true);
					Buttons::buttonAction(SHIFT, false, false);

					autoPilotMode = AUTOPILOT_IN_MENU;
				}

				// Or toggle playback
				else if (randThing < 230) {
					Buttons::buttonAction(PLAY, true, false);
				}

				// Or save song
				/*
				else {
				    autoPilotMode = AUTOPILOT_IN_SONG_SAVER;
				    Buttons::buttonAction(SAVE, true, false);
				    Buttons::buttonAction(SAVE, false, false);

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
			matrixDriver.padAction(kDisplayWidth + 1, autoPilotY, false);
		}

		// Or maybe load a sample
		else {
			autoPilotMode = AUTOPILOT_IN_MENU;
			Buttons::buttonAction(KIT, true, false);
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
			int32_t direction = (randThing >= 128) ? 1 : -1;
			getCurrentUI()->selectEncoderAction(direction);
		}

		// Maybe press back
		else if (randThing < 220) {
			Buttons::buttonAction(BACK, true, false);
		}

		// Maybe press encoder button
		else {
			Buttons::buttonAction(SELECT_ENC, true, false);
		}

		break;

	case AUTOPILOT_IN_SONG_SAVER:

		// Maybe we already actually exited
		if (getCurrentUI() == getRootUI()) {
			autoPilotMode = AUTOPILOT_NONE;
			break;
		}

		Buttons::buttonAction(SAVE, true, false);
		Buttons::buttonAction(SAVE, false, false);

		break;

	case AUTOPILOT_IN_SONG_LOADER:

		if (currentUIMode)
			break;

		// Maybe we already actually exited
		if (getCurrentUI() == getRootUI()) {
			autoPilotMode = AUTOPILOT_NONE;
			break;
		}

		randThing = getRandom255();

		// Maybe turn knob
		if (randThing < 200) {
			randThing = getRandom255();
			int32_t direction = (randThing >= 128) ? 1 : -1;
			getCurrentUI()->selectEncoderAction(direction);
		}

		// Maybe press back
		else if (randThing < 220) {
			Buttons::buttonAction(BACK, true, false);
		}

		// Maybe press load button
		else {
			// matrixDriver.buttonAction(LOAD, true, false);
			// matrixDriver.buttonAction(LOAD, false, false);

			loadSongUI.performLoad();
		}

		break;
	}

	timeNextAutoPilotAction = AudioEngine::audioSampleTimer + getRandom255() * 100;
}

#endif
