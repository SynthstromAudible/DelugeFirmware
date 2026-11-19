#pragma once

/// @file
/// @brief The DIN MIDI the app sent, as hex: the engine's queue is flushed, then the bytes the host stub kept
///        (`deluge_host_midi_out_take`, `src/bsp/rust/src/host_link_stubs.rs`) are taken.

#include "io/midi/midi_engine.h"
#include "libdeluge/system.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" uint32_t deluge_host_midi_out_take(uint8_t* dst, uint32_t cap);

namespace deluge::scenes::spec::midi_out {

/// @brief Every DIN byte sent since the last take, space-separated lower-case hex ("" when none).
inline std::string take() {
	{
		CriticalSectionGuard guard;
		midiEngine.flushAllQueuedMIDI(guard);
	}
	std::string out;
	std::array<uint8_t, 256> chunk{};
	while (uint32_t n = deluge_host_midi_out_take(chunk.data(), chunk.size())) {
		for (uint32_t i = 0; i < n; i++) {
			constexpr std::string_view kDigits = "0123456789abcdef";
			if (!out.empty()) {
				out += ' ';
			}
			out += kDigits[chunk[i] >> 4];
			out += kDigits[chunk[i] & 0xF];
		}
	}
	return out;
}

/// @brief Drop whatever is queued or kept, so an example starts from silence.
inline void clear() {
	(void)take();
}

} // namespace deluge::scenes::spec::midi_out
