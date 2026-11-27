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

#include "gui/ui/browser/dx_cartridge_bank.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "memory/allocate_unique.h"
#include "memory/sdram_allocator.h"
#include "model/edits/setting_edit.h"
#include "model/song/song.h"
#include "processing/dx7/dx_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_instrument.h"
#include "processing/sound/source_resources.h"
#include "storage/DX7Cartridge.h"
#include "util/functions.h"
#include "util/try.h"
#include <memory>

namespace {

/// Everything from the size check onward: no file, no card, just bytes. Both arms end here, so the
/// popups and the checksum-tolerance rule have exactly one home.
bool parseCartridge(std::span<std::byte> readbuffer, DX7Cartridge* data) {
	using namespace deluge;
	using enum deluge::l10n::String;
	constexpr size_t minSize = kSmallSysexSize;

	if (readbuffer.size() < minSize) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	l10n::String error = data->load(readbuffer);
	if (error != EMPTY_STRING) {
		// Allow loading to continue for checksum errors, but fail for other errors
		if (error != deluge::l10n::String::STRING_FOR_DX_ERROR_CHECKSUM_FAIL) {
			display->displayPopup(l10n::get(error), 3);
			return false;
		}
	}

	return true;
}

bool openFile(std::string_view path, DX7Cartridge* data) {
	using namespace deluge;
	using enum deluge::l10n::String;
	constexpr size_t minSize = kSmallSysexSize;

	io::File file = D_TRY_CATCH_MOVE(io::File::open(path, DELUGE_FILE_READ), error, {
		return false; // fail quickly if the file doesn't exist / can't open
	});

	uint32_t filesize = D_TRY_CATCH(file.size(), error, { return false; });
	if (filesize < minSize) {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_FILE_TOO_SMALL));
	}

	l10n::String error = EMPTY_STRING;
	int readsize = std::min((int)filesize, 8192);

	std::unique_ptr buffer = D_TRY_CATCH_MOVE((allocate_unique<std::byte, memory::sdram_allocator>(readsize)), error, {
		display->displayPopup(l10n::get(STRING_FOR_DX_ERROR_READ_ERROR));
		return false;
	});

	std::span<std::byte> readbuffer =
	    D_TRY_CATCH(file.read(std::span<std::byte>(buffer.get(), static_cast<size_t>(readsize))), error, {
		    return false; //
	    });

	return parseCartridge(readbuffer, data);
}

} // namespace

namespace deluge::gui {

DX7Cartridge*& dxCartridgeBank() {
	static DX7Cartridge* bank = nullptr;
	return bank;
}

bool loadDxCartridgeFile(std::string_view path) {
	DX7Cartridge*& bank = dxCartridgeBank();
	if (bank == nullptr) {
		bank = new DX7Cartridge();
	}
	return openFile(path, bank);
}

bool loadDxCartridgeBytes(std::span<std::byte> bytes) {
	DX7Cartridge*& bank = dxCartridgeBank();
	if (bank == nullptr) {
		bank = new DX7Cartridge();
	}
	return parseCartridge(bytes, bank);
}

void loadDxProgram(::Sound& sound, DX7Cartridge& cartridge, int32_t program) {
	if (sound.getSynthMode() != SynthMode::DX7 || sound.dx7Patch.ensure() == nullptr) {
		return;
	}
	std::optional<deluge::instruments::classic::DxPatchState> patch = sound.dx7Patch.state();
	cartridge.unpackProgram(patch->params, program);
	deluge::edits::dxPatchFor(sound, *patch);
	sound.killAllVoices();
	// The synth a DX7 bank is loaded into takes the program's name until it is saved. A drum's kit keeps its own.
	if (sound.isDrum()) {
		return;
	}
	Instrument* instrument = static_cast<SoundInstrument*>(&sound);
	if (instrument->type == OutputType::SYNTH && !instrument->mightExistOnCard) {
		char name[11];
		cartridge.getProgramName(program, name);
		if (name[0] != 0) {
			deluge::edits::trackSettingFor(*instrument, "name", deluge::serialization::Value::makeScalar(name));
		}
	}
}

} // namespace deluge::gui
