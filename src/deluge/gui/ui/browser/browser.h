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
#include "storage/deferred_load.h"
#include "storage/file_item.h"
#include "util/containers.h"

#include <expected>
#include <string>

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
	/// @brief The search name of the read in progress, empty for a read with none.
	///
	/// Owned rather than borrowed. sortFileItems() trims against it, and it is called again after the read that set
	/// it has returned -- getUnusedSlot() and LoadInstrumentPresetUI's readers both sort once more -- by which time
	/// the ListingRead whose string a pointer would name is gone.
	static std::string filenameToStartSearchAt;

	/// @brief True while an async listing (beginListing()'s dispatch onto the storage-owner fiber)
	///        is in flight.
	///
	/// A caller that drives beginListing() from outside the normal HID-event flow (e.g. a headless
	/// harness) needs this to know when it's safe to act on the listing's result
	/// (getCurrentFileItem() etc) instead of racing it.
	/// @return True while a listing is in flight; false once it has completed (or none was started).
	static bool isListingInProgress() { return listingInProgress_; }

	/// @brief How many times getUnusedSlot() has run. For gates only.
	///
	/// getUnusedSlot names a brand-new slot by READING the folder, from inside a listing that is
	/// already running. A gate needs to prove that a listing which suspended in that read does not
	/// name a second time when it resumes — `pickBrandNewName`'s `enteredText.empty()` guard is what
	/// prevents it, and a guard asserted rather than measured is exactly how this rung's earlier
	/// drafts went wrong.
	[[nodiscard]] static int32_t unusedSlotCalls();

	/// @brief Every directory open the browser has ever made. For gates only.
	///
	/// NOT `ListingState::opensThisListing`, and the difference is the whole point: a listing that
	/// RESTARTS re-arms `listing_` and resets that member, so it cannot see the restart it is meant to
	/// catch. This one is never reset, so redone opens show up as a higher total.
	[[nodiscard]] static int32_t totalDirectoryOpens();

	/// @brief Every directory-channel request the browser has issued. For gates only.
	///
	/// A request goes out only when the channel answers UNKNOWN for the open in hand, which for a listing or prediction
	/// that resumes is once per open. One that restarted from the top would ask again for an open it had already made,
	/// so a gate that counts more requests than opens has caught a restart.
	[[nodiscard]] static int32_t directoryRequests();

	/// @brief How many listings have failed through @ref onListingFailed. For gates only.
	[[nodiscard]] static int32_t listingFailures();

	/// @brief The error the most recent of those failures reported. For gates only.
	[[nodiscard]] static Error lastListingFailure();

	ActionResult exitUI() override {
		exitAction();
		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}
	bool isFavouritesVisible() override;
	bool isBanksVisible() override;
	void favouritesBankApplied() override { favouritesChanged(); }

protected:
	Error setEnteredTextFromCurrentFilename();
	Error goUpOneDirectoryLevel();
	virtual Error arrivedInNewFolder(int32_t direction, char const* filenameToStartAt = nullptr,
	                                 char const* defaultDir = nullptr);
	bool predictExtendedText(std::optional<int32_t> editPosOnFailure = std::nullopt) override;
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

	/// @brief The parameters of ONE directory read.
	///
	/// A listing makes up to three reads and they genuinely differ — the post-cull re-read drops the
	/// search string, and the brand-new-name re-read changes the output type, the prefix AND the
	/// search direction — so they are carried rather than recomputed from the caller's arguments.
	struct ListingRead {
		OutputType outputType{};
		char const* filePrefix{};
		std::string filenameToStartAt;
		char const* defaultDirToAlsoTry{}; ///< null on both re-reads: a fallback happens once
		bool allowFolders{true};
		int32_t catalogSearchDirection{CATALOG_SEARCH_RIGHT};
		/// Create the directory before opening it — the missing-folder fallback's `mkdir`, moved onto
		/// the storage tier by riding the channel's request instead of running inline. Set only on the
		/// async arm; the synchronous arm runs on the storage owner already, where an inline mkdir is
		/// where it belongs.
		bool createIfMissing{};

		/// @brief Drop the search string, and the search DIRECTION that depends on it.
		///
		/// The two must move together or the cull dereferences a null pointer. `cullSomeFileItems`
		/// only consults `filenameToStartSearchAt` under CATALOG_SEARCH_BOTH — the "tend towards equal
		/// amounts either side" branch — and it consults it by calling `searchFileItems`, which hands
		/// it straight to `strcmpspecial`. So a read with no search string but a BOTH direction
		/// null-derefs the moment the folder holds enough entries to cull.
		///
		/// The pairing was implicit and the missing-folder fallback got it wrong — it cleared the
		/// search string and left the direction alone. That is NOT currently reachable, and the reason
		/// is worth writing down: `readFileItemsForFolder` maps its directory-open failure through
		/// `delugeStatusToError`, which collapses NOT_FOUND onto `Error::FILE_NOT_FOUND`, so the
		/// fallback's `Error::FOLDER_DOESNT_EXIST` test can never be true and the whole fallback is
		/// dead code. Whoever revives it — a browser entered on a missing folder is *supposed* to fall
		/// back to its default directory and create it — would otherwise revive the crash with it. It
		/// is one function now so the two cannot be separated again.
		void dropSearchString() {
			filenameToStartAt.clear();
			catalogSearchDirection = CATALOG_SEARCH_RIGHT;
		}
	};

	/// @brief Which part of a listing runs next.
	///
	/// A listing is not one directory open. `arrivedInNewFolder` opens nothing itself; between it and
	/// `readFileItemsForFolder` sit three retry loops — a missing folder falls back to
	/// `defaultDirToAlsoTry` and then to `mkdir`, a culled listing is re-read from the start, and a
	/// brand-new name forces a third read. One listing performs up to FOUR opens, on two paths, with
	/// a filesystem mutation between, and which of them happen depends on what the earlier ones
	/// returned.
	///
	/// Each of those opens can defer, so the listing has to suspend and resume BETWEEN them — and,
	/// critically, must never redo an open it has already made. Re-running the whole listing from the
	/// top was tried and cannot work: `readdir_next` MOVES each name out of the directory snapshot as
	/// it walks (`efatfs_core.rs`, "a given entry can only be read once", deliberately, because a
	/// large snapshot is what leaves the arena short), so a directory cannot be enumerated twice from
	/// one handle. A re-run therefore needs a fresh open for every open it already made, which a
	/// single-slot channel cannot serve, and the listing livelocks.
	///
	/// Hence an explicit machine: `Read` performs the pending read, `Select` is the pure tail that
	/// decides what to make current, and either may hand back to `Read` with new parameters.
	enum class ListingPhase : uint8_t {
		Read,     ///< perform ListingState::read — at most one open per entry, then advance
		Select,   ///< the pure selection tail over whatever the read produced
		Finalize, ///< folderContentsReady()
		Done,
	};

	/// @brief Everything a suspended listing needs in order to resume where it left off.
	///
	/// Static for the same reason `pendingListing_` is: one listing at a time. Nothing here is
	/// captured-and-restored — the machine never re-runs a step, so there is nothing to undo.
	struct ListingState {
		bool active{};
		ListingPhase phase{ListingPhase::Read};
		ListingRead read;
		int32_t direction{};
		/// Survives a suspension, and must: a listing that deferred after creating the folder would
		/// otherwise `mkdir` it again on resume and read the resulting EXISTS as terminal.
		bool triedCreatingFolder{};
		/// Whether the selection tail runs after the read that is pending. TRUE by default and for the
		/// post-cull re-read; FALSE only for the brand-new-name re-read, which falls straight through
		/// to "no file selected" exactly as the original `goto useNonExistentFileName` did.
		bool rerunSelectionAfterRead{true};
		/// Whether the read that just completed had a search string — the selection tail branches on
		/// it, and by then `read.filenameToStartAt` may already have been rewritten.
		bool readHadSearchString{};
		/// How many opens this listing has made, across the WHOLE listing rather than per resumption. A gate
		/// compares it with the requests the listing issued.
		int32_t opensThisListing{};
		/// Whether the selection found the search string EXACTLY. `ListingAction::ByPath` is the one
		/// caller that needs to know, and it needs it as an existence answer: a favourite naming a file
		/// that has been deleted must report FILE_NOT_FOUND rather than silently selecting whatever
		/// sorted into its place. Recorded by the search the selection already performs, rather than
		/// searched for a second time, so the two cannot disagree about what "the same name" means.
		bool foundExactMatch{};
		/// How many times this listing suspended. Zero on the synchronous arm by construction, so a
		/// gate can require it to be non-zero and know the async path was actually reached rather than
		/// quietly answered by a direct open.
		int32_t deferrals{};
	};
	static ListingState listing_;

	/// @brief Everything a suspended typing prediction needs in order to resume where it left off.
	///
	/// Prediction performs up to FIVE reads per call: `doneNewRead` guards the re-read, but the
	/// extra-zeroes branch clears it before looping back, so each of the four attempts can read again.
	/// Restarting from the top would therefore livelock against the single-slot channel exactly as the
	/// listing did — read #1 takes the answer and consumes the slot, read #2 finds it empty and
	/// defers, the restart consumes the freshly published answer again. So the loop's progress is
	/// carried and the re-entry continues from it.
	struct PredictionTarget {
		std::string searchString;      ///< as extended so far, including any appended zeroes
		int32_t numExtraZeroesAdded{}; ///< how far through the extra-zero attempts
		bool doneNewRead{};            ///< whether this search string has already been re-read
		/// The file selected when the prediction STARTED, so the tail can tell whether it landed
		/// somewhere new. NOT recomputable on resumption: a read rebuilds fileItems, so
		/// getCurrentFileItem() afterwards names a different file and the comparison would either
		/// fire a spurious preview or miss a real one.
		std::string oldFilename;
		/// What to restore if the prediction ultimately fails — and OPTIONAL, because only ONE of the
		/// four call sites consumes the result. `qwerty_ui.cpp`'s pad-typing path undoes the character
		/// on false; the two backspace paths and the horizontal-encoder path ignore it, and a
		/// resumption that undid unconditionally would corrupt all three.
		std::optional<int32_t> editPosOnFailure;
	};

	struct PredictionState {
		bool active{};
		PredictionTarget target;
		int32_t opensThisPrediction{}; ///< reads that reached the filesystem; the gate counts on it
	};
	static PredictionState prediction_;
	/// @brief The prediction waiting to be re-driven, if one suspended.
	///
	/// A plain optional rather than `DeferredLoad`, deliberately. DeferredLoad exists for work that
	/// is DISPATCHED — its generations, in-flight flag and is_current answer "is the op that came
	/// back still the one we wanted?". A prediction dispatches nothing: it defers inline and is
	/// re-driven inline from the tick, so none of that applies, and `request()` would not even arm
	/// the pending slot (that is `complete()`'s job for an op that reported back).
	///
	/// Latest-wins falls out of assignment: a second keystroke starts a new prediction, which
	/// overwrites this before the tick sees the old one.
	static std::optional<PredictionTarget> pendingPrediction_;

	/// @brief The search loop, over `prediction_.target`. See @ref PredictionTarget.
	bool continuePrediction();

	/// @brief What the selection tail decided to do next.
	enum class SelectionOutcome : uint8_t {
		Finalize,         ///< a file is selected; run the completion hook
		UseNonExistent,   ///< no file selected; clear the selection, then finalize
		RereadFromStart,  ///< re-read this folder with no search string, then run selection AGAIN
		RereadForNewName, ///< re-read for the brand-new name, then UseNonExistent
	};

	/// @brief Run the listing until it completes, fails, or needs an open that is not ready.
	/// @return Error::NONE when the listing is complete, Error::LISTING_DEFERRED when it suspended
	///         (call again; nothing has advanced past the pending open), or the failure.
	Error stepListing();
	/// @brief Dispatch the pending action's own start — its pre-work plus the listing it asks for.
	Error startPendingListing();
	/// @brief Run the pending action's completion tail, once the listing has finished.
	Error finishPendingListing();
	/// @brief Perform ListingState::read, including its missing-folder fallback and mkdir retry.
	/// @return As @ref stepListing. Each iteration of the fallback is one open; a deferral leaves the
	///         state already advanced past any fallback it took, so resuming does not redo it.
	Error performListingRead();
	/// @brief The selection tail: pure, no I/O, decides what a completed read makes current.
	[[nodiscard]] std::expected<SelectionOutcome, Error> applyListingSelection();
	/// @brief Settle on the file at fileIndexSelected: name it and bring it into view.
	[[nodiscard]] std::expected<SelectionOutcome, Error> useFoundFile();
	/// @brief Nominate a brand-new name if one was not already, asking for the re-read it forces.
	[[nodiscard]] std::expected<SelectionOutcome, Error> pickBrandNewName();

	friend class BrowserListingAbProbe;
	friend class PathOperationsBrowserProbe;

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
	/// @brief Whether a directory open made here goes through the directory channel rather than opening directly.
	/// @return True off the storage owner once it has started. False on the owner and before it starts (boot, and
	///         every C++-only build, whose owner never starts), where a direct open is legal.
	[[nodiscard]] static bool usingDirectoryChannel();
	/// @brief Obtain `path` as an open Directory: through the directory channel when @ref usingDirectoryChannel says
	///        so, and by a direct open otherwise.
	/// @param path NUL-terminated directory path.
	/// @param createIfMissing Create `path` before opening it. Only the channel honours this; a direct open ignores
	///        it, so a caller on the owner creates the folder itself.
	/// @return The open Directory, or the Error that stopped it — which may be
	///         @ref Error::LISTING_DEFERRED, meaning "not a failure; the attempt will be re-run".
	[[nodiscard]] static std::expected<deluge::io::Directory, Error> openDirectoryForListing(char const* path,
	                                                                                         bool createIfMissing);

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

	/// @brief The one implementation behind @ref readFileItemsFromFolderAndMemory, over carried state.
	///
	/// Takes `read` by reference because the missing-folder fallback REWRITES it — it moves
	/// `currentDir` to the default directory and drops the search string — and a listing that
	/// suspends mid-fallback has to resume with those rewrites already applied, or it will re-run the
	/// fallback against the wrong directory with the wrong search string. The legacy entry point
	/// above passes a temporary and throws the rewrites away, which is what its callers want.
	/// @param read The read to perform; mutated by the fallback.
	/// @param triedCreatingFolder Whether this listing has already created the default folder.
	Error readFolderAndMemory(ListingRead& read, bool& triedCreatingFolder);
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
