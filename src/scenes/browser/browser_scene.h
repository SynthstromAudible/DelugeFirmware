#pragma once

/// @brief `BrowserScene` -- one file browser, parameterised by descriptor, over the extracted
///        catalogue and the async listing seam.
///
/// Legacy has a `Browser` base (`gui/ui/browser/browser.{h,cpp}`) over `QwertyUI` and a family of
/// subclasses. **Three of them are reachable from the ported menu tree** and are the three descriptors
/// below: `SampleBrowser` (`gui/ui/browser/sample_browser.cpp`), opened by the sample-file items,
/// `LoadMidiDeviceDefinitionUI` (`gui/ui/load/load_midi_device_definition_ui.cpp`), opened by
/// `midi_device_linked`, and `DxSyxBrowser` (`gui/ui/browser/dx_browser.cpp`), opened by "load DX7 .syx file".
/// Everything the three differ in -- title, extensions, where they start, which
/// icon a row draws, what accepting a file does -- is `Descriptor`, and `SceneId::arg` selects one,
/// exactly as `rename::Target` and `context_menu::Descriptor` turned their own legacy hierarchies
/// into tables.
///
/// The **load/save family** (`gui/ui/load/load_song_ui`, `load_instrument_preset_ui`,
/// `load_pattern_ui`, and all of `gui/ui/save/`) is deliberately **out of scope**: those are opened
/// from root UIs rather than from menu items, and they carry a second axis this scene has none of
/// (`SlotBrowser`'s brand-new-name machinery, `mayDefaultToBrandNewNameOnEntry`,
/// `getUnusedSlot`). They are not ported; they need their own descriptors or their own scene.
///
/// **Ports.** `Browser::renderOLED`, `Browser::clampFileSelectionAndScroll`,
/// `Browser::setEnteredTextFromCurrentFilename`, `Browser::backButtonAction`,
/// `Browser::mainButtonAction`, `Browser::selectEncoderAction`'s in-window arm and its four
/// reload arms, `Browser::predictExtendedText`'s in-memory search, and the enter-key bodies of
/// both subclasses (`SampleBrowser::enterKeyPress`,
/// `LoadMidiDeviceDefinitionUI::enterKeyPress`). The SD half is not here: it is
/// `browser::Listing`, which this scene owns one of.
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
///      `notify::Notice::Kind::Working` and completion raises `ClearWorking`; `Notices` turns
///      those into pushing and removing `SceneKind::Working` over this scene. Nothing here draws a
///      spinner.
///   2. **`handle()` swallows every *button and encoder* input while a listing is outstanding**
///      -- the port of the `if (listingInProgress_) return;` guard at the top of
///      `Browser::padAction`, `Browser::mainButtonAction`, `Browser::backButtonAction` and
///      `Browser::selectEncoderAction`. This scene is `SceneStack::input_top()`, so nothing
///      beneath it sees those events either.
///
///      **Pad events do not come this way.** They are claimed by the keyboard widget during
///      `render_grid` and consumed in `after_render`, whose three early returns (`has_result`,
///      a parked `pending_` with nothing in flight, and `in_flight`) are exactly the union
///      `seam_busy()` computes. So the gate is these two together, not `handle()` alone --
///      which matters, because with no input lock `after_render`'s
///      returns are the only thing standing between a held pad and a second listing.
///
///      **It deliberately does not take `Action::Kind::LockInput`**, which is what the seam's own
///      `@note` suggests. A scene cannot release that
///      lock from `on_exit()` (which returns `void`) and the lock is one bare global bool with no
///      owner, so an external `Pop` mid-listing left the entire device deaf. It is also the wrong
///      lock: it *drops* events where legacy's `waitingForSDRoutineToEnd` defers and retries. See
///      the warning on `Action` in `shell/scene.h`.
///   3. **A refused dispatch is retried, not lost.** `Listing::request` can answer `Dropped` (the
///      owner's queue was full) or `Busy` (something else holds the catalogue -- another seam, or a
///      legacy `Browser` mid-listing, since the claim now lives on `FileCatalogue`). The scene keeps
///      the request and retries it on the next tick, which is legacy's own answer to the same return
///      (`Browser`'s `listingInProgress_` is cleared so the next HID event retries).
///
/// @warning **The `Listing` is a member of this scene and must stay one.** See the Lifetime section
///          on `browser::Listing`: a dispatched op holds `this`, there is no cancel, and
///          `~Listing` asserts it is not in flight. Scenes are long-lived singletons, which is what
///          makes that satisfiable.

#include "scenes/browser/listing.h"
#include "scenes/menu/context.h"
#include "scenes/menu/shortcut_blink.h"
#include "scenes/services/favourites.h"
#include "shell/scene.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"
#include "ui/grid/keyboard.h"
#include "ui/oled/scroll_text.h"
#include "ui/oled/text_edit.h"

#include "gfx/oled/icon.h"
#include "model/sample/waveform_navigator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::scenes::browser {

/// @brief Which browser `SceneId::arg` names.
///
/// **Three, not legacy's nine.** These are every legacy `Browser` subclass a node in the ported menu
/// tree opens. Every other one -- `LoadSongUI`, `LoadInstrumentPresetUI`, `LoadPatternUI`,
/// `SaveSongUI`, `SaveInstrumentPresetUI`, `SaveKitRowUI` -- is opened from a root
/// UI or a view the port does not have, so adding an entry for one now would be an untested,
/// unreachable code path.
enum class BrowserId : uint16_t {
	/// `sampleBrowser` (`gui/ui/browser/sample_browser.cpp:84`), the audio-file browser the sample
	/// menu items open.
	Sample = 0,
	/// `loadMidiDeviceDefinitionUI` (`gui/ui/load/load_midi_device_definition_ui.cpp`), which
	/// `midi::device_definition::Linked::writeCurrentValue` opens
	/// (`gui/menu_item/midi/device_definition/linked.h:42`).
	///
	/// @note `midi_device_linked` is not a plain browser
	///       opener but a `Toggle`: `linked.h:42` is only the ON arm of `writeCurrentValue`, and the
	///       OFF arm clears the linked file name. `nodes::MidiDeviceLinkedItem`
	///       (`menu/nodes/output_items.h`) carries both, and is one of the two items that set
	///       `CustomItem::on_focus_is_press()` -- because a restore replaying the ON arm would
	///       silently clear a user's setting.
	MidiDeviceDefinition = 1,
	/// `dxBrowser` (`gui/ui/browser/dx_browser.cpp`), the DX7 `.syx` bank browser "load DX7 .syx file" opens.
	DxSyx = 2,
	Count,
};

/// @brief What accepting a *file* did, as far as the scene is concerned.
enum class AcceptOutcome : uint8_t {
	/// The descriptor did its work; close the browser. Legacy's `close()` after a successful load.
	Close,
	/// The descriptor refused or failed; stay open. The scene raises `error` as a notice first
	/// (legacy's `display->displayError(error)` followed by `return`).
	Stay,
	/// The descriptor cannot act from this layer and its load has to end before the browser may close: park the choice
	/// in `take_choice()` and stay up, taking no other file, until the host calls `BrowserScene::deferred_load_done`.
	/// Each legacy browser loads with itself still up and closes once the load has ended (`claimCurrentFile`,
	/// `LoadMidiDeviceDefinitionUI::definitionLoaded`, `DxSyxBrowser`'s read), and stays up when it fails.
	DeferAndWait,
};

/// @brief What one browser is for -- one legacy `Browser` subclass, as data.
struct Descriptor {
	/// @brief The screen title, as legacy's constructor spells it -- a literal, not an l10n id.
	///
	/// `SampleBrowser::SampleBrowser` sets `title = "Audio files"` and
	/// `LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition` sets
	/// `title = "Load midi device"` (`load_midi_device_definition_ui.cpp:87`). Neither is in
	/// `gui/l10n/strings.h` at all, so there is no id to carry: a `uint16_t` here would have meant
	/// inventing two string ids and a translation table entry each, which is a change to the l10n
	/// data rather than a port of the browser.
	std::string_view title;

	/// @brief `Browser::allowedFileExtensions`, NULL-terminated as
	///        `FileCatalogue::readFileItemsForFolder` takes it.
	char const** extensions;

	/// @brief `arrivedInNewFolder`'s `defaultDirToAlsoTry`, and where the
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
	///        (`sample_browser.cpp:113`); the base leaves it false (`browser.cpp:182`).
	bool allow_folders_sharing_name_with_file;

	/// @brief May the browser open on this context at all?
	///
	/// The survivable spelling of what each legacy `opened()` does instead: `SampleBrowser` walks
	/// straight into `soundEditor.getCurrentAudioFileHolder()->filePath` (in `SampleBrowser::opened`)
	/// with no null test, and `LoadMidiDeviceDefinitionUI::opened` returns false outright when the
	/// output is not a MIDI one. Refusing to open is what
	/// `RenameScene` already does with `Target::can`, for the same reason.
	bool (*can)(const menu::MenuContext&);

	/// @brief Where the browser opens: the directory to list, and the entry to land on inside it.
	///
	/// The port of each subclass's own dissection -- `SampleBrowser::opened`'s
	/// `currentPath`/`searchFilename` split (`sample_browser.cpp:136-171`) and
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
	/// (`sample_browser.cpp:346-351`, `STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED`). Null means "every
	/// folder may be entered", which is what `LoadMidiDeviceDefinitionUI::enterKeyPress` does
	/// (`load_midi_device_definition_ui.cpp:150-154`).
	uint16_t (*folder_refusal)(std::string_view dir, std::string_view name);

	/// @brief What accepting a *file* does. Legacy's `enterKeyPress`, minus the folder arm.
	///
	/// @note **Every file descriptor answers `DeferAndWait`, and that is a finding rather than a stub.** (The `.syx`
	///       one's read goes through `dxCartridge`, for the same reason as the two below.)
	///       `SampleBrowser`'s file arm requests a sample claim (`gui/ui/browser/sample_claim.h`),
	///       which reaches `soundEditor`, `AudioEngine`, `slicer`, `PadLEDs` and
	///       `instrumentClipView`; `LoadMidiDeviceDefinitionUI`'s requests the instrument load
	///       (`gui/ui/load/instrument_load.h`). Both are `gui/` symbols this layer may not name
	///       (`scripts/check_layer_rules.py`, `scenes-includes-outside`). That is the same wall
	///       `scenes/menu/action.h` was built for, and the same answer: the scene parks the chosen
	///       path in `take_choice()` and `gui::ui::dispatchShellChoices` performs the load.
	///
	/// @note **The sample dispatcher owes one more thing than claiming the file.** Legacy pairs the browser with a
	/// *return* affordance:
	///       `SoundEditor::maybeGetParentMenu` (`gui/ui/sound_editor.cpp`, its
	///       file-selector arm) writes `sampleBrowser.menuItemHeadingTo` and
	///       `parentMenuHeadingTo` on the way *in*, and the claim's last step reads them back
	///       (`SampleClaim::stepFinish`; `SampleBrowser::claimCurrentFile` on `main`) to re-root
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
	///       too. That is `deluge::gui::ui::shellSampleClaimDone` (`gui/ui/shell_choice_dispatch.h`),
	///       and the entry point it reaches is `MenuScene::reroot_to_scene_opener_page()` -- so
	///       the browser needs no extra BACK press.
	///       **`osc::AudioRecorder` carries the identical pair and is still unpaid**
	///       (`gui/ui/sound_editor.cpp:2095-2100`, consumed by `osc/audio_recorder.h:40-48`): the
	///       recorder scene has no `take_choice()` equivalent to drain, so nothing calls the entry
	///       point for it. Recorded on `nodes::AudioRecorderItem::on_focus`.
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

/// @brief What the sample browser's main pads show: `SampleBrowser`'s `qwertyVisible` and
/// `currentlyShowingSamplePreview`.
enum class BrowserPads : uint8_t {
	Root,     ///< Nothing of its own: the root view shows through, greyed, and the BROWSE pad blinks.
	Waveform, ///< The previewed sample's waveform.
	Keyboard, ///< The text keyboard.
};

/// @brief What the sample browser asks the host to preview: the file the selection landed on, or nothing.
///
/// Filed wherever legacy's `SampleBrowser` calls `previewIfPossible`: a folder's listing landing
/// (`folderContentsReady`, with the way in: 0 on opening, 1 into a folder, -1 up one), a move (`currentFileChanged`,
/// the move's sign) and a typed search that lands on another file (0); and on leaving. The host previews it with the
/// shared previewer and answers through `BrowserScene::preview_settled`.
struct PreviewRequest {
	BrowserId id = BrowserId::Sample;
	/// The file to preview, from the card's root; empty to stop previewing (a folder, no selection, or leaving).
	std::string path;
	/// The direction the selection moved, for the scroll-in: -1, 0 or 1.
	int8_t direction = 0;
	bool operator==(const PreviewRequest&) const = default;
};

/// @brief What the sample browser asks the host to scroll onto the main pads, where `SampleBrowser` calls
///        `PadLEDs::horizontal::setupScroll`: legacy's own animation runs it, from the host.
struct ScrollRequest {
	/// The selection's move: -1 or 1.
	int8_t direction = 0;
	/// Scroll the root view in (`clearPreviewDisplay`); otherwise the browser's own main pads.
	bool to_root = false;
	/// Scroll what is shown out into nothing (`currentFileChanged`, while the next preview loads).
	bool into_nothing = false;
	bool operator==(const ScrollRequest&) const = default;
};

/// @brief A file the scene could not load itself, waiting for the application-side dispatcher.
///
/// The noun half of the same seam `menu::MenuActionRequest` is the verb half of. See
/// `Descriptor::accept`.
struct BrowserChoice {
	/// @brief What the host is to do with the item.
	enum class Op : uint8_t {
		Enter,              ///< `enterKeyPress`'s file arm: load the file as the browser's Enter does.
		Claim,              ///< A sample browser folder menu's BASIC, SINGLE-CYCLE or WAVETABLE: load with `claim`.
		ImportKit,          ///< The kit menu's LOAD ALL (`FolderImportKind::Kit`).
		ImportMultisamples, ///< The synth menu's MULTISAMPLES (`FolderImportKind::Multisamples`).
		Slice,              ///< The kit menu's SLICE, or Shift+Select in a fresh kit: open `SlicerScene` on the file.
		DeleteFile, ///< The delete-file menu's OK: delete the file from the card (`BrowserScene::file_deleted`).
	};
	/// @brief `ClaimRequest`'s three detection choices: 0 no, 1 auto, 2 definitely.
	struct ClaimArgs {
		int32_t pitch = 0;
		int32_t single_cycle = 0;
		int32_t wavetable = 0;
		bool operator==(const ClaimArgs&) const = default;
	};

	BrowserId id = BrowserId::Sample;
	/// The chosen file's full path, as `getCurrentFilePath()` would have spelled it
	/// (`sample_browser.h:163 getCurrentFilePath`): `currentDir` + "/" + the filename, extension included.
	std::string path;
	Op op = Op::Enter;
	ClaimArgs claim{};
	/// The item's folder, its filename, and whether the item is itself a folder: what a folder import starts from.
	std::string dir;
	std::string filename;
	bool is_folder = false;
	/// The auto-load claim of a landed preview (`ClaimOrigin::AutoLoad`): the browser stays open and the menu where
	/// it is. Only an `Op::Enter` carries it.
	bool keep_open = false;
};

/// @brief The longest name or path this scene will build.
///
/// The edited text is a fixed buffer, so the string this scene draws every frame is never a heap
/// object. 128 covers a FAT long name (255 is the filesystem ceiling, but nothing in a shipped card
/// comes close) with room for the extension the search string carries; past it a keypress simply
/// types nothing.
///
/// @note **This scene is not allocation-free, unlike `RenameScene` and `ContextMenuScene`, and the
///       buffer should not be read as claiming otherwise** It sits on
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

/// @brief `Browser::renderOLED`'s two x origins, on the shipped `PartialInversion` profile.
///
/// Legacy: `textStartX = 14`, `iconStartX = 1`, each stepped by one `kTextSpacingX` when
/// `FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION`
/// (`browser.cpp:928-933`). That setting is what this toolkit spells as `Style::highlighting ==
/// Highlighting::NoInversion` -- the same mapping `list`, `columns`, `popup` and `text_edit`
/// already make -- so the scene reads the profile rather than the flash byte, and both shipped
/// profiles ship `PartialInversion`, so this stays on the `!= NoInversion` arm.
/// @{
inline constexpr int32_t kRowTextX = 14;
inline constexpr int32_t kRowIconX = 1;
/// @}

/// @brief How often the scene asks to be woken while a listing is outstanding.
///
/// **This is what keeps the seam from jamming, and it is not decoration.** `Listing` must be polled
/// every tick (`listing.h`'s `@warning`: an uncollected `Ready` refuses every later request), and
/// the only hook that can file an `Action` is `after_render()` -- which the shell calls only for
/// scenes that actually *drew* this tick (`Shell::tick`'s `after_render` loop). With no input arriving and
/// nothing else asking for a frame, the browser would neither render nor poll, and the listing would
/// land into silence. So while a request is in flight or waiting to be retried, `next_timer()` asks
/// the shell to come back at this interval, and `Shell::tick`'s step 8b turns that answer into the
/// repaint that makes the woken tick draw. `render_oled` arms the panel's own gate at the same
/// interval as well, so a reader of this scene sees the poll where the listing is drawn.
///
/// 10 ms is one tick of the shipped UI loop, i.e. "next tick", spelled as a duration because that is
/// what both APIs take.
inline constexpr uint32_t kListingPollMs = 10;

/// @brief How long a preview's incomplete render keeps being redrawn with no column newly resolved:
///        `SampleBrowser::tickPreviewRedraw`'s `kMaxUnproductiveUs` (300 ms, `model/sample/recorder_write_policy.h`).
inline constexpr uint32_t kWaveformRedrawGiveUpMs = 300;

/// @brief The favourites row while the keyboard shows (`Browser::favouriteRow`) in the default layout, without banks;
/// with
///        banks it is the row below, and the banks take this one. A first press there reveals the keyboard and does
///        nothing else.
inline constexpr int32_t kFavouriteRow = 7;
/// @brief How often the current favourite (and bank) flash: `QwertyUI::timerCallback`'s period.
inline constexpr uint32_t kFavouriteFlashMs = 500;

/// @brief The sample browser's BROWSE pad blink: `SoundEditor`'s `SHORTCUT_BLINK` timer as the browser leaves it.
enum class BrowseBlink : uint8_t {
	Off,     ///< Unset: a preview landed, a move left a shown preview, or a pad or turn revealed the keyboard.
	Menu,    ///< The file selector's own, every 200 ms cycle (`updatePadLightsFor`'s `setupShortcutBlink(x, 5, 0)`).
	Browser, ///< The browser's own, every other cycle (`possiblySetUpBlinking`'s `setupExclusiveShortcutBlink`).
};

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
	/// @brief `Main` while the keyboard or a waveform is drawn on the main pads, the sidebar being the view beneath's
	///        (`QwertyUI::drawKeys`, `gui/ui/qwerty_ui.cpp:76-125`); `None` in the root state, where
	///        `SampleBrowser::renderMainPads` answers false and the root view's pads show.
	deluge::ui::shell::PadRegions pad_regions() const override {
		return pads_ == BrowserPads::Root ? deluge::ui::shell::PadRegions::None : deluge::ui::shell::PadRegions::Main;
	}
	/// @brief True for the sample browser: `SampleBrowser` greys at once (`reassessGreyout(true)`) whenever its pads
	///        change, in `showPreview`, `clearPreviewDisplay` and the keyboard's reveal.
	bool greyout_changes_instantly() const override { return id_ == BrowserId::Sample; }
	/// @brief Everything but the audition column, which `SampleBrowser::padAction` hands to the instrument clip view.
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return id_ == BrowserId::Sample ? deluge::ui::shell::GridPadClaim::AllButAudition
		                                : deluge::ui::shell::GridPadClaim::All;
	}
	void leds(deluge::ui::LedFrame& frame) override;
	/// @brief False: the base view still paints its sidebar, and that is what shows through.
	bool occludes_grid() const override { return false; }
	/// @brief False: `Browser::renderOLED` starts with `drawScreenTitle` and fills the rows below
	///        it, so nothing of the UI underneath is meant to show. It is a full screen, not an
	///        overlay -- unlike `RenameScene` and `ContextMenuScene`, neither of whose legacy
	///        classes sets `oledShowsUIUnderneath`.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	/// @brief Each legacy browser's own mask.
	///
	/// The sample browser greys the mute column only while its keyboard or a waveform shows, or over the keyboard
	/// screen, and every column but the audition column otherwise: `SampleBrowser::getGreyoutColsAndRows`
	/// (`gui/ui/browser/sample_browser.cpp:576-587`). The MIDI device definition browser greys every column, keyboard
	/// included, as `LoadMidiDeviceDefinitionUI` does (`gui/ui/load/load_midi_device_definition_ui.cpp:41-44`).
	deluge::ui::grid::GreyoutMask greyout_mask() const override {
		if (id_ == BrowserId::MidiDeviceDefinition) {
			return deluge::ui::grid::GreyoutMask{0xFFFFFFFFu, 0};
		}
		return pads_ != BrowserPads::Root || keyboard_visible_ || ctx_.root_is_keyboard
		           ? deluge::ui::grid::GreyoutMask{0b10u, 0}
		           : deluge::ui::grid::GreyoutMask{0xFFFFFFFEu, 0};
	}

	/// @name Observable state, for specs and for whoever wires this up.
	/// @{
	/// @brief What the main pads show.
	BrowserPads pads() const { return pads_; }
	/// @brief Which BROWSE blink runs.
	BrowseBlink browse_blink() const { return browse_blink_; }
	/// @brief Whether the next `on_enter` is the drum creator's: `InstrumentClipView::enterDrumCreator` opens the
	///        browser with no `SoundEditor::beginScreen` after it, so only the browser's own blink runs.
	void set_drum_creator_next(bool from_creator) { drum_creator_next_ = from_creator; }
	/// @brief Which descriptor the last `on_enter` selected.
	BrowserId browser_id() const { return id_; }
	/// @brief Whether a MIDI device-definition browser opened since the last call, and clear it. That browser is a
	///        legacy `LoadUI`, whose `focusRegained` blinks LOAD, and the blink outlives it under the menu.
	bool take_load_blink() { return std::exchange(load_blink_pending_, false); }
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
	/// @brief `UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS`: Select is down and its release will enter the item.
	bool holding() const { return holding_; }
	/// @brief Whether the selected item is a folder; false with nothing selected.
	bool current_is_folder() const;
	/// @brief `SampleBrowser::qwertyAlwaysVisible`: KEYBOARD keeps the keyboard up through every move.
	bool keyboard_always_visible() const { return keyboard_always_visible_; }
	/// @brief `SampleBrowser::autoLoadEnabled`: LOAD has the browser claim each preview that lands.
	bool auto_load() const { return auto_load_; }
	/// @brief The previewed waveform's scroll and zoom.
	const deluge::sample::WaveformNav& waveform_nav() const { return nav_; }
	/// @brief The navigator the preview drew @p sample's waveform through, or null when the preview shows another
	///        waveform or none. `main`'s slicer renders through this view, `waveformBasicNavigator`, which only
	///        `SampleBrowser::showPreview` sets up.
	/// @param sample The sample the slicer opens on.
	const deluge::sample::WaveformNav* shown_waveform(const Sample* sample) const {
		return sample != nullptr && nav_identity_ == reinterpret_cast<uintptr_t>(sample) ? &nav_ : nullptr;
	}
	/// @brief The favourites row's row: `kFavouriteRow`, or the one below it while banks show.
	int32_t favourite_row() const;
	/// @brief Use @p favourites in place of the host's, or null to put the host's back. For specs.
	void set_favourites(services::Favourites* favourites) { favourites_override_ = favourites; }
	/// @}

	/// @brief Collect the file the scene could not load itself, exactly once.
	///
	/// The outbox `Descriptor::accept`'s `DeferAndWait` answer fills in; `MenuScene::take_action()` is the
	/// same shape for the verb half of the same wall. `Descriptor::accept` says why the choice
	/// cannot be performed from here.
	///
	/// **The consumer is `deluge::gui::ui::dispatchShellChoices`** (`gui/ui/shell_choice_dispatch.h`),
	/// which drains this slot once per guest tick from `ShellHostUI::timerCallback` and
	/// performs the sample claim (`SampleClaim`) against the parked path. Like `take_action()`'s,
	/// the slot is **one deep and overwrites silently**. A second choice inside one guest tick would
	/// need two presses in that tick: an Enter parks and refuses the next until the host answers
	/// `deferred_load_done`, and a folder menu's accept parks while the load it starts refuses further presses
	/// (`Descriptor::busy`).
	/// @return The choice, or `std::nullopt` when none is waiting.
	std::optional<BrowserChoice> take_choice();

	/// @brief Park @p op on the selected item for the host, leaving this scene and anything over it up: a folder
	///        menu's accept, whose load closes the menu once it ends (`long_press_done`).
	/// @param op    What the host is to do.
	/// @param claim The detection choices, for `Op::Claim`.
	void park_operation(BrowserChoice::Op op, BrowserChoice::ClaimArgs claim = {});

	/// @brief How a load a folder menu started ended: a failure closes the menu, a success the menu and this browser,
	///        as `closeOnFailure` and the load's own exit do in `main`. Filed on this scene's next render.
	/// @param success Whether the load or import succeeded.
	void long_press_done(bool success);
	/// @brief How the load a `DeferAndWait` accept parked ended: a success closes this browser, a failure leaves it up
	///        for another file, as `DxSyxBrowser` does once its read ends. Filed on this scene's next render; ignored
	///        unless such a load is out.
	/// @param success Whether the load succeeded.
	void deferred_load_done(bool success);
	/// @brief Whether a `DeferAndWait` accept's load is out: the host has not yet answered `deferred_load_done`.
	[[nodiscard]] bool awaiting_deferred_load() const { return awaiting_deferred_; }
	/// @brief The host's answer to a parked `Op::Slice`, once it has loaded the file and opened `SlicerScene` on it (or
	///        could not): the next `after_render` pushes `SlicerScene`, with main's `"SLICER"` popup on the
	///        Shift+Select door. A slice that did not open pushes nothing (the host showed why). Ignored unless a slice
	///        is out.
	/// @param opened Whether `slicing::slicer_scene()` holds the file now.
	void slice_ready(bool opened);
	/// @brief The host deleted @p path from the card: if it is still the selected file it leaves the list, the
	///        selection moving on as `Browser::currentFileDeleted` moves it, and the new selection is previewed.
	/// @param path The deleted file's full path.
	void file_deleted(std::string_view path);

	/// @brief Take the preview the selection last asked for, for `deluge::gui::ui::dispatchShellChoices`, which drains
	/// it
	///        every guest tick after `take_choice()`. One deep: a later request overwrites one not yet taken, as
	///        legacy's latest-wins preview does.
	/// @return The request, or `std::nullopt` when none is waiting.
	std::optional<PreviewRequest> take_preview();
	/// @brief The host's answer to the request in hand: its load ended, with a sample in the preview drum or not, or
	/// the
	///        preview was stopped.
	/// @param has_sample Whether the preview drum holds a sample to draw.
	void preview_settled(bool has_sample);
	/// @brief Take the scroll the browser last asked for, for `deluge::gui::ui::dispatchShellChoices`, which drains it
	///        after the preview. A scroll of the browser's own pads is handed over only once they hold what scrolls in.
	/// @return The request, or `std::nullopt` when none is ready.
	std::optional<ScrollRequest> take_scroll();
	/// @brief Whether a scroll is asked for and not yet taken: the host keeps the main columns from then, so the
	///        pads the scroll brings in are not first sent whole.
	[[nodiscard]] bool scroll_pending() const { return pad_scroll_.has_value() || waveform_scroll_ != 0; }
	/// @brief Whether a preview was asked for and the host has not yet answered; `next_timer()` polls meanwhile, so the
	///        host's dispatcher keeps ticking the load.
	[[nodiscard]] bool preview_outstanding() const { return preview_outstanding_; }

	/// @brief Install a dispatcher other than `storage::Owner::run` on the scene's seam.
	///
	/// **For specs.** On the host `Owner::run` is inline (`storage/owner.h`'s `Owner::run`),
	/// so with the shipped dispatcher a listing has already finished when `request()` returns and
	/// the in-flight state -- which is most of this scene's behaviour -- can never be observed.
	/// See the honesty note in `listing.h`.
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

	/// @brief `Browser::clampFileSelectionAndScroll`.
	/// @param visible_rows How many rows the panel shows (legacy's
	///                     `display->getNumBrowserAndMenuLines()`).
	/// @param allow_no_file_selection Legacy's parameter of the same name.
	void clamp(int32_t visible_rows, bool allow_no_file_selection = true);
	/// @brief `Browser::setEnteredTextFromCurrentFilename`: the selected
	///        entry's name with its extension cut off.
	void set_entered_text_from_current_filename();
	/// @brief Apply one keyboard key: enter enters, backspace deletes, anything else types and searches.
	deluge::ui::shell::Result press_key(char c);
	/// @brief `Browser::arrivedInNewFolder`'s first step: hide the keyboard unless it is always visible.
	void arrive_in_new_folder();
	/// @brief Reveal the keyboard: `qwertyVisible`, and the keys drawn.
	void reveal_keyboard();
	/// @brief `SampleBrowser::horizontalEncoderAction`: zoom or scroll the preview, or reveal the keyboard and walk the
	///        cursor.
	deluge::ui::shell::Result horizontal_turn(int32_t delta);
	/// @brief `SampleBrowser::buttonAction`'s X_ENC, LOAD, RECORD and Shift+SAVE arms.
	/// @return The answer, or nullopt for a button those arms do not take.
	std::optional<deluge::ui::shell::Result> sample_button(deluge::ui::shell::ButtonId button, bool on);
	/// @brief The favourites the browser shows: the host's, unless a spec gave its own.
	services::Favourites& favourites();
	/// @brief `Browser::isFavouritesVisible` / `isBanksVisible`: the sample browser's keyboard is up and the layout
	///        shows them.
	bool favourites_visible() const;
	bool banks_visible() const;
	/// @brief `Browser::padAction`'s favourites-row arm: recall the favourite, or with SHIFT set or unset it.
	deluge::ui::shell::Result favourite_press(uint8_t position);
	/// @brief `Browser::favouritesChanged`: redraw the row, and restart its flash.
	void favourites_changed();
	/// @brief The favourites and bank rows, and the current ones' flash, over the keyboard.
	void render_favourites(deluge::ui::grid::GridFrame& f);
	/// @brief The BROWSE pad's flash, over whatever the main pads show, while `browse_blink_` runs.
	void render_browse_blink(deluge::ui::grid::GridFrame& f);
	/// @brief The previewed sample's waveform across the main pads.
	void render_waveform(deluge::ui::grid::GridFrame& f);
	/// @brief File the preview for the selected entry, as `SampleBrowser::previewIfPossible` asks for one: the file's
	///        path, or an empty path for a folder or no selection. Only the sample browser previews.
	/// @param direction The selection's move: -1, 0 or 1.
	void file_preview(int8_t direction);
	/// @brief Replace the edited text with @p s, truncated at `kMaxEnteredChars`.
	/// @param s The new text.
	void set_entered_text(std::string_view s);

	/// @brief `QwertyUI::padAction`'s "normal keys" arm followed by
	///        `Browser::predictExtendedText` -- type one character, then find
	///        the entry it names.
	/// @param c The character typed.
	void type_char(char c);
	/// @brief `QwertyUI::processBackspace`, then the same search.
	void backspace();
	/// @brief The in-memory half of `predictExtendedText`: search the cached window for the edited
	///        text and select what it names.
	/// @return What the cached window could say.
	SearchOutcome search_for_entered_text();
	/// @brief `search_for_entered_text` plus the two arms its caller owes -- restore the previous text
	///        on a miss, dispatch a wider read on `NeedsRead`.
	/// @param revert_cursor_to Where the cursor was before the keystroke, for legacy's
	///                         `enteredTextEditPos--` on a failed prediction (`qwerty_ui.cpp:357`).
	void run_search_after_typing(int32_t revert_cursor_to);
	/// @brief `Browser::selectEncoderAction`, including its four reload arms.
	/// @param offset Signed detents.
	void step_selection(int32_t offset);
	/// @brief `Browser::finishSelectEncoderAction`: apply a new selection.
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
	/// @param shift Whether SHIFT is held: on a file in a kit's sample browser, `SlicerScene`'s door
	///              (`SampleBrowser::enterKeyPress`, `sample_browser.cpp:390-401`).
	/// @return What the shell should do.
	deluge::ui::shell::Result enter_key_press(bool shift);
	/// @brief `SampleBrowser::canImportWholeKit`: a kit whose only drum is the one edited, on source 0.
	/// @return Whether this kit may take a whole folder or a slice.
	bool fresh_kit() const;
	/// @brief `SampleBrowser::timerCallback`'s long-press arm (`sample_browser.cpp:310-356`): a popup, or one of the
	///        two folder menus, for the item Select is held on.
	/// @return What the shell should do.
	deluge::ui::shell::Result long_press();
	/// @brief `Browser::backButtonAction`: ascend, or close at the top.
	/// @return What the shell should do.
	deluge::ui::shell::Result back();
	/// @brief `Browser::close()`, as an `Action`.
	static deluge::ui::shell::Result close();
	/// @brief Is a listing outstanding in any sense -- running, owing a collect, or waiting to be
	///        re-filed?
	///
	/// All three, and the third is the one that bites. `pending_` is a
	/// single slot, so an input that files a second listing over a parked one **loses** the first:
	/// a BACK press would replace a parked `Open` with an `UpLevel`, and the browser would then
	/// list a directory derived from whatever `currentDir` the *foreign* holder of the catalogue
	/// claim left behind. And `pending_` is created in exactly the case the other two miss -- a
	/// `Busy` refusal, where nothing is in flight and no result is owed. `after_render` already got
	/// this right (its retry step returns before the keyboard arm); this is the `handle()` path, which
	/// did not.
	/// @return True when nothing outside `after_render` may touch `FileCatalogue` or file a
	///         listing.
	bool seam_busy() const { return listing_.in_flight() || listing_.has_result() || pending_.has_value(); }
	/// @brief Whether a legacy UI mode is up: Select held for a long press, or X_ENC held. The buttons that legacy
	///        gates on `currentUIMode` (BACK, Select's press, RECORD, Shift+SAVE, X_ENC) do nothing meanwhile.
	bool mode_up() const { return holding_ || xenc_held_; }
	/// @brief The sample browser's gate (`SampleBrowser::buttonAction` and `padAction`): an explicit sample load or a
	///        folder import is running, and every button and key is refused until it ends.
	bool load_running() const {
		return id_ == BrowserId::Sample && ctx_.facts != nullptr && ctx_.facts->sample_load_in_flight();
	}

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
	/// runs again when it lands. Legacy's `Browser::PredictionTarget::doneNewRead`, one tick later.
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
	std::optional<PreviewRequest> preview_{};
	std::optional<ScrollRequest> pad_scroll_{};
	/// A waveform to scroll in once drawn: the move's direction, or 0. `after_render` files it after a grid pass
	/// that drew the waveform.
	int8_t waveform_scroll_ = 0;
	bool preview_outstanding_ = false;
	/// What the main pads show. Legacy's flags beside it, which the image does not always follow
	/// (`SampleBrowser::showPreview` scrolls a waveform in over the keys and leaves them "drawn"):
	/// `qwertyVisible` (a pad types rather than reveals), `qwertyCurrentlyDrawnOnscreen`,
	/// `currentlyShowingSamplePreview`, `qwertyAlwaysVisible` and `autoLoadEnabled`.
	BrowserPads pads_ = BrowserPads::Root;
	bool keyboard_visible_ = false;
	bool keys_drawn_ = false;
	bool showing_preview_ = false;
	bool keyboard_always_visible_ = false;
	bool auto_load_ = false;
	/// X_ENC is held: legacy's `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, entered only with no other mode.
	bool xenc_held_ = false;
	/// The file the outstanding preview names (empty for a stop) and its direction, which its answer acts on.
	std::string preview_path_{};
	int8_t preview_direction_ = 0;
	/// The next `leds()` restarts BACK's blink: the browser has just opened.
	bool back_blink_restart_ = true;
	/// The browser that opened blinks LOAD (`take_load_blink`): set by a successful `on_enter`.
	bool load_blink_pending_ = false;
	/// A spec's favourites, in place of the host's.
	services::Favourites* favourites_override_ = nullptr;
	/// What this tick's favourites and bank rows reported, for `after_render`.
	deluge::ui::Response favourite_resp_{};
	deluge::ui::Response bank_resp_{};
	/// The current favourite's flash: when the rows were last drawn afresh, and the last period flashed.
	std::optional<uint32_t> favourite_flash_anchor_ms_{};
	uint32_t favourite_flash_step_ = 0;
	/// The selected row's side-scroller, armed while the cursor is at the start.
	deluge::ui::ScrollText row_scroll_{};
	/// The host changed the pads, or the list, between ticks; the next tick repaints them.
	bool pads_changed_ = false;
	/// A main-pad press `render_grid` saw with the keyboard hidden, for `after_render`.
	std::optional<deluge::ui::PadEvent> reveal_press_{};
	/// The previewed waveform's scroll and zoom, opened afresh on each new sample (`waveformBasicNavigator.opened()`).
	deluge::sample::WaveformNav nav_{};
	/// The waveform last painted, kept on the pads once the preview gives its sample up.
	RGB waveform_shown_[kDisplayHeight][kDisplayWidth]{};
	uint64_t nav_identity_ = 0;
	/// The incomplete render's progress: the columns it had resolved, and when that last grew (-1: none pending).
	int32_t redraw_resolved_cols_ = -1;
	uint32_t redraw_progress_ms_ = 0;
	/// The BROWSE pad's blink: which one runs, the cycle's start (reset whenever one is armed), and the last step
	/// flashed.
	BrowseBlink browse_blink_ = BrowseBlink::Off;
	std::optional<uint32_t> blink_anchor_ms_{};
	uint32_t blink_fired_step_ = UINT32_MAX;
	/// No preview has been asked for since the browser opened.
	bool first_preview_ = false;
	/// The preview in hand is the open's first, which on `main` landed before the file selector's blink was armed.
	bool spare_blink_from_preview_ = false;
	/// The next `on_enter` is the drum creator's (`set_drum_creator_next`).
	bool drum_creator_next_ = false;

	deluge::ui::oled::TextEditState edit_{};
	deluge::ui::grid::TextKeyboardState kb_{};
	deluge::ui::grid::KeyboardLayoutKind layout_ = deluge::ui::grid::KeyboardLayoutKind::Qwerty;

	/// `Browser::mainButtonAction`'s hold: Select is down, and the release enters. The sample browser answers a hold
	/// of `kLongPressMs` once, at `hold_deadline_ms_`, which it then clears; a hold that answered with a menu or a
	/// popup has also stopped holding, so its release does nothing.
	bool holding_ = false;
	std::optional<uint32_t> hold_deadline_ms_{};
	/// A folder menu's load has ended: true closes the menu and this browser, false the menu only.
	std::optional<bool> long_press_done_{};
	/// A `DeferAndWait` accept's load is out: the host has not answered `deferred_load_done`.
	bool awaiting_deferred_ = false;
	/// How that load ended, for the next `after_render`: true closes this browser.
	std::optional<bool> deferred_done_{};
	/// A parked slice's open, waiting for the host (`slice_out_`), then for `after_render` (`slice_ready_`); and
	/// whether it came through the Shift+Select door.
	bool slice_out_ = false;
	std::optional<bool> slice_ready_{};
	bool slice_from_shift_ = false;
	/// The shift level the last event carried, for the keyboard's Enter key, which `after_render` answers.
	bool shift_ = false;
	/// This browser parked a load for a folder menu and has not heard how it ended: only that load's end is acted on.
	bool awaiting_load_ = false;

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

/// @brief How long Select is held before the sample browser answers it as a long press (`LONG_PRESS_DURATION`,
///        `gui/ui/ui.h`).
inline constexpr uint32_t kLongPressMs = 400;

/// @brief The application's one file browser.
///
/// A function-local static, for the reason `menu_scene()`, `rename_scene()` and
/// `context_menu_scene()` are: it must outlive every `Shell` that borrows it and must not
/// participate in static-initialisation order across translation units.
/// @return The singleton.
BrowserScene& browser_scene();

} // namespace deluge::scenes::browser
