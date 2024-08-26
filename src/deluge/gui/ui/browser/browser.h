/*
 * Copyright Γö¼ΓîÉ 2019-2023 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/qwerty_ui.h"
#include "hid/button.h"
#include "io/debug/log.h"
#include "model/favourite/favourite_manager.h"
#include "storage/file_item.h"
#include "util/container/array/c_string_array.h"

extern "C" {
#include "fatfs/ff.h"

FRESULT f_readdir_get_filepointer(DIR* dp,      /* Pointer to the open directory object */
                                  FILINFO* fno, /* Pointer to file information to return */
                                  FilePointer* filePointer);
}

class Instrument;
class FileItem;
class NumericLayerScrollingText;
class Song;

struct PresetNavigationResult {
	FileItem* fileItem;
	Error error;
	bool loadedFromFile;
};

// FIXME: This is literally a std::expected and should be such.
struct ReturnOfConfirmPresetOrNextUnlaunchedOne {
	FileItem* fileItem;
	Error error;
};

struct Slot {
	int16_t slot;
	int8_t subSlot;
};

#define NUM_FILES_ON_SCREEN 3

#define CATALOG_SEARCH_LEFT 0
#define CATALOG_SEARCH_RIGHT 1
#define CATALOG_SEARCH_BOTH 2

#define FILE_ITEMS_MAX_NUM_ELEMENTS 20
#define FILE_ITEMS_MAX_NUM_ELEMENTS_FOR_NAVIGATION 20 // It "should" be able to be way less than this.

extern char const* allowedFileExtensionsXML[];

class Browser : public QwertyUI {
public:
	Browser();

	void close();
	virtual Error getCurrentFilePath(String* path) = 0;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void currentFileDeleted();
	Error goIntoFolder(char const* folderName);
	Error createFolder();
	Error createFoldersRecursiveIfNotExists(const char* path);
	void selectEncoderAction(int8_t offset) override;
	static FileItem* getCurrentFileItem();
	Error readFileItemsForFolder(char const* filePrefixHere, bool allowFolders, char const** allowedFileExtensionsHere,
	                             char const* filenameToStartAt, int32_t newMaxNumFileItems,
	                             int32_t newCatalogSearchDirection = CATALOG_SEARCH_BOTH);
	Error setFileByFullPath(OutputType outputType, char const* fullPath);
	void sortFileItems();
	FileItem* getNewFileItem();
	static void emptyFileItems();
	static void deleteSomeFileItems(int32_t startAt, int32_t stopAt);
	static void deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement = Availability::ANY);
	Error getUnusedSlot(OutputType outputType, String* newName, char const* thingName);
	bool opened();
	void cullSomeFileItems();
	bool checkFP();

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	static String currentDir;
	static CStringArray fileItems;
	static int32_t numFileItemsDeletedAtStart;
	static int32_t numFileItemsDeletedAtEnd;
	static char const* firstFileItemRemaining;
	static char const* lastFileItemRemaining;

	static OutputType outputTypeToLoad;
	static char const* filenameToStartSearchAt;

	// ui
	bool exitUI() override {
		Browser::close();
		return true;
	}

protected:
	Error setEnteredTextFromCurrentFilename();
	Error goUpOneDirectoryLevel();
	virtual Error arrivedInNewFolder(int32_t direction, char const* filenameToStartAt = nullptr,
	                                 char const* defaultDir = nullptr);
	bool predictExtendedText();
	void goIntoDeleteFileContextMenu();
	ActionResult mainButtonAction(bool on);
	virtual void exitAction();
	virtual ActionResult backButtonAction();
	virtual void folderContentsReady(int32_t entryDirection) {}
	virtual void currentFileChanged(int32_t movementDirection) {}
	void displayText(bool blinkImmediately = false);
	static Slot getSlot(char const* displayName);
	Error readFileItemsFromFolderAndMemory(Song* song, OutputType outputType, char const* filePrefixHere,
	                                       char const* filenameToStartAt, char const* defaultDirToAlsoTry,
	                                       bool allowFoldersint,
	                                       Availability availabilityRequirement = Availability::ANY,
	                                       int32_t newCatalogSearchDirection = CATALOG_SEARCH_RIGHT);
	void favouritesChanged();

	static int32_t fileIndexSelected; // If -1, we have not selected any real file/folder. Maybe there are no files, or
	                                  // maybe we're typing a new name.
	static int32_t scrollPosVertical;
	static int32_t
	    numCharsInPrefix; // Only used for deciding Drum names within Kit. Oh and initial text scroll position.
	static bool qwertyVisible;
	static bool arrivedAtFileByTyping;
	static bool allowFoldersSharingNameWithFile;
	static char const** allowedFileExtensions;

	const uint8_t* fileIcon;
	const uint8_t* fileIconPt2;
	int32_t fileIconPt2Width;

	// 7Seg Only
	static int8_t numberEditPos; // -1 is default
	static NumericLayerScrollingText* scrollingText;
	bool shouldWrapFolderContents; // As in, wrap around at the end.

	bool mayDefaultToBrandNewNameOnEntry;
	bool qwertyAlwaysVisible;
	// filePrefix is SONG/SYNT/SAMP etc., signifying the portion of the filesystem you're in
	char const* filePrefix;
	bool shouldInterpretNoteNamesForThisBrowser;
};

inline void printInstrumentFileList(const char* where) {
	D_PRINT("\n");
	D_PRINT(where);
	D_PRINT(" List: \n");
	for (uint32_t idx = 0; idx < Browser::fileItems.getNumElements(); ++idx) {
		FileItem* fileItem = (FileItem*)Browser::fileItems.getElementAddress(idx);
		D_PRINTLN(" - %s (%lu)", fileItem->displayName, fileItem->filePointer.sclust);
	}
	D_PRINT("\n");
}
