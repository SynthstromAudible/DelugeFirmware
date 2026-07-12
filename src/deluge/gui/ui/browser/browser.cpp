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
#include "gui/ui/browser/default_name.h"
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
#include "util/string.h"
#include "util/try.h"
#include <algorithm>
#include <cstring>
#include <new>

using namespace deluge;

std::string Browser::currentDir{};
bool Browser::qwertyVisible;

deluge::vector<FileItem> Browser::fileItems;
int32_t Browser::scrollPosVertical;
int32_t Browser::fileIndexSelected;
int32_t Browser::numCharsInPrefix;
bool Browser::arrivedAtFileByTyping;
int32_t Browser::numFileItemsDeletedAtStart;
int32_t Browser::numFileItemsDeletedAtEnd;
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
	std::string filePath = getCurrentFilePath();

	FilePointer tempfp;
	bool fileExists = StorageManager::fileExists(filePath.c_str(), &tempfp);
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
	QwertyUI::close();
}

void Browser::emptyFileItems() {

	AudioEngine::logAction("emptyFileItems");

	// Erase in chunks from the end, keeping the audio routine fed in between - this can be thousands of items.
	while (!fileItems.empty()) {
		size_t chunk = std::min<size_t>(256, fileItems.size());
		fileItems.erase(fileItems.end() - chunk, fileItems.end());
		AudioEngine::logAction("emptyFileItems in loop");
		AudioEngine::routineWithClusterLoading();
	}
	fileItems.shrink_to_fit();

	AudioEngine::logAction("emptyFileItems 3");
}

void Browser::deleteSomeFileItems(int32_t startAt, int32_t stopAt) {

	// Erase in chunks from the back of the range, keeping the audio routine fed in between.
	while (stopAt > startAt) {
		int32_t chunkStart = std::max(startAt, stopAt - 256);
		fileItems.erase(fileItems.begin() + chunkStart, fileItems.begin() + stopAt);
		stopAt = chunkStart;
		AudioEngine::routineWithClusterLoading();
	}
}

int32_t maxNumFileItemsNow;

int32_t catalogSearchDirection;

FileItem* Browser::getNewFileItem() {
	bool alreadyCulled = false;

	if (static_cast<int32_t>(fileItems.size()) >= maxNumFileItemsNow) {
doCull:
		cullSomeFileItems();
		alreadyCulled = true;
	}

	try {
		fileItems.push_back(FileItem());
	} catch (deluge::exception&) {
		if (alreadyCulled) {
			return nullptr;
		}
		goto doCull;
	}

	return &fileItems.back();
}

void Browser::cullSomeFileItems() {
	sortFileItems();

	int32_t startAt, stopAt;

	int32_t numFileItemsDeletingNow =
	    static_cast<int32_t>(fileItems.size()) - (maxNumFileItemsNow >> 1); // May get modified below.
	if (numFileItemsDeletingNow <= 0) {
		return;
	}

	// If we already know what side we want to be deleting on...
	if (catalogSearchDirection == CATALOG_SEARCH_LEFT) {
deleteFromLeftSide:
		numFileItemsDeletedAtStart += numFileItemsDeletingNow;
		startAt = 0;
		stopAt = numFileItemsDeletingNow;
		firstFileItemRemaining = (&fileItems[numFileItemsDeletingNow])->displayName;
	}
	else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
deleteFromRightSide:
		numFileItemsDeletedAtEnd += numFileItemsDeletingNow;
		stopAt = static_cast<int32_t>(fileItems.size());
		startAt = stopAt - numFileItemsDeletingNow;
		lastFileItemRemaining = (&fileItems[startAt - 1])->displayName;
	}

	// Or if we've been using a search term *and* searching both directions, try to tend towards keeping equal amounts
	// of FileIems either side.
	else {

		shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
		octaveStartsFromA = false;
		int32_t foundIndex = searchFileItems(filenameToStartSearchAt);

		// If search-item is in second half, delete from start.
		if ((foundIndex << 1) >= static_cast<int32_t>(fileItems.size())) {
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
			int32_t newNumFilesDeleting = (static_cast<int32_t>(fileItems.size()) - foundIndex)
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
	    D_TRY_CATCH(FatFS::Directory::open(currentDir.c_str()), error, { return fatfsErrorToDelugeError(error); });

	numFileItemsDeletedAtStart = 0;
	numFileItemsDeletedAtEnd = 0;
	firstFileItemRemaining = nullptr;
	lastFileItemRemaining = nullptr;
	catalogSearchDirection = newCatalogSearchDirection;
	maxNumFileItemsNow = newMaxNumFileItems;
	filenameToStartSearchAt = filenameToStartAt;

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
		thisItem->filename = staticFNO.fname;
		thisItem->isFolder = isFolder;
		thisItem->filePointer = thisFilePointer;

		// displayName is the sort key, and must equal the real on-card name. The 7SEG short form ("185")
		// is produced at render time, not stored here - storing it made enteredText display-dependent, which is what
		// broke default naming on 7SEG (#1069).
		thisItem->displayName = thisItem->filename.c_str();
	}
	staticDIR.close();

	if (error != Error::NONE) {
		emptyFileItems();
	}

	return error;
}

void Browser::deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement) {
	int32_t writeI = 0;
	FileItem* nextItem = fileItems.data();

	for (int32_t readI = 0; readI < static_cast<int32_t>(fileItems.size()); readI++) {
		FileItem* readItem = nextItem;

		// If there's a next item after "this" item, to compare it to...
		if (readI < static_cast<int32_t>(fileItems.size()) - 1) {
			nextItem = &fileItems[readI + 1];

			// If we're a folder, and the next item is a file of the same name, delete this item.
			if (readItem->isFolder) {
				if (!nextItem->isFolder) {
					int32_t nameLength = readItem->filename.size();
					char const* nextItemFilename = nextItem->filename.c_str();
					if (!memcasecmp(readItem->filename.c_str(), nextItemFilename, nameLength)) {
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
						if (readItem->maybeExistsOnCard && readItem->filePointer.sclust == 0) {
							readItem->filePointer = nextItem->filePointer;
						}
						readI++; // Skip the next item; it'll be overwritten by compaction or erased below.
						nextItem = fileItems.data() + (readI + 1);
						// That may be an out-of-range address, but in that case, it won't get read.
					}
				}

checkAvailabilityRequirement:
				// Check Instrument's availabilityRequirement
				if (readItem->instrumentAlreadyInSong) {
					if (instrumentAvailabilityRequirement == Availability::INSTRUMENT_UNUSED) {
deleteThisItem: // Just skip it; it'll be overwritten by compaction or erased below.
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
					if (nextItem->maybeExistsOnCard && nextItem->filePointer.sclust == 0) {
						nextItem->filePointer = readItem->filePointer;
					}
					goto deleteThisItem;
				}
			}
		}
		else {
			if (readItem->instrument) {
				goto checkAvailabilityRequirement;
			}
		}

		if (&fileItems[writeI] != readItem) {
			fileItems[writeI] = std::move(*readItem);
		}
		writeI++;
	}

	fileItems.erase(fileItems.begin() + writeI, fileItems.end());

	// Our system of keeping FileItems from getting too full by deleting elements from its ends as we go could have
	// caused bad results at the edges of the above, so delete a further one element at each end as needed.
	if (firstFileItemRemaining) {
		fileItems.erase(fileItems.begin());
	}
	if (lastFileItemRemaining) {
		fileItems.pop_back();
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
	// Copy the directory portion (everything before the final '/') straight into currentDir. Don't go via
	// getPathFromFullPath() here: it returns .c_str() of a std::string temporary that's already destroyed by the
	// time we read it (dangling). Build the substring directly into currentDir instead.
	const char* slashPos = strrchr(fullPath, '/');
	if (slashPos != nullptr) {
		currentDir.assign(fullPath, slashPos - fullPath);
	}
	else {
		currentDir.clear();
	}

	// Change to the File Folder
	Error error = arrivedInNewFolder(0, fileName);
	if (error != Error::NONE) {
		return error;
	}

	//  Get the File Index
	fileIndexSelected = searchFileItems(fileName);
	if (fileIndexSelected > static_cast<int32_t>(fileItems.size())) {
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
				if (!deluge::string::caselessEquals(currentDir, defaultDirToAlsoTry)) {
					filenameToStartAt = NULL;
					currentDir = defaultDirToAlsoTry;
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

	if (static_cast<int32_t>(fileItems.size())) {
		sortFileItems();

		if (static_cast<int32_t>(fileItems.size())) {
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

namespace {
/// Adapts Browser::fileItems to the FileListView seam used by nextDefaultName().
class BrowserFileListView final : public deluge::gui::browser::FileListView {
public:
	bool contains(char const* nameWithExtension) const override {
		bool foundExact = false;
		Browser::searchFileItems(nameWithExtension, &foundExact);
		return foundExact;
	}
};
} // namespace

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
	if (static_cast<int32_t>(fileItems.size())) {
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
				scrollPosVertical = fileIndexSelected;
				if (display->getNumBrowserAndMenuLines() > 1) {
					int32_t lastAllowed = static_cast<int32_t>(fileItems.size()) - display->getNumBrowserAndMenuLines();
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

		int32_t i = searchFileItems(filenameToStartAt, &foundExact);
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
		// Come up with a new name variation. Names are display-agnostic ("SONG185", never "185"), so this is one
		// code path for both displays - see default_name.h.
		{
			BrowserFileListView view;
			// Only songs earn letter suffixes; presets pass an empty slotPrefix and take the numeric suffix path,
			// preserving existing preset behaviour.
			char const* slotPrefix = (filePrefix && !memcasecmp(filePrefix, "SONG", 4)) ? filePrefix : "";
			std::string newName = deluge::gui::browser::nextDefaultName(enteredText.c_str(), slotPrefix, view);
			if (newName == enteredText) {
				goto useFoundFile; // No free variation available - stay on the file we found.
			}
			enteredText = newName;
			enteredTextEditPos = enteredText.size();
		}
	}

	// Or if no files found at all...
	else {
		// Can we just pick a brand new name?
		if (mayDefaultToBrandNewNameOnEntry && !direction) {
pickBrandNewNameIfNoneNominated:
			if (enteredText.empty()) {
				error = getUnusedSlot(OutputType::NONE, &enteredText, filePrefix);
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}
				// Note - this is only hit if we're saving the first song created on boot (because the default name
				// won't match anything) Because that will have cleared out all the FileItems, we need to get them
				// again. Actually there would kinda be a way around doing this...
				error = readFileItemsFromFolderAndMemory(currentSong, OutputType::NONE, "SONG", enteredText.c_str(),
				                                         NULL, true, Availability::ANY, CATALOG_SEARCH_BOTH);
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
	folderContentsReady(direction);

	if (display->have7SEG()) {
		displayText();
	}
	return Error::NONE;
}

// You must set currentDir before calling this.
Error Browser::getUnusedSlot(OutputType outputType, std::string* newName, char const* thingName) {

	Error error;
	// Names always carry the prefix now, on both displays, so there is one search key.
	char filenameToStartAt[6]; // thingName is max 4 chars.
	strcpy(filenameToStartAt, thingName);
	strcat(filenameToStartAt, ":"); // Colon is the first character after the digits.
	error = readFileItemsFromFolderAndMemory(currentSong, outputType, getThingName(outputType), filenameToStartAt, NULL,
	                                         false, Availability::ANY, CATALOG_SEARCH_LEFT);

	if (error != Error::NONE) {
doReturn:
		return error;
	}

	sortFileItems();

	{
		int32_t freeSlotNumber = 1;
		int32_t minNumDigits = 1;
		if (!fileItems.empty()) {
			FileItem* fileItem = &fileItems[fileItems.size() - 1];
			// The name carries the prefix on both displays now, so read the real filename, not a display form.
			std::string filename = fileItem->getFilenameWithoutExtension();
			char const* readingChar = &filename.c_str()[strlen(thingName)];
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

		(*newName) = thingName;
		(*newName).append(deluge::string::fromInt(freeSlotNumber, minNumDigits));
		error = Error::NONE;
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

	int32_t newFileIndex;

	if (fileIndexSelected < 0) { // If no file selected and we were typing a new name?
		if (!static_cast<int32_t>(fileItems.size())) {
			return;
		}

		newFileIndex = searchFileItems(enteredText.c_str());
		if (offset < 0) {
			newFileIndex--;
		}
	}
	else {
		// If user is holding shift, skip past any subslots. And the user may have chosen one digit to "edit" (7SEG
		// only - numberEditPos is -1 otherwise).
		//
		// Names always carry the file prefix, so there is one path here, not one per display. (The two branches this
		// replaced were each written for the *other* display's convention, leaving both dead.)
		int32_t numberEditPosNow = numberEditPos;
		if (Buttons::isShiftButtonPressed() && numberEditPosNow == -1) {
			numberEditPosNow = 0;
		}

		if (numberEditPosNow != -1) {
			char const* numberPart = nameAfterPrefix(enteredText.c_str());
			if (!numberPart) {
				goto nonNumeric;
			}
			Slot thisSlot = getSlot(numberPart);
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

			int32_t filePrefixLength = strlen(filePrefix);
			char searchString[16];
			memcpy(searchString, filePrefix, filePrefixLength);
			char* searchStringNumbersStart = searchString + filePrefixLength;
			intToString(thisSlot.slot, searchStringNumbersStart, 1);
			if (offset < 0) {
				char* pos = strchr(searchStringNumbersStart, 0);
				*pos = 'A';
				pos++;
				*pos = 0;
			}
			newFileIndex = searchFileItems(searchString);
			if (offset < 0) {
				newFileIndex--;
			}
		}
		else {
nonNumeric:
			newFileIndex = fileIndexSelected + offset;
		}
	}

	int32_t newCatalogSearchDirection;
	Error error;

	if (newFileIndex < 0) {
		D_PRINTLN("index below 0");
		if (numFileItemsDeletedAtStart) {
			scrollPosVertical = 9999;

tryReadingItems:
			D_PRINTLN("reloading");
			error = readFileItemsFromFolderAndMemory(currentSong, outputTypeToLoad, filePrefix, enteredText.c_str(),
			                                         NULL, true, Availability::ANY, CATALOG_SEARCH_BOTH);
			if (error != Error::NONE) {
gotErrorAfterAllocating:
				D_PRINTLN("error while reloading, emptying file items");
				emptyFileItems();
				return;
				// TODO - need to close UI or something?
			}

			newFileIndex = searchFileItems(enteredText.c_str()) + offset;
			D_PRINTLN("new file Index is %d", newFileIndex);
		}

		else if (!shouldWrapFolderContents && display->have7SEG()) {
			return;
		}

		else { // Wrap to end
			scrollPosVertical = 0;

			if (numFileItemsDeletedAtEnd) {
				newCatalogSearchDirection = CATALOG_SEARCH_LEFT;
searchFromOneEnd:
				D_PRINTLN("reloading and wrap");
				error =
				    readFileItemsFromFolderAndMemory(currentSong, outputTypeToLoad, filePrefix, NULL, NULL, true,
				                                     Availability::ANY, newCatalogSearchDirection); // Load from start
				if (error != Error::NONE) {
					goto gotErrorAfterAllocating;
				}

				newFileIndex = (newCatalogSearchDirection == CATALOG_SEARCH_LEFT)
				                   ? (static_cast<int32_t>(fileItems.size()) - 1)
				                   : 0;
			}
			else {
				newFileIndex = static_cast<int32_t>(fileItems.size()) - 1;
			}
		}
	}

	else if (newFileIndex >= static_cast<int32_t>(fileItems.size())) {
		D_PRINTLN("out of file items");
		if (numFileItemsDeletedAtEnd) {
			scrollPosVertical = 0;
			goto tryReadingItems;
		}

		else if (!shouldWrapFolderContents && display->have7SEG()) {
			return;
		}

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
	if (display->haveOLED()) {
		scrollPosHorizontal = 0;
	}
	else {
		char const* oldCharAddress = enteredText.c_str();
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

	error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		display->displayError(error);
		return;
	}

	displayText();
	// currentFileChanged uses the value as a scroll-animation direction, so give it only the sign.
	currentFileChanged(offset > 0 ? 1 : (offset < 0 ? -1 : 0));
}

bool Browser::predictExtendedText() {
	Error error;
	arrivedAtFileByTyping = true;
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	// Names always carry the file prefix, but on 7SEG the user only ever sees and types the number ("185"). When
	// typing begins with a digit, treat the prefix as implicitly typed - otherwise "1" would match nothing. The typed
	// portion of enteredText is [0, enteredTextEditPos), so the prefix has to go *into* enteredText and be counted,
	// not merely prepended to the search key.
	if (filePrefix && enteredTextEditPos > 0 && !enteredText.empty()) {
		if (enteredText[0] >= '0' && enteredText[0] <= '9') {
			int32_t prefixLength = strlen(filePrefix);
			enteredText.insert(0, filePrefix);
			enteredTextEditPos += prefixLength;
		}
	}

	FileItem* oldFileItem = getCurrentFileItem();
	DWORD oldClust = 0;
	if (oldFileItem) {
		oldClust = oldFileItem->filePointer.sclust;
	}

	std::string searchString;
	searchString = enteredText;
	bool doneNewRead = false;
	searchString.resize(enteredTextEditPos);

	// Reachable only via `goto gotError` from the error paths below.
	if (false) {
gotError:
		display->displayError(error);
		return false;
	}

	int32_t numExtraZeroesAdded = 0;

	// Change 2026-03-29 - this code used to append a tilde to the search string. The tilde would match after all
	// printable characters so it would return 1 greater than the index of the search result we wanted. unfortunately
	// for reasons I don't fully understand (7seg compatibility maybe?) multi digit integers are compared as one number
	// rather than character by character, so if you had files named "SONG1", "SONG2", "SONG10", and you typed in
	// "SONG1", and then pressed the encoder to try to get it to predict "SONG10", it would instead match "SONG2"
	// because 2 is the closest number to 1 that comes after 1. So now we just search for the string as is. The major
	// impact is this now returns the first match instead of the last match.
doSearch:
	int32_t i = searchFileItems(searchString.c_str());

	// If that search takes us off the right-hand end of the list...
	if (i >= static_cast<int32_t>(fileItems.size())) {

		// If we haven't yet done a whole new read from the SD card etc, from within this function, do that now.
		if (!doneNewRead) {
doNewRead:
			doneNewRead = true;
			error = readFileItemsFromFolderAndMemory(
			    currentSong, outputTypeToLoad, filePrefix, searchString.c_str(), NULL, true, Availability::ANY,
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

	// The search returns the index where searchString would be inserted
	// Now check if the file at index i actually matches our prefix. Covers any shenanigans with the weird string
	// matching
	FileItem* fileItem = &fileItems[i];

	// If it didn't match exactly, that's ok, but we need to try some other stuff before we accept that result.
	if (memcasecmp(fileItem->displayName, enteredText.c_str(), enteredTextEditPos)) {
		// If the search landed on the first cached item, the folder cache may be missing earlier entries (files
		// alphabetically before the current one). Re-read the folder so we can find them too (#4584).
		if (i == 0 && !doneNewRead) {
			goto doNewRead;
		}

		// this code is original but I don't know what it does. Slot browser maybe?
		// Just updated to append to the string instead of replacing the tilde
		if (numExtraZeroesAdded < 4) {
			searchString.resize(searchString.size());
			searchString.append("0", 1);
			numExtraZeroesAdded++;
			doneNewRead = false;
			goto doSearch;
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

	fileItems.erase(fileItems.begin() + fileIndexSelected);

	if (fileIndexSelected == static_cast<int32_t>(fileItems.size())) {
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
		displayName = enteredText.c_str();
		o = OLED_HEIGHT_CHARS; // Make sure below loop doesn't keep looping.
		goto drawAFile;
	}

	else {
		for (o = 0; o < OLED_HEIGHT_CHARS - 1; o++) {
			{
				int32_t i = o + scrollPosVertical;

				if (i >= static_cast<int32_t>(fileItems.size())) {
					break;
				}

				FileItem* thisFile = &fileItems[i];
				isFolder = thisFile->isFolder;
				displayName = thisFile->filename.c_str();
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

			int32_t displayStringLength = (uintptr_t)finalCharAddress - (uintptr_t)displayName;

			if (isSelectedIndex) {
				drawTextForOLEDEditing(textStartX, OLED_MAIN_WIDTH_PIXELS, yPixel, maxChars, canvas);
				if (!enteredTextEditPos) {
					deluge::hid::display::OLED::setupSideScroller(0, enteredText.c_str(), textStartX,
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

// Names always carry the file prefix (e.g. "SONG185"). Only rendering strips it, so anything wanting the numeric part
// goes through here first. Zero-padding is skipped too - getSlot() cannot parse "001". See default_name.h.
char const* Browser::nameAfterPrefix(char const* name) const {
	return deluge::gui::browser::numberPartOf(name, filePrefix);
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

	Slot toReturn{};

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
			if (enteredText.empty() && fileIndexSelected == -1) {
				display->setText("----");
			}
			else {

				// A name is always the full on-card name ("SONG185"). On 7SEG we render the numeric part alone
				// ("185") so it fits the four-character display.
				char const* numberPart = nameAfterPrefix(enteredText.c_str());
				if (numberPart) {

					Slot thisSlot = getSlot(numberPart);
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

				scrollingText = display->setScrollingText(enteredText.c_str(), scrollStart);
			}
		}
	}
}

FileItem* Browser::getCurrentFileItem() {
	if (fileIndexSelected == -1) {
		return nullptr;
	}
	return &fileItems[fileIndexSelected];
}

// This and its individual contents are frequently overridden by child classes.
ActionResult Browser::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

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

	if (isFavouritesVisible() && y == favouriteRow && on) {
		if (isSDRoutineActive()) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (Buttons::isShiftButtonPressed()) {
			std::string filePath = getCurrentFilePath();
			if (favouritesManager.isEmpty(x)) {
				if (!getCurrentFileItem()->isFolder) {
					favouritesManager.setFavorite(x, FavouritesManager::favouriteDefaultColor, filePath.c_str());
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
	else if (isBanksVisible() && y == favouriteBankRow && on) {
		if (isSDRoutineActive()) {
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
	renderFavourites();
}

ActionResult Browser::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (isFavouritesVisible()) {
		if (Buttons::isShiftButtonPressed()) {
			if (favouritesManager.currentFavouriteNumber.has_value()) {
				favouritesManager.changeColour(favouritesManager.currentFavouriteNumber.value(), offset);
				favouritesChanged();
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

ActionResult Browser::mainButtonAction(bool on) {
	// Press down
	if (on) {
		if (currentUIMode == UI_MODE_NONE) {
			if (isSDRoutineActive()) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, LONG_PRESS_DURATION);
			currentUIMode = UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS;
		}
	}

	// Release press
	else {
		if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {
			if (isSDRoutineActive()) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			currentUIMode = UI_MODE_NONE;
			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			enterKeyPress();
		}
	}

	return ActionResult::DEALT_WITH;
}

// Virtual function - may be overridden, by child classes that need to do more stuff, e.g. SampleBrowser needs to mute
// any previewing Sample.
ActionResult Browser::backButtonAction() {
	if (isSDRoutineActive()) {
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

	enteredText = currentFileItem->displayName;

	// Cut off the file extension
	if (!currentFileItem->isFolder) {
		char const* enteredTextChars = enteredText.c_str();
		char const* dotAddress = strrchr(enteredTextChars, '.');
		if (dotAddress) {
			int32_t dotPos = (uintptr_t)dotAddress - (uintptr_t)enteredTextChars;
			enteredText.resize(dotPos);
		}
	}

	return Error::NONE;
}

Error Browser::goIntoFolder(char const* folderName) {
	Error error;

	if (!currentDir.empty()) {
		currentDir.append("/");
	}

	currentDir.append(folderName);

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

	char const* currentDirChars = currentDir.c_str();
	char const* slashAddress = strrchr(currentDirChars, '/');
	if (!slashAddress || slashAddress == currentDirChars) {
		return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
	}

	int32_t slashPos = (uintptr_t)slashAddress - (uintptr_t)currentDirChars;
	enteredText = slashAddress + 1;
	currentDir.resize(slashPos);
	enteredTextEditPos = 0;

	display->setNextTransitionDirection(-1);
	Error error = arrivedInNewFolder(-1, enteredText.c_str());
	if (display->haveOLED()) {
		if (error == Error::NONE) {
			renderUIsForOled();
		}
	}
	return error;
}

Error Browser::createFolder() {
	displayText();

	std::string newDirPath;
	Error error;

	newDirPath = currentDir;
	if (!newDirPath.empty()) {
		newDirPath.append("/");
	}

	newDirPath.append(enteredText);

	FRESULT result = f_mkdir(newDirPath.c_str());
	if (result) {
		return Error::SD_CARD;
	}

	error = goIntoFolder(enteredText.c_str());

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

	std::sort(fileItems.begin(), fileItems.end(),
	          [](FileItem const& a, FileItem const& b) { return strcmpspecial(a.displayName, b.displayName) < 0; });
}

int32_t Browser::searchFileItems(char const* searchString, bool* foundExact) {
	auto it = std::ranges::lower_bound(
	    fileItems, searchString, [](char const* a, char const* b) { return strcmpspecial(a, b) < 0; },
	    &FileItem::displayName);
	if (foundExact != nullptr) {
		*foundExact = (it != fileItems.end() && strcmpspecial(it->displayName, searchString) == 0);
	}
	return static_cast<int32_t>(it - fileItems.begin());
}

bool Browser::isFavouritesVisible() {
	return (getCurrentUI()->canDisplayFavourites() && qwertyVisible
	        && FlashStorage::defaultFavouritesLayout != FavouritesDefaultLayout::FavouritesDefaultLayoutOff);
}

bool Browser::isBanksVisible() {
	return (getCurrentUI()->canDisplayFavourites() && qwertyVisible
	        && FlashStorage::defaultFavouritesLayout
	               == FavouritesDefaultLayout::FavouritesDefaultLayoutFavoritesAndBanks);
}
