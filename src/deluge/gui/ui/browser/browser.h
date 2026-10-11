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

// FIXME: std::expected<std::pair<bool, FileItem*>, Error>
struct PresetNavigationResult {
	FileItem* fileItem;
	bool loadedFromFile;
	Error error;
};

struct Slot {
	int16_t slot;
	int8_t subSlot;
};

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
	/// @brief Surveys currentDir in one pass for the existing variations of a name.
	///
	/// Reads the folder directly rather than fileItems, keeping only the two summaries. fileItems is a window around
	/// whichever file the browser is sitting on (FILE_ITEMS_MAX_NUM_ELEMENTS), and a member of a name family can be
	/// any distance from that in sort order, so it cannot answer this.
	///
	/// @pre currentDir names the folder to survey.
	/// @param stem            The name the variations extend.
	/// @param highestNumbered If non-null, receives the greatest name of the form "<stem><digits>", or is left empty
	///                        when the folder holds none.
	/// @param takenLetters    If non-null, receives a bit per existing "<stem><letter>", bit 0 meaning 'A'.
	/// @return Error::NONE, or the card error that cut the pass short - in which case both outputs still hold what
	///         had been found so far, neither of which can overstate the truth.
	/// @see fileItemsAreComplete(), which says when this pass can be skipped entirely.
	static Error surveyNameVariations(char const* stem, String* highestNumbered, uint32_t* takenLetters);

	/// @brief Whether fileItems holds every file the folder listed, rather than a culled window of them.
	///
	/// @return True when questions about the whole folder can be answered from fileItems alone, without going back
	///         to the card.
	static bool fileItemsAreComplete() { return !numFileItemsDeletedAtStart && !numFileItemsDeletedAtEnd; }
	bool opened() override;
	void cullSomeFileItems();
	bool checkFP();

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	static String currentDir;
	static CStringArray fileItems;
	static int32_t numFileItemsDeletedAtStart;
	static int32_t numFileItemsDeletedAtEnd;
	// These hold the displayName of the boundary FileItem kept when we cull items off either end of the list to stay
	// under the memory cap. They must own a *copy* of the name (not borrow the FileItem's buffer): the FileItem they
	// came from can be destructed - freeing its filename buffer - before these get used as search keys in a later
	// sortFileItems(). (See the kit-copy use-after-free fix.)
	static String firstFileItemRemaining;
	static String lastFileItemRemaining;

	static OutputType outputTypeToLoad;
	static char const* filenameToStartSearchAt;

	// ui
	ActionResult exitUI() override {
		exitAction();
		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}
	bool isFavouritesVisible() override;
	bool isBanksVisible() override;

protected:
	Error setEnteredTextFromCurrentFilename();
	Error goUpOneDirectoryLevel();
	virtual Error arrivedInNewFolder(int32_t direction, char const* filenameToStartAt = nullptr,
	                                 char const* defaultDir = nullptr);
	bool predictExtendedText() override;
	void goIntoDeleteFileContextMenu();
	ActionResult mainButtonAction(bool on);
	virtual void exitAction();
	virtual ActionResult backButtonAction();
	virtual void folderContentsReady(int32_t entryDirection) {}
	virtual void currentFileChanged(int32_t movementDirection) {}
	void displayText(bool blinkImmediately = false) override;
	// Keeps the selected file index and top visible browser row inside Browser::fileItems.
	void clampFileSelectionAndScroll(bool allowNoFileSelection = true);
	static Slot getSlot(char const* displayName);
	/// Returns the character just past filePrefix within `name`, or nullptr if `name` does not start with filePrefix.
	/// Names always carry the prefix; only *rendering* strips it.
	char const* nameAfterPrefix(char const* name) const;
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
