/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/ui/browser/browser.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "fatfs.hpp"
#include "gui/context_menu/delete_file.h"
#include "gui/l10n/l10n.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoders.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/try.h"
#include <climits>
#include <cstring>
#include <new>

using namespace deluge;

String Browser::currentDir{};
bool Browser::qwertyVisible;

CStringArray Browser::fileItems{sizeof(FileItem)};
int32_t Browser::scrollPosVertical;
int32_t Browser::fileIndexSelected;
int32_t Browser::numCharsInPrefix;
bool Browser::arrivedAtFileByTyping;
int32_t Browser::numFileItemsDeletedAtStart;
int32_t Browser::numFileItemsDeletedAtEnd;
int8_t Browser::previous_offset_direction;
bool Browser::loading_delayed_during_fast_scroll = false;
int32_t Browser::reversal_screen_top_index = INT32_MIN;
char const* Browser::firstFileItemRemaining;
char const* Browser::lastFileItemRemaining;
OutputType Browser::outputTypeToLoad;
char const** Browser::allowedFileExtensions;
bool Browser::allowFoldersSharingNameWithFile;
char const* Browser::filenameToStartSearchAt;

// 7SEG ONLY
int8_t Browser::numberEditPos;
NumericLayerScrollingText* Browser::scrollingText;

char const* allowedFileExtensionsXML[] = {"XML", "Json", NULL};

Browser::Browser() {
	fileIcon = deluge::hid::display::OLED::songIcon;
	fileIconPt2 = nullptr;
	fileIconPt2Width = 0;
	scrollingText = NULL;
	shouldWrapFolderContents = true;
	previous_offset_direction = 0; // Initialize direction tracking

	mayDefaultToBrandNewNameOnEntry = false;
	qwertyAlwaysVisible = true;
	qwertyVisible = true; // Because for most Browsers, it'll just always be true.
	filePrefix = NULL;
	shouldInterpretNoteNamesForThisBrowser = false;
}

bool Browser::opened() {
	numCharsInPrefix = 0; // For most browsers, this just stays at 0.
	arrivedAtFileByTyping = false;
	allowedFileExtensions = allowedFileExtensionsXML;
	allowFoldersSharingNameWithFile = false;

	numberEditPos = -1;

	return QwertyUI::opened();
}

// returns true if the FP for the filepath is correct
bool Browser::checkFP() {
	FileItem* currentFileItem = getCurrentFileItem();
	String filePath;
	Error error = getCurrentFilePath(&filePath);
	if (error != Error::NONE) {
		D_PRINTLN("couldn't get filepath");
		return false;
	}

	FilePointer tempfp;
	bool fileExists = StorageManager::fileExists(filePath.get(), &tempfp);
	if (!fileExists) {
		D_PRINTLN("couldn't get filepath");
		return false;
	}
	else if (tempfp.sclust != currentFileItem->filePointer.sclust) {
		D_PRINTLN("FPs don't match: correct is %lu but the browser has %lu", tempfp.sclust,
		          currentFileItem->filePointer.sclust);
#if ALPHA_OR_BETA_VERSION
		display->freezeWithError("B001");
#endif
		return false;
	}
	return true;
}

void Browser::close() {
	emptyFileItems();
	favouritesManager.close();
	favouritesVisible = false;
	QwertyUI::close();
}

void Browser::emptyFileItems() {

	AudioEngine::logAction("emptyFileItems");

	for (int32_t i = 0; i < fileItems.getNumElements();) {
		FileItem* item = (FileItem*)fileItems.getElementAddress(i);
		item->~FileItem();

		i++;
		if (!(i & 63)) { //  &127 was even fine, even with only -Og compiler optimization.
			AudioEngine::logAction("emptyFileItems in loop");
			// Sean: replace routineWithClusterLoading call, just yield to run a single thing (probably audio)
			yield([]() { return true; });
		}
	}

	AudioEngine::logAction("emptyFileItems 2");

	fileItems.empty();

	AudioEngine::logAction("emptyFileItems 3");
}

void Browser::deleteSomeFileItems(int32_t startAt, int32_t stopAt) {

	// Call destructors.
	for (int32_t i = startAt; i < stopAt;) {
		FileItem* item = (FileItem*)fileItems.getElementAddress(i);
		item->~FileItem();

		i++;
		if (!(i & 63)) { //  &127 was even fine, even with only -Og compiler optimization.
			// Sean: replace routineWithClusterLoading call, just yield to run a single thing (probably audio)
			yield([]() { return true; });
		}
	}

	fileItems.deleteAtIndex(startAt, stopAt - startAt);
}

int32_t maxNumFileItemsNow;

int32_t catalogSearchDirection;

FileItem* Browser::getNewFileItem() {
	bool alreadyCulled = false;

	if (fileItems.getNumElements() >= maxNumFileItemsNow) {
doCull:
		cullSomeFileItems();
		alreadyCulled = true;
	}

	int32_t newIndex = fileItems.getNumElements();
	Error error = fileItems.insertAtIndex(newIndex);
	if (error != Error::NONE) {
		if (alreadyCulled) {
			return nullptr;
		}
		else {
			goto doCull;
		}
	}

	void* newMemory = fileItems.getElementAddress(newIndex);

	FileItem* thisItem = new (newMemory) FileItem();
	return thisItem;
}

void Browser::cullSomeFileItems() {
	sortFileItems();

	int32_t startAt, stopAt;

	int32_t numFileItemsDeletingNow = fileItems.getNumElements() - (maxNumFileItemsNow >> 1); // May get modified below.
	if (numFileItemsDeletingNow <= 0) {
		return;
	}

	// If we already know what side we want to be deleting on...
	if (catalogSearchDirection == CATALOG_SEARCH_LEFT) {
deleteFromLeftSide:
		numFileItemsDeletedAtStart += numFileItemsDeletingNow;
		startAt = 0;
		stopAt = numFileItemsDeletingNow;
		firstFileItemRemaining = ((FileItem*)fileItems.getElementAddress(numFileItemsDeletingNow))->displayName;
	}
	else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
deleteFromRightSide:
		numFileItemsDeletedAtEnd += numFileItemsDeletingNow;
		stopAt = fileItems.getNumElements();
		startAt = stopAt - numFileItemsDeletingNow;
		lastFileItemRemaining = ((FileItem*)fileItems.getElementAddress(startAt - 1))->displayName;
	}

	// Or if we've been using a search term *and* searching both directions, try to tend towards keeping equal amounts
	// of FileIems either side.
	else {

		shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
		octaveStartsFromA = false;
		int32_t foundIndex = fileItems.search(filenameToStartSearchAt);

		// If search-item is in second half, delete from start.
		if ((foundIndex << 1) >= fileItems.getNumElements()) {
			int32_t newNumFilesDeleting =
			    foundIndex >> 1; // Delete half the existing items to the left of the search-item.
			if (newNumFilesDeleting <= 0) {
				return;
			}
			if (numFileItemsDeletingNow > newNumFilesDeleting) {
				numFileItemsDeletingNow = newNumFilesDeleting;
			}
			goto deleteFromLeftSide;
		}

		// Or, vice versa.
		else {
			int32_t newNumFilesDeleting = (fileItems.getNumElements() - foundIndex)
			                              >> 1; // Delete half the existing items to the right of the search-item.
			if (newNumFilesDeleting <= 0) {
				return;
			}
			if (numFileItemsDeletingNow > newNumFilesDeleting) {
				numFileItemsDeletingNow = newNumFilesDeleting;
			}
			goto deleteFromRightSide;
		}
	}

	if (startAt != stopAt) {
		deleteSomeFileItems(startAt, stopAt); // Check might not be necessary?
	}
}

Error Browser::readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
                                      char const** allowedFileExtensionsHere, char const* filenameToStartAt,
                                      int32_t newMaxNumFileItems, int32_t newCatalogSearchDirection) {

	AudioEngine::logAction("readFileItemsForFolder");

	emptyFileItems();

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}

	staticDIR =
	    D_TRY_CATCH(FatFS::Directory::open(currentDir.get()), error, { return fatfsErrorToDelugeError(error); });

	numFileItemsDeletedAtStart = 0;
	numFileItemsDeletedAtEnd = 0;
	firstFileItemRemaining = nullptr;
	lastFileItemRemaining = nullptr;
	catalogSearchDirection = newCatalogSearchDirection;
	maxNumFileItemsNow = newMaxNumFileItems;
	filenameToStartSearchAt = filenameToStartAt;

	int32_t filePrefixLength;

	if (display->have7SEG()) {
		if (filePrefixHere) {
			filePrefixLength = strlen(filePrefixHere);
		}
	}

	while (true) {
		AudioEngine::logAction("while loop");

		audioFileManager.loadAnyEnqueuedClusters();
		FilePointer thisFilePointer;

		std::tie(staticFNO, thisFilePointer) = D_TRY_CATCH(staticDIR.read_and_get_filepointer(), error, {
			break; // Break on error
		});

		if (staticFNO.fname[0] == 0) {
			break; /* Break on end of dir */
		}
		if (staticFNO.fname[0] == '.') {
			continue; /* Ignore dot entry */
		}
		bool isFolder = staticFNO.fattrib & AM_DIR;
		if (isFolder) {
			if (!allowFolders) {
				continue;
			}
		}
		else {
			char const* dotPos = strrchr(staticFNO.fname, '.');
			if (!dotPos) {
extensionNotSupported:
				continue;
			}
			char const* fileExtension = dotPos + 1;
			char const** thisExtension = allowedFileExtensionsHere;
			while (strcasecmp(fileExtension, *thisExtension)) {
				thisExtension++;
				if (!*thisExtension) {
					goto extensionNotSupported; // If reached end of list
				}
			}
		}

		FileItem* thisItem = getNewFileItem();
		if (!thisItem) {
			error = Error::INSUFFICIENT_RAM;
			break;
		}
		error = thisItem->filename.set(staticFNO.fname);
		if (error != Error::NONE) {
			break;
		}
		thisItem->isFolder = isFolder;
		thisItem->filePointer = thisFilePointer;

		char const* storedFilenameChars = thisItem->filename.get();
		if (display->have7SEG()) {
			if (filePrefixHere) {
				if (memcasecmp(storedFilenameChars, filePrefixHere, filePrefixLength)) {
					goto nonNumericFile;
				}

				char* dotAddress = strrchr(storedFilenameChars, '.');
				if (!dotAddress) {
					goto nonNumericFile; // Shouldn't happen?
				}

				int32_t dotPos = (uint32_t)dotAddress - (uint32_t)storedFilenameChars;
				if (dotPos < filePrefixLength + 3) {
					goto nonNumericFile;
				}

				char const* numbersStartAddress = &storedFilenameChars[filePrefixLength];

				if (!memIsNumericChars(numbersStartAddress, 3)) {
					goto nonNumericFile;
				}

				thisItem->displayName = numbersStartAddress;

				if (*thisItem->displayName == '0') {
					thisItem->displayName++;
					if (*thisItem->displayName == '0') {
						thisItem->displayName++;
					}
				}
			}
			else {
				goto nonNumericFile;
			}
		}
		else {
nonNumericFile:
			thisItem->displayName = storedFilenameChars;
		}
	}
	staticDIR.close();

	if (error != Error::NONE) {
		emptyFileItems();
	}

	return error;
}

void Browser::deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement) {
	int32_t writeI = 0;
	FileItem* nextItem = (FileItem*)fileItems.getElementAddress(0);

	for (int32_t readI = 0; readI < fileItems.getNumElements(); readI++) {
		FileItem* readItem = nextItem;

		// If there's a next item after "this" item, to compare it to...
		if (readI < fileItems.getNumElements() - 1) {
			nextItem = (FileItem*)fileItems.getElementAddress(readI + 1);

			// If we're a folder, and the next item is a file of the same name, delete this item.
			if (readItem->isFolder) {
				if (!nextItem->isFolder) {
					int32_t nameLength = readItem->filename.getLength();
					char const* nextItemFilename = nextItem->filename.get();
					if (!memcasecmp(readItem->filename.get(), nextItemFilename, nameLength)) {
						if (nextItemFilename[nameLength] == '.' && !strchr(&nextItemFilename[nameLength + 1], '.')) {
							goto deleteThisItem;
						}
					}
				}
			}

			// Or if we have an Instrument, and the next item is a file of the same name, delete the next item.
			else if (readItem->instrument) {
				if (!nextItem->instrument && !nextItem->isFolder) {
					if (!strcasecmp(readItem->displayName, nextItem->displayName)) {
						// if (readItem->filename.equalsCaseIrrespective(&nextItem->filename)) {
						nextItem->~FileItem();
						readI++;
						nextItem = (FileItem*)fileItems.getElementAddress(readI + 1);
						// That may set it to an invalid address, but in that case, it won't get read.
					}
				}

checkAvailabilityRequirement:
				// Check Instrument's availabilityRequirement
				if (readItem->instrumentAlreadyInSong) {
					if (instrumentAvailabilityRequirement == Availability::INSTRUMENT_UNUSED) {
deleteThisItem:
						readItem->~FileItem();
						continue;
					}
					else if (instrumentAvailabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
						if (currentSong->doesOutputHaveActiveClipInSession(readItem->instrument)) {
							goto deleteThisItem;
						}
					}
				}
			}

			// Or if next item has an Instrument, and we're just a file...
			else if (nextItem->instrument) {
				if (!strcasecmp(readItem->displayName, nextItem->displayName)) { // And if same name...
					goto deleteThisItem;
				}
			}
		}
		else {
			if (readItem->instrument) {
				goto checkAvailabilityRequirement;
			}
		}

		void* writeAddress = fileItems.getElementAddress(writeI);
		if (writeAddress != readItem) {
			memcpy(writeAddress, readItem, sizeof(FileItem));
		}
		writeI++;
	}

	int32_t numToDelete = fileItems.getNumElements() - writeI;
	if (numToDelete > 0) {
		fileItems.deleteAtIndex(writeI, numToDelete);
	}

	// Our system of keeping FileItems from getting too full by deleting elements from its ends as we go could have
	// caused bad results at the edges of the above, so delete a further one element at each end as needed.
	if (firstFileItemRemaining) {
		fileItems.deleteAtIndex(0);
	}
	if (lastFileItemRemaining) {
		fileItems.deleteAtIndex(fileItems.getNumElements() - 1);
	}
}

Error Browser::setFileByFullPath(OutputType outputType, char const* fullPath) {
	arrivedAtFileByTyping = true;
	FilePointer tempfp;
	bool fileExists = StorageManager::fileExists(fullPath, &tempfp);
	if (!fileExists) {
		return Error::FILE_NOT_FOUND;
	}

	const char* fileName = getFileNameFromEndOfPath(fullPath);
	currentDir.set(getPathFromFullPath(fullPath));

	// Change to the File Folder
	Error error = arrivedInNewFolder(0, fileName);
	if (error != Error::NONE) {
		return error;
	}

	//  Get the File Index
	fileIndexSelected = fileItems.search(fileName);
	if (fileIndexSelected > fileItems.getNumElements()) {
		return Error::FILE_NOT_FOUND;
	}

	// Update the Display
	scrollPosVertical = fileIndexSelected;
	setEnteredTextFromCurrentFilename();
	renderUIsForOled();

	// Inform the Load UI that the File has changed
	currentFileChanged(1);
	return Error::NONE;
}

// song may be supplied as NULL, in which case it won't be searched for Instruments; sometimes this will get called when
// the currentSong is not set up.
Error Browser::readFileItemsFromFolderAndMemory(Song* song, OutputType outputType, char const* filePrefixHere,
                                                char const* filenameToStartAt, char const* defaultDirToAlsoTry,
                                                bool allowFolders, Availability availabilityRequirement,
                                                int32_t newCatalogSearchDirection) {
	// filenameToStartAt should have .XML at the end of it.
	bool triedCreatingFolder = false;

tryReadingItems:
	Error error = readFileItemsForFolder(filePrefixHere, allowFolders, allowedFileExtensions, filenameToStartAt,
	                                     FILE_ITEMS_MAX_NUM_ELEMENTS, newCatalogSearchDirection);
	if (error != Error::NONE) {

		// If folder didn't exist, try our alternative one if there is one.
		if (error == Error::FOLDER_DOESNT_EXIST) {
			if (defaultDirToAlsoTry) {
				// ... only if we haven't already tried the alternative folder.
				if (!currentDir.equalsCaseIrrespective(defaultDirToAlsoTry)) {
					filenameToStartAt = NULL;
					Error error = currentDir.set(defaultDirToAlsoTry);
					if (error != Error::NONE) {
						return error;
					}
					goto tryReadingItems;
				}

				// Or if we have already tried it and it didn't exist, try creating it...
				else {
					// But not if we already tried.
					if (triedCreatingFolder) {
						return error;
					}
					FRESULT result = f_mkdir(defaultDirToAlsoTry);
					if (result == FR_OK) {
						triedCreatingFolder = true;
						goto tryReadingItems;
					}
					else {
						return fresultToDelugeErrorCode(result);
					}
				}
			}
		}

		return error;
	}

	if (song && outputType != OutputType::NONE) {
		error = song->addInstrumentsToFileItems(outputType);
		if (error != Error::NONE) {
			return error;
		}
	}

	if (fileItems.getNumElements()) {
		sortFileItems();

		if (fileItems.getNumElements()) {
			// Delete folders sharing name of file.
			// And, files sharing name of in-memory Instrument.
			if (!allowFoldersSharingNameWithFile) {
				deleteFolderAndDuplicateItems(
				    Availability::ANY); // I think this is right - was Availability::INSTRUMENT_UNUSED until 2023-01
			}
		}
	}

	return Error::NONE;
}

// If OLED, then you should make sure renderUIsForOLED() gets called after this.
// outputTypeToLoad must be set before calling this.
Error Browser::arrivedInNewFolder(int32_t direction, char const* filenameToStartAt, char const* defaultDirToAlsoTry) {
	arrivedAtFileByTyping = false;

	if (!qwertyAlwaysVisible) {
		qwertyVisible = false;
	}

	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

tryReadingItems:
	bool doWeHaveASearchString = (filenameToStartAt && *filenameToStartAt);
	int32_t newCatalogSearchDirection = doWeHaveASearchString ? CATALOG_SEARCH_BOTH : CATALOG_SEARCH_RIGHT;
	Error error =
	    readFileItemsFromFolderAndMemory(currentSong, outputTypeToLoad, filePrefix, filenameToStartAt,
	                                     defaultDirToAlsoTry, true, Availability::ANY, newCatalogSearchDirection);
	if (error != Error::NONE) {
gotErrorAfterAllocating:
		emptyFileItems();
		return error;
	}

	enteredTextEditPos = 0;
	if (display->haveOLED()) {
		scrollPosHorizontal = 0;
	}

	bool foundExact = false;
	if (fileItems.getNumElements()) {
		fileIndexSelected = 0;

		if (!doWeHaveASearchString) {
noExactFileFound:
			// We did not find exact file.
			// Normally, we'll need to just use one of the ones we found. (That's just always the first one, I think...)
			if (!mayDefaultToBrandNewNameOnEntry || direction) {

				// But since we're going to just use the first file, if we've deleted items at the start (meaning we had
				// a search string), we need to go back and get them.
				if (numFileItemsDeletedAtStart) {
					filenameToStartAt = NULL;
					goto tryReadingItems;
				}
setEnteredTextAndUseFoundFile:
				error = setEnteredTextFromCurrentFilename();
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}
useFoundFile:
				scrollPosVertical = fileIndexSelected > 0 ? fileIndexSelected - 1 : fileIndexSelected;
				if (display->getNumBrowserAndMenuLines() > 1) {
					int32_t lastAllowed = fileItems.getNumElements() - display->getNumBrowserAndMenuLines();
					if (scrollPosVertical > lastAllowed) {
						scrollPosVertical = lastAllowed;
						if (scrollPosVertical < 0) {
							scrollPosVertical = 0;
						}
					}
				}

				goto everythingFinalized;
			}

			// But sometimes...
			else {
				// Ok so we didn't find an exact file, we've just entered the browser, and we're allowed new names.
				// So, choose a brand new name (if there wasn't already a new one nominated).
				goto pickBrandNewNameIfNoneNominated;
			}
		}

		int32_t i = fileItems.search(filenameToStartAt, &foundExact);
		if (!foundExact) {
			goto noExactFileFound;
		}

		fileIndexSelected = i;

		// Usually we'll just use that file.
		if (!mayDefaultToBrandNewNameOnEntry || direction) {
			goto setEnteredTextAndUseFoundFile;
		}

		// We found an exact file. But if we've just entered the Browser and are allowed, then we need to find a new
		// subslot variation. Come up with a new name variation.
		error = setEnteredTextFromCurrentFilename();
		if (error != Error::NONE) {
			goto gotErrorAfterAllocating;
		}
		// `#if 1 || !OLED` macro was here
		char const* enteredTextChars = enteredText.get();
		if (!memcasecmp(enteredTextChars, "SONG", 4)) {
			Slot thisSlot = getSlot(&enteredTextChars[4]);
			if (thisSlot.slot < 0) {
				goto doNormal;
			}

			if (thisSlot.subSlot >= 25) {
				goto useFoundFile;
			}

			char nameBuffer[20];
			char* nameBufferPos = nameBuffer;
			if (display->haveOLED()) {
				*(nameBufferPos++) = 'S';
				*(nameBufferPos++) = 'O';
				*(nameBufferPos++) = 'N';
				*(nameBufferPos++) = 'G';
			}
			intToString(thisSlot.slot, nameBufferPos);
			char* subSlotPos = strchr(nameBufferPos, 0);
			char* charPosHere = subSlotPos + 1;
			*(charPosHere++) = '.';
			*(charPosHere++) = 'X';
			*(charPosHere++) = 'M';
			*(charPosHere++) = 'L';
			*(charPosHere) = 0;
			while (true) {
				thisSlot.subSlot++;

				*subSlotPos = 'A' + thisSlot.subSlot;
				bool foundExactHere;
				fileIndexSelected = fileItems.search(nameBuffer, &foundExactHere);
				if (!foundExactHere) {
					break;
				}
				else if (thisSlot.subSlot >= 25) {
					goto setEnteredTextAndUseFoundFile; // If we're stuck on the "Z" subslot.
				}
			}
			*(subSlotPos + 1) = 0; // Removes ".XML"
			error = enteredText.set(nameBuffer);
			if (error != Error::NONE) {
				goto gotErrorAfterAllocating;
			}
		}
		/* This was originally never accessible as the `else` branch of a `#if 1 || !OLED` macro
		int32_t length = enteredText.getLength();
		if (length > 0) {
		    char const* enteredTextChars = enteredText.get();
		    if (enteredTextChars[length - 1] >= '0' && enteredTextChars[length - 1] <= '9') {
		        enteredText.concatenateAtPos("A", length, 1);
		    }
		    else if (length >= 2 && enteredTextChars[length - 2] >= '0' && enteredTextChars[length - 2] <= '9'
		             && ((enteredTextChars[length - 1] >= 'a'
		                  && enteredTextChars[length - 1]
		                         < 'z') // That's *less than*, because if it's Z, we'll have to doNormal.
		                 || (enteredTextChars[length - 1] >= 'A' && enteredTextChars[length - 1] < 'Z'))) {
		        char newSuffix = enteredTextChars[length - 1] + 1;
		        enteredText.concatenateAtPos(&newSuffix, length - 1, 1);
		    }
		    else
		        goto doNormal;
		}
*/
		else {
doNormal: // FileItem* currentFile = (FileItem*)fileItems.getElementAddress(fileIndexSelected);
			String endSearchString;
			// error = currentFile->getFilenameWithoutExtension(&endSearchString);		if (error != Error::NONE) goto
			// gotErrorAfterAllocating;
			endSearchString.set(&enteredText);

			// Did it already have an underscore at the end with a positive integer after it?
			char const* endSearchStringChars = endSearchString.get();
			char delimeterChar = '_';
tryAgain:
			char const* delimeterAddress = strrchr(endSearchStringChars, delimeterChar);
			int32_t numberStartPos;
			if (delimeterAddress) {
				int32_t underscorePos = delimeterAddress - endSearchStringChars;

				// Ok, it what comes after the underscore a positive integer?
				int32_t number = stringToUIntOrError(delimeterAddress + 1);
				if (number < 0) {
					goto noNumberYet;
				}

				numberStartPos = underscorePos + 1;
				error = endSearchString.concatenateAtPos(":", numberStartPos);
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating; // Colon is the next character after the ascii digits, so searching
					                              // for this will get us past the final number present.
				}
			}
			else {
noNumberYet:
				if (delimeterChar == '_') {
					delimeterChar = ' ';
					goto tryAgain;
				}
				numberStartPos = endSearchString.getLength() + 1;
				error = endSearchString.concatenate(display->haveOLED() ? " :" : "_:");
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating; // See above comment.
				}
			}

			int32_t searchResult = fileItems.search(endSearchString.get());
#if ALPHA_OR_BETA_VERSION
			if (searchResult <= 0) {
				FREEZE_WITH_ERROR("E448");
				error = Error::BUG;
				goto gotErrorAfterAllocating;
			}
#endif
			FileItem* prevFile = (FileItem*)fileItems.getElementAddress(searchResult - 1);
			String prevFilename;
			error = prevFile->getFilenameWithoutExtension(&prevFilename);
			if (error != Error::NONE) {
				goto gotErrorAfterAllocating;
			}
			char const* prevFilenameChars = prevFilename.get();
			int32_t number;
			if (prevFilename.getLength() > numberStartPos) {
				number = stringToUIntOrError(&prevFilenameChars[numberStartPos]);
				if (number < 0) {
					number = 1;
				}
			}
			else {
				number = 1;
			}

			number++;
			enteredText.set(&endSearchString);
			error = enteredText.shorten(numberStartPos);
			if (error != Error::NONE) {
				goto gotErrorAfterAllocating;
			}
			error = enteredText.concatenateInt(number);
			if (error != Error::NONE) {
				goto gotErrorAfterAllocating;
			}

			enteredTextEditPos = enteredText.getLength();
		}
	}

	// Or if no files found at all...
	else {
		// Can we just pick a brand new name?
		if (mayDefaultToBrandNewNameOnEntry && !direction) {
pickBrandNewNameIfNoneNominated:
			if (enteredText.isEmpty()) {
				error = getUnusedSlot(OutputType::NONE, &enteredText, filePrefix);
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}
				// Note - this is only hit if we're saving the first song created on boot (because the default name
				// won't match anything) Because that will have cleared out all the FileItems, we need to get them
				// again. Actually there would kinda be a way around doing this...
				error = readFileItemsFromFolderAndMemory(currentSong, OutputType::NONE, "SONG", enteredText.get(), NULL,
				                                         true, Availability::ANY, CATALOG_SEARCH_BOTH);
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}
			}
		}
		else {
			enteredText.clear();
		}
	}

useNonExistentFileName:     // Normally this will get skipped over - if we found a file.
	fileIndexSelected = -1; // No files.
	scrollPosVertical = 0;

everythingFinalized:
	if (!loading_delayed_during_fast_scroll) {
		// Only call if we're not in fast scroll mode to avoid updating the screen preview
		folderContentsReady(direction);
	}

	if (display->have7SEG()) {
		displayText();
	}
	return Error::NONE;
}

// You must set currentDir before calling this.
Error Browser::getUnusedSlot(OutputType outputType, String* newName, char const* thingName) {

	Error error;
	if (display->haveOLED()) {
		char filenameToStartAt[6]; // thingName is max 4 chars.
		strcpy(filenameToStartAt, thingName);
		strcat(filenameToStartAt, ":");
		error = readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType), filenameToStartAt,
		                                         NULL, false, Availability::ANY, CATALOG_SEARCH_LEFT);
	}
	else {
		char const* filenameToStartAt = ":"; // Colon is the first character after the digits
		error = readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType), filenameToStartAt,
		                                         NULL, false, Availability::ANY, CATALOG_SEARCH_LEFT);
	}

	if (error != Error::NONE) {
doReturn:
		return error;
	}

	sortFileItems();

	if (display->haveOLED()) {
		int32_t freeSlotNumber = 1;
		int32_t minNumDigits = 1;
		if (fileItems.getNumElements()) {
			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(fileItems.getNumElements() - 1);
			String displayName;
			error = fileItem->getDisplayNameWithoutExtension(&displayName);
			if (error != Error::NONE) {
				goto emptyFileItemsAndReturn;
			}
			char const* readingChar = &displayName.get()[strlen(thingName)];
			freeSlotNumber = 0;
			minNumDigits = 0;
			while (*readingChar >= '0' && *readingChar <= '9') {
				freeSlotNumber *= 10;
				freeSlotNumber += *readingChar - '0';
				minNumDigits++;
				readingChar++;
			}
			freeSlotNumber++;
		}

		error = newName->set(thingName);
		if (error != Error::NONE) {
			goto emptyFileItemsAndReturn;
		}
		error = newName->concatenateInt(freeSlotNumber, minNumDigits);
	}
	else {
		int32_t nextHigherSlotFound = kNumSongSlots; // I think the use of this is a bit deprecated...
		int32_t i = fileItems.getNumElements();

		// Ok, due to not bothering to reload fileItems if we need to look too far back, we may sometimes fail to see an
		// empty slot further back when later ones are taken. Oh well.
goBackOne:
		i--;
		int32_t freeSlotNumber;
		if (i < 0) {
noMoreToLookAt:
			if (nextHigherSlotFound <= 0) {
				newName->clear(); // Indicate no slots available.
				goto emptyFileItemsAndReturn;
			}
			freeSlotNumber = 0;
		}
		else {
			FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);
			String displayName;
			error = fileItem->getDisplayNameWithoutExtension(&displayName);
			if (error != Error::NONE) {
				goto emptyFileItemsAndReturn;
			}
			char const* displayNameChars = displayName.get();
			if (displayNameChars[0] < '0') {
				goto noMoreToLookAt;
			}

			Slot slotHere = getSlot(displayNameChars);
			if (slotHere.slot < 0) {
				goto goBackOne;
			}

			freeSlotNumber = slotHere.slot + 1; // Well, hopefully it's free...
			if (freeSlotNumber >= nextHigherSlotFound) {
				nextHigherSlotFound = slotHere.slot;
				goto goBackOne;
			}
		}

		// If still here, we found an unused slot.
		error = newName->setInt(freeSlotNumber);
	}

emptyFileItemsAndReturn:
	emptyFileItems();
	goto doReturn;
}

void Browser::selectEncoderAction(int8_t offset) {
	arrivedAtFileByTyping = false;

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {
		return; // This was from SampleBrowser. Is it still necessary?
	}

	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	int32_t new_file_index = calculateNewFileIndex(offset);

	// Handle index bounds and reload items if necessary
	Error error = handleIndexBoundsAndReload(new_file_index, offset);
	if (error == Error::UNSPECIFIED) {
		// Early return case - either no elements or should not wrap on 7SEG
		return;
	}
	else if (error != Error::NONE) {
		D_PRINTLN("error while reloading, emptying file items");
		emptyFileItems();
		return;
	}

	// Set the final file index - this corresponds to the original code's single assignment
	fileIndexSelected = new_file_index;

	updateUIState();

	// Set entered text from current filename
	error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		display->displayError(error);
		return;
	}

	displayText();

	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT) && offset != 0) {
		// we're fast scrolling, so to make it even faster and to keep a jam flowing we won't
		// loaded a preview of the files we scroll to until we decide to, by releasing shift or by pressing select
		loading_delayed_during_fast_scroll = true;
	}
	else {
		// Normal scrolling, load preview immediately
		currentFileChanged(offset); // let the relevant loading UI know of the change
		loading_delayed_during_fast_scroll = false;
	}
}

int32_t Browser::calculateNewFileIndex(int8_t offset) {

	if (fileIndexSelected < 0) { // If no file selected and we were typing a new name?
		if (!fileItems.getNumElements()) {
			return INT32_MIN; // Special value to indicate early return (no elements available)
		}

		int32_t new_file_index = fileItems.search(enteredText.get());
		if (offset < 0) {
			new_file_index--;
		}

		return new_file_index;
	}

	if (display->haveOLED()) { // OLED version
		// Calculate vertical scroll speed
		int32_t scroll_multiplier = 1;
		if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
			// this method of checking for shift press ignores the sticky shift state.

			scroll_multiplier = NUM_FILES_ON_SCREEN;

			// check if scroll direction was reversed during fast scroll sequence.
			if (loading_delayed_during_fast_scroll) {
				if (previous_offset_direction == -1 * offset) {
					// Direction was reversed, so determine the index range of the files on the screen
					reversal_screen_top_index = scrollPosVertical;
				}

				if (reversal_screen_top_index != INT32_MIN) {
					int32_t target_index = fileIndexSelected + offset; // we are moving one at a time for now
					int32_t reversal_screen_bottom = reversal_screen_top_index + NUM_FILES_ON_SCREEN - 1;
					// Check if the target move would take us outside the captured screen range
					if (target_index >= reversal_screen_top_index && target_index <= reversal_screen_bottom) {
						// within screen range use single step scrolling
						scroll_multiplier = 1;
					}
					else {
						// Move would exit screen range - resume fast scrolling
						reversal_screen_top_index = INT32_MIN;
						// scroll_multiplier stays at default NUM_FILES_ON_SCREEN
					}
				}
			}
			else if (fileIndexSelected == scrollPosVertical + NUM_FILES_ON_SCREEN / 2) {
				// Check if current selection is in the center of the screen, and move one more to reach the end of next
				// screen
				scroll_multiplier = NUM_FILES_ON_SCREEN + 1;
			}
			previous_offset_direction = offset; // Update previous direction tracker
		}
		else {
			// Reset reversal state and direction tracking during regular scrolling
			previous_offset_direction = 0;
			reversal_screen_top_index = INT32_MIN;
		}
		// Show current position and target
		int32_t new_file_index = fileIndexSelected + (offset * scroll_multiplier);
		char const* current_name = "none";
		if (getCurrentFileItem()) {
			current_name = getCurrentFileItem()->displayName;
		}
		// D_PRINTLN("At '%s' (index %d), trying to move %d to reach index %d",
		// 					current_name, fileIndexSelected, offset * scroll_multiplier, new_file_index);
		return new_file_index;
	}
	else {
		// 7SEG version, filePrefix is e.g. SYNT, KIT, SONG, used in combination with numeric filenames
		if (filePrefix && Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
			int32_t file_prefix_length = strlen(filePrefix);
			char const* entered_text_chars = enteredText.get();
			if (!memcasecmp(filePrefix, entered_text_chars, file_prefix_length)) {
				Slot this_slot = getSlot(&entered_text_chars[file_prefix_length]);
				if (this_slot.slot >= 0) {
					this_slot.slot += offset;

					char search_string[9];
					memcpy(search_string, filePrefix, file_prefix_length);
					char* search_string_numbers_start = search_string + file_prefix_length;
					int32_t min_num_digits = 3;
					intToString(this_slot.slot, search_string_numbers_start, min_num_digits);
					if (offset < 0) {
						char* pos = strchr(search_string_numbers_start, 0);
						*pos = 'A';
						pos++;
						*pos = 0;
					}
					int32_t new_file_index = fileItems.search(search_string);
					if (offset < 0) {
						new_file_index--;
					}
					return new_file_index;
				}
			}
		}
		// Non-numeric 7seg filename case
		return fileIndexSelected + offset;
	}
}

Error Browser::handleIndexBoundsAndReload(int32_t& new_file_index, int8_t offset) {
	if (new_file_index == INT32_MIN) {
		// Early return case from calculateNewFileIndex - no elements available
		return Error::UNSPECIFIED;
	}
	int32_t min_allowed_index = loading_delayed_during_fast_scroll ? 0 : 1;
	if (new_file_index < min_allowed_index) {
		return handleIndexBelowZero(new_file_index, offset);
	}

	int32_t max_allowed_index =
	    loading_delayed_during_fast_scroll ? fileItems.getNumElements() - 1 : fileItems.getNumElements() - 2;
	if (new_file_index > max_allowed_index) {
		return handleIndexAboveMax(new_file_index, offset);
	}
	// otherwise the index is within bounds
	return Error::NONE;
}

Error Browser::handleIndexBelowZero(int32_t& new_file_index, int8_t offset) {

	if (numFileItemsDeletedAtStart) {
		// reload items because we know there are still items that can be loaded to the left, since they were
		// deleted from the fileItems array during the culling process when the folder contents were read
		scrollPosVertical = 9999;
		// Calculate the full movement from current position to target position
		int32_t movement_amount = new_file_index - fileIndexSelected;
		return reloadItemsAndUpdateIndex(new_file_index, offset, true, movement_amount);
	}
	else if (!shouldWrapFolderContents && display->have7SEG()) {
		return Error::UNSPECIFIED;
	}
	else { // Wrap to end of folder

		if (fileIndexSelected == 0) { // If we're at index 0, always wrap directly to the end
			scrollPosVertical = 0;
			if (numFileItemsDeletedAtEnd) {
				// this means there are more files outside of the current array that will need need to be loaded after
				// wrapping
				return reloadFromOneEnd(new_file_index, CATALOG_SEARCH_LEFT);
			}
			else { // otherwise the current array contains all the files in the folder
				new_file_index = fileItems.getNumElements() - 1;
				return Error::NONE;
			}
		}
		else {
			// If we're out of bounds but didn't start at index 0, that means we moved more than one place,
			// so we need to stop at index 0 to ensure we display the first set of files
			new_file_index = 0;
			return Error::NONE;
		}
	}
}

Error Browser::handleIndexAboveMax(int32_t& new_file_index, int8_t offset) {

	if (numFileItemsDeletedAtEnd) {
		// reload items because we know there are still items that can be loaded to the right, since they
		// were deleted from the fileItems array during the culling process when the folder contents were read
		scrollPosVertical = 0;
		// Calculate the full movement from current position to target position
		int32_t movement_amount = new_file_index - fileIndexSelected;
		return reloadItemsAndUpdateIndex(new_file_index, offset, true, movement_amount);
	}
	else if (!shouldWrapFolderContents && display->have7SEG()) {
		return Error::UNSPECIFIED;
	}
	else {
		int32_t last_index = fileItems.getNumElements() - 1;
		// If we're at the last index and there are no files to the right,
		// that means we need to wrap to the start of the folder
		if (fileIndexSelected == last_index) {
			scrollPosVertical = 9999;
			if (numFileItemsDeletedAtStart) {
				// this means there are more files outside of the current array that will need need to be loaded after
				// wrapping
				return reloadFromOneEnd(new_file_index, CATALOG_SEARCH_RIGHT);
			}
			else { // otherwise the current array contains all the files in the folder
				new_file_index = 0;
				return Error::NONE;
			}
		}
		else {
			// If we're out of bounds but didn't start at the last index, that means we moved more than one place,
			// so we need to stop at the last index to ensure we display the last set of files
			new_file_index = last_index;
			return Error::NONE;
		}
	}
}

Error Browser::reloadItemsAndUpdateIndex(int32_t& new_file_index, int8_t offset, bool use_entered_text,
                                         int32_t movement_amount) {

	Error error;

	// Remember the current file name before reloading
	String filename_temp;
	FileItem* current_file = getCurrentFileItem();
	if (current_file) {
		error = filename_temp.set(current_file->filename.get());
		if (error != Error::NONE) {
			// Fallback to enteredText if we can't set filename
			filename_temp.set(&enteredText);
		}
	}
	else {
		// No current file, use enteredText
		filename_temp.set(&enteredText);
	}

	int32_t search_direction = CATALOG_SEARCH_BOTH;

	// With fast scrolling we can choose a specific search direction to ensure adequate room for movement
	// and reduce the frequency of reloading the fileItems array
	if (movement_amount > 1) {
		// fast scrolling downwards on screen, load more files to the right in the array
		search_direction = CATALOG_SEARCH_RIGHT;
	}
	else if (movement_amount < -1) {
		// fast scrolling upwards on screen, load more files to the left in the array
		search_direction = CATALOG_SEARCH_LEFT;
	}

	error = readFileItemsFromFolderAndMemory(currentSong, outputTypeToLoad, filePrefix,
	                                         use_entered_text ? enteredText.get() : nullptr, nullptr, true,
	                                         Availability::ANY, search_direction);

	// #if ALPHA_OR_BETA_VERSION
	// show a list of the files in the fileItems array for debugging purposes
	// 	D_PRINT("File Items: ");
	// 	for (int i = 0; i < fileItems.getNumElements(); ++i) {
	// 		FileItem* item = (FileItem*)fileItems.getElementAddress(i);
	// 		String displayName;
	// 		Error error = item->getDisplayNameWithoutExtension(&displayName);
	// 		if (error == Error::NONE) {
	// 			D_PRINT("%s ", displayName.get());
	// 		} else {
	// 			D_PRINT("Error getting name ");
	// 		}
	// 	}
	// 	D_PRINTLN("");
	// #endif

	if (error != Error::NONE) {
		return error;
	}

	// Find where our original file ended up in the new array
	int32_t original_file_new_index = -1;
	for (int32_t i = 0; i < fileItems.getNumElements(); i++) {
		FileItem* item = (FileItem*)fileItems.getElementAddress(i);
		if (item && item->filename.equals(&filename_temp)) {
			original_file_new_index = i;
			break;
		}
	}

	// Apply the original movement from the new position
	if (original_file_new_index >= 0) {
		int32_t target_index = original_file_new_index + movement_amount;
		// D_PRINTLN("applying movement: %d + %d = %d", original_file_new_index, movement_amount, target_index);
		new_file_index = target_index;
	}
	else {
		// Original file not found - this is expected with CATALOG_SEARCH_LEFT
		// Position ourselves based on where the original filename would fit
		if (search_direction == CATALOG_SEARCH_LEFT && movement_amount < -1) {
			// For leftward search, the loaded files are all to the left of our original position,
			// which would put our selected file at the top of the screen with blank spaces below,
			// so we need to offset the index. Rightward search doesn't have the same issue.
			new_file_index = fileItems.getNumElements() - NUM_FILES_ON_SCREEN;
		}
		else {
			new_file_index = offset < 0 ? 0 : (fileItems.getNumElements() - 1); // just in case
		}
	}

	// Ensure we're within bounds
	if (new_file_index < 0) {
		new_file_index = 0;
	}
	else if (new_file_index >= fileItems.getNumElements()) {
		new_file_index = fileItems.getNumElements() - 1;
	}

	return Error::NONE;
}

Error Browser::reloadFromOneEnd(int32_t& new_file_index, int32_t search_direction) {
	// D_PRINTLN("reloading and wrap");
	Error error = readFileItemsFromFolderAndMemory(currentSong, outputTypeToLoad, filePrefix, nullptr, nullptr, true,
	                                               Availability::ANY, search_direction);

	if (error != Error::NONE) {
		return error;
	}

	new_file_index = (search_direction == CATALOG_SEARCH_LEFT) ? (fileItems.getNumElements() - 1) : 0;
	return Error::NONE;
}

void Browser::updateUIState() {
	if (!qwertyAlwaysVisible) {
		qwertyVisible = false;
	}

	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) { // fast scrolling
		// Fast scroll logic - only adjust if selection goes off-screen
		scrollPosVertical =
		    std::clamp(scrollPosVertical, fileIndexSelected - NUM_FILES_ON_SCREEN + 1, fileIndexSelected);
	}
	else {
		// For folders with fewer items than display slots, always start from index 0
		if (fileItems.getNumElements() <= NUM_FILES_ON_SCREEN) {
			scrollPosVertical = 0;
		}
		else {
			scrollPosVertical = fileIndexSelected - 1;
			if (scrollPosVertical < 0 && numFileItemsDeletedAtStart == 0) {
				scrollPosVertical = 0;
			}
			else if (fileIndexSelected == fileItems.getNumElements() - 1 && numFileItemsDeletedAtEnd == 0) {
				scrollPosVertical--;
			}
		}
	}

	enteredTextEditPos = 0;
	if (display->haveOLED()) {
		scrollPosHorizontal = 0;
	}
	else {
		char const* oldCharAddress = enteredText.get();
		char const* newCharAddress = getCurrentFileItem()->displayName; // Will have file extension, so beware...
		while (true) {
			char oldChar = *oldCharAddress;
			char newChar = *newCharAddress;

			if (oldChar >= 'A' && oldChar <= 'Z') {
				oldChar += 32;
			}
			if (newChar >= 'A' && newChar <= 'Z') {
				newChar += 32;
			}

			if (oldChar != newChar) {
				break;
			}
			oldCharAddress++;
			newCharAddress++;
			enteredTextEditPos++;
		}
	}
}

bool Browser::predictExtendedText() {
	Error error;
	arrivedAtFileByTyping = true;
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	FileItem* oldFileItem = getCurrentFileItem();
	DWORD oldClust = 0;
	if (oldFileItem) {
		oldClust = oldFileItem->filePointer.sclust;
	}

	String searchString;
	searchString.set(&enteredText);
	bool doneNewRead = false;
	error = searchString.shorten(enteredTextEditPos);
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		return false;
	}

	int32_t numExtraZeroesAdded = 0;

addTildeAndSearch:
	error = searchString.concatenate("~");
	if (error != Error::NONE) {
		goto gotError;
	}

	// Ok, search whatever FileItems we currently have in memory.
doSearch:
	int32_t i = fileItems.search(searchString.get());

	// If that search takes us off the right-hand end of the list...
	if (i >= fileItems.getNumElements()) {

		// If we haven't yet done a whole new read from the SD card etc, from within this function, do that now.
		if (!doneNewRead) {
doNewRead:
			doneNewRead = true;
			error = readFileItemsFromFolderAndMemory(
			    currentSong, outputTypeToLoad, filePrefix, searchString.get(), NULL, true, Availability::ANY,
			    CATALOG_SEARCH_BOTH); // This could probably actually be made to work with searching left only...
			if (error != Error::NONE) {
gotErrorAfterAllocating:
				emptyFileItems();
				goto gotError;
				// TODO - need to close UI or something?
			}
			goto doSearch;
		}

		// Otherwise if we already tried that, then our whole search is fruitless.
notFound:
		if (display->haveOLED() && !mayDefaultToBrandNewNameOnEntry) {
			if (fileIndexSelected >= 0) {
				setEnteredTextFromCurrentFilename(); // Set it back
			}
			return false;
		}

		fileIndexSelected = -1;
		return true;
	}

	if (i == 0) {
		if (!doneNewRead) {
			goto doNewRead;
		}
		else {
			goto notFound;
		}
	}

	i--;
	FileItem* fileItem = (FileItem*)fileItems.getElementAddress(i);

	// If it didn't match exactly, that's ok, but we need to try some other stuff before we accept that result.
	if (memcasecmp(fileItem->displayName, enteredText.get(), enteredTextEditPos)) {
		if (numExtraZeroesAdded < 4) {
			error = searchString.concatenateAtPos("0", searchString.getLength() - 1, 1);
			if (error != Error::NONE) {
				goto gotError; // Gets rid of previously appended "~"
			}
			numExtraZeroesAdded++;
			doneNewRead = false;
			goto addTildeAndSearch;
		}
		else {
			goto notFound;
		}
	}

	fileIndexSelected = i;

	// Move scroll only if found item is completely offscreen.
	if (display->have7SEG() || scrollPosVertical > i || scrollPosVertical < i - (OLED_HEIGHT_CHARS - 1) + 1) {
		scrollPosVertical = i;
	}

	error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		goto gotError;
	}

	displayText();

	// If we're now on a different file than before, preview it
	if (fileItem->filePointer.sclust != oldClust) {
		currentFileChanged(0);
	}

	return true;
}

void Browser::currentFileDeleted() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return; // Shouldn't happen...
	}
	if (currentFileItem->instrument && !currentFileItem->instrumentAlreadyInSong) {
		currentFileItem->instrument->shouldHibernate = false;
	}
	currentFileItem->~FileItem();

	fileItems.deleteAtIndex(fileIndexSelected);

	if (fileIndexSelected == fileItems.getNumElements()) {
		fileIndexSelected--; // It might go to -1 if no files left.
		enteredText.clear();
		enteredTextEditPos = 0;
	}
	else {
		setEnteredTextFromCurrentFilename();
	}
	currentFileChanged(0);
}

void Browser::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	canvas.drawScreenTitle(title);

	int32_t textStartX = 14;
	int32_t iconStartX = 1;
	if (FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION) {
		textStartX += kTextSpacingX;
		iconStartX = kTextSpacingX;
	}

	int32_t yPixel = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	yPixel += OLED_MAIN_TOPMOST_PIXEL;

	int32_t maxChars = (uint32_t)(OLED_MAIN_WIDTH_PIXELS - textStartX) / (uint32_t)kTextSpacingX;

	bool isFolder = false;
	bool isSelectedIndex = true;
	char const* displayName;
	int32_t o;

	// If we're currently typing a filename which doesn't (yet?) have a file...
	if (fileIndexSelected == -1) {
		displayName = enteredText.get();
		o = OLED_HEIGHT_CHARS; // Make sure below loop doesn't keep looping.
		goto drawAFile;
	}

	else {
		for (o = 0; o < OLED_HEIGHT_CHARS - 1; o++) {
			{
				int32_t i = o + scrollPosVertical;

				if (i < 0 || i >= fileItems.getNumElements()) {
					break;
				}

				FileItem* thisFile = (FileItem*)fileItems.getElementAddress(i);
				isFolder = thisFile->isFolder;
				displayName = thisFile->filename.get();
				isSelectedIndex = (i == fileIndexSelected);
			}
drawAFile:
			// Draw graphic
			int32_t iconWidth = 8;
			uint8_t const* graphic = isFolder ? deluge::hid::display::OLED::folderIcon : fileIcon;
			canvas.drawGraphicMultiLine(graphic, iconStartX, yPixel + 0, iconWidth);
			if (!isFolder && fileIconPt2 && fileIconPt2Width) {
				canvas.drawGraphicMultiLine(fileIconPt2, iconStartX + iconWidth, yPixel + 0, fileIconPt2Width);
			}

			// Draw filename
			char finalChar = isFolder ? 0 : '.';
searchForChar:
			char const* finalCharAddress = strrchr(displayName, finalChar);
			if (!finalCharAddress) { // Shouldn't happen... or maybe for in-memory presets?
				finalChar = 0;
				goto searchForChar;
			}

			int32_t displayStringLength = (uint32_t)finalCharAddress - (uint32_t)displayName;

			if (isSelectedIndex) {
				drawTextForOLEDEditing(textStartX, OLED_MAIN_WIDTH_PIXELS, yPixel, maxChars, canvas);
				if (!enteredTextEditPos) {
					deluge::hid::display::OLED::setupSideScroller(0, enteredText.get(), textStartX,
					                                              OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
					                                              kTextSpacingX, kTextSpacingY, true);
				}
			}
			else {
				canvas.drawString(std::string_view{displayName, static_cast<size_t>(displayStringLength)}, textStartX,
				                  yPixel, kTextSpacingX, kTextSpacingY);
			}

			yPixel += kTextSpacingY;
		}
	}
}

// Supply a string with no prefix (e.g. SONG), and no file extension.
// If name is non-numeric, a slot of -1 will be returned.
Slot Browser::getSlot(char const* displayName) {

	char const* charPos = displayName;
	if (*charPos == '0') { // If first digit is 0, then no more digits allowed.
		charPos++;
	}
	else { // Otherwise, up to 3 digits allowed.
		while (*charPos >= '0' && *charPos <= '9' && charPos < (displayName + 3)) {
			charPos++;
		}
	}

	int32_t numDigitsFound = charPos - displayName;

	Slot toReturn;

	if (!numDigitsFound) { // We are required to have found at least 1 digit.
nonNumeric:
		toReturn.slot = -1;
doReturn:
		return toReturn;
	}

	char thisSlotNumber[4];
	memcpy(thisSlotNumber, displayName, numDigitsFound);
	thisSlotNumber[numDigitsFound] = 0;
	toReturn.slot = stringToInt(thisSlotNumber);

	// Get the file's subslot
	uint8_t subSlotChar = *charPos;
	switch (subSlotChar) {

	case 'a' ... 'z':
		subSlotChar -= 32;
		// No break.

	case 'A' ... 'Z': {
		toReturn.subSlot = subSlotChar - 'A';
		charPos++;
		char nextChar = *charPos;
		if (nextChar) {
			goto nonNumeric; // Ensure no more characters
		}
		break;
	}

		// case '.':
		// if (strchr(charPos + 1, '.')) goto nonNumeric; // Ensure no more dots after this dot.
		//  No break.

	case 0:
		toReturn.subSlot = -1;
		break;

	default:
		goto nonNumeric; // Ensure no more characters
	}

	goto doReturn;
}

void Browser::displayText(bool blinkImmediately) {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		if (arrivedAtFileByTyping || qwertyVisible) {
			if (!arrivedAtFileByTyping) {
				// This means a key has been hit while browsing
				// to bring up the keyboard, so set position to -1
				// this might not be neccesary?
				numberEditPos = -1;
			}
			QwertyUI::displayText(blinkImmediately);
		}
		else {
			if (enteredText.isEmpty() && fileIndexSelected == -1) {
				display->setText("----");
			}
			else {

				if (filePrefix) {

					Slot thisSlot = getSlot(enteredText.get());
					if (thisSlot.slot >= 0) {
						display->setTextAsSlot(thisSlot.slot, thisSlot.subSlot, (fileIndexSelected != -1), true,
						                       numberEditPos, blinkImmediately);
						return;
					}
				}
				int16_t scrollStart = enteredTextEditPos;
				// if the first difference would be visible on
				// screen anyway, start scroll from the beginning
				if (enteredTextEditPos < 3) {
					scrollStart = 0;
				}
				else {
					// provide some context in case the post-fix is long
					scrollStart = enteredTextEditPos - 2;
				}

				scrollingText = display->setScrollingText(enteredText.get(), scrollStart);
			}
		}
	}
}

FileItem* Browser::getCurrentFileItem() {
	if (fileIndexSelected == -1) {
		return nullptr;
	}
	return (FileItem*)fileItems.getElementAddress(fileIndexSelected);
}

// This and its individual contents are frequently overridden by child classes.
ActionResult Browser::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Select encoder
	if (b == SELECT_ENC) {
		return mainButtonAction(on);
	}

	// Save button, to delete file
	else if (b == SAVE && Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
		if (!currentUIMode && on) {
			FileItem* currentFileItem = getCurrentFileItem();
			if (currentFileItem) {
				if (currentFileItem->isFolder) {
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_FOLDERS_CANNOT_BE_DELETED_ON_THE_DELUGE));
					return ActionResult::DEALT_WITH;
				}
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				// deletes the underlying item
				goIntoDeleteFileContextMenu();
			}
		}
	}

	else if (b == SHIFT && on == false) {
		if (loading_delayed_during_fast_scroll) {
			folderContentsReady(0); // this makes it so the preview will load for all file types, including songs.
			loading_delayed_during_fast_scroll = false;
		}
		return ActionResult::NOT_DEALT_WITH; // Let normal shift logic handle sticky shift
	}

	// Back button
	else if (b == BACK) {
		if (on && !currentUIMode) {
			return backButtonAction();
		}
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

ActionResult Browser::padAction(int32_t x, int32_t y, int32_t on) {

	if (favouritesVisible && y == favouriteRow && on) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (Buttons::isShiftButtonPressed()) {
			String filePath;
			Error error = getCurrentFilePath(&filePath);
			if (error != Error::NONE) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_ERROR_FILE_NOT_FOUND));
			}
			if (favouritesManager.isEmpty(x)) {
				if (!getCurrentFileItem()->isFolder) {
					favouritesManager.setFavorite(x, FavouritesManager::favouriteDefaultColor, filePath.get());
					favouritesChanged();
				}
			}
			else {
				favouritesManager.unsetFavorite(x);
				favouritesChanged();
			}
		}
		else {
			const std::string favoritePath = favouritesManager.getFavoriteFilename(x);
			favouritesChanged();
			if (!favoritePath.empty()) {
				setFileByFullPath(outputTypeToLoad, favoritePath.c_str());
			}
			else {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_FAVOURITES_EMPTY));
			}
		}
		return ActionResult::DEALT_WITH;
	}
	else if (favouritesVisible && banksVisible && y == favouriteBankRow && on) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		favouritesManager.selectFavouritesBank(x);
		favouritesChanged();
		return ActionResult::DEALT_WITH;
	}
	else {
		return QwertyUI::padAction(x, y, on);
	}
	return ActionResult::DEALT_WITH;
}

void Browser::favouritesChanged() {
	favouritesVisible = true;
	renderFavourites();
}

ActionResult Browser::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (favouritesVisible) {
		if (Buttons::isShiftButtonPressed()) {
			if (favouritesManager.currentFavouriteNumber.has_value()) {
				favouritesManager.changeColour(favouritesManager.currentFavouriteNumber.value(), offset);
				favouritesChanged();
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

ActionResult Browser::mainButtonAction(bool on) { // specifically the select encoder press
	// Press down
	if (on) {
		if (currentUIMode == UI_MODE_NONE) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, LONG_PRESS_DURATION);
			currentUIMode = UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS;
		}
	}

	// Release press
	else {
		if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			currentUIMode = UI_MODE_NONE;
			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);

			// Trigger delayed loading if necessary, but not if we're entering a folder,
			// since we want to be able to use the select button for that
			// while doing fast scrolling without loading the first file in a folder
			if (loading_delayed_during_fast_scroll) {
				FileItem* currentFileItem = getCurrentFileItem();
				if (!currentFileItem || !currentFileItem->isFolder) {
					currentFileChanged(0); // Signal that we want to load the current file
					loading_delayed_during_fast_scroll = false;
				}
			}

			enterKeyPress();
		}
	}

	return ActionResult::DEALT_WITH;
}

// Virtual function - may be overridden, by child classes that need to do more stuff, e.g. SampleBrowser needs to mute
// any previewing Sample.
ActionResult Browser::backButtonAction() {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	Error error = goUpOneDirectoryLevel();
	if (error != Error::NONE) {
		exitAction();
	}

	return ActionResult::DEALT_WITH;
}

// Virtual function - may be overridden, by child classes that need to do more stuff on exit.
void Browser::exitAction() {
	close();
}

void Browser::goIntoDeleteFileContextMenu() {
	using namespace gui;
	bool available = context_menu::deleteFile.setupAndCheckAvailability();

	if (available) {
		display->setNextTransitionDirection(1);
		openUI(&context_menu::deleteFile);
	}
}

Error Browser::setEnteredTextFromCurrentFilename() {
	FileItem* currentFileItem = getCurrentFileItem();

	Error error = enteredText.set(currentFileItem->displayName);
	if (error != Error::NONE) {
		return error;
	}

	// Cut off the file extension
	if (!currentFileItem->isFolder) {
		char const* enteredTextChars = enteredText.get();
		char const* dotAddress = strrchr(enteredTextChars, '.');
		if (dotAddress) {
			int32_t dotPos = (uint32_t)dotAddress - (uint32_t)enteredTextChars;
			error = enteredText.shorten(dotPos);
			if (error != Error::NONE) {
				return error;
			}
		}
	}

	return Error::NONE;
}

Error Browser::goIntoFolder(char const* folderName) {
	Error error;

	if (!currentDir.isEmpty()) {
		error = currentDir.concatenate("/");
		if (error != Error::NONE) {
			return error;
		}
	}

	error = currentDir.concatenate(folderName);
	if (error != Error::NONE) {
		return error;
	}

	enteredText.clear();
	enteredTextEditPos = 0;

	display->setNextTransitionDirection(1);
	error = arrivedInNewFolder(1);
	if (display->haveOLED()) {
		if (error == Error::NONE) {
			renderUIsForOled();
		}
	}

	return error;
}

Error Browser::goUpOneDirectoryLevel() {

	char const* currentDirChars = currentDir.get();
	char const* slashAddress = strrchr(currentDirChars, '/');
	if (!slashAddress || slashAddress == currentDirChars) {
		return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
	}

	int32_t slashPos = (uint32_t)slashAddress - (uint32_t)currentDirChars;
	Error error = enteredText.set(slashAddress + 1);
	if (error != Error::NONE) {
		return error;
	}
	currentDir.shorten(slashPos);
	if (error != Error::NONE) {
		return error;
	}
	enteredTextEditPos = 0;

	display->setNextTransitionDirection(-1);
	error = arrivedInNewFolder(-1, enteredText.get());
	if (display->haveOLED()) {
		if (error == Error::NONE) {
			renderUIsForOled();
		}
	}
	return error;
}

Error Browser::createFolder() {
	displayText();

	String newDirPath;
	Error error;

	newDirPath.set(&currentDir);
	if (!newDirPath.isEmpty()) {
		error = newDirPath.concatenate("/");
		if (error != Error::NONE) {
			return error;
		}
	}

	error = newDirPath.concatenate(&enteredText);
	if (error != Error::NONE) {
		return error;
	}

	FRESULT result = f_mkdir(newDirPath.get());
	if (result) {
		return Error::SD_CARD;
	}

	error = goIntoFolder(enteredText.get());

	return error;
}

Error Browser::createFoldersRecursiveIfNotExists(const char* path) {
	if (!path || *path == '\0') {
		return Error::UNSPECIFIED;
	}

	char tempPath[256];
	size_t len = 0;

	// Iterate through the path and create directories step by step
	for (const char* p = path; *p; ++p) {
		tempPath[len++] = *p;
		tempPath[len] = '\0';

		if (*p == '/' || *(p + 1) == '\0') {
			FRESULT result = f_mkdir(tempPath);
			if (result != FR_OK && result != FR_EXIST) {
				return fresultToDelugeErrorCode(FR_NO_PATH);
			}
		}
	}
	return Error::NONE;
}

void Browser::sortFileItems() {
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	fileItems.sortForStrings();

	// If we're just wanting to look to one side or the other of a given filename, then delete everything in the other
	// direction.
	if (filenameToStartSearchAt && *filenameToStartSearchAt) {

		if (catalogSearchDirection == CATALOG_SEARCH_LEFT) {
			bool foundExact;
			int32_t searchIndex = fileItems.search(filenameToStartSearchAt, &foundExact);
			// Check for duplicates.
			if (foundExact) {
				int32_t prevIndex = searchIndex - 1;
				if (prevIndex >= 0) {
					FileItem* prevItem = (FileItem*)fileItems.getElementAddress(prevIndex);
					if (!strcmpspecial(prevItem->displayName, filenameToStartSearchAt)) {
						searchIndex = prevIndex;
					}
				}
			}
			int32_t numToDelete = fileItems.getNumElements() - searchIndex;
			if (numToDelete > 0) {
				deleteSomeFileItems(searchIndex, fileItems.getNumElements());
				numFileItemsDeletedAtEnd += numToDelete;
			}
		}
		else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
			bool foundExact;
			int32_t searchIndex = fileItems.search(filenameToStartSearchAt, &foundExact);
			// Check for duplicates.
			if (foundExact) {
				int32_t nextIndex = searchIndex + 1;
				if (nextIndex < fileItems.getNumElements()) {
					FileItem* nextItem = (FileItem*)fileItems.getElementAddress(nextIndex);
					if (!strcmpspecial(nextItem->displayName, filenameToStartSearchAt)) {
						searchIndex = nextIndex;
					}
				}
			}
			int32_t numToDelete = searchIndex + (int32_t)foundExact;
			if (numToDelete > 0) {
				deleteSomeFileItems(0, numToDelete);
				numFileItemsDeletedAtStart += numToDelete;
			}
		}
	}

	// If we'd previously deleted items from either end of the list (apart from due to search direction as above),
	// we need to now delete any items which would have fallen in that region.
	if (lastFileItemRemaining) {
		int32_t searchIndex = fileItems.search(lastFileItemRemaining);
		int32_t itemsToDeleteAtEnd = fileItems.getNumElements() - searchIndex - 1;
		if (itemsToDeleteAtEnd > 0) {
			deleteSomeFileItems(searchIndex + 1, fileItems.getNumElements());
			numFileItemsDeletedAtEnd += itemsToDeleteAtEnd;
		}
	}

	if (firstFileItemRemaining) {
		int32_t itemsToDeleteAtStart = fileItems.search(firstFileItemRemaining);
		if (itemsToDeleteAtStart) {
			deleteSomeFileItems(0, itemsToDeleteAtStart);
			numFileItemsDeletedAtStart += itemsToDeleteAtStart;
		}
	}
}
