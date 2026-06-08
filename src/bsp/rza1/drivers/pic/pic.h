#pragma once
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stdint.h>

extern "C" {
#include "RZA1/cpu_specific.h"
#include "RZA1/mtu/mtu.h"
#include "RZA1/system/iodefines/dmac_iodefine.h"
#include "RZA1/uart/sio_char.h"
#include "drivers/uart/uart.h"
}

/**
 * @brief The static class for interacting with the PIC peripheral
 *
 */
class PIC {
	static constexpr uint32_t kUartFullSpeedPadsHz = 200000; // 400000 glitches sometimes, especially if you zoom lots

	enum class Message : uint8_t {
		NONE = 0,

		SET_COLOUR_FOR_TWO_COLUMNS = 1,
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

public:
	/**
	 * @brief The response received from the PIC
	 */
	enum class Response : uint8_t { // Technically matches Message except in some spots????
		NONE = 0,

		UNKNOWN_BOOT_RESPONSE = 129,

		RESET_SETTINGS = 175,
		FIRMWARE_VERSION_NEXT = 245,
		UNKNOWN_OLED_RELATED_COMMAND = 246,
		SET_DC_HIGH = 251,

		NEXT_PAD_OFF = 252,
		UNKNOWN_BREAK = 253,
		NO_PRESSES_HAPPENING = 254,
	};
	static constexpr Response kPadAndButtonMessagesEnd{180};

	/**
	 * @brief Change the UART baud rate to the output speed safe for rapid pad updates
	 */
	static void setupForPads() { uartSetBaudRate(UART_CHANNEL_PIC, kUartFullSpeedPadsHz); }

	/**
	 * @brief Set the Colour for two columns of leds
	 * @details This allows updating the Deluge main pads in groups of columns,
	 *          as though the pair were a continuous strip of 16 (rather than 8) LEDs
	 *
	 * @param idx The column-pair idx (so half the number of squares from the left)
	 * @param colours The colours to set the pads to
	 */
	static void setColourForTwoColumns(size_t idx, const std::array<RGB, kDisplayHeight * 2>& colours) {
		send(util::to_underlying(Message::SET_COLOUR_FOR_TWO_COLUMNS) + idx);
		for (const RGB& colour : colours) {
			send(colour);
		}
	}

	/**
	 * @brief Set the PIC's debounce time in milliseconds.
	 *        This prevents extra keypresses from happening when the switch is not fully closed.
	 * @see https://learn.adafruit.com/make-it-switch/debouncing
	 *
	 * @param time_ms The number of milliseconds to use debounce using
	 */
	static void setDebounce(uint8_t time_ms) { send(Message::SET_DEBOUNCE_TIME, time_ms); }

	/**
	 * @brief This sets the indicator for the gold knobs,
	 *
	 * @param which The knob to set
	 * @param indicator An array of brightness values for each LED
	 */
	static void setGoldKnobIndicator(bool which, const std::array<uint8_t, kNumGoldKnobIndicatorLEDs>& indicator) {
		Message knob = which ? Message::SET_GOLD_KNOB_1_INDICATORS : Message::SET_GOLD_KNOB_0_INDICATORS;
		send(knob, indicator);
	}

	static void setLEDOff(size_t idx) { send(util::to_underlying(Message::SET_LED_OFF) + idx); }
	static void setLEDOn(size_t idx) { send(util::to_underlying(Message::SET_LED_ON) + idx); }

	/**
	 * @brief Request that the PIC resend all button states
	 */
	static void resendButtonStates() { send(Message::RESEND_BUTTON_STATES); }

	static void setMinInterruptInterval(uint8_t time_ms) { send(Message::SET_MIN_INTERRUPT_INTERVAL, time_ms); }
	static void setFlashLength(uint8_t time_ms) { send(Message::SET_FLASH_LENGTH, time_ms); }

	static void setUARTSpeed() {
		uint8_t speed = 4000000.0f / kUartFullSpeedPadsHz - 0.5f; // Speed is 4MHz / (x + 1)
		send(Message::SET_UART_SPEED, speed);
	}

	static void flashMainPad(size_t idx) { send(util::to_underlying(Message::SET_PAD_FLASHING) + idx); }

	/**
	 * @brief Flash a pad using the PIC's built-in timer and colour system
	 *
	 * @param idx The pad to flash
	 * @param colour_idx The colour "index" of colours the PIC knows
	 */
	static void flashMainPadWithColourIdx(size_t idx, int32_t colour_idx) {
		send(util::to_underlying(Message::SET_FLASH_COLOR) + colour_idx);
		flashMainPad(idx);
	}

	static void update7SEG(const std::array<uint8_t, kNumericDisplayLength>& display) {
		send(Message::UPDATE_SEVEN_SEGMENT_DISPLAY, display);
	}

	static void enableOLED() { send(Message::ENABLE_OLED); }
	static void selectOLED() { send(Message::SELECT_OLED); }
	static void deselectOLED() { send(Message::DESELECT_OLED); }
	static void setDCLow() { send(Message::SET_DC_LOW); }
	static void setDCHigh() { send(Message::SET_DC_HIGH); }

	static void requestFirmwareVersion() { send(Message::REQUEST_FIRMWARE_VERSION); }

	static void sendColour(const RGB& colour) { send(colour); }

	static void setRefreshTime(uint8_t time_ms) { send(Message::SET_REFRESH_TIME, time_ms); }
	static void setDimmerInterval(uint8_t interval) { send(Message::SET_DIMMER_INTERVAL, interval); }

	/**
	 * @brief This is used to make smooth scrolling on the horizontal axis, by filling up an 8-pixel framebuffer on the
	 * PIC that it can then provide an animation to smear across the axis to give a sensation of motion See (scrolling
	 * between clip pages)
	 *
	 * @param idx the row to set
	 */
	static void sendScrollRow(size_t idx, RGB colour) {
		send(util::to_underlying(Message::SET_SCROLL_ROW) + idx);
		send(colour);
	}

	static void setupHorizontalScroll(uint8_t bitflags) {
		send(util::to_underlying(Message::SET_SCROLL_LEFT) + bitflags);
	}

	static void doVerticalScroll(bool direction, const std::array<RGB, kDisplayWidth + kSideBarWidth>& colours) {
		Message msg = direction ? Message::SET_SCROLL_UP : Message::SET_SCROLL_DOWN;
		send(msg, colours);
	}

	static void doneSendingRows() { send(Message::DONE_SENDING_ROWS); }

	/**
	 * @brief Flush the UART buffer if it's not already being done
	 */
	static void flush() { uartFlushIfNotSending(UART_ITEM_PIC); }

	/**
	 * @brief Wait for the last flush() operation to complete (blocking)
	 */
	static void waitForFlush() {
		while (!(DMACn(PIC_TX_DMA_CHANNEL).CHSTAT_n & (1 << 6))) {}
	}

	/**
	 * @brief Read a single response from the PIC
	 *
	 * @return Response The PIC response message (Response::NONE if no byte immediately available)
	 */
	static Response read() {
		uint8_t value;
		uartGetChar(UART_ITEM_PIC, (char*)&value);
		return Response{value};
	}

	/**
	 * @brief Fetch a response from the PIC (blocking)
	 *
	 */
	static Response read(uint32_t timeout) {
		uint16_t timeWaitBegan = *TCNT[TIMER_SYSTEM_FAST];
		while ((uint16_t)(*TCNT[TIMER_SYSTEM_FAST] - timeWaitBegan) < timeout) {
			Response value = PIC::read();
			if (value != Response::NONE) {
				return value;
			}
		}
		return Response::NONE;
	}

	/**
	 * @brief Fetch a response from the PIC, using a callback handler
	 *
	 * @param handler The response handler function. If the return value is nonzero, the read attempt will immediately
	 * halt;
	 * @param timeout The timeout to run for. Can be cancelled by the callback returning a nonzero value.
	 */
	static int32_t read(uint32_t timeout, std::function<int32_t(Response)> handler) {
		uint16_t timeWaitBegan = *TCNT[TIMER_SYSTEM_FAST];
		int32_t result = 1; // error with failure by default
		while ((uint16_t)(*TCNT[TIMER_SYSTEM_FAST] - timeWaitBegan) < timeout) {
			result = handler(PIC::read());
			if (result != 0) {
				return result;
			}
		}
		return result;
	}

private:
	/**
	 * @brief Send a message and a variable number of following bytes
	 */
	template <typename... Bytes>
	inline static void send(Message msg, Bytes... bytes) {
		send(msg);
		for (const uint8_t byte : {bytes...}) {
			send(byte);
		}
	}

	/**
	 * @brief Send a message followed by all the objects in the provided array
	 */
	template <typename T, size_t size>
	inline static void send(Message msg, const std::array<T, size>& bytes) {
		send(msg);
		for (auto byte : bytes) {
			send(byte);
		}
	}

	/**
	 * @brief Send a single message
	 */
	inline static void send(Message msg) { send(util::to_underlying(msg)); }

	/**
	 * @brief Send a single colour
	 */
	inline static void send(const RGB& colour) {
		send(colour.r);
		send(colour.g);
		send(colour.b);
	}

	/**
	 * @brief Send a byte. This was originally bufferPICUart()
	 */
	inline static void send(uint8_t msg) {
		intptr_t writePos = uartItems[UART_ITEM_PIC].txBufferWritePos;
		volatile char* uncached_tx_buf = (volatile char*)(picTxBuffer + UNCACHED_MIRROR_OFFSET);
		uncached_tx_buf[writePos] = msg;
		uartItems[UART_ITEM_PIC].txBufferWritePos += 1;
		uartItems[UART_ITEM_PIC].txBufferWritePos &= (PIC_TX_BUFFER_SIZE - 1);
	}
};
