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
#include "util/containers.h"

class Instrument;
class FileItem;
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

	/// @brief Tear the browser down, cancelling anything it has in flight.
	///
	/// `virtual` deliberately, unlike `UI::close()` which it hides. A subclass with its own in-flight
	/// work — `DxSyxBrowser`'s cartridge read — has to be able to cancel it here, and the call that
	/// matters comes through `exitAction()`, where the static type is `Browser`. A non-virtual
	/// subclass `close()` would compile, look right, and never run on the path that actually closes
	/// the browser.
	virtual void close();
	virtual std::string getCurrentFilePath() = 0;
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
	Error getUnusedSlot(OutputType outputType, std::string* newName, char const* thingName);
	bool opened() override;
	void cullSomeFileItems();

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	/// @brief The per-tick surface, and FINAL on purpose.
	///
	/// The listing pump lives here, and a subclass must not be able to skip it. Subclasses put their
	/// own per-tick work in browserGraphicsRoutine() instead, which this calls.
	///
	/// `final` because the alternative failed silently. SampleBrowser and LoadSongUI both overrode
	/// graphicsRoutine() and tail-called UI::graphicsRoutine() — a shape that, once the base acquired
	/// per-tick work of its own, would have skipped it entirely on the two most-used browsers, with
	/// nothing to say so. The guarantee wanted here is structural, not a runtime assertion: writing
	/// the override on the wrong function must not compile.
	void graphicsRoutine() final;

	// ui

	static std::string currentDir;
	static deluge::vector<FileItem> fileItems; // Sorted by displayName per strcmpspecial (see sortFileItems())
	/// Mirrors CStringArray::search(): binary search by displayName using strcmpspecial; returns the matching
	/// index, or the insertion point if not found. Set shouldInterpretNoteNames and octaveStartsFromA first.
	static int32_t searchFileItems(char const* searchString, bool* foundExact = nullptr);
	static int32_t numFileItemsDeletedAtStart;
	static int32_t numFileItemsDeletedAtEnd;
	/// Names of the entries that bracket the culled window, remembered ACROSS the cull that erases them.
	/// Owning copies, deliberately: these used to be `char const*` aliases into FileItem::filename, i.e.
	/// pointers into vector elements this very code then erased.
	static std::string firstFileItemRemaining;
	static std::string lastFileItemRemaining;

	static OutputType outputTypeToLoad;
	static char const* filenameToStartSearchAt;

	/// @brief True while an async listing (beginListing()'s dispatch onto the storage-owner fiber)
	///        is in flight.
	///
	/// A caller that drives beginListing() from outside the normal HID-event flow (e.g. a headless
	/// harness) needs this to know when it's safe to act on the listing's result
	/// (getCurrentFileItem() etc) instead of racing it.
	/// @return True while a listing is in flight; false once it has completed (or none was started).
	static bool isListingInProgress() { return listingInProgress_; }

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
	/// @brief A subclass's own per-tick work. Called by graphicsRoutine() after the base's.
	///
	/// Override this, never graphicsRoutine() — which is `final` precisely so that the mistake is a
	/// compile error rather than a browser whose listing silently stops being pumped. There is no
	/// need to chain to UI::graphicsRoutine(): the base makes that call once, for everybody.
	virtual void browserGraphicsRoutine() {}

	virtual void folderContentsReady(int32_t entryDirection) {}
	virtual void currentFileChanged(int32_t movementDirection) {}
	void displayText(bool blinkImmediately = false) override;
	// Keeps the selected file index and top visible browser row inside Browser::fileItems.
	void clampFileSelectionAndScroll(bool allowNoFileSelection = true);

	// --- async-listing base machinery ---
	//
	// One active listing at a time (browser state is already static/shared, e.g. fileItems),
	// so the pending request + flag are static too.
	enum class ListingAction { Open, IntoFolder, UpLevel, Reload, ByPath };

	struct ListingRequest {
		ListingAction action;
		int32_t direction; ///< arrivedInNewFolder direction (0 open, ±1 nav)
		// params captured per-action (owned copies — the op reads them on the fiber):
		std::string filenameToStartAt; ///< Open
		std::string defaultDir;        ///< Open
		std::string folderOrPath;      ///< IntoFolder (folder name) / ByPath (full path)
	};
	static bool listingInProgress_;
	/// @brief Whether the listing in progress is this tier's to run, decided once when it began.
	///
	/// Set by @ref beginListing, read by @ref graphicsRoutine. A flag rather than re-asking the
	/// selector each tick because the two must not be able to disagree part-way through an attempt:
	/// an attempt that started on the storage owner and then found itself also being run from the
	/// tick surface would run one listing twice, on two tiers, over one static fileItems.
	static bool listingOnTickSurface_;
	static ListingRequest pendingListing_;

	/// @brief The inputs one listing attempt must see, so that re-running it takes the path the
	///        first pass took rather than the path the first pass's own side effects created.
	///
	/// A listing is not one directory open. `arrivedInNewFolder` opens nothing itself; it calls
	/// `readFileItemsFromFolderAndMemory`, which calls `readFileItemsForFolder`, and between them
	/// sit three retry loops — a missing folder falls back to `defaultDirToAlsoTry` and then to
	/// `mkdir`, a culled listing is re-read from the start, and a brand-new name forces a third
	/// read. One listing therefore performs up to FOUR opens, and which of them happen depends on
	/// what the earlier ones returned.
	///
	/// So the listing cannot be cut into "open, then enumerate, then commit". Instead any open that
	/// is not ready returns @ref Error::LISTING_DEFERRED, every caller propagates it as it already
	/// propagates every other error, and the per-tick surface re-runs the whole attempt from the
	/// top. Nothing is resumed mid-function, which is what leaves the three `goto` control flows
	/// untouched.
	///
	/// Re-running is only sound if the second pass sees the first pass's INPUTS. Three of the
	/// listing's own mutations break that, and every one of them is load-bearing:
	///
	/// - `currentDir` is overwritten with `defaultDirToAlsoTry` on the fallback path, so a re-run
	///   would find the `caselessEquals` guard already true and skip the fallback entirely, going
	///   straight to `mkdir`.
	/// - `enteredText` is rewritten by `setEnteredTextFromCurrentFilename`, `getUnusedSlot` and
	///   `nextDefaultName`. The last is the sharp one: it advances SONG185 to SONG186, so each
	///   re-run would advance again. And the `enteredText.empty()` guard flips to false once
	///   `getUnusedSlot` has run, skipping the re-read that follows it.
	/// - `triedCreatingFolder` was a local, so it reset on every re-run — and a pass that had
	///   already created the folder would try again and read the resulting EXISTS as terminal.
	///
	/// Hence: capture the first two on the first attempt, put them back before every re-entry, and
	/// keep the third here rather than on the stack.
	///
	/// @warning Two of those three are DEFENSIVE, not demonstrated. Neutering each against the
	///          idempotence gate showed the retry loops are convergent — a re-run that skips the
	///          fallback opens the fallback directory directly, and a re-run after a successful
	///          `mkdir` finds the directory there and never re-enters that branch. The one path that
	///          genuinely diverges is `getUnusedSlot`, whose re-read is skipped on a re-run when
	///          `enteredText` is left non-empty; it needs `mayDefaultToBrandNewNameOnEntry`, which
	///          only `SaveUI` sets, so no gate here covers it. They are kept because the convergence
	///          argument is subtle and costs two string assignments to make unnecessary — but do not
	///          read the green gate as proof of them.
	struct ListingAttempt {
		bool active = false;        ///< an attempt is under way, possibly across several ticks
		std::string dirAtEntry;     ///< currentDir as it was when the attempt began
		std::string enteredAtEntry; ///< enteredText as it was when the attempt began
		bool triedCreatingFolder{}; ///< attempt-scoped, NOT call-scoped: survives a re-entry
		int32_t opensThisAttempt{}; ///< which open this pass is on; the defer seam counts on it

		/// @brief Start a fresh attempt, capturing the inputs a re-entry must restore.
		void begin();
		/// @brief Put the captured inputs back, ready to re-run the attempt from the top.
		void restore();
	};
	static ListingAttempt attempt_;

	/// @brief Test seam: make the Nth open of an attempt defer ONCE, instead of opening. 0 = never.
	///
	/// Cleared as it fires, which is the whole of what makes it model a channel: an open that
	/// deferred is answered on the NEXT ask. A seam that stayed armed would defer the same open on
	/// every pass and the attempt would never finish -- which is exactly what the first version of it
	/// did, and what the gate below caught.
	///
	/// Exists so the idempotence of a re-run can be gated directly — for each N, a listing that
	/// deferred at its Nth open must finish identical to one that never deferred. Nothing in the
	/// firmware sets it; only @ref BrowserListingAbProbe does.
	static int32_t deferAtOpenNumber_;
	friend class BrowserListingAbProbe;

	/// @brief Extra context for ListingAction::Reload's two selectEncoderAction call sites.
	///
	/// The encoder offset and catalog-search-direction don't fit ListingRequest's generic fields
	/// (which are shared across every action), so they're captured here alongside pendingListing_.
	static int32_t pendingReloadCatalogSearchDirection_;
	static bool pendingReloadSearchFromEnd_;

	/// @brief Dispatch `req`'s listing onto the storage owner.
	///
	/// Sets listingInProgress_ and shows the loading indicator; on a dropped dispatch clears the
	/// flag (retried on the next HID event).
	/// @param req The listing to run.
	/// @return True if the listing was actually dispatched (queued or run inline), false if the
	///         dispatch was dropped (owner queue full) — callers that mutate state before calling
	///         this that only a completed listing would reconcile must check the return and revert
	///         on false.
	bool beginListing(ListingRequest req);
	/// @brief Obtain `path` as an open Directory: from the async directory channel where it backs
	///        this build and an attempt is under way, and by a direct open otherwise.
	///
	/// The synchronous arm is what ships. With the channel inactive its weak fallback answers UNKNOWN
	/// forever, so a caller that only polled would defer every tick and never list anything at all.
	/// @param path NUL-terminated directory path.
	/// @return The open Directory, or the Error that stopped it — which may be
	///         @ref Error::LISTING_DEFERRED, meaning "not a failure; the attempt will be re-run".
	[[nodiscard]] static std::expected<deluge::io::Directory, Error> openDirectoryForListing(char const* path);

	/// @brief Owner-op entry point dispatched to run the pending listing.
	/// @param self The active Browser* the listing was dispatched from.
	static void runListingTrampoline(void* self);
	/// @brief Runs on the fiber: dispatches the pending listing action and its completion hooks.
	void runPendingListing();

	// Completion hooks (run inside the op, on the fiber):
	virtual void onBrowserOpened() {} ///< Open-only bespoke tail (default empty).
	/// @brief Default handling for a failed listing: displayError() + close() (not exitAction()).
	/// @param error The failure reported by the listing.
	virtual void onListingFailed(Error error);

	// Bodies of the listing actions, run on the fiber inside runPendingListing().
	Error openListingImpl(int32_t direction, char const* filenameToStartAt, char const* defaultDir);
	Error goIntoFolderImpl(char const* folderName);
	Error goUpOneDirectoryLevelImpl();
	Error setFileByFullPathImpl(char const* fullPath);
	Error reloadImpl(int32_t direction);

	/// @brief Shared selectEncoderAction tail: applies the fileIndexSelected/scroll update and fires
	///        currentFileChanged.
	///
	/// Runs either synchronously (no reload needed) or from reloadImpl() on the fiber.
	/// @param newFileIndex The newly selected file index.
	/// @param offset       The encoder offset that produced this selection.
	/// @return Error::NONE on success, or the failure from the underlying reload/selection.
	Error finishSelectEncoderAction(int32_t newFileIndex, int8_t offset);
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
	static int8_t numberEditPos;   // -1 is default
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
	for (FileItem const& fileItem : Browser::fileItems) {
		// .c_str(): displayName() returns const std::string&, and passing a
		// non-trivial type through a variadic function is undefined behaviour.
		D_PRINTLN(" - %s", fileItem.displayName().c_str());
	}
	D_PRINT("\n");
}
