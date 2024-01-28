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
	int32_t error;
	bool loadedFromFile;
};

struct ReturnOfConfirmPresetOrNextUnlaunchedOne {
	FileItem* fileItem;
	int32_t error;
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
	virtual int32_t getCurrentFilePath(String* path) = 0;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	void currentFileDeleted();
	int32_t goIntoFolder(char const* folderName);
	int32_t createFolder();
	void selectEncoderAction(int8_t offset);
	static FileItem* getCurrentFileItem();
	int32_t readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
	                               char const** allowedFileExtensionsHere, char const* filenameToStartAt,
	                               int32_t newMaxNumFileItems, int32_t newCatalogSearchDirection = CATALOG_SEARCH_BOTH);
	void sortFileItems();
	FileItem* getNewFileItem();
	static void emptyFileItems();
	static void deleteSomeFileItems(int32_t startAt, int32_t stopAt);
	static void deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement = Availability::ANY);
	int32_t getUnusedSlot(OutputType outputType, String* newName, char const* thingName);
	bool opened();
	void cullSomeFileItems();
	bool checkFP();

	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

	static String currentDir;
	static CStringArray fileItems;
	static int32_t numFileItemsDeletedAtStart;
	static int32_t numFileItemsDeletedAtEnd;
	static char const* firstFileItemRemaining;
	static char const* lastFileItemRemaining;

	static OutputType outputTypeToLoad;
	static char const* filenameToStartSearchAt;

	// ui
	UIType getUIType() { return UIType::BROWSER; }

protected:
	int32_t setEnteredTextFromCurrentFilename();
	int32_t goUpOneDirectoryLevel();
	virtual int32_t arrivedInNewFolder(int32_t direction, char const* filenameToStartAt = NULL,
	                                   char const* defaultDir = NULL);
	bool predictExtendedText();
	void goIntoDeleteFileContextMenu();
	ActionResult mainButtonAction(bool on);
	virtual void exitAction();
	virtual ActionResult backButtonAction();
	virtual void folderContentsReady(int32_t entryDirection) {}
	virtual void currentFileChanged(int32_t movementDirection) {}
	void displayText(bool blinkImmediately = false);
	static Slot getSlot(char const* displayName);
	int32_t readFileItemsFromFolderAndMemory(Song* song, OutputType outputType, char const* filePrefixHere,
	                                         char const* filenameToStartAt, char const* defaultDirToAlsoTry,
	                                         bool allowFoldersint,
	                                         Availability availabilityRequirement = Availability::ANY,
	                                         int32_t newCatalogSearchDirection = CATALOG_SEARCH_RIGHT);

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

#include "io/debug/print.h"
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
