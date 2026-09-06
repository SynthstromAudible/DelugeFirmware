#pragma once

/// @brief `BrowserScene` -- one file browser, parameterised by descriptor, over the extracted
///        catalogue and the async listing seam.
///
/// Legacy has a `Browser` base (`gui/ui/browser/browser.{h,cpp}`) over `QwertyUI` and a family of
/// subclasses. **Two of them are reachable from the ported menu tree** and are the two descriptors
/// below: `SampleBrowser` (`gui/ui/browser/sample_browser.cpp`), opened by the sample-file items,
/// and `LoadMidiDeviceDefinitionUI` (`gui/ui/load/load_midi_device_definition_ui.cpp`), opened by
/// `midi_device_linked`. Everything the two differ in -- title, extensions, where they start, which
/// icon a row draws, what accepting a file does -- is `Descriptor`, and `SceneId::arg` selects one,
/// exactly as `rename::Target` and `context_menu::Descriptor` turned their own legacy hierarchies
/// into tables.
///
/// The **load/save family** (`gui/ui/load/load_song_ui`, `load_instrument_preset_ui`,
/// `load_pattern_ui`, and all of `gui/ui/save/`) is deliberately **out of scope**: those are opened
/// from root UIs rather than from menu items, and they carry a second axis this scene has none of
/// (`SlotBrowser`'s brand-new-name machinery, `mayDefaultToBrandNewNameOnEntry`,
/// `getUnusedSlot`). A later plan gives them their own descriptors or their own scene.
///
/// **Ports.** `Browser::renderOLED` (`browser.cpp:925 renderOLED`),
/// `Browser::clampFileSelectionAndScroll` (`:1009`), `Browser::setEnteredTextFromCurrentFilename`
/// (`:1316`), `Browser::backButtonAction` (`:1283`), `Browser::mainButtonAction` (`:1250`),
/// `Browser::selectEncoderAction`'s in-window arm and its four reload arms (`:549`,
/// `:641-691`), `Browser::predictExtendedText`'s in-memory search (`:782`), and the enter-key
/// bodies of both subclasses (`sample_browser.cpp:333 enterKeyPress`,
/// `load_midi_device_definition_ui.cpp:142 enterKeyPress`). The SD half is not here: it is
/// `browser::Listing` (task 2), which this scene owns one of.
///
/// **What this scene may not do.** Like `MenuScene`, `RenameScene` and `ContextMenuScene` it never
/// calls an `OledFrame` drawing primitive: the title, the row icons, the plain rows and the edited
/// row all come from `ui::oled` widgets (`title`, `icon`, `text`, `text_edit`), so the layout rules
/// stay in one place. It never reads a clock other than the frame's own `now_ms()`.
///
/// @par The listing is asynchronous, and that is the whole shape of this scene
///
/// Legacy completes a listing *inside* the storage-owner op and calls the UI back from there. This
/// shell is tick-driven, so the scene **requests** a listing, renders whatever it has while
/// `Listing::in_flight()` is true, and **collects** the result on a later tick in `after_render()`.
/// Three consequences, all load-bearing:
///
///   1. **The busy indicator is not this scene's.** `Listing::request` raises
///      `notify::Notice::Kind::Working` and completion raises `ClearWorking`; E1's `Notices` turns
///      those into pushing and removing `SceneKind::Working` over this scene. Nothing here draws a
///      spinner.
///   2. **`handle()` swallows every *button and encoder* input while a listing is outstanding**
///      -- the port of the `if (listingInProgress_) return;` guard at the top of `padAction`
///      (`browser.cpp:1187`), `mainButtonAction` (`:1251`), `backButtonAction` (`:1284`) and
///      `selectEncoderAction` (`:550`). This scene is `SceneStack::input_top()`, so nothing
///      beneath it sees those events either.
///
///      **Pad events do not come this way.** They are claimed by the keyboard widget during
///      `render_grid` and consumed in `after_render`, whose three early returns (`has_result`,
///      a parked `pending_` with nothing in flight, and `in_flight`) are exactly the union
///      `seam_busy()` computes. So the gate is these two together, not `handle()` alone --
///      which matters, because removing the input lock in fix round 1 left `after_render`'s
///      returns as the only thing standing between a held pad and a second listing.
///
///      **It deliberately does not take `Action::Kind::LockInput`**, which is what the seam's own
///      `@note` suggests and what this scene did until fix round 1. A scene cannot release that
///      lock from `on_exit()` (which returns `void`) and the lock is one bare global bool with no
///      owner, so an external `Pop` mid-listing left the entire device deaf. It is also the wrong
///      lock: it *drops* events where legacy's `waitingForSDRoutineToEnd` defers and retries. See
///      the warning on `Action` in `shell/scene.h`.
///   3. **A refused dispatch is retried, not lost.** `Listing::request` can answer `Dropped` (the
///      owner's queue was full) or `Busy` (something else holds the catalogue -- another seam, or a
///      legacy `Browser` mid-listing, since the claim now lives on `FileCatalogue`). The scene keeps
///      the request and retries it on the next tick, which is legacy's own answer to the same return
///      ("clear so the next HID event retries", `browser.cpp:113`).
///
/// @warning **The `Listing` is a member of this scene and must stay one.** See the Lifetime section
///          on `browser::Listing`: a dispatched op holds `this`, there is no cancel, and
///          `~Listing` asserts it is not in flight. Scenes are long-lived singletons, which is what
///          makes that satisfiable.

#include "scenes/browser/listing.h"
#include "scenes/menu/context.h"
#include "shell/scene.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"
#include "ui/grid/keyboard.h"
#include "ui/oled/text_edit.h"

#include "gfx/oled/icon.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::scenes::browser {

/// @brief Which browser `SceneId::arg` names.
///
/// **Two, not legacy's nine.** These are every legacy `Browser` subclass a node in the ported menu
/// tree opens. Every other one -- `LoadSongUI`, `LoadInstrumentPresetUI`, `LoadPatternUI`,
/// `SaveSongUI`, `SaveInstrumentPresetUI`, `SaveKitRowUI`, `DxSyxBrowser` -- is opened from a root
/// UI or a view this plan does not port, so adding an entry for one now would be an untested,
/// unreachable code path.
enum class BrowserId : uint16_t {
	/// `sampleBrowser` (`gui/ui/browser/sample_browser.cpp:83`), the audio-file browser the sample
	/// menu items open.
	Sample = 0,
	/// `loadMidiDeviceDefinitionUI` (`gui/ui/load/load_midi_device_definition_ui.cpp:38`), which
	/// `midi::device_definition::Linked::writeCurrentValue` opens
	/// (`gui/menu_item/midi/device_definition/linked.h:42`).
	///
	/// @note **Shipped by task 3, wired by task 7** -- which reused this descriptor unchanged, as
	///       task 3 intended. What task 7 found is that `midi_device_linked` is not a plain browser
	///       opener but a `Toggle`: `linked.h:42` is only the ON arm of `writeCurrentValue`, and the
	///       OFF arm clears the linked file name. `nodes::MidiDeviceLinkedItem`
	///       (`menu/nodes/output_items.h`) carries both, and is one of the two items that set
	///       `CustomItem::on_focus_is_press()` -- because a restore replaying the ON arm would
	///       silently clear a user's setting.
	MidiDeviceDefinition = 1,
	Count,
};

/// @brief What accepting a *file* did, as far as the scene is concerned.
enum class AcceptOutcome : uint8_t {
	/// The descriptor did its work; close the browser. Legacy's `close()` after a successful load.
	Close,
	/// The descriptor refused or failed; stay open. The scene raises `error` as a notice first
	/// (legacy's `display->displayError(error)` followed by `return`).
	Stay,
	/// The descriptor cannot act from this layer: park the choice in `take_choice()` and close.
	///
	/// Exactly one descriptor answers this, and it is not a placeholder -- see
	/// `Descriptor::accept`.
	Defer,
};

/// @brief What one browser is for -- one legacy `Browser` subclass, as data.
struct Descriptor {
	/// @brief The screen title, as legacy's constructor spells it -- a literal, not an l10n id.
	///
	/// `SampleBrowser::SampleBrowser` sets `title = "Audio files"` (`sample_browser.cpp:89`) and
	/// `LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition` sets
	/// `title = "Load midi device"` (`load_midi_device_definition_ui.cpp:87`). Neither is in
	/// `gui/l10n/strings.h` at all, so there is no id to carry: a `uint16_t` here would have meant
	/// inventing two string ids and a translation table entry each, which is a change to the l10n
	/// data rather than a port of the browser.
	std::string_view title;

	/// @brief `Browser::allowedFileExtensions`, NULL-terminated as
	///        `FileCatalogue::readFileItemsForFolder` takes it.
	char const** extensions;

	/// @brief `arrivedInNewFolder`'s `defaultDirToAlsoTry` (`browser.cpp:367`), and where the
	///        browser opens when it has no remembered path.
	std::string_view default_dir;

	/// @brief `Browser::fileIcon` -- the row icon for a non-folder.
	const gfx::oled::Icon* file_icon;

	/// @brief `Browser::fileIconPt2`, or null. Only the MIDI plug has one (its ninth column).
	const gfx::oled::Icon* file_icon_pt2;

	/// @brief `Browser::shouldInterpretNoteNamesForThisBrowser` (`browser.h:290`), threaded through
	///        the listing rather than read off a browser -- see `FileCatalogue`'s own note.
	bool interpret_note_names;

	/// @brief `Browser::allowFoldersSharingNameWithFile`. `SampleBrowser::opened` sets it true
	///        (`sample_browser.cpp:112`); the base leaves it false (`browser.cpp:182`).
	bool allow_folders_sharing_name_with_file;

	/// @brief May the browser open on this context at all?
	///
	/// The survivable spelling of what each legacy `opened()` does instead: `SampleBrowser` walks
	/// straight into `soundEditor.getCurrentAudioFileHolder()->filePath` (`sample_browser.cpp:131`)
	/// with no null test, and `LoadMidiDeviceDefinitionUI::opened` returns false outright when the
	/// output is not a MIDI one (`load_midi_device_definition_ui.cpp:45`). Refusing to open is what
	/// `RenameScene` already does with `Target::can`, for the same reason.
	bool (*can)(const menu::MenuContext&);

	/// @brief Where the browser opens: the directory to list, and the entry to land on inside it.
	///
	/// The port of each subclass's own dissection -- `SampleBrowser::opened`'s
	/// `currentPath`/`searchFilename` split (`sample_browser.cpp:135-170`) and
	/// `LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition` (`:83-124`). Both end in
	/// "a folder plus a filename to select", which is exactly what a `ListingRequest` takes.
	///
	/// @param ctx  What the browser was opened over.
	/// @param dir  Out: the directory to list. Left empty to use `Descriptor::default_dir`.
	/// @param name Out: the filename to select on arrival, or empty for "the first entry".
	void (*start_at)(const menu::MenuContext& ctx, std::string& dir, std::string& name);

	/// @brief Why this folder may not be entered, as an l10n string id; 0 to allow it.
	///
	/// One legacy refusal, and it is `SampleBrowser`'s: `TEMP` inside `SAMPLES/CLIPS`
	/// (`sample_browser.cpp:345-350`, `STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED`). Null means "every
	/// folder may be entered", which is what `LoadMidiDeviceDefinitionUI::enterKeyPress` does
	/// (`load_midi_device_definition_ui.cpp:150-154`).
	uint16_t (*folder_refusal)(std::string_view dir, std::string_view name);

	/// @brief What accepting a *file* does. Legacy's `enterKeyPress`, minus the folder arm.
	///
	/// @note **`BrowserId::Sample` answers `Defer`, and that is a finding rather than a stub.**
	///       `SampleBrowser`'s file arm is `claimCurrentFile()` (`sample_browser.cpp:398`, itself
	///       dispatched onto the storage owner), which reaches `soundEditor`, `AudioEngine`,
	///       `slicer`, `PadLEDs` and `instrumentClipView` -- five `gui/` and `hid/` symbols this
	///       layer may not name (`scripts/check_layer_rules.py`, `scenes-includes-outside`). That
	///       is the same wall `scenes/menu/action.h` was built for, and the same answer: the scene
	///       parks the chosen path in `take_choice()` and the wiring plan's dispatcher performs the
	///       load against the legacy object. `BrowserId::MidiDeviceDefinition` needs no such thing
	///       -- `StorageManager::loadMidiDeviceDefinitionFile` is in `storage/`, which this layer
	///       may include, so its accept hook really loads the file.
	///
	/// @note **The `Defer` dispatcher owes one more thing than `claimCurrentFile()`, and this is the
	///       only place it is written down.** Legacy pairs the browser with a *return* affordance:
	///       `SoundEditor::maybeGetParentMenu` (`gui/ui/sound_editor.cpp:2018 maybeGetParentMenu`,
	///       file-selector arm at `:2034-2039`) writes `sampleBrowser.menuItemHeadingTo` and
	///       `parentMenuHeadingTo` on the way *in*, and `claimCurrentFile` reads them back
	///       (`gui/ui/browser/sample_browser.cpp:1075-1086`) to re-root
	///       `soundEditor.menuItemNavigationRecord` onto the oscillator's horizontal page with the
	///       file selector focused. So accepting a file lands the user on the source page, not back
	///       on the item that opened the browser.
	///
	///       `nodes::FileBrowserItem` deliberately does **not** port it (`nodes/sample.cpp`): both
	///       members live on a `gui/` object this layer may not name, and the port's equivalent of
	///       `menuItemNavigationRecord` is `MenuState::path`/`depth`, which only `MenuScene` may
	///       write -- an item writing the menu's own navigation state is what `MenuState`'s mailbox
	///       contract (`scenes/menu/menu_scene.h`) exists to prevent. Whoever performs
	///       `claimCurrentFile` against `take_choice()` is therefore the owner of the re-rooting
	///       too, and will need a `MenuScene` entry point for it. Until then the cost is one extra
	///       BACK press after a file is accepted. `osc::AudioRecorder` carries the identical pair
	///       (`gui/ui/sound_editor.cpp:2042-2047`, consumed by `osc/audio_recorder.h:40-48`) with
	///       the same disposal, recorded on `nodes::AudioRecorderItem::on_focus`.
	///
	/// @param ctx   What the browser was opened over.
	/// @param path  The chosen file's full path (`currentDir` + "/" + the filename).
	/// @param error Out: what to announce when the answer is `Stay`.
	/// @return What the scene should do next.
	AcceptOutcome (*accept)(const menu::MenuContext& ctx, std::string_view path, Error& error);
};

/// @brief The table. Indexed by `BrowserId`; `BrowserId::Count` or past it returns entry 0.
/// @param id Which browser.
/// @return Its title, extensions, icons and hooks.
const Descriptor& descriptor(BrowserId id);

/// @brief A file the scene could not load itself, waiting for the application-side dispatcher.
///
/// The noun half of the same seam `menu::MenuActionRequest` is the verb half of. See
/// `Descriptor::accept`.
struct BrowserChoice {
	BrowserId id = BrowserId::Sample;
	/// The chosen file's full path, as `getCurrentFilePath()` would have spelled it
	/// (`sample_browser.h:125 getCurrentFilePath`): `currentDir` + "/" + the filename, extension included.
	std::string path;
};

/// @brief The longest name or path this scene will build.
///
/// The edited text is a fixed buffer, so the string this scene draws every frame is never a heap
/// object. 128 covers a FAT long name (255 is the filesystem ceiling, but nothing in a shipped card
/// comes close) with room for the extension the search string carries; past it a keypress simply
/// types nothing.
///
/// @note **This scene is not allocation-free, unlike `RenameScene` and `ContextMenuScene`, and the
///       buffer should not be read as claiming otherwise** (fix round 1, Minor). It sits on
///       `browser::Listing`, whose `ListingRequest` carries three owning `std::string`s by
///       construction (`listing.h`: on Embassy the caller's buffers are gone by the time the op
///       runs), and over `FileCatalogue`, whose `currentDir` and `FileItem::filename` are
///       `std::string`s. `step_selection`, `request_reload` and `search_for_entered_text` each
///       build a NUL-terminated copy of the edited text for `searchFileItems`, which takes a
///       `char const*`. What the fixed buffer does buy is that the *render* path allocates nothing.
inline constexpr size_t kMaxEnteredChars = 128;

/// @brief `SceneKind` this scene is registered under: `FirstApp + 3` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kBrowserSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 3);

/// @brief `Browser::renderOLED`'s two x origins, on the shipped `invert_highlight` profile.
///
/// Legacy: `textStartX = 14`, `iconStartX = 1`, each stepped by one `kTextSpacingX` when
/// `FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION`
/// (`browser.cpp:928-933`). That setting is what this toolkit spells as `Style::invert_highlight`
/// -- the same mapping `list`, `columns`, `popup` and `text_edit` already make -- so the scene
/// reads the profile rather than the flash byte, and both shipped profiles set it true.
/// @{
inline constexpr int32_t kRowTextX = 14;
inline constexpr int32_t kRowIconX = 1;
/// @}

/// @brief How often the scene asks to be woken while a listing is outstanding.
///
/// **This is what keeps the seam from jamming, and it is not decoration.** `Listing` must be polled
/// every tick (`listing.h`'s `@warning`: an uncollected `Ready` refuses every later request), and
/// the only hook that can file an `Action` is `after_render()` -- which the shell calls only for
/// scenes that actually *drew* this tick (`shell/shell.cpp:360-364`). With no input arriving and
/// nothing else asking for a frame, the browser would neither render nor poll, and the listing would
/// land into silence. So while a request is in flight or waiting to be retried, `next_timer()` asks
/// the shell to come back at this interval, and `Shell::tick`'s step 8b turns that answer into the
/// repaint that makes the woken tick draw. `render_oled` arms the panel's own gate at the same
/// interval as well; that arm predates the step-8b fold (before which `next_timer()` alone was a
/// listing that landed into silence) and is kept as the statement a reader of this scene can see.
///
/// 10 ms is one tick of the shipped UI loop, i.e. "next tick", spelled as a duration because that is
/// what both APIs take.
inline constexpr uint32_t kListingPollMs = 10;

/// @brief The file browser. One instance for the whole application (`registry.h`).
class BrowserScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at what @p ctx resolved.
	///
	/// Split from `on_enter` for the reason `RenameScene::open` and `ContextMenuScene::open` are:
	/// a `SceneId` carries one `uint16_t`, and a context is not one.
	/// @param ctx What the descriptor's hooks read and write.
	void open(const menu::MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override { return kBrowserSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;

	/// @brief True: the keyboard is painted over whatever grid is beneath, and the sidebar columns
	///        it does not own keep showing the view underneath -- what `QwertyUI::drawKeys`'s
	///        `clearMainPadsWithoutSending` (main pads only) leaves.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: the base view still paints its sidebar, and that is what shows through.
	bool occludes_grid() const override { return false; }
	/// @brief False: legacy greys through `getGreyoutColsAndRows`, it never halves every pad.
	bool dims_grid() const override { return false; }
	/// @brief False: `Browser::renderOLED` starts with `drawScreenTitle` and fills the rows below
	///        it, so nothing of the UI underneath is meant to show. It is a full screen, not an
	///        overlay -- unlike `RenameScene` and `ContextMenuScene`, neither of whose legacy
	///        classes sets `oledShowsUIUnderneath`.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	/// @brief `*cols = 0xFFFFFFFF` on both: `SampleBrowser::getGreyoutColsAndRows`
	///        (`sample_browser.cpp:516 getGreyoutColsAndRows`) and
	///        `LoadMidiDeviceDefinitionUI::getGreyoutColsAndRows`
	///        (`load_midi_device_definition_ui.cpp:40 getGreyoutColsAndRows`).
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return {0xFFFFFFFFu, 0}; }

	/// @name Observable state, for specs and for whoever wires this up.
	/// @{
	/// @brief Which descriptor the last `on_enter` selected.
	BrowserId browser_id() const { return id_; }
	/// @brief `QwertyUI::enteredText` -- the name shown on the selected row.
	std::string_view entered_text() const { return std::string_view{entered_, entered_len_}; }
	/// @brief `Browser::fileIndexSelected`; -1 while there is no real selection.
	int32_t file_index() const { return file_index_; }
	/// @brief `Browser::scrollPosVertical` -- the top visible row.
	int32_t scroll() const { return scroll_; }
	/// @brief `QwertyUI::enteredTextEditPos` -- the insertion point, and the left edge of
	///        `text_edit`'s prediction highlight.
	int32_t cursor() const { return edit_.cursor; }
	/// @brief `QwertyUI::scrollPosHorizontal` -- the first character `text_edit` draws.
	int32_t text_scroll() const { return edit_.scroll; }
	/// @brief `FileCatalogue::currentDir` as this scene last saw it.
	std::string_view current_dir() const;
	/// @brief Is a listing dispatched and not yet collected?
	bool listing_in_flight() const { return listing_.in_flight(); }
	/// @}

	/// @brief Collect the file the scene could not load itself, exactly once.
	///
	/// The outbox `Descriptor::accept`'s `Defer` answer fills in; `MenuScene::take_action()` is the
	/// same shape for the verb half of the same wall. **Nothing in this repository consumes it** --
	/// that is the wiring plan's, and `Descriptor::accept` says why.
	/// @return The choice, or `std::nullopt` when none is waiting.
	std::optional<BrowserChoice> take_choice();

	/// @brief Install a dispatcher other than `storage::Owner::run` on the scene's seam.
	///
	/// **For specs.** On the host `Owner::run` is inline (`storage/owner.h:55-59`), so with the
	/// shipped dispatcher a listing has already finished when `request()` returns and the in-flight
	/// state -- which is most of this scene's behaviour -- can never be observed. See the honesty
	/// note in `listing.h`.
	/// @param dispatch The dispatcher; must satisfy `Listing::Dispatch`'s contract.
	void set_dispatch(Listing::Dispatch dispatch) { listing_.set_dispatch(dispatch); }

private:
	/// @brief What the in-memory search over the cached window could say.
	enum class SearchOutcome : uint8_t {
		/// It named an entry, which is now selected.
		Found,
		/// Nothing in the folder starts with the typed text (legacy's `notFound`, `browser.cpp:841`).
		NotFound,
		/// The cached window cannot answer -- the folder has to be read again around the search
		/// string. Legacy loops synchronously (`doNewRead`, `:826`); this scene dispatches.
		NeedsRead,
	};

	/// @brief Build and file the request that opens the browser (`ListingAction::Open`).
	void request_open();
	/// @brief Fill in the fields every request of this scene's shares (extensions, flags, default
	///        directory).
	/// @param action Which listing.
	/// @return The request, ready for `file_request`.
	ListingRequest make_request(ListingAction action) const;
	/// @brief Try to dispatch @p req now; keep it for the next tick if the seam refuses.
	/// @param req The request.
	void file_request(ListingRequest req);
	/// @brief Apply a landed listing: selection, entered text, scroll.
	/// @param out The collected result.
	/// @return What the shell should do about it.
	deluge::ui::shell::Result apply(const ListingResult& out);

	/// @brief `Browser::clampFileSelectionAndScroll` (`browser.cpp:1009`).
	/// @param visible_rows How many rows the panel shows (legacy's
	///                     `display->getNumBrowserAndMenuLines()`).
	/// @param allow_no_file_selection Legacy's parameter of the same name.
	void clamp(int32_t visible_rows, bool allow_no_file_selection = true);
	/// @brief `Browser::setEnteredTextFromCurrentFilename` (`browser.cpp:1316`): the selected
	///        entry's name with its extension cut off.
	void set_entered_text_from_current_filename();
	/// @brief Replace the edited text with @p s, truncated at `kMaxEnteredChars`.
	/// @param s The new text.
	void set_entered_text(std::string_view s);

	/// @brief `QwertyUI::padAction`'s "normal keys" arm followed by
	///        `Browser::predictExtendedText` (`browser.cpp:782`) -- type one character, then find
	///        the entry it names.
	/// @param c The character typed.
	void type_char(char c);
	/// @brief `QwertyUI::processBackspace` (`gui/ui/qwerty_ui.cpp:408-418`), then the same search.
	void backspace();
	/// @brief The in-memory half of `predictExtendedText`: search the cached window for the edited
	///        text and select what it names.
	/// @return What the cached window could say.
	SearchOutcome search_for_entered_text();
	/// @brief `search_for_entered_text` plus the two arms its caller owes -- put the old name back
	///        on a miss, dispatch a wider read on `NeedsRead`.
	/// @param revert_cursor_to Where the cursor was before the keystroke, for legacy's
	///                         `enteredTextEditPos--` on a failed prediction (`qwerty_ui.cpp:357`).
	void run_search_after_typing(int32_t revert_cursor_to);
	/// @brief `Browser::selectEncoderAction` (`browser.cpp:549`), including its four reload arms.
	/// @param offset Signed detents.
	void step_selection(int32_t offset);
	/// @brief `Browser::finishSelectEncoderAction` (`browser.cpp:702`): apply a new selection.
	/// @param new_index The index to select.
	/// @param offset    The turn that produced it.
	void finish_select(int32_t new_index, int32_t offset);
	/// @brief File one of `selectEncoderAction`'s four folder-window re-reads.
	/// @param from_end   Legacy's `pendingReloadSearchFromEnd_` (`browser.h:178`).
	/// @param direction  Legacy's `pendingReloadCatalogSearchDirection_` (`:177`); read only when
	///                   @p from_end.
	/// @param offset     The turn that provoked it, applied to the search on landing.
	void request_reload(bool from_end, int32_t direction, int32_t offset);
	/// @brief `Browser::mainButtonAction`'s release arm -> `enterKeyPress`.
	/// @return What the shell should do.
	deluge::ui::shell::Result enter_key_press();
	/// @brief `Browser::backButtonAction` (`browser.cpp:1283`): ascend, or close at the top.
	/// @return What the shell should do.
	deluge::ui::shell::Result back();
	/// @brief `Browser::close()`, as an `Action`.
	static deluge::ui::shell::Result close();
	/// @brief Is a listing outstanding in any sense -- running, owing a collect, or waiting to be
	///        re-filed?
	///
	/// All three, and the third is the one that bites (fix round 1, Critical 2). `pending_` is a
	/// single slot, so an input that files a second listing over a parked one **loses** the first:
	/// a BACK press would replace a parked `Open` with an `UpLevel`, and the browser would then
	/// list a directory derived from whatever `currentDir` the *foreign* holder of the catalogue
	/// claim left behind. And `pending_` is created in exactly the case the other two miss -- a
	/// `Busy` refusal, where nothing is in flight and no result is owed. `after_render` already got
	/// this right (its retry step returns before the keyboard arm); this is the post-render
	/// `handle()` path, which did not.
	/// @return True when nothing outside `after_render` may touch `FileCatalogue` or file a
	///         listing.
	bool seam_busy() const { return listing_.in_flight() || listing_.has_result() || pending_.has_value(); }

	menu::MenuContext ctx_{};
	BrowserId id_ = BrowserId::Sample;

	/// The SD half. **A member, and the lifetime rule says it must stay one** -- see `listing.h`.
	Listing listing_{};
	/// A request the seam refused (`Busy`/`Dropped`), retried on the next `after_render`.
	std::optional<ListingRequest> pending_{};
	/// The entry to select once the pending listing lands -- legacy's `filenameToStartAt`, kept on
	/// this side because `ListingAction::Open` performs no selection search of its own
	/// (`listing.h`: the seam carries no selection state).
	std::string select_after_listing_{};
	/// Legacy's `pendingReloadSearchFromEnd_` / `pendingReloadCatalogSearchDirection_` pair
	/// (`browser.h:177-178`) and the turn that provoked the reload.
	/// @{
	bool reload_from_end_ = false;
	int32_t reload_direction_ = 0;
	int32_t reload_offset_ = 0;
	/// @}
	/// True while a listing was dispatched to widen the search window: `search_for_entered_text`
	/// runs again when it lands. Legacy's `doneNewRead` (`browser.cpp:799`), one tick later.
	bool searching_ = false;
	/// Where the cursor goes if that second search also finds nothing -- legacy's
	/// `enteredTextEditPos--` on a false `predictExtendedText()` (`qwerty_ui.cpp:357-359`), which
	/// has to survive the tick boundary the re-read introduces. Only read while `searching_`.
	int32_t search_revert_cursor_ = 0;

	char entered_[kMaxEnteredChars] = {};
	uint16_t entered_len_ = 0;

	/// `Browser::fileIndexSelected` (`browser.h:268`) / `scrollPosVertical` (`:270`).
	/// @{
	int32_t file_index_ = -1;
	int32_t scroll_ = 0;
	/// @}

	std::optional<BrowserChoice> choice_{};

	deluge::ui::oled::TextEditState edit_{};
	deluge::ui::grid::TextKeyboardState kb_{};
	deluge::ui::grid::KeyboardLayoutKind layout_ = deluge::ui::grid::KeyboardLayoutKind::Qwerty;

	/// The end-of-string cursor's blink -- the same idiom `RenameScene` uses.
	deluge::ui::Blink cursor_blink_{};
	uint32_t last_render_ms_ = 0;
	/// How many rows the last OLED pass had room for, so `clamp` outside a render can use the
	/// panel's real answer rather than a guess. Seeded to the shipped 48-row panel's 3, which is
	/// also what legacy's `getNumBrowserAndMenuLines()` returns on every panel.
	int32_t visible_rows_ = 3;

	/// What this tick's `render_grid` observed, for `after_render` to act on -- the same
	/// render-observes / after_render-acts split `MenuScene` and `RenameScene` use.
	deluge::ui::Response resp_{};
	/// Did `render_grid` run this tick? `RenameScene::grid_pass_`'s reason, verbatim: the shell
	/// calls `after_render()` for every scene that drew on *any* surface, so an OLED-only tick
	/// arrives here with `resp_` default-constructed.
	bool grid_pass_ = false;
};

/// @brief The application's one file browser.
///
/// A function-local static, for the reason `menu_scene()`, `rename_scene()` and
/// `context_menu_scene()` are: it must outlive every `Shell` that borrows it and must not
/// participate in static-initialisation order across translation units.
/// @return The singleton.
BrowserScene& browser_scene();

} // namespace deluge::scenes::browser
