#pragma once

/// @file
/// @brief The DX7 cartridge the specs and the menu oracle freezer read: a 32-program bank built in memory.

#include "storage/DX7Cartridge.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace deluge::scenes::spec {

/// @brief A 32-voice bulk dump whose program @c i is named "PROG <i>" and whose voice bytes differ per program, its
/// checksum right.
inline std::array<std::byte, kSysexSize> make_bank() {
	std::array<std::byte, kSysexSize> bank{};
	const uint8_t header[] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
	for (size_t i = 0; i < sizeof header; i++) {
		bank[i] = std::byte{header[i]};
	}
	for (int32_t voice = 0; voice < 32; voice++) {
		std::byte* v = &bank[6 + voice * 128];
		for (int32_t i = 0; i < 118; i++) {
			v[i] = std::byte{static_cast<uint8_t>((voice * 7 + i) % 32)};
		}
		std::string name = "PROG " + std::to_string(voice);
		name.resize(10, ' ');
		for (int32_t i = 0; i < 10; i++) {
			v[118 + i] = std::byte{static_cast<uint8_t>(name[i])};
		}
	}
	bank[kSysexSize - 2] = sysexChecksum({&bank[6], 4096});
	bank[kSysexSize - 1] = std::byte{0xF7};
	return bank;
}

} // namespace deluge::scenes::spec
