#pragma once
#include <cstdint>
#include <vector>

/// Host-build doubles for the UART and USB symbols the MIDI queue manager writes through.
///
/// The drain path talks to hardware via three free symbols. Defining them here rather than injecting an
/// interface keeps the production code free of a test seam and costs no indirection on the real-time
/// path; the substitution happens at link time.
namespace MidiTransportMock {
/// Clears captured bytes and restores default UART space.
void reset();
/// Every byte the queue manager handed to bufferMIDIUart(), in order.
std::vector<uint8_t> const& sent_bytes();
/// Sets the space uartGetTxBufferSpace() reports, to exercise the pacing paths.
void set_uart_space(int32_t space);
} // namespace MidiTransportMock
