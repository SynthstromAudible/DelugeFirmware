#pragma once
#include "definitions_cxx.hpp"
#include "gui/colour.h"
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

extern "C" {
#include "RZA1/cpu_specific.h"
#include "RZA1/uart/sio_char.h"
#include "drivers/uart/uart.h"
}

class PIC {
public:
	enum class Response : uint8_t {
		NEXT_PAD_OFF = 252,
		NO_PRESSES_HAPPENING = 254,
	};
	static constexpr Response kPadAndButtonMessagesEnd{180};

	enum class Message : uint8_t {
		NONE = 0,

		SET_COLOUR_FOR_TWO_COLUMNS = 1, // This allows updating the Deluge main pads in groups of columns
		/* 9 of these (8 pairs of main pads, 1 pair of side pads)*/

		SET_FLASH_COLOR = 10,

		SET_DEBOUNCE_TIME = 18,
		SET_REFRESH_TIME = 19,

		SET_GOLD_KNOB_0_INDICATORS = 20,
		SET_GOLD_KNOB_1_INDICATORS = 21,
		RESEND_BUTTON_STATES = 22,
		SET_FLASH_LENGTH = 23,
		SET_PAD_FLASHING = 24,

		SET_LED_OFF = 152,
		SET_LED_ON = 188,

		UPDATE_SEVEN_SEGMENT_DISPLAY = 224,
		SET_UART_SPEED = 225,

		SET_SCROLL_ROW = 228,

		SET_SCROLL_LEFT = 236,
		SET_SCROLL_RIGHT,
		SET_SCROLL_RIGHT_FULL,
		SET_SCROLL_LEFT_FULL,

		DONE_SENDING_ROWS = 240,

		SET_SCROLL_UP = 241,
		SET_SCROLL_DOWN = 242,

		SET_DIMMER_INTERVAL = 243,
		SET_MIN_INTERRUPT_INTERVAL = 244,
		REQUEST_FIRMWARE_VERSION = 245,
		ENABLE_OLED = 247,
		SELECT_OLED = 248,
		DESELECT_OLED = 249,
		SET_DC_LOW = 250,
		SET_DC_HIGH = 251,

	};

	static void setColourForTwoColumns(size_t idx, const std::array<Colour, kDisplayHeight * 2>& colours) {
		send(util::to_underlying(Message::SET_COLOUR_FOR_TWO_COLUMNS) + idx);
		for (const Colour& colour : colours) {
			send(colour);
		}
	}
	static void setDebounce(uint8_t time_ms) { send(Message::SET_DEBOUNCE_TIME, time_ms); }
	static void setGoldKnobIndicator(bool which, const std::array<uint8_t, kNumGoldKnobIndicatorLEDs>& indicator) {
		Message knob = which ? Message::SET_GOLD_KNOB_0_INDICATORS : Message::SET_GOLD_KNOB_1_INDICATORS;
		send(knob, indicator);
	}

	static void resendButtonStates() { send(Message::RESEND_BUTTON_STATES); }

	static void setMinInterruptInterval(uint8_t time_ms) { send(Message::SET_MIN_INTERRUPT_INTERVAL, time_ms); }
	static void setFlashLength(uint8_t time_ms) { send(Message::SET_FLASH_LENGTH, time_ms); }
	static void setUARTSpeed(uint8_t speed) { send(Message::SET_UART_SPEED, speed); }

	static void flashPad(size_t idx) { send(util::to_underlying(Message::SET_PAD_FLASHING) + idx); }

	static void flashPadWithColour(size_t idx, int32_t colour) {
		send(util::to_underlying(Message::SET_FLASH_COLOR) + colour);
		flashPad(idx);
	}

	static void setLEDOff(size_t idx) { send(util::to_underlying(Message::SET_LED_OFF) + idx); }

	static void setLEDOn(size_t idx) { send(util::to_underlying(Message::SET_LED_ON) + idx); }

	static void update7SEG(const std::array<uint8_t, kNumericDisplayLength>& display) {
		send(Message::UPDATE_SEVEN_SEGMENT_DISPLAY, display);
	}

	static void enableOLED() { send(Message::ENABLE_OLED); }
	static void selectOLED() { send(Message::SELECT_OLED); }
	static void deselectOLED() { send(Message::DESELECT_OLED); }
	static void setDCLow() { send(Message::SET_DC_LOW); }
	static void setDCHigh() { send(Message::SET_DC_HIGH); }

	static void requestFirmwareVersion() { send(Message::REQUEST_FIRMWARE_VERSION); }

	static void sendColour(const Colour& colour) { send(colour); }

	static void setRefreshTime(uint8_t time_ms) { send(Message::SET_REFRESH_TIME, time_ms); }
	static void setDimmerInterval(uint8_t interval) { send(Message::SET_DIMMER_INTERVAL, interval); }

	/**
	 * @brief This is used to make smooth scrolling on the horizontal axis, by filling up an 8-pixel framebuffer on the PIC
	 *        that it can then provide an animation to smear across the axis to give a sensation of motion
	 *        See (scrolling between clip pages)
	 *
	 * @param idx the row to set
	 */
	static void sendScrollRow(size_t idx, Colour colour) {
		send(util::to_underlying(Message::SET_SCROLL_ROW) + idx);
		send(colour);
	}

	static void setupHorizontalScroll(uint8_t bitflags) {
		send(util::to_underlying(Message::SET_SCROLL_LEFT) + bitflags);
	}

	static void setupVerticalScroll(bool direction, const std::array<Colour, kDisplayWidth + kSideBarWidth>& colours) {
		Message msg = direction ? Message::SET_SCROLL_UP : Message::SET_SCROLL_DOWN;
		send(msg, colours);
	}

	static void doneSendingRows() { send(Message::DONE_SENDING_ROWS); }

	static void flush() { uartFlushIfNotSending(UART_ITEM_PIC); }

private:
	template <typename... Bytes>
	inline static void send(Message msg, Bytes... bytes) {
		send(msg);
		for (const uint8_t byte : {bytes...}) {
			send(byte);
		}
	}

	template <typename T, size_t size>
	inline static void send(Message msg, const std::array<T, size>& bytes) {
		send(msg);
		for (auto byte : bytes) {
			send(byte);
		}
	}

	inline static void send(Message msg) { send(util::to_underlying(msg)); }

	inline static void send(const Colour& colour) {
		send(colour.r);
		send(colour.g);
		send(colour.b);
	}

	inline static void send(uint8_t msg) {
		intptr_t writePos = uartItems[UART_ITEM_PIC].txBufferWritePos + UNCACHED_MIRROR_OFFSET;
		picTxBuffer[writePos] = msg;
		uartItems[UART_ITEM_PIC].txBufferWritePos += 1;
		uartItems[UART_ITEM_PIC].txBufferWritePos &= (PIC_TX_BUFFER_SIZE - 1);
	}
};
