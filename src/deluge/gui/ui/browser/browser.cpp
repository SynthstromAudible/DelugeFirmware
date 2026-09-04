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
#include "io/file.hpp"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/string.h"
#include "util/try.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

using namespace deluge;

using deluge::storage::FileCatalogue;

// The catalogue's state, bound by reference so Browser's names and FileCatalogue's name the same
// objects. Constant initialisation (each is just the address of a namespace-scope static in
// file_catalogue.cpp), so this cannot race the other translation unit's dynamic initialisation --
// only the referenced objects' own construction order is unspecified across TUs, exactly as it was
// when these definitions lived here.
std::string& Browser::currentDir = FileCatalogue::currentDir;
deluge::vector<FileItem>& Browser::fileItems = FileCatalogue::fileItems;
int32_t& Browser::numFileItemsDeletedAtStart = FileCatalogue::numFileItemsDeletedAtStart;
int32_t& Browser::numFileItemsDeletedAtEnd = FileCatalogue::numFileItemsDeletedAtEnd;
std::string& Browser::firstFileItemRemaining = FileCatalogue::firstFileItemRemaining;
std::string& Browser::lastFileItemRemaining = FileCatalogue::lastFileItemRemaining;
char const*& Browser::filenameToStartSearchAt = FileCatalogue::filenameToStartSearchAt;

bool Browser::qwertyVisible;

int32_t Browser::scrollPosVertical;
int32_t Browser::fileIndexSelected;
int32_t Browser::numCharsInPrefix;
bool Browser::arrivedAtFileByTyping;
OutputType Browser::outputTypeToLoad;
char const** Browser::allowedFileExtensions;
bool Browser::allowFoldersSharingNameWithFile;

bool Browser::listingInProgress_;
Browser::ListingRequest Browser::pendingListing_;
int32_t Browser::pendingReloadCatalogSearchDirection_;
bool Browser::pendingReloadSearchFromEnd_;

// 7SEG ONLY
int8_t Browser::numberEditPos;

Browser::Browser() {
	fileIcon = deluge::hid::display::OLED::songIcon;
	fileIconPt2 = nullptr;
	fileIconPt2Width = 0;
	shouldWrapFolderContents = true;

	mayDefaultToBrandNewNameOnEntry = false;
	qwertyAlwaysVisible = true;
	qwertyVisible = true; // Because for most Browsers, it'll just always be true.
	filePrefix = NULL;
	shouldInterpretNoteNamesForThisBrowser = false;
}

// --- async-listing base machinery ---

bool Browser::beginListing(ListingRequest req) {
	// Claim the CATALOGUE, not just this class's own flag. listingInProgress_ only ever guarded
	// Browser against Browser; the scenes layer's tick-driven seam
	// (deluge::scenes::browser::Listing) drives the same FileCatalogue statics from outside this
	// class, and while the legacy UI and the scenes stack coexist the two would otherwise be
	// mutually invisible over one shared fileItems vector. The claim lives with the resource
	// (storage/file_catalogue.h) so neither side has to know about the other.
	//
	// Before the display call, so a refused listing touches nothing.
	if (!FileCatalogue::claim(this)) {
		return false;
	}
	pendingListing_ = std::move(req);
	listingInProgress_ = true;
	display->displayLoadingAnimationText("Working", /*delayed=*/true);
	if (!deluge::storage::Owner::run(&Browser::runListingTrampoline, this)) {
		// Owner queue full → op won't run; clear so the next HID event retries.
		listingInProgress_ = false;
		FileCatalogue::release(this);
		display->removeLoadingAnimation();
		return false;
	}
	return true;
}

void Browser::runListingTrampoline(void* self) {
	static_cast<Browser*>(self)->runPendingListing();
}

void Browser::runPendingListing() {
	Error error = Error::NONE;
	switch (pendingListing_.action) {
	case ListingAction::Open:
		error = openListingImpl(pendingListing_.direction, pendingListing_.filenameToStartAt.c_str(),
		                        pendingListing_.defaultDir.c_str());
		break;
	case ListingAction::IntoFolder:
		error = goIntoFolderImpl(pendingListing_.folderOrPath.c_str());
		break;
	case ListingAction::UpLevel:
		error = goUpOneDirectoryLevelImpl();
		break;
	case ListingAction::ByPath:
		error = setFileByFullPathImpl(pendingListing_.folderOrPath.c_str());
		break;
	case ListingAction::Reload:
		error = reloadImpl(pendingListing_.direction);
		break;
	}
	display->removeLoadingAnimation();
	if (error == Error::NONE) {
		if (pendingListing_.action == ListingAction::Open) {
			onBrowserOpened();
		}
		// folderContentsReady already ran inside arrivedInNewFolder for success.
	}
	else {
		onListingFailed(error);
	}
	listingInProgress_ = false;
	// Released HERE, at the end of the op, and that is the contract rather than an accident of
	// where the flag happened to be cleared: the catalogue claim is held until the CONSUMER has
	// finished reading fileItems, and legacy's consumer reads inside the op -- arrivedInNewFolder,
	// the selection search, folderContentsReady() and the render have all already run by this
	// line. The scenes seam holds its claim longer (until take_result()) for exactly the same
	// reason: its consumer runs on a later tick. See FileCatalogue::claim.
	FileCatalogue::release(this);
}

void Browser::onListingFailed(Error error) {
	display->displayError(error);
	// close(), not exitAction(): exitAction() is virtual and some subclasses override it with side
	// effects a plain listing failure shouldn't trigger (e.g. LoadSongUI::exitAction() shows a
	// different popup and may not close; LoadInstrumentPresetUI's calls revertToInitialPreset()).
	close();
	// By the time a listing can fail we've usually already drawn the QWERTY pads, and close() alone
	// doesn't force a redraw over them - without this, a listing failure could leave stale pads on
	// screen until something else triggers a render.
	renderingNeededRegardlessOfUI();
}

bool Browser::opened() {
	numCharsInPrefix = 0; // For most browsers, this just stays at 0.
	arrivedAtFileByTyping = false;
	allowedFileExtensions = allowedFileExtensionsXML;
	allowFoldersSharingNameWithFile = false;

	numberEditPos = -1;

	return QwertyUI::opened();
}

void Browser::close() {
	emptyFileItems();
	favouritesManager.close();
	QwertyUI::close();
}

// --- file catalogue: delegation only ---
//
// The bodies of these moved verbatim to deluge::storage::FileCatalogue (storage/file_catalogue.cpp)
// so the scenes layer can reach a listing without including gui/. Nothing here may grow a second
// line: sim/scenes_spec/file_catalogue_spec.cpp characterises the catalogue, not the browser, and
// listing logic that drifted back in here would be outside it.

void Browser::emptyFileItems() {
	FileCatalogue::emptyFileItems();
}

void Browser::deleteSomeFileItems(int32_t startAt, int32_t stopAt) {
	FileCatalogue::deleteSomeFileItems(startAt, stopAt);
}

FileItem* Browser::getNewFileItem() {
	return FileCatalogue::getNewFileItem(shouldInterpretNoteNamesForThisBrowser);
}

void Browser::cullSomeFileItems() {
	FileCatalogue::cullSomeFileItems(shouldInterpretNoteNamesForThisBrowser);
}

Error Browser::readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
                                      char const** allowedFileExtensionsHere, char const* filenameToStartAt,
                                      int32_t newMaxNumFileItems, int32_t newCatalogSearchDirection) {
	return FileCatalogue::readFileItemsForFolder(filePrefixHere, allowFolders, allowedFileExtensionsHere,
	                                             filenameToStartAt, newMaxNumFileItems, newCatalogSearchDirection,
	                                             shouldInterpretNoteNamesForThisBrowser);
}

void Browser::deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement) {
	FileCatalogue::deleteFolderAndDuplicateItems(instrumentAvailabilityRequirement);
}

Error Browser::setFileByFullPath(OutputType outputType, char const* fullPath) {
	beginListingOrRetryLater({.action = ListingAction::ByPath, .direction = 0, .folderOrPath = fullPath});
	return Error::NONE;
}

Error Browser::setFileByFullPathImpl(char const* fullPath) {
	arrivedAtFileByTyping = true;
	// TODO(conflation-batch-4): unknown is being treated as absent here; the load/browse/favourites
	// batch gives this site its real ruling.
	bool fileExists = StorageManager::fileExists(fullPath).value_or(false);
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
					auto result = deluge::io::mkdir(defaultDirToAlsoTry);
					if (result.has_value()) {
						triedCreatingFolder = true;
						goto tryReadingItems;
					}
					else {
						return delugeStatusToError(deluge::io::to_deluge_status(result.error()));
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

// Body of ListingAction::Open, run on the fiber inside runPendingListing(). arrivedInNewFolder()
// is the full synchronous listing body: readFileItemsFromFolderAndMemory + fileIndexSelected
// search + folderContentsReady() + render.
Error Browser::openListingImpl(int32_t direction, char const* filenameToStartAt, char const* defaultDir) {
	return arrivedInNewFolder(direction, filenameToStartAt, defaultDir);
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
	scrollPosHorizontal = 0;

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
				// Starting in the middle or end of a short folder should still fill as many display rows as possible.
				clampFileSelectionAndScroll();

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

	return Error::NONE;
}

// You must set currentDir before calling this.
Error Browser::getUnusedSlot(OutputType outputType, std::string* newName, char const* thingName) {

	Error error;
	// Names always carry the prefix now, on both displays, so there is one search key.
	// Sean: thingName is usually max 4 chars (e.g. SONG, SYNT, KIT), but can be longer
	// - e.g. "PATTERN" with pattern browser or "MIDIDEVICE" with midi device definition browser.
	// it's used for proposing the file name and on 7SEG if you type a # it will prefix it
	uint8_t buffer_size = 20;
	char filenameToStartAt[buffer_size];
	strncpy(filenameToStartAt, thingName, buffer_size - 2); // Leave 2 chars for "colon + null terminator"
	strcat(filenameToStartAt, ":");                         // Colon is the first character after the digits.
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
	if (listingInProgress_) {
		return;
	}

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

	// From here on, a reload of the folder listing off SD may be required (numFileItemsDeletedAtStart/End
	// wraparound cases below). If so, dispatch it onto the storage owner - reloadImpl() does the reload
	// AND then runs the shared tail (see finishSelectEncoderAction()) once it completes, on the fiber. If
	// no reload is needed, the tail runs synchronously right here: readFileItemsFromFolderAndMemory() is
	// the only SD-touching call in this whole function.
	if (newFileIndex < 0) {
		D_PRINTLN("index below 0");
		if (numFileItemsDeletedAtStart) {
			scrollPosVertical = 9999;
			D_PRINTLN("reloading");
			pendingReloadSearchFromEnd_ = false;
			beginListingOrRetryLater({.action = ListingAction::Reload, .direction = offset});
			return;
		}

		else { // Wrap to end
			scrollPosVertical = 0;

			if (numFileItemsDeletedAtEnd) {
				D_PRINTLN("reloading and wrap");
				pendingReloadSearchFromEnd_ = true;
				pendingReloadCatalogSearchDirection_ = CATALOG_SEARCH_LEFT;
				beginListingOrRetryLater({.action = ListingAction::Reload, .direction = offset});
				return;
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
			D_PRINTLN("reloading");
			pendingReloadSearchFromEnd_ = false;
			beginListingOrRetryLater({.action = ListingAction::Reload, .direction = offset});
			return;
		}

		else {
			scrollPosVertical = 9999;

			if (numFileItemsDeletedAtStart) {
				D_PRINTLN("reloading and wrap");
				pendingReloadSearchFromEnd_ = true;
				pendingReloadCatalogSearchDirection_ = CATALOG_SEARCH_RIGHT;
				beginListingOrRetryLater({.action = ListingAction::Reload, .direction = offset});
				return;
			}
			else {
				newFileIndex = 0;
			}
		}
	}

	Error error = finishSelectEncoderAction(newFileIndex, offset);
	if (error != Error::NONE) {
		display->displayError(error);
	}
}

// Shared tail of selectEncoderAction(): fileIndexSelected/scroll update + setEnteredTextFromCurrentFilename +
// currentFileChanged. Called either synchronously (no reload needed) or from reloadImpl() on the fiber once
// a dispatched reload completes.
Error Browser::finishSelectEncoderAction(int32_t newFileIndex, int8_t offset) {
	if (!qwertyAlwaysVisible) {
		qwertyVisible = false;
	}

	fileIndexSelected = newFileIndex;
	// A fast turn may be delivered as a multi-file offset; after a folder-window re-read, that offset can still
	// overshoot.
	clampFileSelectionAndScroll(false);

	if (fileIndexSelected == -1) {
		// allowNoFileSelection=false only leaves -1 when there are no cached files at all,
		// so there is no selection to reconcile -- not an error.
		return Error::NONE;
	}

	enteredTextEditPos = 0;
	scrollPosHorizontal = 0;

	Error error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		return error;
	}

	displayText();
	// currentFileChanged uses the value as a scroll-animation direction, so give it only the sign.
	currentFileChanged(offset > 0 ? 1 : (offset < 0 ? -1 : 0));
	return Error::NONE;
}

// Body of ListingAction::Reload, run on the fiber: reloads the folder listing off SD for one of
// selectEncoderAction's two scroll-reload sites (pendingReloadSearchFromEnd_ selects which), then
// runs the shared tail via finishSelectEncoderAction().
Error Browser::reloadImpl(int32_t direction) {
	int8_t offset = static_cast<int8_t>(direction);
	int32_t newFileIndex;
	Error error;

	if (pendingReloadSearchFromEnd_) {
		error = readFileItemsFromFolderAndMemory(currentSong, outputTypeToLoad, filePrefix, NULL, NULL, true,
		                                         Availability::ANY, pendingReloadCatalogSearchDirection_);
		if (error != Error::NONE) {
			D_PRINTLN("error while reloading, emptying file items");
			emptyFileItems();
			// TODO - need to close UI or something?
			// Reload failure stays silent (no popup, no exit) - swallow it here rather than letting it
			// propagate to runPendingListing()/onListingFailed().
			return Error::NONE;
		}

		newFileIndex = (pendingReloadCatalogSearchDirection_ == CATALOG_SEARCH_LEFT)
		                   ? (static_cast<int32_t>(fileItems.size()) - 1)
		                   : 0;
	}
	else {
		error = readFileItemsFromFolderAndMemory(currentSong, outputTypeToLoad, filePrefix, enteredText.c_str(), NULL,
		                                         true, Availability::ANY, CATALOG_SEARCH_BOTH);
		if (error != Error::NONE) {
			D_PRINTLN("error while reloading, emptying file items");
			emptyFileItems();
			// TODO - need to close UI or something?
			// Reload failure stays silent (no popup, no exit) - swallow it here rather than letting it
			// propagate to runPendingListing()/onListingFailed().
			return Error::NONE;
		}

		newFileIndex = searchFileItems(enteredText.c_str()) + offset;
		D_PRINTLN("new file Index is %d", newFileIndex);
	}

	error = finishSelectEncoderAction(newFileIndex, offset);
	if (error != Error::NONE) {
		// Matches the synchronous no-reload path (selectEncoderAction()): displayError only, browser stays
		// open. Swallow here rather than propagating to runPendingListing()/onListingFailed(), which would
		// wrongly close() the browser on a tail error - reload failures never close the browser.
		display->displayError(error);
	}
	return Error::NONE;
}

bool Browser::predictExtendedText() {
	Error error;
	arrivedAtFileByTyping = true;
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	// Captured by value (not the FileItem*, which readFileItemsFromFolderAndMemory()/doNewRead below can
	// invalidate by reallocating fileItems) so we can tell after the search whether we landed on a
	// different file - filename is a FileItem's identity.
	FileItem* oldFileItem = getCurrentFileItem();
	std::string oldFilename;
	if (oldFileItem) {
		oldFilename = oldFileItem->filename;
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
		if (!mayDefaultToBrandNewNameOnEntry) {
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
	if (memcasecmp(fileItem->displayName().c_str(), enteredText.c_str(), enteredTextEditPos)) {
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

	// Typing/prediction can land on a cached item without needing to move the viewport unless it is offscreen.
	clampFileSelectionAndScroll();

	error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		goto gotError;
	}

	displayText();

	// If we're now on a different file than before, preview it
	if (fileItem->filename != oldFilename) {
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
	// Deleting the last visible item can leave the top row past the new end of the list.
	clampFileSelectionAndScroll();
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
	// Use the display contract for browser/menu rows instead of assuming the OLED character-grid height.
	int32_t visibleRows = display->getNumBrowserAndMenuLines();
	if (visibleRows < 1) {
		visibleRows = 1;
	}

	// If we're currently typing a filename which doesn't (yet?) have a file...
	if (fileIndexSelected == -1) {
		displayName = enteredText.c_str();
		o = visibleRows; // Make sure below loop doesn't keep looping.
		goto drawAFile;
	}

	else {
		for (o = 0; o < visibleRows; o++) {
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

void Browser::clampFileSelectionAndScroll(bool allowNoFileSelection) {
	int32_t numFileItems = static_cast<int32_t>(fileItems.size());
	if (numFileItems <= 0) {
		// No cached files means there is no real selection, and the viewport must reset to the top.
		fileIndexSelected = -1;
		scrollPosVertical = 0;
		return;
	}

	if (fileIndexSelected >= numFileItems) {
		// A large encoder offset can overshoot the freshly cached window; land on the last cached item instead.
		fileIndexSelected = numFileItems - 1;
	}
	else if (fileIndexSelected < 0) {
		// -1 is valid only while typing a new name; encoder browsing must stay on a real cached file.
		fileIndexSelected = allowNoFileSelection ? -1 : 0;
	}

	// Fast encoder turns can arrive as multi-file jumps after the cached folder window has been re-read.
	// Keep both the selection and the visible window inside the files we actually have.
	int32_t visibleRows = display->getNumBrowserAndMenuLines();
	if (visibleRows < 1) {
		// Defensive fallback for mock or future displays; the scroll math needs at least one visible row.
		visibleRows = 1;
	}
	int32_t lastAllowedScroll = numFileItems - visibleRows;
	if (lastAllowedScroll < 0) {
		// Short folders cannot fill every row, so their top visible row is always the first item.
		lastAllowedScroll = 0;
	}

	if (fileIndexSelected == -1) {
		// While typing a new name, the rendered row is enteredText rather than an item from fileItems.
		scrollPosVertical = 0;
		return;
	}

	if (scrollPosVertical > fileIndexSelected) {
		// The selected item is above the current viewport; move it to the first visible row.
		scrollPosVertical = fileIndexSelected;
	}
	else if (scrollPosVertical < fileIndexSelected - visibleRows + 1) {
		// The selected item is below the current viewport; move it to the last visible row.
		scrollPosVertical = fileIndexSelected - visibleRows + 1;
	}

	if (scrollPosVertical > lastAllowedScroll) {
		// Keep the viewport from starting so low that the bottom browser rows would be blank.
		scrollPosVertical = lastAllowedScroll;
	}
	if (scrollPosVertical < 0) {
		// Short-folder and typing cases can make the intermediate top row negative; clamp back to the start.
		scrollPosVertical = 0;
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
	renderUIsForOled();
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
	if (listingInProgress_) {
		return ActionResult::DEALT_WITH;
	}

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
	if (listingInProgress_) {
		return ActionResult::DEALT_WITH;
	}

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
	if (listingInProgress_) {
		return ActionResult::DEALT_WITH;
	}
	if (isSDRoutineActive()) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	// goUpOneDirectoryLevel() checks for the root-of-tree case synchronously (see its comment) and returns
	// NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP without dispatching, so pressing Back at the root is handled
	// right here, synchronously. Any other failure is discovered on the fiber and routed through
	// onListingFailed (displayError + close()).
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
		openUI(&context_menu::deleteFile);
	}
}

Error Browser::setEnteredTextFromCurrentFilename() {
	FileItem* currentFileItem = getCurrentFileItem();

	enteredText = currentFileItem->displayName();

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
	beginListingOrRetryLater({.action = ListingAction::IntoFolder, .direction = 1, .folderOrPath = folderName});
	return Error::NONE;
}

Error Browser::goIntoFolderImpl(char const* folderName) {
	Error error;

	if (!currentDir.empty()) {
		currentDir.append("/");
	}

	currentDir.append(folderName);

	enteredText.clear();
	enteredTextEditPos = 0;

	error = arrivedInNewFolder(1);
	if (error == Error::NONE) {
		renderUIsForOled();
	}

	return error;
}

Error Browser::goUpOneDirectoryLevel() {
	// The "already at root" case is checked synchronously here, not on the fiber: it returns
	// NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP with no popup and no UI transition, and backButtonAction()
	// depends on getting that back synchronously so it can handle the root case inline. Only dispatch
	// the (SD-touching) listing when there IS a level to ascend.
	char const* currentDirChars = currentDir.c_str();
	char const* slashAddress = strrchr(currentDirChars, '/');
	if (!slashAddress || slashAddress == currentDirChars) {
		return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
	}

	beginListingOrRetryLater({.action = ListingAction::UpLevel, .direction = -1});
	return Error::NONE;
}

// The pre-listing currentDir manipulation (finding the parent, resizing currentDir) runs here, on
// the fiber alongside the listing, rather than synchronously in goUpOneDirectoryLevel() - it must
// happen atomically with arrivedInNewFolder()'s SD access, not race the caller's next HID event.
// goUpOneDirectoryLevel() has already confirmed synchronously that there IS a level to go up before
// dispatching here; the check is repeated defensively since currentDir is re-read from scratch.
Error Browser::goUpOneDirectoryLevelImpl() {

	char const* currentDirChars = currentDir.c_str();
	char const* slashAddress = strrchr(currentDirChars, '/');
	if (!slashAddress || slashAddress == currentDirChars) {
		return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
	}

	int32_t slashPos = (uintptr_t)slashAddress - (uintptr_t)currentDirChars;
	enteredText = slashAddress + 1;
	currentDir.resize(slashPos);
	enteredTextEditPos = 0;

	Error error = arrivedInNewFolder(-1, enteredText.c_str());
	if (error == Error::NONE) {
		renderUIsForOled();
	}
	return error;
}

// Returns the SYNCHRONOUS mkdir outcome, not the async listing that follows it on success.
// acceptCurrentOption() (save_song_or_instrument.cpp) branches on this return to decide whether to
// close the context menu: mkdir failure must still be reported synchronously so the menu stays open
// with the error. A listing failure after a successful mkdir is routed through onListingFailed()
// instead, after the menu has already closed on the (correct) mkdir success.
Error Browser::createFolder() {
	displayText();

	std::string newDirPath;

	newDirPath = currentDir;
	if (!newDirPath.empty()) {
		newDirPath.append("/");
	}

	newDirPath.append(enteredText);

	auto result = deluge::io::mkdir(newDirPath.c_str());
	if (!result.has_value()) {
		return Error::SD_CARD;
	}

	goIntoFolder(enteredText.c_str()); // Dispatches the listing async; always returns Error::NONE.

	return Error::NONE;
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
			auto result = deluge::io::mkdir(tempPath);
			if (!result.has_value() && result.error() != deluge::io::Status::EXISTS) {
				return delugeStatusToError(
				    DELUGE_ERR_NOT_FOUND); // hardcoded, preserving the pre-existing quirk's shape (always
				                           // ignores the real failure); the exact Error value shifts from the
				                           // original's FOLDER_DOESNT_EXIST to FILE_NOT_FOUND here, an
				                           // unavoidable consequence of the NOT_FOUND collapse -- see the
				                           // design doc's §2 caveat
			}
		}
	}
	return Error::NONE;
}

void Browser::sortFileItems() {
	FileCatalogue::sortFileItems(shouldInterpretNoteNamesForThisBrowser);
}

int32_t Browser::searchFileItems(char const* searchString, bool* foundExact) {
	return FileCatalogue::searchFileItems(searchString, foundExact);
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
