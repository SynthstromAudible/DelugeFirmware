#include "midi_transport_mock.h"

namespace {
std::vector<uint8_t> g_sent;
int32_t g_uart_space = 1024;
} // namespace

namespace MidiTransportMock {
void reset() {
	g_sent.clear();
	g_uart_space = 1024;
}
std::vector<uint8_t> const& sent_bytes() {
	return g_sent;
}
void set_uart_space(int32_t space) {
	g_uart_space = space;
}
} // namespace MidiTransportMock

// The queue manager includes both UART headers inside an extern "C" block, so these must have C linkage
// to resolve against the calls it emits.
extern "C" {
/// Captures a byte the queue manager staged for the DIN port, standing in for the real ring write.
void bufferMIDIUart(char charToSend) {
	g_sent.push_back(static_cast<uint8_t>(charToSend));
	if (g_uart_space > 0) {
		g_uart_space--;
	}
}

int32_t uartGetTxBufferSpace(int32_t item) {
	(void)item;
	return g_uart_space;
}
/// The real firmware sets this from the USB driver; tests hold it at "idle".
uint8_t anyUSBSendingStillHappening[2] = {0, 0};
}

/// Host no-ops for the interrupt-masking pair. The queue manager uses CriticalSectionGuard to make its
/// producer/consumer slot writes atomic against an ISR; host tests are single-threaded, so there is
/// nothing to mask. Real definitions are ARM assembly and cannot link here. C linkage, because
/// timers_interrupts.h declares them inside an extern "C" block.
extern "C" {
void ENTER_CRITICAL_SECTION() {
}
void EXIT_CRITICAL_SECTION() {
}
}

/// Set by the queue manager when USB output is waiting; the engine owns it on device.
bool anythingInUSBOutputBuffer = false;
