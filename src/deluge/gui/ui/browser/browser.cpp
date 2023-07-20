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

#include "definitions_cxx.hpp"
#include "storage/audio/audio_file_manager.h"
#include "gui/ui/browser/browser.h"
#include "hid/matrix/matrix_driver.h"
#include "gui/context_menu/delete_file.h"
#include "hid/display/numeric_driver.h"
#include "hid/buttons.h"
#include <string.h>
#include "gui/ui_timer_manager.h"
#include "extern.h"
#include "util/functions.h"
#include "storage/storage_manager.h"
#include "hid/display/oled.h"
#include <new>
#include "storage/file_item.h"
#include "model/song/song.h"
#include "model/instrument/instrument.h"
#include "gui/views/view.h"
#include "hid/encoders.h"
#include "io/uart/uart.h"
#include "processing/engines/audio_engine.h"

extern "C" {
#include "drivers/uart/uart.h"
}

using namespace deluge;

String Browser::currentDir{};
bool Browser::qwertyVisible;

CStringArray Browser::fileItems{sizeof(FileItem)};
int Browser::scrollPosVertical;
int Browser::fileIndexSelected;
int Browser::numCharsInPrefix;
bool Browser::arrivedAtFileByTyping;
int Browser::numFileItemsDeletedAtStart;
int Browser::numFileItemsDeletedAtEnd;
char const* Browser::firstFileItemRemaining;
char const* Browser::lastFileItemRemaining;
InstrumentType Browser::instrumentTypeToLoad;
char const** Browser::allowedFileExtensions;
bool Browser::allowFoldersSharingNameWithFile;
char const* Browser::filenameToStartSearchAt;

#if HAVE_OLED
#else
int8_t Browser::numberEditPos;
NumericLayerScrollingText* Browser::scrollingText;
#endif

char const* allowedFileExtensionsXML[] = {"XML", NULL};

Browser::Browser() {
#if HAVE_OLED
	fileIcon = OLED::songIcon;
#else
	scrollingText = NULL;
	shouldWrapFolderContents = true;
#endif
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
#if HAVE_OLED
#else
	numberEditPos = -1;
#endif
	return QwertyUI::opened();
}

void Browser::close() {
	emptyFileItems();
	QwertyUI::close();
}

void Browser::emptyFileItems() {

	AudioEngine::logAction("emptyFileItems");

	for (int i = 0; i < fileItems.getNumElements();) {
		FileItem* item = (FileItem*)fileItems.getElementAddress(i);
		item->~FileItem();

		i++;
		if (!(i & 63)) { //  &127 was even fine, even with only -Og compiler optimization.
			AudioEngine::logAction("emptyFileItems in loop");
			AudioEngine::routineWithClusterLoading();
		}
	}

	AudioEngine::logAction("emptyFileItems 2");

	fileItems.empty();

	AudioEngine::logAction("emptyFileItems 3");
}

void Browser::deleteSomeFileItems(int startAt, int stopAt) {

	// Call destructors.
	for (int i = startAt; i < stopAt;) {
		FileItem* item = (FileItem*)fileItems.getElementAddress(i);
		item->~FileItem();

		i++;
		if (!(i & 63)) { //  &127 was even fine, even with only -Og compiler optimization.
			AudioEngine::routineWithClusterLoading();
		}
	}

	fileItems.deleteAtIndex(startAt, stopAt - startAt);
}

int maxNumFileItemsNow;

int catalogSearchDirection;

FileItem* Browser::getNewFileItem() {
	bool alreadyCulled = false;

	if (fileItems.getNumElements() >= maxNumFileItemsNow) {
doCull:
		cullSomeFileItems();
		alreadyCulled = true;
	}

	int newIndex = fileItems.getNumElements();
	int error = fileItems.insertAtIndex(newIndex);
	if (error) {
		if (alreadyCulled) {
			return NULL;
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

	int startAt, stopAt;

	int numFileItemsDeletingNow = fileItems.getNumElements() - (maxNumFileItemsNow >> 1); // May get modified below.
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

	// Or if we've been using a search term *and* searching both directions, try to tend towards keeping equal amounts of FileIems either side.
	else {

		shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
		octaveStartsFromA = false;
		int foundIndex = fileItems.search(filenameToStartSearchAt);

		// If search-item is in second half, delete from start.
		if ((foundIndex << 1) >= fileItems.getNumElements()) {
			int newNumFilesDeleting = foundIndex >> 1; // Delete half the existing items to the left of the search-item.
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
			int newNumFilesDeleting = (fileItems.getNumElements() - foundIndex)
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

int Browser::readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
                                    char const** allowedFileExtensionsHere, char const* filenameToStartAt,
                                    int newMaxNumFileItems, int newCatalogSearchDirection) {

	AudioEngine::logAction("readFileItemsForFolder");

	emptyFileItems();

	int error = storageManager.initSD();
	if (error) {
		return error;
	}

	FRESULT result = f_opendir(&staticDIR, currentDir.get());
	if (result) {
		return fresultToDelugeErrorCode(result);
	}

	/*
	error = fileItems.allocateMemory(FILE_ITEMS_MAX_NUM_ELEMENTS, false);
	if (error) {
		f_closedir(&staticDIR);
		return error;
	}
	*/

	numFileItemsDeletedAtStart = 0;
	numFileItemsDeletedAtEnd = 0;
	firstFileItemRemaining = NULL;
	lastFileItemRemaining = NULL;
	catalogSearchDirection = newCatalogSearchDirection;
	maxNumFileItemsNow = newMaxNumFileItems;
	filenameToStartSearchAt = filenameToStartAt;

#if !HAVE_OLED
	int filePrefixLength;
	if (filePrefixHere) {
		filePrefixLength = strlen(filePrefixHere);
	}
#endif

	while (true) {
		AudioEngine::logAction("while loop");

		audioFileManager.loadAnyEnqueuedClusters();
		FilePointer thisFilePointer;

		result = f_readdir_get_filepointer(&staticDIR, &staticFNO, &thisFilePointer); /* Read a directory item */

		if (result != FR_OK || staticFNO.fname[0] == 0) {
			break; /* Break on error or end of dir */
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
			error = ERROR_INSUFFICIENT_RAM;
			break;
		}
		error = thisItem->filename.set(staticFNO.fname);
		if (error) {
			break;
		}
		thisItem->isFolder = isFolder;
		thisItem->filePointer = thisFilePointer;

		char const* storedFilenameChars = thisItem->filename.get();
#if !HAVE_OLED
		if (filePrefixHere) {
			if (memcasecmp(storedFilenameChars, filePrefixHere, filePrefixLength)) {
				goto nonNumericFile;
			}

			char* dotAddress = strrchr(storedFilenameChars, '.');
			if (!dotAddress) {
				goto nonNumericFile; // Shouldn't happen?
			}

			int dotPos = (uint32_t)dotAddress - (uint32_t)storedFilenameChars;
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
		else
#endif
		{
nonNumericFile:
			thisItem->displayName = storedFilenameChars;
		}
	}

	f_closedir(&staticDIR);

	if (error) {
		emptyFileItems();
	}

	return error;
}

void Browser::deleteFolderAndDuplicateItems(int instrumentAvailabilityRequirement) {
	int writeI = 0;
	FileItem* nextItem = (FileItem*)fileItems.getElementAddress(0);

	for (int readI = 0; readI < fileItems.getNumElements(); readI++) {
		FileItem* readItem = nextItem;

		// If there's a next item after "this" item, to compare it to...
		if (readI < fileItems.getNumElements() - 1) {
			nextItem = (FileItem*)fileItems.getElementAddress(readI + 1);

			// If we're a folder, and the next item is a file of the same name, delete this item.
			if (readItem->isFolder) {
				if (!nextItem->isFolder) {
					int nameLength = readItem->filename.getLength();
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
						//if (readItem->filename.equalsCaseIrrespective(&nextItem->filename)) {
						nextItem->~FileItem();
						readI++;
						nextItem = (FileItem*)fileItems.getElementAddress(readI + 1);
						// That may set it to an invalid address, but in that case, it won't get read.
					}
				}

checkAvailabilityRequirement:
				// Check Instrument's availabilityRequirement
				if (readItem->instrumentAlreadyInSong) {
					if (instrumentAvailabilityRequirement == AVAILABILITY_INSTRUMENT_UNUSED) {
deleteThisItem:
						readItem->~FileItem();
						continue;
					}
					else if (instrumentAvailabilityRequirement == AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION) {
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

	int numToDelete = fileItems.getNumElements() - writeI;
	if (numToDelete > 0) {
		fileItems.deleteAtIndex(writeI, numToDelete);
	}

	// Our system of keeping FileItems from getting too full by deleting elements from its ends as we go could have caused bad results at the edges of the above,
	// so delete a further one element at each end as needed.
	if (firstFileItemRemaining) {
		fileItems.deleteAtIndex(0);
	}
	if (lastFileItemRemaining) {
		fileItems.deleteAtIndex(fileItems.getNumElements() - 1);
	}
}

// song may be supplied as NULL, in which case it won't be searched for Instruments; sometimes this will get called when the currentSong is not set up.
int Browser::readFileItemsFromFolderAndMemory(Song* song, InstrumentType instrumentType, char const* filePrefixHere,
                                              char const* filenameToStartAt, char const* defaultDirToAlsoTry,
                                              bool allowFolders, int availabilityRequirement,
                                              int newCatalogSearchDirection) {
	// filenameToStartAt should have .XML at the end of it.
	bool triedCreatingFolder = false;

tryReadingItems:
	int error = readFileItemsForFolder(filePrefixHere, allowFolders, allowedFileExtensions, filenameToStartAt,
	                                   FILE_ITEMS_MAX_NUM_ELEMENTS, newCatalogSearchDirection);
	if (error) {

		// If folder didn't exist, try our alternative one if there is one.
		if (error == ERROR_FOLDER_DOESNT_EXIST) {
			if (defaultDirToAlsoTry) {
				// ... only if we haven't already tried the alternative folder.
				if (!currentDir.equalsCaseIrrespective(defaultDirToAlsoTry)) {
					filenameToStartAt = NULL;
					int error = currentDir.set(defaultDirToAlsoTry);
					if (error) {
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

	if (song && instrumentType != InstrumentType::NONE) {
		error = song->addInstrumentsToFileItems(instrumentType);
		if (error) {
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
				    AVAILABILITY_ANY); // I think this is right - was AVAILABILITY_INSTRUMENT_UNUSED until 2023-01
			}
		}
	}
	return NO_ERROR;
}

// If HAVE_OLED, then you should make sure renderUIsForOLED() gets called after this.
// instrumentTypeToLoad must be set before calling this.
int Browser::arrivedInNewFolder(int direction, char const* filenameToStartAt, char const* defaultDirToAlsoTry) {
	arrivedAtFileByTyping = false;

	if (!qwertyAlwaysVisible) {
		qwertyVisible = false;
	}

	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

tryReadingItems:
	bool doWeHaveASearchString = (filenameToStartAt && *filenameToStartAt);
	int newCatalogSearchDirection = doWeHaveASearchString ? CATALOG_SEARCH_BOTH : CATALOG_SEARCH_RIGHT;
	int error = readFileItemsFromFolderAndMemory(currentSong, instrumentTypeToLoad, filePrefix, filenameToStartAt,
	                                             defaultDirToAlsoTry, true, 0, newCatalogSearchDirection);
	if (error) {
gotErrorAfterAllocating:
		emptyFileItems();
		return error;
	}

	enteredTextEditPos = 0;
#if HAVE_OLED
	scrollPosHorizontal = 0;
#endif

	bool foundExact = false;
	if (fileItems.getNumElements()) {
		fileIndexSelected = 0;

		if (!doWeHaveASearchString) {
noExactFileFound:
			// We did not find exact file.
			// Normally, we'll need to just use one of the ones we found. (That's just always the first one, I think...)
			if (!mayDefaultToBrandNewNameOnEntry || direction) {

				// But since we're going to just use the first file, if we've deleted items at the start (meaning we had a search string), we need to go back and get them.
				if (numFileItemsDeletedAtStart) {
					filenameToStartAt = NULL;
					goto tryReadingItems;
				}
setEnteredTextAndUseFoundFile:
				error = setEnteredTextFromCurrentFilename();
				if (error) {
					goto gotErrorAfterAllocating;
				}
useFoundFile:
				scrollPosVertical = fileIndexSelected;
#if BROWSER_AND_MENU_NUM_LINES > 1
				int lastAllowed = fileItems.getNumElements() - BROWSER_AND_MENU_NUM_LINES;
				if (scrollPosVertical > lastAllowed) {
					scrollPosVertical = lastAllowed;
					if (scrollPosVertical < 0) {
						scrollPosVertical = 0;
					}
				}
#endif
				goto everythingFinalized;
			}

			// But sometimes...
			else {
				// Ok so we didn't find an exact file, we've just entered the browser, and we're allowed new names.
				// So, choose a brand new name (if there wasn't already a new one nominated).
				goto pickBrandNewNameIfNoneNominated;
			}
		}

		int i = fileItems.search(filenameToStartAt, &foundExact);
		if (!foundExact) {
			goto noExactFileFound;
		}

		fileIndexSelected = i;

		// Usually we'll just use that file.
		if (!mayDefaultToBrandNewNameOnEntry || direction) {
			goto setEnteredTextAndUseFoundFile;
		}

		// We found an exact file. But if we've just entered the Browser and are allowed, then we need to find a new subslot variation.
		// Come up with a new name variation.
		error = setEnteredTextFromCurrentFilename();
		if (error) {
			goto gotErrorAfterAllocating;
		}
#if 1 || !HAVE_OLED
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
#if HAVE_OLED
			*(nameBufferPos++) = 'S';
			*(nameBufferPos++) = 'O';
			*(nameBufferPos++) = 'N';
			*(nameBufferPos++) = 'G';
#endif
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
			if (error) {
				goto gotErrorAfterAllocating;
			}
		}
#else
		int length = enteredText.getLength();
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
#endif
		else {
doNormal: //FileItem* currentFile = (FileItem*)fileItems.getElementAddress(fileIndexSelected);
			String endSearchString;
			//error = currentFile->getFilenameWithoutExtension(&endSearchString);		if (error) goto gotErrorAfterAllocating;
			endSearchString.set(&enteredText);

			// Did it already have an underscore at the end with a positive integer after it?
			char const* endSearchStringChars = endSearchString.get();
			char delimeterChar = '_';
tryAgain:
			char const* delimeterAddress = strrchr(endSearchStringChars, delimeterChar);
			int numberStartPos;
			if (delimeterAddress) {
				int underscorePos = delimeterAddress - endSearchStringChars;

				// Ok, it what comes after the underscore a positive integer?
				int number = stringToUIntOrError(delimeterAddress + 1);
				if (number < 0) {
					goto noNumberYet;
				}

				numberStartPos = underscorePos + 1;
				error = endSearchString.concatenateAtPos(":", numberStartPos);
				if (error) {
					goto gotErrorAfterAllocating; // Colon is the next character after the ascii digits, so searching for this will get us past the final number present.
				}
			}
			else {
noNumberYet:
				if (delimeterChar == '_') {
					delimeterChar = ' ';
					goto tryAgain;
				}
				numberStartPos = endSearchString.getLength() + 1;
				error = endSearchString.concatenate(HAVE_OLED ? " :" : "_:");
				if (error) {
					goto gotErrorAfterAllocating; // See above comment.
				}
			}

			int searchResult = fileItems.search(endSearchString.get());
#if ALPHA_OR_BETA_VERSION
			if (searchResult <= 0) {
				numericDriver.freezeWithError("E448");
				error = ERROR_BUG;
				goto gotErrorAfterAllocating;
			}
#endif
			FileItem* prevFile = (FileItem*)fileItems.getElementAddress(searchResult - 1);
			String prevFilename;
			error = prevFile->getFilenameWithoutExtension(&prevFilename);
			if (error) {
				goto gotErrorAfterAllocating;
			}
			char const* prevFilenameChars = prevFilename.get();
			int number;
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
			if (error) {
				goto gotErrorAfterAllocating;
			}
			error = enteredText.concatenateInt(number);
			if (error) {
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
				error = getUnusedSlot(InstrumentType::NONE, &enteredText, "SONG");
				if (error) {
					goto gotErrorAfterAllocating;
				}

				// Because that will have cleared out all the FileItems, we need to get them again. Actually there would kinda be a way around doing this...
				error = readFileItemsFromFolderAndMemory(currentSong, InstrumentType::NONE, "SONG", enteredText.get(), NULL, false, 0,
				                                         CATALOG_SEARCH_BOTH);
				if (error) {
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
	folderContentsReady(direction);

#if !HAVE_OLED
	displayText();
#endif
	return NO_ERROR;
}

// You must set currentDir before calling this.
int Browser::getUnusedSlot(InstrumentType instrumentType, String* newName, char const* thingName) {

#if HAVE_OLED
	char filenameToStartAt[6]; // thingName is max 4 chars.
	strcpy(filenameToStartAt, thingName);
	strcat(filenameToStartAt, ":");
#else
	char const* filenameToStartAt = ":";    // Colon is the first character after the digits
#endif

	int error = readFileItemsFromFolderAndMemory(currentSong, instrumentType, getThingName(instrumentType),
	                                             filenameToStartAt, NULL, false, 0, CATALOG_SEARCH_LEFT);

	if (error) {
doReturn:
		return error;
	}

	sortFileItems();

#if HAVE_OLED
	int freeSlotNumber = 1;
	int minNumDigits = 1;
	if (fileItems.getNumElements()) {
		FileItem* fileItem = (FileItem*)fileItems.getElementAddress(fileItems.getNumElements() - 1);
		String displayName;
		error = fileItem->getDisplayNameWithoutExtension(&displayName);
		if (error) {
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
	if (error) {
		goto emptyFileItemsAndReturn;
	}
	error = newName->concatenateInt(freeSlotNumber, minNumDigits);

#else
	int nextHigherSlotFound = numSongSlots; // I think the use of this is a bit deprecated...

	int i = fileItems.getNumElements();
goBackOne
    : // Ok, due to not bothering to reload fileItems if we need to look too far back, we may sometimes fail to see an empty slot further back when later ones are taken. Oh well.
	i--;
	int freeSlotNumber;
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
		if (error) {
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
#endif

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

	int newFileIndex;

	if (fileIndexSelected < 0) { // If no file selected and we were typing a new name?
		if (!fileItems.getNumElements()) {
			return;
		}

		newFileIndex = fileItems.search(enteredText.get());
		if (offset < 0) {
			newFileIndex--;
		}
	}
	else {
		// If user is holding shift, skip past any subslots. And on numeric Deluge, user may have chosen one digit to "edit".
#if !HAVE_OLED
		// TODO: deal with deleted FileItems here...
		int numberEditPosNow = numberEditPos;
		if (Buttons::isShiftButtonPressed() && numberEditPosNow == -1) {
			numberEditPosNow = 0;
		}

		if (numberEditPosNow != -1) {
			Slot thisSlot = getSlot(enteredText.get());
			if (thisSlot.slot < 0) {
				goto nonNumeric;
			}

			thisSlot.subSlot = -1;
			switch (numberEditPosNow) {
			case 0:
				thisSlot.slot += offset;
				break;

			case 1:
				thisSlot.slot = (thisSlot.slot / 10 + offset) * 10;
				break;

			case 2:
				thisSlot.slot = (thisSlot.slot / 100 + offset) * 100;
				break;

			default:
				__builtin_unreachable();
			}

			char searchString[6];
			char* searchStringNumbersStart = searchString;
			int minNumDigits = 1;
#else
		if (filePrefix && Buttons::isShiftButtonPressed()) {
			int filePrefixLength = strlen(filePrefix);
			char const* enteredTextChars = enteredText.get();
			if (memcasecmp(filePrefix, enteredTextChars, filePrefixLength)) {
				goto nonNumeric;
			}
			Slot thisSlot = getSlot(&enteredTextChars[filePrefixLength]);
			if (thisSlot.slot < 0) {
				goto nonNumeric;
			}
			thisSlot.slot += offset;

			char searchString[9];
			memcpy(searchString, filePrefix, filePrefixLength);
			char* searchStringNumbersStart = searchString + filePrefixLength;
			int minNumDigits = 3;
#endif
			intToString(thisSlot.slot, searchStringNumbersStart, minNumDigits);
			if (offset < 0) {
				char* pos = strchr(searchStringNumbersStart, 0);
				*pos = 'A';
				pos++;
				*pos = 0;
			}
			newFileIndex = fileItems.search(searchString);
			if (offset < 0) {
				newFileIndex--;
			}
		}
		else {
nonNumeric:
			newFileIndex = fileIndexSelected + offset;
		}
	}

	int newCatalogSearchDirection;
	int error;

	if (newFileIndex < 0) {
		if (numFileItemsDeletedAtStart) {
			scrollPosVertical = 9999;

tryReadingItems:
			uartPrintln("reloading");
			error = readFileItemsFromFolderAndMemory(currentSong, instrumentTypeToLoad, filePrefix, enteredText.get(),
			                                         NULL, true, 0, CATALOG_SEARCH_BOTH);
			if (error) {
gotErrorAfterAllocating:
				emptyFileItems();
				return;
				// TODO - need to close UI or something?
			}

			newFileIndex = fileItems.search(enteredText.get()) + offset;
		}

#if !HAVE_OLED
		else if (!shouldWrapFolderContents) {
			return;
		}
#endif

		else { // Wrap to end
			scrollPosVertical = 0;

			if (numFileItemsDeletedAtEnd) {
				newCatalogSearchDirection = CATALOG_SEARCH_LEFT;
searchFromOneEnd:
				uartPrintln("reloading and wrap");
				error = readFileItemsFromFolderAndMemory(currentSong, instrumentTypeToLoad, filePrefix, NULL, NULL,
				                                         true, 0, newCatalogSearchDirection); // Load from start
				if (error) {
					goto gotErrorAfterAllocating;
				}

				newFileIndex =
				    (newCatalogSearchDirection == CATALOG_SEARCH_LEFT) ? (fileItems.getNumElements() - 1) : 0;
			}
			else {
				newFileIndex = fileItems.getNumElements() - 1;
			}
		}
	}

	else if (newFileIndex >= fileItems.getNumElements()) {
		if (numFileItemsDeletedAtEnd) {
			scrollPosVertical = 0;
			goto tryReadingItems;
		}

#if !HAVE_OLED
		else if (!shouldWrapFolderContents) {
			return;
		}
#endif

		else {
			scrollPosVertical = 9999;

			if (numFileItemsDeletedAtStart) {
				newCatalogSearchDirection = CATALOG_SEARCH_RIGHT;
				goto searchFromOneEnd;
			}
			else {
				newFileIndex = 0;
			}
		}
	}

	if (!qwertyAlwaysVisible) {
		qwertyVisible = false;
	}

	fileIndexSelected = newFileIndex;

	if (scrollPosVertical > fileIndexSelected) {
		scrollPosVertical = fileIndexSelected;
	}
	else if (scrollPosVertical < fileIndexSelected - NUM_FILES_ON_SCREEN + 1) {
		scrollPosVertical = fileIndexSelected - NUM_FILES_ON_SCREEN + 1;
	}

	enteredTextEditPos = 0;
#if HAVE_OLED
	scrollPosHorizontal = 0;
#else
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
#endif

	error = setEnteredTextFromCurrentFilename();
	if (error) {
		numericDriver.displayError(error);
		return;
	}

	displayText();
	currentFileChanged(offset);
}

bool Browser::predictExtendedText() {
	int error;
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
	if (error) {
gotError:
		numericDriver.displayError(error);
		return false;
	}

	int numExtraZeroesAdded = 0;

addTildeAndSearch:
	error = searchString.concatenate("~");
	if (error) {
		goto gotError;
	}

	// Ok, search whatever FileItems we currently have in memory.
doSearch:
	int i = fileItems.search(searchString.get());

	// If that search takes us off the right-hand end of the list...
	if (i >= fileItems.getNumElements()) {

		// If we haven't yet done a whole new read from the SD card etc, from within this function, do that now.
		if (!doneNewRead) {
doNewRead:
			doneNewRead = true;
			error = readFileItemsFromFolderAndMemory(
			    currentSong, instrumentTypeToLoad, filePrefix, searchString.get(), NULL, true, 0,
			    CATALOG_SEARCH_BOTH); // This could probably actually be made to work with searching left only...
			if (error) {
gotErrorAfterAllocating:
				emptyFileItems();
				goto gotError;
				// TODO - need to close UI or something?
			}
			goto doSearch;
		}

		// Otherwise if we already tried that, then our whole search is fruitless.
notFound:
		if (false
		    && !mayDefaultToBrandNewNameOnEntry) { // Disabled - now you're again always allowed to type characters even if no such file exists.
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
			if (error) {
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
	if (!HAVE_OLED || scrollPosVertical > i || scrollPosVertical < i - (OLED_HEIGHT_CHARS - 1) + 1) {
		scrollPosVertical = i;
	}

	error = setEnteredTextFromCurrentFilename();
	if (error) {
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
}

#if HAVE_OLED
int textStartX = 14;

void Browser::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	OLED::drawScreenTitle(title);

	int yPixel = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	yPixel += OLED_MAIN_TOPMOST_PIXEL;

	int maxChars = (unsigned int)(OLED_MAIN_WIDTH_PIXELS - textStartX) / (unsigned int)TEXT_SPACING_X;

	bool isFolder = false;
	bool isSelectedIndex = true;
	char const* displayName;
	int o;

	// If we're currently typing a filename which doesn't (yet?) have a file...
	if (fileIndexSelected == -1) {
		displayName = enteredText.get();
		o = OLED_HEIGHT_CHARS; // Make sure below loop doesn't keep looping.
		goto drawAFile;
	}

	else {
		for (o = 0; o < OLED_HEIGHT_CHARS - 1; o++) {
			{
				int i = o + scrollPosVertical;

				if (i >= fileItems.getNumElements()) {
					break;
				}

				FileItem* thisFile = (FileItem*)fileItems.getElementAddress(i);
				isFolder = thisFile->isFolder;
				displayName = thisFile->filename.get();
				isSelectedIndex = (i == fileIndexSelected);
			}
drawAFile:
			// Draw graphic
			uint8_t const* graphic = isFolder ? OLED::folderIcon : fileIcon;
			OLED::drawGraphicMultiLine(graphic, 1, yPixel + 0, 8, OLED::oledMainImage[0]);

			// Draw filename
			char finalChar = isFolder ? 0 : '.';
searchForChar:
			char const* finalCharAddress = strrchr(displayName, finalChar);
			if (!finalCharAddress) { // Shouldn't happen... or maybe for in-memory presets?
				finalChar = 0;
				goto searchForChar;
			}

			int displayStringLength = (uint32_t)finalCharAddress - (uint32_t)displayName;

			if (isSelectedIndex) {
				drawTextForOLEDEditing(textStartX, OLED_MAIN_WIDTH_PIXELS, yPixel, maxChars, OLED::oledMainImage);
				if (!enteredTextEditPos) {
					OLED::setupSideScroller(0, enteredText.get(), textStartX, OLED_MAIN_WIDTH_PIXELS, yPixel,
					                        yPixel + 8, TEXT_SPACING_X, TEXT_SPACING_Y, true);
				}
			}
			else {
				OLED::drawStringFixedLength(displayName, displayStringLength, textStartX, yPixel,
				                            OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
				                            TEXT_SPACING_Y);
			}

			yPixel += TEXT_SPACING_Y;
		}
	}
}

#endif

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

	int numDigitsFound = charPos - displayName;

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

		//case '.':
		//if (strchr(charPos + 1, '.')) goto nonNumeric; // Ensure no more dots after this dot.
		// No break.

	case 0:
		toReturn.subSlot = -1;
		break;

	default:
		goto nonNumeric; // Ensure no more characters
	}

	goto doReturn;
}

void Browser::displayText(bool blinkImmediately) {
#if HAVE_OLED
	renderUIsForOled();
#else
	if (arrivedAtFileByTyping) {
doQWERTYDisplay:
		QwertyUI::displayText(blinkImmediately);
	}
	else {
		if (enteredText.isEmpty() && fileIndexSelected == -1) {
			numericDriver.setText("----");
		}
		else {

			if (filePrefix) {

				Slot thisSlot = getSlot(enteredText.get());
				if (thisSlot.slot < 0) {
					goto nonNumeric;
				}

				numericDriver.setTextAsSlot(thisSlot.slot, thisSlot.subSlot, (fileIndexSelected != -1), true,
				                            numberEditPos, blinkImmediately);
			}

			else {
nonNumeric:
				goto doQWERTYDisplay; // Abandon the below for now.
				numberEditPos = -1;
				if (qwertyVisible) {
					goto doQWERTYDisplay;
				}
				else {
					scrollingText = numericDriver.setScrollingText(enteredText.get(), numCharsInPrefix);
				}
			}
		}
	}
#endif
}

FileItem* Browser::getCurrentFileItem() {
	if (fileIndexSelected == -1) {
		return NULL;
	}
	else {
		return (FileItem*)fileItems.getElementAddress(fileIndexSelected);
	}
}

// This and its individual contents are frequently overridden by child classes.
ActionResult Browser::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	// Select encoder
	if (b == SELECT_ENC) {
		return mainButtonAction(on);
	}

	// Save button, to delete file
	else if (b == SAVE && Buttons::isShiftButtonPressed()) {
		if (!currentUIMode && on) {
			FileItem* currentFileItem = getCurrentFileItem();
			if (currentFileItem) {
				if (currentFileItem->isFolder) {
					numericDriver.displayPopup(HAVE_OLED ? "Folders cannot be deleted on the Deluge" : "CANT");
					return ActionResult::DEALT_WITH;
				}
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				goIntoDeleteFileContextMenu();
			}
		}
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

ActionResult Browser::mainButtonAction(bool on) {
	// Press down
	if (on) {
		if (currentUIMode == UI_MODE_NONE) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, 500);
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
			uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
			enterKeyPress();
		}
	}

	return ActionResult::DEALT_WITH;
}

// Virtual function - may be overridden, by child classes that need to do more stuff, e.g. SampleBrowser needs to mute any previewing Sample.
ActionResult Browser::backButtonAction() {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	int error = goUpOneDirectoryLevel();
	if (error) {
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
		numericDriver.setNextTransitionDirection(1);
		openUI(&context_menu::deleteFile);
	}
}

int Browser::setEnteredTextFromCurrentFilename() {
	FileItem* currentFileItem = getCurrentFileItem();

	int error = enteredText.set(currentFileItem->displayName);
	if (error) {
		return error;
	}

	// Cut off the file extension
	if (!currentFileItem->isFolder) {
		char const* enteredTextChars = enteredText.get();
		char const* dotAddress = strrchr(enteredTextChars, '.');
		if (dotAddress) {
			int dotPos = (uint32_t)dotAddress - (uint32_t)enteredTextChars;
			error = enteredText.shorten(dotPos);
			if (error) {
				return error;
			}
		}
	}

	return NO_ERROR;
}

int Browser::goIntoFolder(char const* folderName) {
	int error;

	if (!currentDir.isEmpty()) {
		error = currentDir.concatenate("/");
		if (error) {
			return error;
		}
	}

	error = currentDir.concatenate(folderName);
	if (error) {
		return error;
	}

	enteredText.clear();
	enteredTextEditPos = 0;

	numericDriver.setNextTransitionDirection(1);
	error = arrivedInNewFolder(1);
#if HAVE_OLED
	if (!error) {
		renderUIsForOled();
	}
#endif
	return error;
}

int Browser::goUpOneDirectoryLevel() {

	char const* currentDirChars = currentDir.get();
	char const* slashAddress = strrchr(currentDirChars, '/');
	if (!slashAddress || slashAddress == currentDirChars) {
		return ERROR_NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
	}

	int slashPos = (uint32_t)slashAddress - (uint32_t)currentDirChars;
	int error = enteredText.set(slashAddress + 1);
	if (error) {
		return error;
	}
	currentDir.shorten(slashPos);
	if (error) {
		return error;
	}
	enteredTextEditPos = 0;

	numericDriver.setNextTransitionDirection(-1);
	error = arrivedInNewFolder(-1, enteredText.get());
#if HAVE_OLED
	if (!error) {
		renderUIsForOled();
	}
#endif
	return error;
}

int Browser::createFolder() {
	displayText();

	String newDirPath;
	int error;

	newDirPath.set(&currentDir);
	if (!newDirPath.isEmpty()) {
		error = newDirPath.concatenate("/");
		if (error) {
			return error;
		}
	}

	error = newDirPath.concatenate(&enteredText);
	if (error) {
		return error;
	}

	FRESULT result = f_mkdir(newDirPath.get());
	if (result) {
		return ERROR_SD_CARD;
	}

	error = goIntoFolder(enteredText.get());

	return error;
}

void Browser::sortFileItems() {
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	fileItems.sortForStrings();

	// If we're just wanting to look to one side or the other of a given filename, then delete everything in the other direction.
	if (filenameToStartSearchAt && *filenameToStartSearchAt) {

		if (catalogSearchDirection == CATALOG_SEARCH_LEFT) {
			bool foundExact;
			int searchIndex = fileItems.search(filenameToStartSearchAt, &foundExact);
			// Check for duplicates.
			if (foundExact) {
				int prevIndex = searchIndex - 1;
				if (prevIndex >= 0) {
					FileItem* prevItem = (FileItem*)fileItems.getElementAddress(prevIndex);
					if (!strcmpspecial(prevItem->displayName, filenameToStartSearchAt)) {
						searchIndex = prevIndex;
					}
				}
			}
			int numToDelete = fileItems.getNumElements() - searchIndex;
			if (numToDelete > 0) {
				deleteSomeFileItems(searchIndex, fileItems.getNumElements());
				numFileItemsDeletedAtEnd += numToDelete;
			}
		}
		else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
			bool foundExact;
			int searchIndex = fileItems.search(filenameToStartSearchAt, &foundExact);
			// Check for duplicates.
			if (foundExact) {
				int nextIndex = searchIndex + 1;
				if (nextIndex < fileItems.getNumElements()) {
					FileItem* nextItem = (FileItem*)fileItems.getElementAddress(nextIndex);
					if (!strcmpspecial(nextItem->displayName, filenameToStartSearchAt)) {
						searchIndex = nextIndex;
					}
				}
			}
			int numToDelete = searchIndex + (int)foundExact;
			if (numToDelete > 0) {
				deleteSomeFileItems(0, numToDelete);
				numFileItemsDeletedAtStart += numToDelete;
			}
		}
	}

	// If we'd previously deleted items from either end of the list (apart from due to search direction as above),
	// we need to now delete any items which would have fallen in that region.
	if (lastFileItemRemaining) {
		int searchIndex = fileItems.search(lastFileItemRemaining);
		int itemsToDeleteAtEnd = fileItems.getNumElements() - searchIndex - 1;
		if (itemsToDeleteAtEnd > 0) {
			deleteSomeFileItems(searchIndex + 1, fileItems.getNumElements());
			numFileItemsDeletedAtEnd += itemsToDeleteAtEnd;
		}
	}

	if (firstFileItemRemaining) {
		int itemsToDeleteAtStart = fileItems.search(firstFileItemRemaining);
		if (itemsToDeleteAtStart) {
			deleteSomeFileItems(0, itemsToDeleteAtStart);
			numFileItemsDeletedAtStart += itemsToDeleteAtStart;
		}
	}
}
