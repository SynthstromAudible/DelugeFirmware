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

#include <ContextMenuOverwriteBootloader.h>
#include "storagemanager.h"
#include "numericdriver.h"
#include "functions.h"
#include "GeneralMemoryAllocator.h"
#include "oled.h"

extern "C" {
#include "spibsc.h"
#include "r_spibsc_flash_api.h"
}

ContextMenuOverwriteBootloader contextMenuOverwriteBootloader;

ContextMenuOverwriteBootloader::ContextMenuOverwriteBootloader() {
#if HAVE_OLED
	title = "Overwrite bootloader at own risk";
#endif
}

char const** ContextMenuOverwriteBootloader::getOptions() {
#if HAVE_OLED
	static char const* options[] = {"Accept risk"};
#else
	static char const* options[] = {"Sure"};
#endif
	return options;
}

#define FLASH_WRITE_SIZE 256 // Bigger doesn't seem to work...

bool ContextMenuOverwriteBootloader::acceptCurrentOption() {

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
		result = f_readdir(&staticDIR, &fno);            // Read a directory item
		if (result != FR_OK || fno.fname[0] == 0) break; // Break on error or end of dir

		if (fno.fname[0] == '_') continue; // Avoid hidden files created by stupid Macs

		// Only bootloader bin files should start with "BOOT"
		if (memcasecmp(fno.fname, "BOOT", 4)) continue;

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
			uint8_t* buffer = (uint8_t*)generalMemoryAllocator.alloc(fileSize, NULL, false, true);
			if (!buffer) {
				error = ERROR_INSUFFICIENT_RAM;
				goto gotError;
			}

			FIL currentFile;
			// Open the file
			result = f_open(&currentFile, fno.fname, FA_READ);
			if (result != FR_OK) {
gotFresultErrorAfterAllocating:
				generalMemoryAllocator.dealloc(buffer);
				goto gotFresultError;
			}

			// The file opened. Copy it to RAM
			UINT numBytesRead;
			result = f_read(&currentFile, buffer, fileSize, &numBytesRead);
			if (result != FR_OK) goto gotFresultErrorAfterAllocating;
			f_close(&currentFile);

			if (numBytesRead != fileSize) { // Can this happen?
				error = ERROR_SD_CARD;
				generalMemoryAllocator.dealloc(buffer);
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
				if (flashError) goto gotFlashError;
				eraseAddress += 0x10000; // 64K
			}

			// Copy new program from RAM to flash memory
			uint32_t flashWriteAddress = startFlashAddress;
			uint8_t* readAddress = buffer;

			while (true) {

				int bytesLeft = startFlashAddress + fileSize - flashWriteAddress;
				if (bytesLeft <= 0) break;

				int bytesToWrite = bytesLeft;
				if (bytesToWrite > FLASH_WRITE_SIZE) bytesToWrite = FLASH_WRITE_SIZE;

				int32_t error = R_SFLASH_ByteProgram(flashWriteAddress, readAddress, bytesToWrite, SPIBSC_CH,
				                                     SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT, SPIBSC_OUTPUT_ADDR_24);
				if (error) goto gotFlashError;

				flashWriteAddress += FLASH_WRITE_SIZE;
				readAddress += FLASH_WRITE_SIZE;
			}

			generalMemoryAllocator.dealloc(buffer);

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
