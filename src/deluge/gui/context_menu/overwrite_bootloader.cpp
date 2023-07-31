/*
 * Copyright © 2022-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/overwrite_bootloader.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"
#include "memory/general_memory_allocator.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

extern "C" {
#include "RZA1/spibsc/r_spibsc_flash_api.h"
#include "RZA1/spibsc/spibsc.h"
}

namespace deluge::gui::context_menu {
OverwriteBootloader overwriteBootloader{};

char const* OverwriteBootloader::getTitle() {
	static char const* title = "Overwrite bootloader at own risk";
	return title;
}

Sized<char const**> OverwriteBootloader::getOptions() {
#if HAVE_OLED
	static char const* options[] = {"Accept risk"};
#else
	static char const* options[] = {"Sure"};
#endif
	return {options, 1};
}

constexpr size_t FLASH_WRITE_SIZE = 256; // Bigger doesn't seem to work...

bool OverwriteBootloader::acceptCurrentOption() {

#if !HAVE_OLED
	numericDriver.displayLoadingAnimation();
#endif

	int error = storageManager.initSD();
	if (error) {
gotError:
		numericDriver.displayError(error);
		return false;
	}

	FRESULT result = f_opendir(&staticDIR, "");
	if (result) {
gotFresultError:
		error = fresultToDelugeErrorCode(result);
		goto gotError;
	}

	char const* errorMessage;

	if (false) {
longError:
		numericDriver.displayPopup(errorMessage);
		return false;
	}

	while (true) {

		FILINFO fno;
		result = f_readdir(&staticDIR, &fno); // Read a directory item
		if (result != FR_OK || fno.fname[0] == 0) {
			break; // Break on error or end of dir
		}

		if (fno.fname[0] == '_') {
			continue; // Avoid hidden files created by stupid Macs
		}

		// Only bootloader bin files should start with "BOOT"
		if (memcasecmp(fno.fname, "BOOT", 4)) {
			continue;
		}

		char const* dotPos = strchr(fno.fname, '.');
		if (dotPos != 0 && !strcasecmp(dotPos, ".BIN")) {

			// We found our .bin file!
			//displayPrompt("Found file");

			uint32_t fileSize = fno.fsize;

			f_closedir(&staticDIR);

			// But make sure it's not too big
			if (fileSize > 0x80000 - 0x1000) {
				errorMessage = HAVE_OLED ? "Bootloader file too large" : "BIG";
				goto longError;
			}

			// Or to small
			if (fileSize < 1024) {
				errorMessage = HAVE_OLED ? "Bootloader file too small" : "SMALL";
				goto longError;
			}

			// Allocate RAM
			uint8_t* buffer = (uint8_t*)GeneralMemoryAllocator::get().alloc(fileSize, NULL, false, true);
			if (!buffer) {
				error = ERROR_INSUFFICIENT_RAM;
				goto gotError;
			}

			FIL currentFile;
			// Open the file
			result = f_open(&currentFile, fno.fname, FA_READ);
			if (result != FR_OK) {
gotFresultErrorAfterAllocating:
				GeneralMemoryAllocator::get().dealloc(buffer);
				goto gotFresultError;
			}

			// The file opened. Copy it to RAM
			UINT numBytesRead;
			result = f_read(&currentFile, buffer, fileSize, &numBytesRead);
			if (result != FR_OK) {
				goto gotFresultErrorAfterAllocating;
			}
			f_close(&currentFile);

			if (numBytesRead != fileSize) { // Can this happen?
				error = ERROR_SD_CARD;
				GeneralMemoryAllocator::get().dealloc(buffer);
				goto gotError;
			}

			// Erase the flash memory
			uint32_t numFlashSectors = ((fileSize - 1) >> 16) + 1;

			uint32_t startFlashAddress = 0;

			char const* workingMessage = "Overwriting. Don't switch off";

			if (false) {
gotFlashError:
#if HAVE_OLED
				OLED::removeWorkingAnimation();
				workingMessage = "Flash error. Trying again. Don't switch off";
#else
				numericDriver.displayPopup("RETR");
#endif
			}

#if HAVE_OLED
			OLED::displayWorkingAnimation(workingMessage);
#endif

			uint32_t eraseAddress = startFlashAddress;
			while (numFlashSectors-- && eraseAddress < 0x01000000) {
				int32_t flashError =
				    R_SFLASH_EraseSector(eraseAddress, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, 1, SPIBSC_OUTPUT_ADDR_24);
				if (flashError) {
					goto gotFlashError;
				}
				eraseAddress += 0x10000; // 64K
			}

			// Copy new program from RAM to flash memory
			uint32_t flashWriteAddress = startFlashAddress;
			uint8_t* readAddress = buffer;

			while (true) {

				int bytesLeft = startFlashAddress + fileSize - flashWriteAddress;
				if (bytesLeft <= 0) {
					break;
				}

				int bytesToWrite = bytesLeft;
				if (bytesToWrite > FLASH_WRITE_SIZE) {
					bytesToWrite = FLASH_WRITE_SIZE;
				}

				int32_t error = R_SFLASH_ByteProgram(flashWriteAddress, readAddress, bytesToWrite, SPIBSC_CH,
				                                     SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT, SPIBSC_OUTPUT_ADDR_24);
				if (error) {
					goto gotFlashError;
				}

				flashWriteAddress += FLASH_WRITE_SIZE;
				readAddress += FLASH_WRITE_SIZE;
			}

			GeneralMemoryAllocator::get().dealloc(buffer);

#if HAVE_OLED
			OLED::removeWorkingAnimation();
			OLED::consoleText("Bootloader updated");
#else
			numericDriver.displayPopup("DONE");
#endif

			return false; // We do want to exit this context menu.
		}
	}

	errorMessage = HAVE_OLED ? "No boot*.bin file found" : "FILE";
	goto longError;
}
} // namespace deluge::gui::context_menu
