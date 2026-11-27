#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

class DX7Cartridge;
class Sound;

namespace deluge::gui {

/// @brief The DX7 bank last read from a `.syx` file: what the cartridge page lists and loads programs from. Null until
///        the first read; a read allocates it once and reuses it after.
DX7Cartridge*& dxCartridgeBank();

/// @brief Read @p path and parse it into the bank.
/// @return True if a cartridge was parsed (a checksum failure still loads, after its popup).
bool loadDxCartridgeFile(std::string_view path);

/// @brief Parse an already-read cartridge into the bank, with the same popups as `loadDxCartridgeFile`.
/// @param bytes The cartridge's opening bytes. Writable: `DX7Cartridge::load` unpacks in place.
/// @return True if a cartridge was parsed (a checksum failure still loads, after its popup).
bool loadDxCartridgeBytes(std::span<std::byte> bytes);

/// @brief Load program @p program of @p cartridge into @p sound's DX7: its patch unpacked over the DX7's, its voices
///        cut, and an unsaved synth renamed to the program.
/// @param sound     The sound; nothing is loaded unless it is in DX7 mode.
/// @param cartridge The bank.
/// @param program   The program index, below `cartridge.numPatches()`.
void loadDxProgram(::Sound& sound, DX7Cartridge& cartridge, int32_t program);

} // namespace deluge::gui
