/*
 * Copyright © 2026 Synthstrom Audible Limited
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

// The parameter dump's host writer: the stream tools/audible_window/dump.py reads.

#include "processing/sound/param_dump.h"

#if defined(DELUGE_PARAM_DUMP)

#include "modulation/params/param.h"
#include "processing/engines/audio_engine.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace deluge::param_dump {

namespace {

namespace params = deluge::modulation::params;

/// The dump file, opened at the first record when RENDER_PARAM_DUMP names one; null when it does not.
std::FILE* file() {
	static std::FILE* const out = []() -> std::FILE* {
		const char* path = std::getenv("RENDER_PARAM_DUMP");
		if (path == nullptr || *path == '\0') {
			return nullptr;
		}
		std::FILE* f = std::fopen(path, "wb");
		if (f != nullptr) {
			const uint16_t header[3] = {1, static_cast<uint16_t>(params::LOCAL_LAST),
			                            static_cast<uint16_t>(params::kNumParams - params::FIRST_GLOBAL)};
			std::fwrite("PDMP", 1, 4, f);
			std::fwrite(header, sizeof(uint16_t), 3, f);
		}
		return f;
	}();
	return out;
}

/// Each sound's number, in the order of its first record.
std::unordered_map<const void*, uint16_t>& sound_numbers() {
	static std::unordered_map<const void*, uint16_t> numbers;
	return numbers;
}
/// Each voice's note serial: its sound's note-on count when the voice's current note started.
std::unordered_map<const void*, uint32_t>& voice_serials() {
	static std::unordered_map<const void*, uint32_t> serials;
	return serials;
}
/// Each sound's note-on count.
std::unordered_map<const void*, uint32_t>& note_ons() {
	static std::unordered_map<const void*, uint32_t> counts;
	return counts;
}

void write_record(std::FILE* f, uint8_t kind, uint8_t scope, const void* sound, const void* voice, uint16_t count) {
	const auto [it, added] = sound_numbers().try_emplace(sound, static_cast<uint16_t>(sound_numbers().size()));
	const uint16_t number = it->second;
	const uint32_t time = AudioEngine::audioSampleTimer;
	const uint32_t serial = voice == nullptr ? 0 : voice_serials()[voice];
	std::fwrite(&kind, 1, 1, f);
	std::fwrite(&scope, 1, 1, f);
	std::fwrite(&number, sizeof(number), 1, f);
	std::fwrite(&time, sizeof(time), 1, f);
	std::fwrite(&serial, sizeof(serial), 1, f);
	std::fwrite(&count, sizeof(count), 1, f);
}

} // namespace

void note_on(const void* sound, const void* voice) {
	if (file() == nullptr) {
		return;
	}
	voice_serials()[voice] = ++note_ons()[sound];
}

void plane(uint8_t scope, const void* sound, const void* voice, std::span<const int32_t> finals) {
	std::FILE* f = file();
	if (f == nullptr) {
		return;
	}
	write_record(f, 0, scope, sound, voice, static_cast<uint16_t>(finals.size()));
	std::fwrite(finals.data(), sizeof(int32_t), finals.size(), f);
	std::fflush(f); // the harness ends without closing files: each record must be whole on disk
}

void voice_sources(const void* sound, const void* voice, std::span<const SourceState> states) {
	std::FILE* f = file();
	if (f == nullptr) {
		return;
	}
	write_record(f, 1, 0, sound, voice, static_cast<uint16_t>(states.size()));
	for (const SourceState& s : states) {
		const uint8_t flags[4] = {s.part, s.source, static_cast<uint8_t>(s.active), static_cast<uint8_t>(s.cache)};
		std::fwrite(flags, 1, sizeof(flags), f);
		std::fwrite(&s.phase_increment, sizeof(s.phase_increment), 1, f);
	}
	std::fflush(f);
}

} // namespace deluge::param_dump

#endif
