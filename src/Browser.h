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

#ifndef BROWSER_H_
#define BROWSER_H_

#include <CStringArray.h>
#include "QwertyUI.h"

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
	int error;
	bool loadedFromFile;
};

struct ReturnOfConfirmPresetOrNextUnlaunchedOne {
	FileItem* fileItem;
	int error;
};

struct Slot {
	int16_t slot;
	int8_t subSlot;
};

#define NUM_FILES_ON_SCREEN 3

#define CATALOG_SEARCH_LEFT 0
#define CATALOG_SEARCH_RIGHT 1
#define CATALOG_SEARCH_BOTH 2

extern char const* allowedFileExtensionsXML[];

class Browser : public QwertyUI {
public:
	Browser();

	void close();
	virtual int getCurrentFilePath(String* path) = 0;
	int buttonAction(int x, int y, bool on, bool inCardRoutine);
	void currentFileDeleted();
	int goIntoFolder(char const* folderName);
	int createFolder();
	void selectEncoderAction(int8_t offset);
	static FileItem* getCurrentFileItem();
	static int readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
	                                  char const** allowedFileExtensionsHere, char const* filenameToStartAt,
	                                  int newMaxNumFileItems, int newCatalogSearchDirection = CATALOG_SEARCH_BOTH);
	static void sortFileItems();
	static FileItem* getNewFileItem();
	static void emptyFileItems();
	static void deleteSomeFileItems(int startAt, int stopAt);
	static void deleteFolderAndDuplicateItems(int instrumentAvailabilityRequirement = AVAILABILITY_ANY);
	static PresetNavigationResult doPresetNavigation(int offset, Instrument* oldInstrument, int availabilityRequirement,
	                                                 bool doBlink);
	static int getUnusedSlot(int instrumentType, String* newName, char const* thingName);
	static ReturnOfConfirmPresetOrNextUnlaunchedOne
	confirmPresetOrNextUnlaunchedOne(int instrumentType, String* searchName, int availabilityRequirement);
	static ReturnOfConfirmPresetOrNextUnlaunchedOne
	findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, int instrumentType, int availabilityRequirement);
	bool opened();
	static void cullSomeFileItems();

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
#endif

	static String currentDir;
	static CStringArray fileItems;
	static int numFileItemsDeletedAtStart;
	static int numFileItemsDeletedAtEnd;
	static char const* firstFileItemRemaining;
	static char const* lastFileItemRemaining;

	static int instrumentTypeToLoad;
	static char const* filenameToStartSearchAt;

protected:
	int setEnteredTextFromCurrentFilename();
	int goUpOneDirectoryLevel();
	virtual int arrivedInNewFolder(int direction, char const* filenameToStartAt = NULL, char const* defaultDir = NULL);
	bool predictExtendedText();
	void goIntoDeleteFileContextMenu();
	int mainButtonAction(bool on);
	virtual void exitAction();
	virtual int backButtonAction();
	virtual void folderContentsReady(int entryDirection) {
	}
	virtual void currentFileChanged(int movementDirection) {
	}
	void displayText(bool blinkImmediately = false);
	static Slot getSlot(char const* displayName);
	static int readFileItemsFromFolderAndMemory(Song* song, int instrumentType, char const* filePrefixHere,
	                                            char const* filenameToStartAt, char const* defaultDirToAlsoTry,
	                                            bool allowFoldersint, int availabilityRequirement = AVAILABILITY_ANY,
	                                            int newCatalogSearchDirection = CATALOG_SEARCH_RIGHT);

	static int
	    fileIndexSelected; // If -1, we have not selected any real file/folder. Maybe there are no files, or maybe we're typing a new name.
	static int scrollPosVertical;
	static int numCharsInPrefix; // Only used for deciding Drum names within Kit. Oh and initial text scroll position.
	static bool qwertyVisible;
	static bool arrivedAtFileByTyping;
	static bool allowFoldersSharingNameWithFile;
	static char const** allowedFileExtensions;

#if HAVE_OLED
	const uint8_t* fileIcon;
#else
	static int8_t numberEditPos; // -1 is default
	static NumericLayerScrollingText* scrollingText;
	bool shouldWrapFolderContents; // As in, wrap around at the end.
#endif
	bool allowBrandNewNames;
	bool qwertyAlwaysVisible;
	char const* filePrefix;
};

#endif /* BROWSER_H_ */
