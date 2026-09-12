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
#include "libdeluge/file_directory.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/byte_channel.h"
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

std::string Browser::currentDir{};
bool Browser::qwertyVisible;

deluge::vector<FileItem> Browser::fileItems;
int32_t Browser::scrollPosVertical;
int32_t Browser::fileIndexSelected;
int32_t Browser::numCharsInPrefix;
bool Browser::arrivedAtFileByTyping;
int32_t Browser::numFileItemsDeletedAtStart;
int32_t Browser::numFileItemsDeletedAtEnd;
std::string Browser::firstFileItemRemaining;
std::string Browser::lastFileItemRemaining;
OutputType Browser::outputTypeToLoad;
char const** Browser::allowedFileExtensions;
bool Browser::allowFoldersSharingNameWithFile;
char const* Browser::filenameToStartSearchAt;

namespace {
/// See Browser::unusedSlotCalls().
int32_t g_unusedSlotCalls = 0;
/// See Browser::totalDirectoryOpens().
int32_t g_totalDirectoryOpens = 0;
} // namespace

int32_t Browser::unusedSlotCalls() {
	return g_unusedSlotCalls;
}

int32_t Browser::totalDirectoryOpens() {
	return g_totalDirectoryOpens;
}

bool Browser::listingInProgress_;
bool Browser::listingOnTickSurface_;
Browser::ListingState Browser::listing_;
Browser::PredictionState Browser::prediction_;
std::optional<Browser::PredictionTarget> Browser::pendingPrediction_;
int32_t Browser::deferAtOpenNumber_ = 0;
Browser::ListingRequest Browser::pendingListing_;
int32_t Browser::pendingReloadCatalogSearchDirection_;
bool Browser::pendingReloadSearchFromEnd_;

// 7SEG ONLY
int8_t Browser::numberEditPos;

char const* allowedFileExtensionsXML[] = {"XML", "Json", NULL};

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
	pendingListing_ = std::move(req);
	listingInProgress_ = true;
	display->displayLoadingAnimationText("Working", /*delayed=*/true);

	// `deferAtOpenNumber_` is the second disjunct so that the idempotence gate can exercise the
	// re-entry path on a build where the directory channel is not compiled in at all. What that gate
	// checks -- that a listing re-run from the top finishes identically to one that never deferred --
	// is a property of the listing, not of the channel, and it is the riskiest half of this rung.
	// Only the actions that run through the listing machine may take the tick surface. Reload does
	// its own read (readFileItemsFromFolderAndMemory, not arrivedInNewFolder), so it has no machine to
	// suspend into — running it here would block the interaction tier on the card, which is the exact
	// thing this rung exists to stop. It keeps the storage owner until it is converted in turn.
	const bool machineBacked = pendingListing_.action != ListingAction::Reload;
	listingOnTickSurface_ = machineBacked && (deluge_file_directory_async_active() || deferAtOpenNumber_ != 0);
	if (listingOnTickSurface_) {
		// The attempt runs on the INTERACTION tier, in graphicsRoutine(), start to finish, and it
		// never blocks there: every directory open goes through the channel and every enumeration is
		// FS-free (dir.read() walks the snapshot the open already took).
		//
		// Deliberately NOT dispatched to the owner first. An attempt that began on the owner and was
		// re-entered from the tick surface would have two tiers touching one static ListingAttempt
		// and one static fileItems -- a cross-tier race in exchange for nothing, since the owner is
		// no longer where the waiting happens.
		//
		// The residuals, recorded rather than hidden. The directory OPEN is what this rung moved off
		// the interaction tier; these are the card calls on the same path that it did not:
		//
		//  - StorageManager::initSD(), a flag check on an already-mounted card but real blocking work
		//    on a re-mount (the ejection path, which already blocks).
		//  - deluge::io::mkdir(defaultDirToAlsoTry) on the FOLDER_DOESNT_EXIST fallback — a card
		//    WRITE, through a blocking spin, routinely reached by the Save UIs when their default
		//    folder is absent.
		//  - StorageManager::fileExists() in setFileByFullPathImpl, so on every ByPath listing.
		//
		// And one that is not a call but a loss: the success tail (folderContentsReady /
		// onBrowserOpened / currentFileChanged) used to be serialised on the owner alongside every
		// other op that touches Browser::fileItems. It no longer is. All four are rung 4's, together
		// with predictExtendedText and LoadInstrumentPresetUI.
		return true;
	}

	if (!deluge::storage::Owner::run(&Browser::runListingTrampoline, this)) {
		// Owner queue full → op won't run; clear so the next HID event retries.
		listingInProgress_ = false;
		display->removeLoadingAnimation();
		return false;
	}
	return true;
}

void Browser::runListingTrampoline(void* self) {
	static_cast<Browser*>(self)->runPendingListing();
}

/// @brief Map a DIRECTORY open's status onto an Error.
///
/// Exists because `delugeStatusToError` is shared with the file paths and collapses
/// `DELUGE_ERR_NOT_FOUND` onto `Error::FILE_NOT_FOUND`. For a directory open that is the wrong
/// answer, and not merely imprecise: `Error::FOLDER_DOESNT_EXIST` is what the missing-folder fallback
/// tests for, and what `LoadInstrumentPresetUI::confirmPresetOrNextUnlaunchedOne` tests for before
/// degrading to `justGetAnyPreset()`. Both went dead when the filesystem moved to efatfs and the
/// directory open started routing through the shared mapping -- silently, because "folder missing"
/// and "file missing" both stop a listing, just with the wrong recovery and the wrong message.
///
/// Deliberately NOT a change to `delugeStatusToError` itself: a FILE that is not there really is
/// `FILE_NOT_FOUND`, and this is the one place that knows it opened a directory.
static Error directoryOpenError(DelugeStatus status) {
	return status == DELUGE_ERR_NOT_FOUND ? Error::FOLDER_DOESNT_EXIST : delugeStatusToError(status);
}

bool Browser::usingDirectoryChannel() {
	// Every caller that reaches readFileItemsForFolder from the interaction tier can now suspend:
	// listings and typing predictions resume from graphicsRoutine(), and instrument acquisition runs
	// on the storage owner instead (rung 5), where the on_owner() term below already excludes it --
	// a recursive subfolder descent cannot be split across deferrals at all (rung 5 spec, section
	// 2b), so it must never reach the channel. `inConvertedCaller` used to gate this to just the
	// listing/prediction pair, because those three readers had nowhere to put a deferral; now that
	// they run on the owner instead, the gate is deletable rather than needing a replacement.
	//
	// The selector is last because with the channel absent the weak collect answers UNKNOWN forever,
	// so a caller that consulted it would defer every tick and never read anything.
	return !deluge::storage::Owner::on_owner() && deluge_file_directory_async_active();
}

std::expected<deluge::io::Directory, Error> Browser::openDirectoryForListing(char const* path, bool createIfMissing) {
	// A path the channel cannot key is refused HERE, with a real error, rather than passed on.
	// `deluge_file_directory_request` silently drops an over-long path and the collect then answers
	// UNKNOWN forever — which for the audio loader is a benign "ask again" but here is the attempt's
	// only forward path, so the browser would re-run the listing every tick with no error ever shown.
	// Deeply nested user folders can exceed the bound.
	if (strlen(path) >= DELUGE_FILE_DIRECTORY_MAX_PATH) {
		return std::unexpected(Error::FILE_NOT_FOUND);
	}

	if (!usingDirectoryChannel()) {
		auto opened = deluge::io::Directory::open(path);
		if (!opened.has_value()) {
			return std::unexpected(directoryOpenError(deluge::io::to_deluge_status(opened.error())));
		}
		return std::move(*opened);
	}

	uint32_t handle = 0;
	DelugeStatus status = DELUGE_OK;
	switch (deluge_file_directory_collect(path, &handle, &status)) {
	case DELUGE_FILE_DIRECTORY_READY:
		return deluge::io::Directory::adopt(handle);
	case DELUGE_FILE_DIRECTORY_UNKNOWN:
		// "Request it (again)" — the documented meaning, and never a failure to report. This is also
		// where the FIRST request for a path is issued: the attempt names a directory the channel has
		// not been told about, so its opening drive lands here.
		deluge_file_directory_request(path, createIfMissing);
		listing_.deferrals++;
		return std::unexpected(Error::LISTING_DEFERRED);
	case DELUGE_FILE_DIRECTORY_PENDING:
		// Requested or being opened. Ask again next tick and do NOT re-request, which would replace a
		// request that is about to be served.
		listing_.deferrals++;
		return std::unexpected(Error::LISTING_DEFERRED);
	case DELUGE_FILE_DIRECTORY_FAILED:
		// Terminal: the channel will not retry and neither may we, or a folder that cannot be opened
		// would be re-requested on every tick for as long as the browser sits on it. Mapped exactly as
		// the synchronous arm maps it, so the fallback fires on both.
		return std::unexpected(directoryOpenError(status));
	default:
		// Not reachable through this header, and NOT folded into the FAILED arm above. `out_status`
		// is only written on FAILED, so an unrecognised status carries DELUGE_OK — and mapping that
		// through delugeStatusToError yields Error::NONE, which a caller reads as "the listing
		// succeeded and the folder is empty". A wrong status must not be able to become success.
		return std::unexpected(Error::UNSPECIFIED);
	}
}

void Browser::runPendingListing() {
	// A listing spans however many ticks its opens need, and `listing_` carries where it is up to. A
	// re-entry while one is active RESUMES the machine — it must NOT re-run the action's *Impl, which
	// would re-arm the machine from the top and redo every open it has already made, and whose
	// pre-work is not idempotent anyway (goIntoFolderImpl APPENDS to currentDir).
	Error error = listing_.active ? stepListing() : startPendingListing();

	if (error == Error::LISTING_DEFERRED) {
		// Not a failure and not a completion: an open this listing needs is not ready. Leave
		// listingInProgress_ set and the loading animation up, and let the per-tick surface resume.
		// Nothing was committed — a suspended listing stops before its Finalize phase — so the browser
		// still shows the previous listing, which is what makes the wait unobservable.
		return;
	}
	if (error == Error::NONE) {
		// The action's own tail. It lives here rather than in the *Impl because a resumed listing
		// never returns through the *Impl that started it, so a tail left there would simply not run
		// — the browser would list the new folder and then never repaint for it.
		error = finishPendingListing();
	}

	display->removeLoadingAnimation();
	if (error == Error::NONE) {
		if (pendingListing_.action == ListingAction::Open) {
			onBrowserOpened();
		}
		// folderContentsReady already ran inside the listing machine for success.
	}
	else {
		onListingFailed(error);
	}
	listingInProgress_ = false;
}

Error Browser::startPendingListing() {
	switch (pendingListing_.action) {
	case ListingAction::Open:
		return openListingImpl(pendingListing_.direction, pendingListing_.filenameToStartAt.c_str(),
		                       pendingListing_.defaultDir.c_str());
	case ListingAction::IntoFolder:
		return goIntoFolderImpl(pendingListing_.folderOrPath.c_str());
	case ListingAction::UpLevel:
		return goUpOneDirectoryLevelImpl();
	case ListingAction::ByPath:
		return setFileByFullPathImpl(pendingListing_.folderOrPath.c_str());
	case ListingAction::Reload:
		return reloadImpl(pendingListing_.direction);
	}
	return Error::NONE;
}

Error Browser::finishPendingListing() {
	switch (pendingListing_.action) {
	case ListingAction::IntoFolder:
	case ListingAction::UpLevel:
		renderUIsForOled();
		return Error::NONE;

	case ListingAction::ByPath: {
		// The file has to be THERE. The listing searched for exactly this name; if it did not match,
		// the path names a file that no longer exists -- a favourite pointing at something deleted --
		// and selecting whatever sorted into its place would open the wrong file silently.
		if (!listing_.foundExactMatch) {
			return Error::FILE_NOT_FOUND;
		}
		// fileIndexSelected is already the match, set by that same search: no second lookup, so the
		// two cannot disagree.
		// Update the Display
		scrollPosVertical = fileIndexSelected;
		setEnteredTextFromCurrentFilename();
		renderUIsForOled();
		// Inform the Load UI that the File has changed
		currentFileChanged(1);
		return Error::NONE;
	}

	case ListingAction::Open:
	case ListingAction::Reload:
		return Error::NONE;
	}
	return Error::NONE;
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
	// Cancel any attempt still in flight. This rung's is_current: a listing can now span several
	// ticks, so it can outlive the browser that asked for it, and committing into a closed browser
	// would put one folder's contents behind another's name. Cancelling HERE rather than testing
	// getCurrentUI() in the pump because this is the precise event -- the pump's view of the UI stack
	// says nothing about whether this browser is still interested, and in a headless driver it is not
	// meaningful at all.
	//
	// Nothing needs to chase the channel: an answer nobody collects is refused at its take on the
	// next card generation, and the handle it holds is closed there or by the next flush.
	if (listingInProgress_) {
		// Only runPendingListing's own exit used to clear this, and under the owner arm that exit was
		// always reached because the queued op always ran. A tick-surface attempt cancelled here has
		// no such exit, so without this the browser closes with "Working" left on screen and nothing
		// remaining to take it down.
		display->removeLoadingAnimation();
	}
	listingInProgress_ = false;
	listingOnTickSurface_ = false;
	listing_.active = false;

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
		firstFileItemRemaining = (&fileItems[numFileItemsDeletingNow])->displayName();
	}
	else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
deleteFromRightSide:
		numFileItemsDeletedAtEnd += numFileItemsDeletingNow;
		stopAt = static_cast<int32_t>(fileItems.size());
		startAt = stopAt - numFileItemsDeletingNow;
		lastFileItemRemaining = (&fileItems[startAt - 1])->displayName();
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

	// initSD() MOUNTS, and mounting is real card I/O. On the synchronous arm this runs on the storage
	// owner and that is where it belongs; with the channel active this runs on the interaction tier,
	// so the mount moves to the channel's driver instead -- it mounts lazily when an open reports no
	// filesystem, on the storage tier, and bumps the generation the C++ side watches.
	//
	// Card PRESENCE is still reported, by the open failing, so nothing is lost by skipping the
	// pre-flight; what would be lost is the firstCardRead() edge, which graphicsRoutine() collects.
	Error error = Error::NONE;
	if (!usingDirectoryChannel()) {
		error = StorageManager::initSD();
		if (error != Error::NONE) {
			return error;
		}
	}

	// Every open a LISTING makes is counted, so a gate can force the Nth one to defer and check that
	// the finished listing is identical either way. Counted here rather than at the call sites
	// because this is the ONLY place the browser opens a directory.
	//
	// Gated on `listing_.active`, and that is deliberate rather than a gap: `opensSoFar` only tracks
	// the two state machines that resume THROUGH graphicsRoutine() -- a listing from the pump, a
	// prediction from a keystroke -- because those are the only callers this per-open counter needs
	// to force a specific Nth open to defer for. Every other caller that reaches this function now
	// dispatches onto the storage owner instead (rung 5): `LoadInstrumentPresetUI`'s readers and
	// `InstrumentClipMinder::createNewInstrument()`'s Browser::getUnusedSlot() read both run there,
	// where a defer is a fiber yield, not a LISTING_DEFERRED handed back to a caller with nowhere to
	// put it -- they have their own dispatch-count instrumentation (presetAcquisitionsRun()) rather
	// than this counter.
	// Whichever converted caller is active. Only one can be: a listing runs from the pump, a
	// prediction from a keystroke, and neither nests inside the other.
	int32_t* opensSoFar = listing_.active      ? &listing_.opensThisListing
	                      : prediction_.active ? &prediction_.opensThisPrediction
	                                           : nullptr;
	if (opensSoFar != nullptr && deferAtOpenNumber_ != 0 && *opensSoFar + 1 == deferAtOpenNumber_) {
		// Disarmed as it fires: a real channel answers the next ask, so a seam that stayed armed would
		// defer this same open on every resumption and the listing would never finish.
		deferAtOpenNumber_ = 0;
		listing_.deferrals++;
		return Error::LISTING_DEFERRED;
	}

	// Local RAII handle: the destructor closes it on every return path below, so there's no manual
	// close() to forget or double up on. (Historically this contrasted with a shared
	// FatFS::Directory staticDIR global behind a port selector — both are gone: staticDIR was
	// deleted with C-FatFS, and efatfs is the sole filesystem.)
	//
	// A handle adopted from the directory channel lands in exactly the same RAII, which is why
	// Directory::adopt exists: there are only eight directory handles, and one that arrived
	// asynchronously is no less urgent to close than one this thread opened.
	auto opened = openDirectoryForListing(currentDir.c_str(), listing_.active && listing_.read.createIfMissing);
	if (!opened.has_value() && opened.error() == Error::LISTING_DEFERRED) {
		return opened.error(); // nothing was opened; see Browser::ListingState
	}
	// Counted here, AFTER the deferral check, so it means "opens that actually reached the
	// filesystem" rather than "times we asked". A deferral opens nothing, so a listing that suspends
	// and resumes makes exactly as many opens as one that never suspended -- which is the whole
	// property the machine exists for, and what lets the two harness legs be compared directly. The
	// counter also spans resumptions, so a design that restarted from the top would show MORE opens
	// than the reference and be caught by the gate rather than by reasoning.
	if (opensSoFar != nullptr) {
		(*opensSoFar)++;
	}
	g_totalDirectoryOpens++;
	if (!opened.has_value()) {
		return opened.error();
	}
	auto dir = std::move(*opened);

	// Emptied HERE, not before the open. A deferred or failed open must leave the previous listing
	// alone: the browser hides a suspended LISTING behind listingInProgress_, but the typing
	// prediction reaches this same function with no such gate and would blank the folder
	// mid-keystroke. The walk below still starts from a clean list, which is all the original
	// placement guaranteed.
	emptyFileItems();

	numFileItemsDeletedAtStart = 0;
	numFileItemsDeletedAtEnd = 0;
	firstFileItemRemaining.clear();
	lastFileItemRemaining.clear();
	catalogSearchDirection = newCatalogSearchDirection;
	maxNumFileItemsNow = newMaxNumFileItems;
	filenameToStartSearchAt = filenameToStartAt;

	while (true) {
		AudioEngine::logAction("while loop");

		std::optional<DelugeDirEntry> entry = D_TRY_CATCH(dir.read(), error, {
			break; // Break on error
		});

		if (!entry.has_value()) {
			break; /* Break on end of dir */
		}
		if (entry->name[0] == '.') {
			continue; /* Ignore dot entry */
		}
		bool isFolder = entry->is_directory;
		if (isFolder) {
			if (!allowFolders) {
				continue;
			}
		}
		else {
			char const* dotPos = strrchr(entry->name, '.');
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
		thisItem->filename = entry->name;
		thisItem->isFolder = isFolder;

		// displayName is the sort key, and must equal the real on-card name. The 7SEG short form ("185")
		// is produced at render time, not stored here - storing it made enteredText display-dependent, which is what
		// broke default naming on 7SEG (#1069).
	}

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
					if (!strcasecmp(readItem->displayName().c_str(), nextItem->displayName().c_str())) {
						// if (readItem->filename.equalsCaseIrrespective(&nextItem->filename)) {
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
				if (!strcasecmp(readItem->displayName().c_str(),
				                nextItem->displayName().c_str())) { // And if same name...
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
	if (!firstFileItemRemaining.empty()) {
		fileItems.erase(fileItems.begin());
	}
	if (!lastFileItemRemaining.empty()) {
		fileItems.pop_back();
	}
}

Error Browser::setFileByFullPath(OutputType outputType, char const* fullPath) {
	beginListing({.action = ListingAction::ByPath, .direction = 0, .folderOrPath = fullPath});
	return Error::NONE;
}

Error Browser::setFileByFullPathImpl(char const* fullPath) {
	arrivedAtFileByTyping = true;
	// No StorageManager::fileExists() pre-flight any more. It was a blocking card open on a path the
	// listing below is about to read anyway, and with the channel active this function runs on the
	// interaction tier, where blocking on the card is exactly what this ladder exists to stop.
	//
	// The listing answers the same question better: it searches for this very name and records
	// whether it matched EXACTLY, which finishPendingListing() then reads. That also retires a check
	// that could barely fire -- the old tail tested `fileIndexSelected > fileItems.size()`, but
	// searchFileItems returns an INSERTION POINT, which is always <= size, so a missing file fell
	// through it and selected whatever sorted into its place.
	//
	// (TODO(conflation-batch-4) went with it: the removed call treated "could not tell" as "absent".)
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

	// Everything after the listing moved to finishPendingListing(): this listing can suspend, and a
	// resumption does not return through here.
	return arrivedInNewFolder(0, fileName);
}

// song may be supplied as NULL, in which case it won't be searched for Instruments; sometimes this will get called when
// the currentSong is not set up.
Error Browser::readFileItemsFromFolderAndMemory(Song* song, OutputType outputType, char const* filePrefixHere,
                                                char const* filenameToStartAt, char const* defaultDirToAlsoTry,
                                                bool allowFolders, Availability availabilityRequirement,
                                                int32_t newCatalogSearchDirection) {
	// The legacy entry point: build the carried state, run the read, discard the fallback's rewrites.
	// Every caller here is synchronous and cannot suspend, so there is nothing for the rewrites to be
	// resumed with. `song` is ignored in favour of `currentSong` for the same reason it always was --
	// every call site passes exactly that.
	ListingRead read{
	    .outputType = outputType,
	    .filePrefix = filePrefixHere,
	    .filenameToStartAt = filenameToStartAt ? filenameToStartAt : "",
	    .defaultDirToAlsoTry = defaultDirToAlsoTry,
	    .allowFolders = allowFolders,
	    .catalogSearchDirection = newCatalogSearchDirection,
	};
	bool triedCreatingFolder = false;
	return readFolderAndMemory(read, triedCreatingFolder);
}

namespace {
/// The path the owner arm is creating. Held here because the dispatched body runs after this call
/// has returned, so it cannot read the caller's argument.
std::string g_ownerCreatePath;

} // namespace

Error Browser::readFolderAndMemory(ListingRead& read, bool& triedCreatingFolder) {
	// filenameToStartAt should have .XML at the end of it.
	while (true) {
		// ONE open per iteration. A deferral returns immediately with `read` and `currentDir` already
		// carrying whatever fallback this loop had taken, so resuming continues from that open rather
		// than re-running the ones before it.
		Error error = readFileItemsForFolder(read.filePrefix, read.allowFolders, allowedFileExtensions,
		                                     read.filenameToStartAt.empty() ? nullptr : read.filenameToStartAt.c_str(),
		                                     FILE_ITEMS_MAX_NUM_ELEMENTS, read.catalogSearchDirection);
		if (error == Error::LISTING_DEFERRED) {
			return error;
		}
		if (error == Error::NONE) {
			break;
		}

		// If folder didn't exist, try our alternative one if there is one.
		if (error != Error::FOLDER_DOESNT_EXIST || read.defaultDirToAlsoTry == nullptr) {
			return error;
		}
		// ... only if we haven't already tried the alternative folder.
		if (!deluge::string::caselessEquals(currentDir, read.defaultDirToAlsoTry)) {
			read.dropSearchString();
			currentDir = read.defaultDirToAlsoTry;
			continue;
		}
		// Or if we have already tried it and it didn't exist, try creating it... but not if we
		// already tried.
		if (triedCreatingFolder) {
			return error;
		}
		triedCreatingFolder = true;
		if (deluge_file_directory_async_active()) {
			// Ask the CHANNEL to create it, on the storage tier, as part of the next open. Doing it
			// here would be a card WRITE on the interaction tier, which the listing now runs on — no
			// more allowed than a read. Same number of opens either way; only the tier moves.
			read.createIfMissing = true;
			continue;
		}
		// No channel arm here: the channel arm of this function set `createIfMissing` above and never
		// reaches this line. What it needs is the TIER, so the mkdir moves onto the owner and the
		// failure it used to report becomes one the following listing attempt reports instead.
		g_ownerCreatePath = read.defaultDirToAlsoTry;
		deluge::storage::bytes::runOnStorageOwner(
		    [](void*) {
			    deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Browser);
			    (void)deluge::io::mkdir(g_ownerCreatePath.c_str());
		    },
		    nullptr);
	}

	if (currentSong && read.outputType != OutputType::NONE) {
		Error error = currentSong->addInstrumentsToFileItems(read.outputType);
		if (error != Error::NONE) {
			return error;
		}
	}

	if (!fileItems.empty()) {
		sortFileItems();

		if (!fileItems.empty()) {
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
void Browser::graphicsRoutine() {
	// The base's per-tick work goes HERE, above the subclass hook, so that a browser cannot both have
	// per-tick work of its own and skip the listing's.
	//
	// This IS the listing, not a nudge to it: with the channel active beginListing() dispatches
	// nothing and every pass of the attempt runs from here, including the first. A pass either
	// finishes the listing or defers on an open that is not ready, and a deferred pass leaves
	// listingInProgress_ set so the next tick runs the attempt again from the top.
	// Gated on the SELECTOR, not on attempt_.active: the first pass has not begun an attempt yet, so
	// that condition would never let one start. The selector is exact, because it is the same thing
	// beginListing() consulted -- with the channel on it dispatched nothing and the listing is ours to
	// run; with it off the owner has it, and running it here as well would run one listing twice, on
	// two tiers, over one static fileItems.
	// The mount edge, collected wherever the browser ticks. The channel's driver mounts on the storage
	// tier without telling anyone, so this is what still runs firstCardRead() -- handed to the storage
	// owner, because it opens every resident sample and may not run here.
	if (deluge_file_directory_async_active()) {
		StorageManager::noticeCardMount(/*mayRunHere=*/false);
	}

	// Re-ask for a prediction whose folder read has not arrived.
	//
	// take_pending() takes the target OUT before re-driving, not after: the drive runs inline from
	// here and may arm a SECOND deferral, which clearing afterwards would wipe -- leaving the
	// prediction to never finish. The same reasoning as LoadSongUI's preview re-ask.
	if (auto target = std::exchange(pendingPrediction_, std::nullopt); target.has_value()) {
		// Taken OUT before re-driving, not after: the drive runs inline from here and may arm a
		// SECOND deferral, which clearing afterwards would wipe -- leaving the prediction to never
		// finish. The same reasoning as LoadSongUI's preview re-ask.
		prediction_.target = std::move(*target);
		prediction_.active = true;
		(void)continuePrediction();
	}

	if (listingInProgress_ && listingOnTickSurface_) {
		// No is_current test here: close() cancels the attempt at the moment it becomes stale, which
		// is the precise event. See Browser::close().
		runPendingListing();
	}

	browserGraphicsRoutine();
	// Made once, for everybody. The subclass hook must not repeat it.
	UI::graphicsRoutine();
}

Error Browser::arrivedInNewFolder(int32_t direction, char const* filenameToStartAt, char const* defaultDirToAlsoTry) {
	arrivedAtFileByTyping = false;

	if (!qwertyAlwaysVisible) {
		qwertyVisible = false;
	}

	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	// Arm the machine and run it. On the synchronous arm nothing ever defers, so this returns with the
	// listing complete and behaves exactly as the one straight-line function it replaced.
	const bool haveSearchString = (filenameToStartAt != nullptr && *filenameToStartAt != 0);
	listing_ = ListingState{
	    .active = true,
	    .phase = ListingPhase::Read,
	    .read =
	        {
	            .outputType = outputTypeToLoad,
	            .filePrefix = filePrefix,
	            .filenameToStartAt = haveSearchString ? filenameToStartAt : "",
	            .defaultDirToAlsoTry = defaultDirToAlsoTry,
	            .allowFolders = true,
	            .catalogSearchDirection = haveSearchString ? CATALOG_SEARCH_BOTH : CATALOG_SEARCH_RIGHT,
	        },
	    .direction = direction,
	    // Spelled out rather than left to the default: the first read's selection is what chooses the
	    // file, and a listing that skipped it would complete with nothing selected.
	    .rerunSelectionAfterRead = true,
	};
	return stepListing();
}

Error Browser::stepListing() {
	while (true) {
		switch (listing_.phase) {
		case ListingPhase::Read: {
			listing_.readHadSearchString = !listing_.read.filenameToStartAt.empty();
			const Error error = performListingRead();
			if (error == Error::LISTING_DEFERRED) {
				// Suspended ON the pending open. Nothing has advanced past it, so resuming re-asks for
				// that same directory and no open is ever made twice.
				return error;
			}
			if (error != Error::NONE) {
				emptyFileItems();
				listing_.active = false;
				return error;
			}
			listing_.phase = listing_.rerunSelectionAfterRead ? ListingPhase::Select : ListingPhase::Finalize;
			if (listing_.phase == ListingPhase::Finalize) {
				// The brand-new-name re-read falls straight through to "no file selected", exactly as
				// the original `goto useNonExistentFileName` did.
				fileIndexSelected = -1;
				scrollPosVertical = 0;
			}
			listing_.rerunSelectionAfterRead = true;
			break;
		}

		case ListingPhase::Select: {
			const auto outcome = applyListingSelection();
			if (!outcome.has_value()) {
				if (outcome.error() == Error::LISTING_DEFERRED) {
					// The SELECTION reads the card too: getUnusedSlot names a brand-new slot by scanning
					// the folder, from inside this listing. Its deferral suspends the listing where it
					// stands -- phase stays Select, listing_ stays active -- so the pump resumes into
					// the same selection.
					//
					// Clearing listing_.active here, which is what the general failure path below does,
					// left the pump with no active listing: it called startPendingListing() and RESTARTED
					// from the top, redoing every open before the deferral. Against the real channel --
					// one slot, consumed at take -- that is the livelock rung 3 already fixed once.
					return outcome.error();
				}
				emptyFileItems();
				listing_.active = false;
				return outcome.error();
			}
			switch (*outcome) {
			case SelectionOutcome::Finalize:
				listing_.phase = ListingPhase::Finalize;
				break;
			case SelectionOutcome::UseNonExistent:
				fileIndexSelected = -1; // No files.
				scrollPosVertical = 0;
				listing_.phase = ListingPhase::Finalize;
				break;
			case SelectionOutcome::RereadFromStart:
				// Since we're going to just use the first file, if we've deleted items at the start
				// (meaning we had a search string), we need to go back and get them.
				listing_.read.dropSearchString();
				listing_.read.defaultDirToAlsoTry = nullptr; // the fallback already happened, if at all
				listing_.rerunSelectionAfterRead = true;
				listing_.phase = ListingPhase::Read;
				break;
			case SelectionOutcome::RereadForNewName:
				// Only hit when saving the first song created on boot: getUnusedSlot cleared out all
				// the FileItems, so they have to be fetched again. The original's parameters, which
				// differ from the main read's in every field that matters.
				listing_.read = ListingRead{
				    .outputType = OutputType::NONE,
				    .filePrefix = "SONG",
				    .filenameToStartAt = enteredText,
				    .defaultDirToAlsoTry = nullptr,
				    .allowFolders = true,
				    .catalogSearchDirection = CATALOG_SEARCH_BOTH,
				};
				listing_.rerunSelectionAfterRead = false;
				listing_.phase = ListingPhase::Read;
				break;
			}
			break;
		}

		case ListingPhase::Finalize:
			folderContentsReady(listing_.direction);
			listing_.phase = ListingPhase::Done;
			listing_.active = false;
			return Error::NONE;

		case ListingPhase::Done:
			return Error::NONE;
		}
	}
}

Error Browser::performListingRead() {
	return readFolderAndMemory(listing_.read, listing_.triedCreatingFolder);
}

std::expected<Browser::SelectionOutcome, Error> Browser::applyListingSelection() {
	enteredTextEditPos = 0;
	scrollPosHorizontal = 0;

	bool foundExact = false;
	if (fileItems.empty()) {
		// No files found at all. Can we just pick a brand new name?
		if (!mayDefaultToBrandNewNameOnEntry || listing_.direction) {
			enteredText.clear();
			return SelectionOutcome::UseNonExistent;
		}
		return pickBrandNewName();
	}

	fileIndexSelected = 0;

	if (listing_.readHadSearchString) {
		const int32_t i = searchFileItems(listing_.read.filenameToStartAt.c_str(), &foundExact);
		if (foundExact) {
			fileIndexSelected = i;
			listing_.foundExactMatch = true;

			// Usually we'll just use that file.
			if (!mayDefaultToBrandNewNameOnEntry || listing_.direction) {
				return useFoundFile();
			}

			// We found an exact file. But if we've just entered the Browser and are allowed, then we
			// need to find a new subslot variation. Come up with a new name variation.
			const Error error = setEnteredTextFromCurrentFilename();
			if (error != Error::NONE) {
				return std::unexpected(error);
			}
			// Names are display-agnostic ("SONG185", never "185"), so this is one code path for both
			// displays - see default_name.h.
			BrowserFileListView view;
			// Only songs earn letter suffixes; presets pass an empty slotPrefix and take the numeric
			// suffix path, preserving existing preset behaviour.
			char const* slotPrefix = (filePrefix && !memcasecmp(filePrefix, "SONG", 4)) ? filePrefix : "";
			const std::string newName = deluge::gui::browser::nextDefaultName(enteredText.c_str(), slotPrefix, view);
			if (newName == enteredText) {
				// No free variation available - stay on the file we found.
				scrollPosVertical = fileIndexSelected;
				clampFileSelectionAndScroll();
				return SelectionOutcome::Finalize;
			}
			enteredText = newName;
			enteredTextEditPos = enteredText.size();
			return SelectionOutcome::UseNonExistent;
		}
	}

	// We did not find an exact file. Normally, we'll need to just use one of the ones we found.
	// (That's just always the first one, I think...)
	if (!mayDefaultToBrandNewNameOnEntry || listing_.direction) {
		if (numFileItemsDeletedAtStart) {
			return SelectionOutcome::RereadFromStart;
		}
		return useFoundFile();
	}

	// Ok so we didn't find an exact file, we've just entered the browser, and we're allowed new
	// names. So, choose a brand new name (if there wasn't already a new one nominated).
	return pickBrandNewName();
}

std::expected<Browser::SelectionOutcome, Error> Browser::useFoundFile() {
	const Error error = setEnteredTextFromCurrentFilename();
	if (error != Error::NONE) {
		return std::unexpected(error);
	}
	scrollPosVertical = fileIndexSelected;
	// Starting in the middle or end of a short folder should still fill as many display rows as
	// possible.
	clampFileSelectionAndScroll();
	return SelectionOutcome::Finalize;
}

std::expected<Browser::SelectionOutcome, Error> Browser::pickBrandNewName() {
	if (enteredText.empty()) {
		const Error error = getUnusedSlot(OutputType::NONE, &enteredText, filePrefix);
		if (error != Error::NONE) {
			return std::unexpected(error);
		}
		return SelectionOutcome::RereadForNewName;
	}
	return SelectionOutcome::UseNonExistent;
}

// You must set currentDir before calling this.
Error Browser::getUnusedSlot(OutputType outputType, std::string* newName, char const* thingName) {
	// The FOURTH leaf that reaches a folder read, alongside LoadInstrumentPresetUI's three readers --
	// and the one the conversion kept missing, because it does not look like a "preset reader" at all.
	// It has two legal callers, and only two:
	//
	//  - the storage owner, where InstrumentClipMinder::createNewInstrument() runs it and a read that
	//    is not ready is a fiber yield;
	//  - `pickBrandNewName()`, from INSIDE an active listing, where a read that is not ready comes
	//    back as Error::LISTING_DEFERRED and suspends the listing in its Select phase (see
	//    stepListing()). That is why this check permits `listing_.active` and its three siblings do
	//    not: those three cannot be suspended mid-walk, this one is only ever entered from a machine
	//    that can.
	//
	// Boot is the same legal exception the siblings carry. Anything else is an interaction-tier call
	// with nowhere to put a deferral. FREEZE_WITH_ERROR rather than a bare assert() for the reason
	// given at the sibling in findAnUnlaunchedPresetIncludingWithinSubfolders().
	if (ALPHA_OR_BETA_VERSION && !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()
	    && !listing_.active) {
		FREEZE_WITH_ERROR("TIR4");
	}
	g_unusedSlotCalls++;

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
			beginListing({.action = ListingAction::Reload, .direction = offset});
			return;
		}

		else { // Wrap to end
			scrollPosVertical = 0;

			if (numFileItemsDeletedAtEnd) {
				D_PRINTLN("reloading and wrap");
				pendingReloadSearchFromEnd_ = true;
				pendingReloadCatalogSearchDirection_ = CATALOG_SEARCH_LEFT;
				beginListing({.action = ListingAction::Reload, .direction = offset});
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
			beginListing({.action = ListingAction::Reload, .direction = offset});
			return;
		}

		else {
			scrollPosVertical = 9999;

			if (numFileItemsDeletedAtStart) {
				D_PRINTLN("reloading and wrap");
				pendingReloadSearchFromEnd_ = true;
				pendingReloadCatalogSearchDirection_ = CATALOG_SEARCH_RIGHT;
				beginListing({.action = ListingAction::Reload, .direction = offset});
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
		if (error == Error::LISTING_DEFERRED) {
			// NOT a failure, so it must not reach the swallow below. Swallowing it would report
			// success for a listing that never happened: runPendingListing() would clear
			// listingInProgress_, nothing would re-ask, and the browser would sit on the empty
			// fileItems this pass left behind.
			return error;
		}
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
		if (error == Error::LISTING_DEFERRED) {
			// NOT a failure, so it must not reach the swallow below. Swallowing it would report
			// success for a listing that never happened: runPendingListing() would clear
			// listingInProgress_, nothing would re-ask, and the browser would sit on the empty
			// fileItems this pass left behind.
			return error;
		}
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
	// No LISTING_DEFERRED arm here, deliberately: finishSelectEncoderAction re-reads nothing (its only
	// fallible call is setEnteredTextFromCurrentFilename), so a guard for it could never fire.
	if (error != Error::NONE) {
		// Matches the synchronous no-reload path (selectEncoderAction()): displayError only, browser stays
		// open. Swallow here rather than propagating to runPendingListing()/onListingFailed(), which would
		// wrongly close() the browser on a tail error - reload failures never close the browser.
		display->displayError(error);
	}
	return Error::NONE;
}

bool Browser::predictExtendedText(std::optional<int32_t> editPosOnFailure) {
	arrivedAtFileByTyping = true;
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	// Captured by value (not the FileItem*, which the re-read below can invalidate by reallocating
	// fileItems) so we can tell after the search whether we landed on a different file - filename is
	// a FileItem's identity. Carried on the target because a prediction can now finish on a later
	// tick, by which time getCurrentFileItem() names whatever the re-read selected instead.
	FileItem* oldFileItem = getCurrentFileItem();
	std::string oldFilename;
	if (oldFileItem) {
		oldFilename = oldFileItem->filename;
	}

	std::string searchString = enteredText;
	searchString.resize(enteredTextEditPos);

	// Latest wins: this keystroke's prediction replaces any still waiting to be re-driven, whose
	// search string and undo position belong to a character the user has already moved past.
	pendingPrediction_.reset();

	prediction_ = PredictionState{
	    .active = true,
	    .target =
	        {
	            .searchString = std::move(searchString),
	            .numExtraZeroesAdded = 0,
	            .doneNewRead = false,
	            .oldFilename = std::move(oldFilename),
	            .editPosOnFailure = editPosOnFailure,
	        },
	    .opensThisPrediction = 0,
	};
	return continuePrediction();
}

bool Browser::continuePrediction() {
	Error error;
	PredictionTarget& target = prediction_.target;

	// Reachable only via `goto gotError` from the error paths below.
	if (false) {
gotError:
		prediction_.active = false;
		display->displayError(error);
		return false;
	}

	// Change 2026-03-29 - this code used to append a tilde to the search string. The tilde would match after all
	// printable characters so it would return 1 greater than the index of the search result we wanted. unfortunately
	// for reasons I don't fully understand (7seg compatibility maybe?) multi digit integers are compared as one number
	// rather than character by character, so if you had files named "SONG1", "SONG2", "SONG10", and you typed in
	// "SONG1", and then pressed the encoder to try to get it to predict "SONG10", it would instead match "SONG2"
	// because 2 is the closest number to 1 that comes after 1. So now we just search for the string as is. The major
	// impact is this now returns the first match instead of the last match.
doSearch:
	int32_t i = searchFileItems(target.searchString.c_str());

	// If that search takes us off the right-hand end of the list...
	if (i >= static_cast<int32_t>(fileItems.size())) {

		// If we haven't yet done a whole new read from the SD card etc, from within this function, do that now.
		if (!target.doneNewRead) {
doNewRead:
			error = readFileItemsFromFolderAndMemory(
			    currentSong, outputTypeToLoad, filePrefix, target.searchString.c_str(), NULL, true, Availability::ANY,
			    CATALOG_SEARCH_BOTH); // This could probably actually be made to work with searching left only...
			if (error == Error::LISTING_DEFERRED) {
				// Optimistic: the typed character STAYS and the prediction finishes on a later tick.
				// `target` already carries the loop's progress -- which search string, how many extra
				// zeroes, whether this string has been re-read -- so the resumption performs only the
				// read that deferred and does not redo the ones before it.
				//
				// Left active on purpose: graphicsRoutine() drives it.
				pendingPrediction_ = target;
				return true;
			}
			if (error != Error::NONE) {
gotErrorAfterAllocating:
				emptyFileItems();
				goto gotError;
				// TODO - need to close UI or something?
			}
			// Marked done only once the read has actually HAPPENED. Setting it before, which is where
			// it used to be, meant a resumption re-entered the loop with the flag already true and
			// skipped the very read that had deferred -- the prediction then finished having read one
			// time fewer than a synchronous run, on a smaller folder view, and landed somewhere else.
			target.doneNewRead = true;
			goto doSearch;
		}

		// Otherwise if we already tried that, then our whole search is fruitless.
notFound:
		prediction_.active = false;
		if (!mayDefaultToBrandNewNameOnEntry) {
			if (fileIndexSelected >= 0) {
				setEnteredTextFromCurrentFilename(); // Set it back
			}
			// The caller that undoes the typed character does so on a false return -- but a prediction
			// that SUSPENDED already returned true to it and will never return again, so the undo
			// happens here instead. Only that caller sets this; the other three ignore the result and
			// must not have their edit position moved.
			if (target.editPosOnFailure.has_value()) {
				enteredTextEditPos = *target.editPosOnFailure;
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
		if (i == 0 && !target.doneNewRead) {
			goto doNewRead;
		}

		// this code is original but I don't know what it does. Slot browser maybe?
		// Just updated to append to the string instead of replacing the tilde
		if (target.numExtraZeroesAdded < 4) {
			target.searchString.append("0", 1);
			target.numExtraZeroesAdded++;
			target.doneNewRead = false;
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
	if (fileItem->filename != target.oldFilename) {
		currentFileChanged(0);
	}

	prediction_.active = false;
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

	bool inFavouriteOrBanksColumn = (x >= 0 && x < static_cast<int32_t>(kNumFavourites));

	if (isFavouritesVisible() && inFavouriteOrBanksColumn && y == favouriteRow) {
		if (on) {
			if (isSDRoutineActive()) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			// The bank this row shows may still be arriving, or may not have been readable. Neither may be
			// edited -- saving a bank that never loaded replaces the favourites still on the card -- and
			// recalling from one would recall nothing.
			switch (favouritesManager.bankState()) {
			case deluge::favourites::BankState::Loading:
				return ActionResult::DEALT_WITH;
			case deluge::favourites::BankState::Unavailable:
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SD_CARD_ERROR));
				return ActionResult::DEALT_WITH;
			case deluge::favourites::BankState::Loaded:
				break;
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
		}
		return ActionResult::DEALT_WITH;
	}
	else if (isBanksVisible() && inFavouriteOrBanksColumn && y == favouriteBankRow) {
		if (on) {
			if (isSDRoutineActive()) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			favouritesManager.selectFavouritesBank(x);
			favouritesChanged();
			return ActionResult::DEALT_WITH;
		}
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
			if (favouritesManager.bankState() == deluge::favourites::BankState::Unavailable) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SD_CARD_ERROR));
			}
			else if (favouritesManager.currentFavouriteNumber.has_value()) {
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
	beginListing({.action = ListingAction::IntoFolder, .direction = 1, .folderOrPath = folderName});
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

	// The renderUIsForOled() tail moved to finishPendingListing(): this listing can suspend, and a
	// resumption does not return through here.
	return arrivedInNewFolder(1);
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

	beginListing({.action = ListingAction::UpLevel, .direction = -1});
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

	// The renderUIsForOled() tail moved to finishPendingListing() — see goIntoFolderImpl.
	return arrivedInNewFolder(-1, enteredText.c_str());
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

	// Fire-and-forget, like the recursive helper below and for the same reason: the listing this
	// dispatches into already reports a folder it cannot open, so a card failure is not lost -- it
	// simply arrives from the listing rather than from here. See the comment above this function,
	// which already describes a listing failure after a successful mkdir taking that route.
	if (deluge::storage::bytes::submitMkdirAll(newDirPath.c_str())) {
		goIntoFolder(enteredText.c_str()); // Dispatches the listing async; always returns Error::NONE.
		return Error::NONE;
	}
	g_ownerCreatePath = newDirPath;
	// Where blocking is legal only. A refusal creates nothing, and the listing below reports the folder missing.
	(void)deluge::storage::bytes::runInlineWhereLegal(
	    [](void*) {
		    deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Browser);
		    (void)deluge::io::mkdir(g_ownerCreatePath.c_str());
	    },
	    nullptr);

	goIntoFolder(enteredText.c_str()); // Dispatches the listing async; always returns Error::NONE.

	return Error::NONE;
}

namespace {

/// The segment walk, for the arm that has no channel to do it.
///
/// Unchanged from what `createFoldersRecursiveIfNotExists` did inline, including its pre-existing
/// quirk, which is not this rung's to fix. The channel arm does not come here at all: its walk lives
/// in Rust, in one place, so neither this caller nor `smSysex` needs a loop of its own.
Error createFoldersRecursiveOnOwner(const char* path) {
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

} // namespace

Error Browser::createFoldersRecursiveIfNotExists(const char* path) {
	if (!path || *path == '\0') {
		return Error::UNSPECIFIED;
	}

	// FIRE-AND-FORGET, deliberately. The four callers reach here from `opened()`, which returns `bool`
	// once and has nowhere to wait, so a deferred creation cannot gate a UI open. A card failure now
	// surfaces when that folder's listing fails -- which already has a failure path, and already
	// handles a missing folder -- rather than refusing to open a UI that would work for every other
	// folder. That is a user-visible change, and an improvement, but a change.
	// Through the seam's submit machine rather than a bare request, so `serviceRoutine` collects the
	// answer. A request nobody collects leaves its outcome unlearned and its answer sitting in the
	// slot -- fire-and-forget describes the CALLER, not the channel.
	if (deluge::storage::bytes::submitMkdirAll(path)) {
		return Error::NONE;
	}
	g_ownerCreatePath = path;
	// Where blocking is legal only. A refusal creates nothing, and that folder's listing reports it missing.
	(void)deluge::storage::bytes::runInlineWhereLegal(
	    [](void*) {
		    deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Browser);
		    (void)createFoldersRecursiveOnOwner(g_ownerCreatePath.c_str());
	    },
	    nullptr);
	return Error::NONE;
}

void Browser::sortFileItems() {
	shouldInterpretNoteNames = shouldInterpretNoteNamesForThisBrowser;
	octaveStartsFromA = false;

	std::sort(fileItems.begin(), fileItems.end(), [](FileItem const& a, FileItem const& b) {
		return strcmpspecial(a.displayName().c_str(), b.displayName().c_str()) < 0;
	});
}

int32_t Browser::searchFileItems(char const* searchString, bool* foundExact) {
	// Project each item to a plain C string so the comparator is symmetric in its argument types, as
	// ranges::lower_bound requires of a strict weak order. The projected pointer never outlives the call.
	auto it = std::ranges::lower_bound(
	    fileItems, searchString, [](char const* a, char const* b) { return strcmpspecial(a, b) < 0; },
	    [](FileItem const& f) { return f.displayName().c_str(); });
	if (foundExact != nullptr) {
		*foundExact = (it != fileItems.end() && strcmpspecial(it->displayName().c_str(), searchString) == 0);
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
