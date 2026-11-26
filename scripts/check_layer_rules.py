#!/usr/bin/env python3
"""Symbol-rule ratchet enforcing the UI redesign's dependency direction.

Each rule is (name, from_glob, kind, regex, spec), and every kind is "symbol": the regex matches any source line
that is not a comment or an #include. The include rules are scripts/layers.toml's, checked by
scripts/check_layers.py. The baseline holds counts per (file, rule) and may only shrink: a PR that raises a count,
or adds a new (file, rule) pair, fails.

  scripts/check_layer_rules.py            # check (exit 1 on regression)
  scripts/check_layer_rules.py --list     # print current counts
  scripts/check_layer_rules.py --update   # rewrite the baseline from the tree
  scripts/check_layer_rules.py --audit-symbols  # every named row's identifiers really exist
"""

import argparse
import re
import subprocess
import sys
from collections.abc import Callable
from fnmatch import fnmatch
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "scripts" / "layer_rules_baseline.txt"
SUFFIXES = {".c", ".cpp", ".h", ".hpp"}


def fetched_vendor(repo: Path) -> Callable[[Path], bool]:
    """A predicate for untracked files under a `third_party/` directory: a cache a build fetched, not project code."""
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=repo, capture_output=True, check=False
    )
    tracked = (
        set(result.stdout.decode().split("\0")) if result.returncode == 0 else set()
    )

    def skip(path: Path) -> bool:
        rel = path.relative_to(repo)
        return "third_party" in rel.parts and rel.as_posix() not in tracked

    return skip


NON_UI = (
    "src/deluge/{model,playback,processing,storage,io,modulation,memory,sync,util}/**"
)

# The `scenes-no-root-ui` row's alternatives, named rather than inlined so `--audit-symbols` can
# check that each one is a symbol the source actually has. Every entry below was taken from an
# `extern <T> <name>;` declaration in `src/deluge/gui/views/*.h`, `src/deluge/gui/ui/**/*.h` or
# `src/deluge/gui/context_menu/*.h` whose type is a `UI` subclass -- i.e. from the declarations,
# not from memory.
#
# `view` (`View view`, `gui/views/view.h`) is deliberately NOT here: as a whole-word pattern it
# would match the English word in prose and any local named `view`, and its false-positive rate
# would make the row unusable.
ROOT_UI_GLOBALS = (
    # the root views (`gui/views/`)
    "sessionView",
    "arrangerView",
    "automationView",
    "performanceView",
    "instrumentClipView",
    "audioClipView",
    "keyboardScreen",
    # the rename UIs `rename::RenameScene` replaces (`gui/ui/rename/`)
    "renameClipUI",
    "renameOutputUI",
    "renameMidiCCUI",
    # the browsers and savers the file scenes replace (`gui/ui/browser/`, `gui/ui/save/`,
    # `gui/ui/load/`) -- the group a scene is most likely to reach for.
    "loadSongUI",
    "saveSongUI",
    "loadInstrumentPresetUI",
    "saveInstrumentPresetUI",
    "loadPatternUI",
    "savePatternUI",
    "saveKitRowUI",
    "loadMidiDeviceDefinitionUI",
    "saveMidiDeviceDefinitionUI",
    # The two `gui/context_menu/` singletons `dispatchShellActions` opens for
    # `MenuAction::OpenSongMidiLearn` and `OpenSongMacros`: those verbs exist BECAUSE the scenes
    # layer may not name these two. They are `UI` subclasses like every other name here
    # (`ContextMenu : public UI`, `gui/context_menu/context_menu.h`), from a third directory.
    # `--audit-symbols`' root for this row is `src/deluge/gui`, so both are inside what it reads.
    # Mentions in `src/ui/scenes` (`menu/action.h`, `context_menu/context_menu_scene.h`) are whole-line
    # `///` comments, which `is_comment_line` skips -- the row keeps them descriptions.
    "midiLearnMode",
    "configureSongMacros",
)
# The `StemExport` PROCESS entry points -- the `scenes-no-stem-export-process` row's alternatives,
# named here for the same reason `CATALOGUE_DISPATCH_CALLS` is: `--audit-symbols` checks each is a
# method the source really declares (`StemExport`, `processing/stem_export/stem_export.h`).
#
# Why this needs a row of its own, and why it is the only arm of `dispatchShellActions` that does.
# Every other verb's legacy target is already unreachable from `src/ui/scenes`: `performanceView`,
# `audioClipView`, `midiLearnMode` and `configureSongMacros` are `ROOT_UI_GLOBALS`
# above, and `Screensaver::settingsChanged` lives in `hid/display/screensaver.h`, which
# `scenes-includes-gui` and `scenes-includes-outside` BOTH refuse (probed: adding that include to
# `menu/nodes/settings.cpp` raises two regressions). `stemExport` is the exception --
# `processing/**` is on `scenes-includes-outside`' allow-list on purpose, so
# `MenuAction::StartStemExport` is the one verb whose target the scenes layer could reach through an
# allowed include. The export's seven options are DEVICE rows the host stores on `stemExport`, so the
# menu reaches them through the device port, not the global.
#
# The split is `CATALOGUE_DISPATCH_CALLS`' split, for the same reason: the *settings* stay readable
# and writable, the calls that START, RUN, STOP or ABORT the export are the wiring layer's.
#
# The other public methods (`startOutputRecordingUntilLoopEndAndSilence`, `stopPlayback`,
# `stopOutputRecording`, `checkForLoopEnd` and `checkForSilence` among them) are deliberately NOT
# here: `stopPlayback` in particular is a name `PlaybackHandler` also has, and this row is a
# whole-word symbol match with no receiver in it. Three names end in `StemExportProcess` and
# `exportRoutine` (the step the export's job runs) is declared by `StemExport` alone, so none of the
# four collides with anything.
STEM_EXPORT_PROCESS_CALLS = (
    "startStemExportProcess",
    "exportRoutine",
    "stopStemExportProcess",
    "abortStemExportProcess",
)
# The `scenes-no-legacy-waveform` row's alternative, named here for the same reason
# `ROOT_UI_GLOBALS` is: `--audit-symbols` checks it is a global the source really declares, from its
# `extern` declaration: `extern WaveformRenderer waveformRenderer` (`gui/waveform/waveform_renderer.h`).
#
# The waveform's arithmetic (columns, peaks, scroll and zoom) is `src/ui/views/waveform/`'s, and its scan is
# `model/sample/waveform_scan.h`'s, both of which `src/ui/scenes` may reach; the drawing into `PadLEDs`'
# images stays in `gui/waveform/waveform_renderer.h`, which the scenes layer draws around through
# `ui/widgets/grid/`. This row is what keeps the split split. It is a SYMBOL row for the same reason
# `scenes-no-root-ui` is: `scenes-includes-gui` already forbids the header, so the way a regression
# would actually be spelled is a bare `extern WaveformRenderer waveformRenderer;`.
LEGACY_WAVEFORM_GLOBALS = ("waveformRenderer",)
# The `FileCatalogue` entry points that DISPATCH a listing -- they read the card, refill or
# re-order the single shared `FileCatalogue::fileItems`, and so may only be called while the
# catalogue's single-flight claim is held (`storage/file_catalogue.h`, `claim`/`release`). The
# read-only and per-item spellings (`fileItems`, `currentDir`, `searchFileItems`,
# `repinComparator`, `emptyFileItems`) are deliberately NOT here: `BrowserScene` calls those on
# a landed result and must keep being able to.
CATALOGUE_DISPATCH_CALLS = (
    "readFileItemsForFolder",
    "sortFileItems",
    "cullSomeFileItems",
    "deleteSomeFileItems",
    "deleteFolderAndDuplicateItems",
    "getNewFileItem",
)
# The legacy HID OUTPUT DRIVERS the `scenes-no-legacy-hid-drivers` row forbids -- the five
# spellings by which legacy code lights an LED, arms a UI timer, or asks for a redraw. Named
# here, like `ROOT_UI_GLOBALS`, so `--audit-symbols` can check each is a real symbol.
#
# This is `model-calls-ui`'s alternative list minus everything another row already holds the
# scenes layer to: `display->` is `scenes-no-display`, and `getRootUI(`/`getCurrentUI(`/
# `openUI(`/`changeRootUI(` are `scenes-no-root-ui`'s `UI_STACK_CALLS`. What was left over was
# what this row covers for the scenes layer, and it matters because the three services here are the port of `View` (`gui/views/view.cpp`), which is *the* legacy
# caller of all five -- `indicator_leds::setLedState`/`blinkLed`/`setKnobIndicatorLevel`/
# `indicateAlertOnLed` for the transport and gold-knob LEDs, `uiTimerManager.setTimer` for the
# MIDI-learn flash and the two 25 ms automation timers, `uiNeedsRendering`/`renderUIsForOled`
# for every repaint. The port declares LEDs through `shell::Leds`, files `Action::Kind::Repaint`
# and returns a deadline from `Service::next_timer()`; reaching for these instead is the exact
# regression this row exists to catch, and it is spelled as a bare call because
# `scenes-includes-gui` already forbids the header.
#
# `currentUIMode` and `isUIModeActive(` are deliberately NOT here, and the reason is measured
# rather than assumed: `is_comment_line` does not strip a TRAILING comment, so the three
# `holding_save_`/`holding_load_` assignments in `services/global_controls.cpp`, whose trailing comments name
# `currentUIMode = ...`, would give this row a non-zero baseline out of pure
# comment text. They are also a different concern -- reading a mode, not driving an output -- which the
# ports model as their own booleans.
LEGACY_HID_DRIVERS = (
    "uiNeedsRendering",
    "renderUIsForOled",
    "indicator_leds",
    "PadLEDs",
    "uiTimerManager",
)
# ...and the matching regex, which is not mechanically derivable from the names above because
# each is spelled with a different suffix (a call, a namespace qualifier, a member access).
#
# `PadLEDs::flashCursor` is excluded BY NAME, on exactly the argument that keeps `fileItems` out
# of `CATALOGUE_DISPATCH_CALLS`: it is not a driver call, it is the persisted SETTING BYTE that
# `flash::Status::readCurrentValue`/`writeCurrentValue`
# read and write, ported as a value in `menu/nodes/settings.cpp` and recorded there
# (the `hid/` state note in `nodes/settings.h`). Excluding the spelling rather than the file keeps the other four
# drivers -- and every other `PadLEDs::` member -- gated inside `menu/nodes/settings.cpp` too.
LEGACY_HID_DRIVER_RE = (
    r"\b(uiNeedsRendering\(|renderUIsForOled\(|indicator_leds::|"
    r"PadLEDs::(?!flashCursor\b)|uiTimerManager\.)"
)
# The UI-stack entry points (declared in `gui/ui/ui.h`), matched with a following `(`.
UI_STACK_CALLS = (
    "openUI",
    "closeUI",
    "changeRootUI",
    "getRootUI",
    "getCurrentUI",
    "swapOutRootUILowLevel",
)

# The legacy files authorised to reach into the scenes UI -- the `legacy-includes-port` and
# `legacy-names-port` rows' exclusion list, one entry per argued seam. Everything else under
# `src/deluge/**` is held to zero.
#
# Why the list is spelled out file by file rather than as `gui/ui/shell_*`: a glob would silently
# exempt the next `shell_`-prefixed file somebody drops into `gui/ui/`, and the whole value of
# these two rows is that adding a legacy->port edge is a decision somebody has to write down here.
#
# * `src/deluge/deluge.cpp` -- the three boot calls: `scenes::assigned_node_ids()` and the service
#   install. `src/ui/scenes/registry.h`'s own note is the boot author's copy of this.
# * `gui/ui/shell_host_ui.{h,cpp}` -- the guest itself, `ShellHostUI : UI`.
# * `gui/ui/shell_guest_surfaces.{h,cpp}` -- `GuestSurfaceSet`, the `SurfaceSet` that copies into
#   legacy's `OLED::main` and `PadLEDs::image`.
# * `gui/ui/shell_guest_services.cpp` -- where the three `scenes::services::` singletons are built
#   and handed to `Shell::set_services`. Its HEADER is deliberately NOT excluded: it
#   names none of this, and leaving it in scope is what keeps that true.
# * `gui/ui/shell_action_dispatch.cpp` -- `dispatchShellActions`, the poll of
#   `MenuScene::take_action()` that `ShellHostUI::timerCallback` runs once per tick. Its
#   HEADER is deliberately NOT excluded, on `shell_guest_services.h`'s terms: it declares one
#   no-arguments free function and names no port type, and leaving it in scope is what keeps that
#   true.
# * `gui/ui/menu_entry.cpp` -- `openMenu`, the one door the fourteen entry points use.
#   Its DECLARATION (`util/menu_entry.h`) is likewise not excluded, and must stay includable
#   from `model/clip/instrument_clip_minder.cpp`, which is why it names no port type.
LEGACY_PORT_SEAMS = (
    "src/deluge/deluge.cpp",
    "src/deluge/gui/ui/shell_host_ui.h",
    "src/deluge/gui/ui/shell_host_ui.cpp",
    "src/deluge/gui/ui/shell_guest_surfaces.h",
    "src/deluge/gui/ui/shell_guest_surfaces.cpp",
    "src/deluge/gui/ui/shell_guest_services.cpp",
    "src/deluge/gui/ui/shell_action_dispatch.cpp",
    # The noun half of the same seam. It names
    # `deluge::ui::scenes::browser::browser_scene()` for exactly the reason the row above does -- the
    # scene hands a chosen file it may not load to its host, and this is the side of the wall that gets
    # to name `AudioEngine`, `PadLEDs` and `instrumentClipView`. Its HEADER
    # (`browser_host.h`) is deliberately NOT listed: it names no `ui/scenes/` type, taking
    # `std::string_view` where the choice's path would go, so the seam is one translation unit wide.
    "src/deluge/gui/ui/browser_host.cpp",
    # The pad half of the same seam. It names `keyboardScreen`,
    # `instrumentClipView` and `performanceView` -- the legacy views the pad grid forwards to --
    # for the same reason the row above names
    # `AudioEngine`/`PadLEDs`/`instrumentClipView`: this is the side of the
    # wall that gets to name them. Its HEADER (`shell_pad_forward.h`) is deliberately NOT listed: it
    # names no `ui/scenes/` or `ui/shell/` type, taking plain `int32_t` coordinates, so this seam is one
    # translation unit wide -- exactly the line `browser_host.h` draws above.
    #
    # The sixth arm, `automationView`, is the one arm that cannot be a bare
    # forward: whether the menu is editing an automation-view param is state no legacy object
    # holds, so the question is ported rather than called.
    # This `.cpp` also names `deluge::ui::scenes::menu_scene()`
    # (`ui/scenes/registry.h`) and reads a focused node's `deluge::ui::scenes::menu::ParamRef`
    # (`ui/scenes/menu/node.h`) -- both `ui/scenes/` names, both covered by this same entry.
    "src/deluge/gui/ui/shell_pad_forward.cpp",
    # The automation view's half of the menu's seam: the poll of `MenuScene::take_transition()` that
    # `ShellHostUI::timerCallback` runs once per tick, and what legacy's sound editor does to `automationView` for each
    # change of the menu's param (`ui/scenes/menu/transition.h`). It names `deluge::ui::scenes::menu_scene()` and the
    # menu's `AutomationParam` and `MenuTransition` because it reads them, and `automationView` because this is the
    # side of the wall that may. Its HEADER (`shell_automation_glue.h`) is deliberately NOT listed: it declares free
    # functions over legacy types and integers and names no port type.
    "src/deluge/gui/ui/shell_automation_glue.cpp",
    # The slicer scene's host: performs `SlicerScene`'s `SliceOps` with `SliceCore`. Its HEADER (`shell_slice_ops.h`)
    # names no port type, so it is not listed.
    "src/deluge/gui/ui/shell_slice_ops.cpp",
    # And the menu's: what a menu opens over (the clip, its track and kind, the kit row selected, affect-entire), from
    # the clip a legacy view has open, until the views keep it.
    "src/deluge/gui/ui/menu_view_of.h",
    "src/deluge/gui/ui/menu_view_of.cpp",
    "src/deluge/gui/ui/menu_entry.cpp",
    # The note bridge's installer. `src/ui/scenes` may include `model/**`
    # (`src/ui/scenes/CMakeLists.txt`), so `Note`/`NoteRow` are not what earns this file its place
    # here -- it is `gui/views/instrument_clip_view.h`, which `src/ui/scenes` may NOT include, and the
    # iterance preset table, `iterancePresets` (`util/lookuptables/lookuptables.cpp`), which
    # `src/ui/scenes` may not reach either (it lives in a `.cpp`, not the model header the table's
    # declaration sits in). This is the side of the wall that gets to name `instrumentClipView`
    # itself and that table, the same reason `shell_pad_forward.cpp` above gets to name
    # `instrumentClipView`/`keyboardScreen`/`performanceView`. It names
    # `deluge::ui::scenes::menu::NoteBridge` and `deluge::ui::scenes::menu::set_note_bridge`
    # (`ui/scenes/menu/note_bridge.h`) to install the function pointers those legacy names sit behind.
    # Its HEADER (`shell_note_bridge.h`) is deliberately NOT listed: it names no `ui/scenes/` type,
    # only the no-arguments `wireShellNoteBridge()` that installs the bridge, so this seam too is
    # one translation unit wide -- exactly the line `browser_host.h` and
    # `shell_pad_forward.h` draw above.
    "src/deluge/gui/ui/shell_note_bridge.cpp",
    # The note modes' routing through the guest. It names
    # `deluge::ui::scenes::menu_scene()` (`ui/scenes/registry.h`) and reads its `menu_context().root`
    # (`ui/scenes/menu/menu_scene.h`), and walks `shellHostUI`'s `deluge::ui::shell::SceneStack` for the
    # menu's identity -- whether the note editor or a note-row editor is open.
    # And it names `instrumentClipView`, whose note handlers the buttons, the two
    # encoder hooks and the exit forward to. That is the same pair of names
    # `shell_pad_forward.cpp` above is listed for, and the same side of the wall.
    #
    # What it names on each side. Legacy side:
    # `closePortedMenu()` (`gui/ui/shell_action_dispatch.h`) and `runtimeFeatureSettings` (the toggle's `HorizontalMenus`
    # read). Port side: the `deluge::ui::scenes::menu::nodes::*` note and row nodes plus
    # `sequence_direction` (`ui/scenes/menu/nodes/note.h`), and `MenuScene::focused`, `read_value_again`,
    # `go_up_one_level` and `enter_submenu` -- the ports of the note-mode handlers' own
    # `readValueAgain` / `goUpOneLevel` / `toggleNoteEditorParamMenu` calls. Its HEADER
    # (`shell_note_mode.h`) is deliberately NOT listed: it names no `ui/scenes/` or `ui/shell/` type --
    # `NoteModeKind` is its own enum, and every call takes `hid::Button`, integers and bools -- so
    # `shell_host_ui.cpp` and the host spec suite's fake reach it without the `deluge_ui_scenes`
    # closure, and this seam too is one translation unit wide.
    "src/deluge/gui/ui/shell_note_mode.cpp",
    # The notification sink's installer. It names
    # `deluge::ui::shell::NotificationSink` and `deluge::ui::shell::set_notification_sink`
    # (`ui/shell/notices.h`) to point the shell's `Notice::Kind::Notification` consumer at
    # `showNoticeNotification` / `cancelNoticeNotification` (`gui/ui/notify_drain.h`) -- the legacy
    # display calls `drain_notifications` itself makes for the same two kinds, so the application's
    # two queue consumers end in one call. Naming both is the whole job, which is why it cannot live
    # in `notify_drain.cpp` (not a seam, and it should stay one). Its HEADER
    # (`shell_notification_sink.h`) is deliberately NOT listed: it names no `ui/shell/` type, only the
    # no-arguments `wireShellNotificationSink()`, so this seam too is one translation unit wide --
    # exactly the line `shell_note_bridge.h` draws above.
    "src/deluge/gui/ui/shell_notification_sink.cpp",
    # The root views' gold knobs: the root's `KnobPresentation` over the legacy display, `indicator_leds` and `view`,
    # and the three `UI` hooks' forward into the shell's `ModKnobs`. It names the knobs and their port because it
    # implements the port on this side of the wall. Its HEADER (`root_knobs.h`) is deliberately NOT listed: it names no
    # port type, so `ui.cpp` and the views reach it as they reach any legacy function.
    "src/deluge/gui/ui/root_knobs.cpp",
    # The one declaration of the shared `ModKnobs` (`sharedModKnobs()`), defined in `shell_guest_services.cpp`. Its
    # own header because it names the port's type, which `shell_guest_services.h` must not.
    "src/deluge/gui/ui/shell_guest_knobs.h",
    # The ports' host half: one file per port the scenes read or write through (`ui/scenes/bind/ports/`), and the one
    # `host_ports()` built from them. They name the port types because they ARE their implementations on this side of
    # the wall, and the legacy runtime objects the ports answer from (the audio engine, the card's loads, the UI
    # modes) because this is the only side that may. The whole directory is the seam, so a port's host file needs no
    # entry of its own.
    "src/deluge/gui/ui/shell_host/*",
)

RULES = [
    (
        # The symbol half of `legacy-includes-port`, for exactly the reason `scenes-no-root-ui` is
        # a symbol row rather than an include row: the source routinely names a symbol through a
        # bare declaration BECAUSE the include is forbidden (`extern uint32_t jcong;`,
        # `src/ui/scenes/menu/nodes/settings.cpp`). A legacy file that wants
        # `deluge::ui::scenes::scene_registry()` and finds the include row in its way can write
        # `namespace deluge::ui::scenes { const SceneTable& scene_registry(); }` and link, because
        # the archive is in the closure. This row is what sees that spelling.
        #
        # Matched without a trailing `::` so that the namespace-reopening form above counts too,
        # and `\b`-anchored on `deluge::` so it cannot fire on the English words "scenes" and
        # "shell", which the prose of the scanned files is full of. Same ratchet-not-proof caveat as every
        # symbol row here: a `using namespace` plus an unqualified type would slip past, and
        # `is_comment_line` skips only whole-line comments.
        "legacy-names-port",
        ("src/deluge/**", *LEGACY_PORT_SEAMS),
        "symbol",
        r"\bdeluge::(scenes|ui::shell)\b",
        "2026-08-26-ui-migration-design.md",
    ),
    (
        "model-calls-ui",
        NON_UI,
        "symbol",
        (
            r"\b(uiNeedsRendering\(|renderUIsForOled\(|display->|getRootUI\(|getCurrentUI\(|currentUIMode|isUIModeActive\(|"
            r"indicator_leds::|PadLEDs::|uiTimerManager\.|openUI\(|changeRootUI\()"
        ),
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        "model-calls-display",
        NON_UI,
        "symbol",
        r"\bdisplay->",
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        # The input-consumption contract (ui/widgets/core/frame_context.h): a widget reads input only
        # through its frame's accessors -- f.turn(), f.edit(0), f.select_pressed() -- so that
        # focus masking and input claims are not something a widget can forget to apply. Reaching
        # into the snapshot behind the frame skips both. The two frame files are excluded because
        # they *are* the accessors: `ctx.input.turn` inside FrameInput::turn() is the one place
        # the field may be read.
        #
        # Line-based, like every rule here: `auto& in = f.ctx.input;` followed by `in.turn` slips
        # past it, and a `/* ... */` block comment naming `input.turn` counts (only `//`-prefixed
        # lines are skipped). It is a ratchet against drift, not a proof.
        "ui-widgets-read-input-directly",
        ("src/ui/widgets/{oled,grid}/**", "src/ui/widgets/{oled,grid}/frame.{h,cpp}"),
        "symbol",
        r"\binput\.(turn|edit|horizontal|vertical|select_pressed|back_pressed|shift|now_ms)\b",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # The shell reads no wall clock of its own -- every tick and gate deadline is
        # driven by the `now_ms` the caller passes in, so the shell can be driven by a fake clock
        # in a spec exactly as it is driven by the real one on device. Two rows, one glob each
        # (this checker matches one glob per rule row): src/ui/shell/** and src/ui/widgets/** both apply.
        "shell-no-clock",
        "src/ui/shell/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # Same rule, src/ui/widgets/** side -- the scenes and widgets that draw on the shell's ticks must
        # be exactly as clock-free as the shell that drives them.
        "shell-no-clock",
        "src/ui/widgets/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # The gold knobs' focus is addresses (View::knobFocus()); a gesture resolves the owner's stack when it acts
        # (bridge::knobOwnerStackAt). No legacy file keeps the knobs a long-lived stack.
        "views-no-knob-stack",
        "src/deluge/**",
        "symbol",
        r"\bactiveModControllableModelStack\b",
        "2026-10-18-scenes-bind-to-model-design.md",
    ),
    (
        # Code names none of ModControllableAudio and the GlobalEffectable classes: an owner's effects are an
        # EffectsOwner value (model/fx/effects_owner.h), found by address or, from a ModelStack, through
        # model::effects_owner_of on its typed owner. One row per tree: the checker matches one glob per row.
        "no-legacy-effects-owners",
        "src/**",
        "symbol",
        r"\b(ModControllableAudio|GlobalEffectable(ForClip|ForSong)?)\b",
        "2026-11-13-controls-layer-design.md",
    ),
    (
        "no-legacy-effects-owners",
        "tests/**",
        "symbol",
        r"\b(ModControllableAudio|GlobalEffectable(ForClip|ForSong)?)\b",
        "2026-11-13-controls-layer-design.md",
    ),
    (
        # An owner's params are named by its typed owner (model::LaneOwner): no untyped owner class, its conversion,
        # or valueChangedEnoughToMatter. One row per tree: the checker matches one glob per row.
        "no-mod-controllable",
        "src/**",
        "symbol",
        r"\b(ModControllable|toModControllable|valueChangedEnoughToMatter)\b",
        "2026-11-25-modelstack-stage1-design.md",
    ),
    (
        "no-mod-controllable",
        "tests/**",
        "symbol",
        r"\b(ModControllable|toModControllable|valueChangedEnoughToMatter)\b",
        "2026-11-25-modelstack-stage1-design.md",
    ),
    (
        # The engine stack names left in the param path, counted per file: the count may only fall. modulation/ still
        # threads stacks through playback and recording (ModelStack stage 2); it moves out of src/deluge once its
        # count is zero. The delay hook and the playhead move's deferred notify take a lane (bridge::LaneTarget) and
        # stay at zero. One row per glob: the checker matches one glob per row.
        "no-new-model-stack-in-params",
        "src/deluge/modulation/**",
        "symbol",
        r"\bModelStack\w*\b",
        "2026-11-25-modelstack-stage1-design.md",
    ),
    (
        "no-new-model-stack-in-params",
        "src/deluge/model/mod_controllable/delay_type.*",
        "symbol",
        r"\bModelStack\w*\b",
        "2026-11-25-modelstack-stage1-design.md",
    ),
    (
        "no-new-model-stack-in-params",
        "src/deluge/playback/move_section.*",
        "symbol",
        r"\bModelStack\w*\b",
        "2026-11-25-modelstack-stage1-design.md",
    ),
    (
        # The host chain's param table and pages are the deluge_host_chains library (host/chains), named for
        # what they serve. One row per tree: the checker matches one glob per row.
        "no-global-effectable-table",
        "src/**",
        "symbol",
        r"\bglobal_effectable\b|\bdeluge_global_effectable\b",
        "2026-11-15-effect-param-tables-design.md",
    ),
    (
        "no-global-effectable-table",
        "tests/**",
        "symbol",
        r"\bglobal_effectable\b|\bdeluge_global_effectable\b",
        "2026-11-15-effect-param-tables-design.md",
    ),
    (
        # A scene holds no engine object: a menu's context and the gold knobs' focus hold addresses into the model,
        # and their reads come from the records and the runtime facts.
        "scenes-no-engine-pointers",
        "src/ui/scenes/**",
        "symbol",
        (
            r"\b(Sound|ParamManager\w*|Output|ArpeggiatorSettings|Source|MultiRange|AudioClip"
            r"|InstrumentClip)\s*\*"
        ),
        "2026-10-18-scenes-bind-to-model-design.md",
    ),
    (
        # No scene names `currentSong`: the live code takes the Song& from MenuContext. Baseline 0
        # (shrink-only).
        "scenes-currentSong",
        "src/ui/scenes/**",
        "symbol",
        r"\bcurrentSong\b",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Menus draw only through widgets: no direct OledFrame primitive calls from the scene
        # layer. The real risk is `f.<primitive>`, not `canvas.` -- OledFrame exposes
        # pixel/hline/rect/glyphs directly (`ui/widgets/oled/frame.h`).
        #
        # A ratchet against drift, not a proof: this only sees the two local names it lists, so a
        # primitive reached through some other local (`region.pixel(...)`, say) slips past. Same
        # caveat as ui-widgets-read-input-directly -- the row catches the habit, not every spelling.
        "scenes-no-frame-primitives",
        # `src/ui/scenes/**`, not just `menu/**`: the rule is about the scenes layer (`rename/`, for
        # one, draws its box through `ui::widgets::oled::popup_frame` rather than `f.rect`, as this row asks).
        "src/ui/scenes/**",
        "symbol",
        r"\b(canvas|f)\.(pixel|hline|vline|line|rect|invert|clear|glyph)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # A THIRD shell-no-clock row: the checker matches one glob per row, which is why this rule
        # is already two rows (src/ui/shell/** above, src/ui/widgets/** above). The scenes layer must be
        # exactly as clock-free as the shell and toolkit layers it sits on top of.
        "shell-no-clock",
        "src/ui/scenes/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # The action seam, stated as a rule rather than as a convention. Five menu items
        # perform an action that legacy does by opening or reconfiguring a ROOT UI --
        # `performanceView`, `sessionView`'s macro editor, the MIDI-learn overlay. The
        # scenes layer may not do that: it files a `MenuAction` (`menu/action.h`) into
        # `MenuState`, drained where `pending_scene` drains, and the application-side wiring
        # performs it. This row is what makes the seam necessary rather than merely tidy.
        #
        # It must be a SYMBOL row, not an include row: `scenes-includes-gui` (scripts/layers.toml) already
        # forbids `#include "gui/ui/session_view.h"`, but this library routinely names a legacy
        # global through a bare `extern` declaration instead of its header (`extern uint32_t
        # jcong;`, `nodes/settings.cpp`) precisely because the include rule forbids the
        # header -- so an include row alone cannot see the call.
        #
        # `getRootUI()`/`getCurrentUI()` are listed alongside the view globals because reading which root UI is current
        # is the same coupling as calling into it: main's `isRelevant() == (getRootUI() == &sessionView)` predicates
        # (`origin/main:src/deluge/gui/menu_item/song/midi_learn.h`) are ported as `MenuContext` questions, not as
        # root-UI reads.
        #
        # Same ratchet-not-proof caveat as the rows above: it sees these spellings, not every
        # possible one.
        #
        # The identifier list is `ROOT_UI_GLOBALS` / `UI_STACK_CALLS` below, and `--audit-symbols`
        # checks every name in it is a symbol the source declares: a ratchet listing a name that does
        # not exist (`performanceSessionView` for `performanceView`, say) guards nothing while
        # reading as though it does.
        "scenes-no-root-ui",
        "src/ui/scenes/**",
        "symbol",
        r"\b("
        + "|".join(ROOT_UI_GLOBALS)
        + r")\b|\b("
        + "|".join(UI_STACK_CALLS)
        + r")\(",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Same rule, `src/ui/shell/**` side -- one glob per row, as `shell-no-clock` is three rows and
        # `scenes-no-legacy-waveform` two. `deluge_app` links `deluge_ui_shell` `PUBLIC`
        # (`src/deluge/CMakeLists.txt`), so a bare `extern SessionView sessionView;` in `src/ui/shell`
        # resolves, and `shell-includes-outside` is an INCLUDE row that a bare `extern` walks
        # straight past -- the same gap `scenes-no-root-ui` closes one layer up. The shell runs
        # inside the shipping firmware as `ShellHostUI`'s guest, so a shell file naming
        # `sessionView` would be naming the very object the guest replaces, in the same image.
        "scenes-no-root-ui",
        "src/ui/shell/**",
        "symbol",
        r"\b("
        + "|".join(ROOT_UI_GLOBALS)
        + r")\b|\b("
        + "|".join(UI_STACK_CALLS)
        + r")\(",
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # The notify seam's half of the same argument. `model-calls-display` above holds the
        # model layer to this; NON_UI does not cover `src/ui/scenes`, so this row does. A scene
        # reports to the user through `notify::` and `Action::Kind::Popup` (the rename refusal,
        # for one), never by reaching for the global `display`.
        "scenes-no-display",
        "src/ui/scenes/**",
        "symbol",
        r"\bdisplay->",
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        # The ONE arm of `dispatchShellActions` no other row can see.
        # See `STEM_EXPORT_PROCESS_CALLS` above for why: `processing/**` is on
        # `scenes-includes-outside`' allow-list, so unlike every other verb's target this one is
        # reachable from `src/ui/scenes` through an include the rules already permit -- and the layer
        # really does use it, for the seven stem-export toggles in `menu/nodes/roots.cpp`.
        #
        # `src/ui/scenes/**` only, and one glob per row as ever. `src/ui/shell/**` is not in scope
        # because `shell-includes-outside` refuses `processing/` outright and the shell has no
        # `StemExport` declaration to name; `src/ui/widgets/**` likewise.
        #
        # Same ratchet-not-proof caveat as every symbol row here: it sees these four spellings, a
        # call through a member function pointer slips past, and `is_comment_line` skips only
        # whole-line comments. What it catches is a scene deciding to do the export itself instead
        # of filing `MenuAction::StartStemExport` -- which is the habit the seam exists to prevent.
        "scenes-no-stem-export-process",
        "src/ui/scenes/**",
        "symbol",
        r"\b(" + "|".join(STEM_EXPORT_PROCESS_CALLS) + r")\b",
        "2026-08-26-ui-migration-design.md",
    ),
    (
        # The async listing seam, stated as a rule. `storage::FileCatalogue` holds ONE shared
        # `fileItems` vector and ONE single-flight claim, and the claim is what stops a scenes
        # listing and a legacy `Browser` listing from interleaving over it (the guard is on the
        # resource, so both dispatch paths claim through the same object). `browser::Listing` is the only thing in this layer that claims, so it must
        # be the only thing in this layer that dispatches -- a scene calling
        # `FileCatalogue::readFileItemsForFolder` for itself would refill the vector with no claim
        # held, exactly the exposure the guard exists to close.
        #
        # It is deliberately a SYMBOL row rather than an include row: `browser_scene.cpp` already
        # includes `storage/file_catalogue.h` legitimately (it renders `fileItems` and searches
        # them on a landed result), so gating the include would gate the wrong thing. The
        # alternatives are `CATALOGUE_DISPATCH_CALLS` above, which records why the read-only
        # spellings are excluded.
        #
        # Same ratchet-not-proof caveat as the rows above: a dispatch reached through some other
        # spelling (a member function pointer, say) slips past. It catches the habit.
        "scenes-listing-through-the-seam",
        (
            "src/ui/scenes/**",
            "src/ui/scenes/browser/listing.cpp",
            "src/ui/scenes/browser/listing.h",
        ),
        "symbol",
        r"\b(" + "|".join(CATALOGUE_DISPATCH_CALLS) + r")\b",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # The waveform split, scenes side. See `LEGACY_WAVEFORM_GLOBALS` above for why the
        # split exists and why this must be a symbol row.
        "scenes-no-legacy-waveform",
        "src/ui/scenes/**",
        "symbol",
        r"\b(" + "|".join(LEGACY_WAVEFORM_GLOBALS) + r")\b",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Same rule, `src/ui/widgets/**` side -- one glob per row, as `shell-no-clock` is three rows. The
        # toolkit is where the shortcut would be most tempting: `ui/widgets/grid/markers.cpp` and
        # `ui/widgets/oled/sample_position.cpp` draw exactly what `WaveformRenderer` draws, and calling the
        # legacy renderer instead would be both a layer violation and a silent un-porting.
        "scenes-no-legacy-waveform",
        "src/ui/widgets/**",
        "symbol",
        r"\b(" + "|".join(LEGACY_WAVEFORM_GLOBALS) + r")\b",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # The services' boundary, stated as a rule. See `LEGACY_HID_DRIVERS` above for what the
        # five spellings are, why the other seven of `model-calls-ui`'s alternatives are not
        # repeated here, and why `PadLEDs::flashCursor` is excluded by name.
        #
        # Same ratchet-not-proof caveat as every symbol row: it sees these spellings, not every
        # possible one, and `is_comment_line` skips only whole-line comments.
        "scenes-no-legacy-hid-drivers",
        "src/ui/scenes/**",
        "symbol",
        LEGACY_HID_DRIVER_RE,
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # Same rule, `src/ui/widgets/**` side -- one glob per row, as `shell-no-clock` is three rows and
        # `scenes-no-legacy-waveform` is two. The toolkit half matters because `Rainfall` and
        # `Starfield` (`src/ui/widgets/oled/`) are ports of code from `hid/display/`, the directory
        # `indicator_leds` and `PadLEDs` live in, where those calls were allowed.
        "scenes-no-legacy-hid-drivers",
        "src/ui/widgets/**",
        "symbol",
        LEGACY_HID_DRIVER_RE,
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # ...and the `src/ui/shell/**` side, on the argument the `scenes-no-root-ui` shell row above
        # sets out. It matters more here than the root-UI half, because these five are exactly what
        # the shell's own seams replace -- `SurfaceSet::set_led`/`set_indicator` for
        # `indicator_leds::` and `PadLEDs::`, `Service::next_timer()` for `uiTimerManager.`, and the
        # `RepaintGate`s for `uiNeedsRendering`/`renderUIsForOled`. A shell file reaching for one is
        # reaching around its own hardware seam, which is the one thing `src/ui/shell` is for.
        #
        # `src/ui/shell/board_surfaces.cpp` is deliberately NOT excluded: it binds the sinks to
        # `libdeluge`, not to legacy's `hid/`, and it has zero hits.
        "scenes-no-legacy-hid-drivers",
        "src/ui/shell/**",
        "symbol",
        LEGACY_HID_DRIVER_RE,
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # No scene raises `Action::Kind::LockInput`. It is the shell's own
        # word (`ui/shell/scene.h`'s `Action` enum carries the warning), and the scenes library has
        # @warnings saying "must not, and does not" (`browser/listing.h`, `browser/browser_scene.h`,
        # `marker/marker_scene.h`, `recorder/recorder_scene.h`, `overlay/count_in_scene.h`); this
        # row is the mechanism behind them.
        #
        # The reason it matters is in `listing.h`: a scene that takes the lock across an operation
        # with no cancel and no timeout leaves the whole UI deaf.
        # `src/ui/shell/**` is NOT in scope -- the shell defines the enum and `Router::route` is what
        # honours it (`router.cpp`'s `case Action::Kind::LockInput`).
        "scenes-no-input-lock",
        "src/ui/scenes/**",
        "symbol",
        r"\bLockInput\b",
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # A tripwire for the services' OLED timer claim.
        #
        # `Shell::tick`'s step 8b skips a SCENE whose surface is covered (`Shell::oled_paintable`),
        # so a scene's OLED claim is checked by machine. Step 8c cannot do that for a SERVICE --
        # a service has no stack index and draws nothing -- so a service claiming `oled` arms
        # EVERY OLED surface, every period, unconditionally. An OLED surface blits whenever it
        # rendered (`Shell::tick` step 5 sets `blit_pending` on any successful `render_oled`; there
        # is no content diff, unlike the grid's `last_sent`), so the claim IS the blit rate -- an
        # unconditional claim is a 50-100 Hz two-panel blit -- and its only other machine check is
        # one leg of `tests/unit/ui/shell/flush_spec.cpp`.
        #
        # `Service::timer_repaints`'s doc (`src/ui/shell/service.h`) says all of that and says "claim
        # `oled` only for a timer whose deadline really does change OLED content" -- but nothing
        # made the next service author read it, which is exactly the gap this row closes. It is a
        # RATCHET, not a ban: a service that genuinely needs an OLED repaint gets one, by an agreed
        # `--update` with the argument in the commit message and a `flush_spec` leg to match. What
        # it stops is the claim arriving as a habit.
        #
        # SCOPE. It is `src/ui/shell/**` AND `src/ui/scenes/services/**`, less `src/ui/shell/scene.h`:
        #
        # * `src/ui/shell/**` because `shell::Notices` and `shell::Screensaver` are live `Service`
        #   implementations too. `audit_service_scope()` below fails the build for a `Service`
        #   declared outside this scope.
        # * `src/ui/shell/scene.h` is excluded because it DEFINES `RepaintSurfaces` and is where
        #   `Scene::timer_repaints()`'s `{true, true}` base body lives. It declares no `Service`.
        #
        # Still NOT `src/ui/scenes/**`: four SCENES (`ParamPopupScene`, `CountInScene`,
        # `ScreensaverScene`, and the menu's own) legitimately claim `{.oled = true}` and have
        # `oled_paintable` standing behind them.
        #
        # REGEX. `return {.grid = true};` and `return {};` are SAFE (`RepaintSurfaces`' members
        # default to `false`; `ui/shell/scene.h`'s `static_assert`s pin it), so this row does not
        # have to catch them. What it must catch is every remaining way to arm the OLED:
        #
        # * `.oled = <anything not literally false>` -- the designated initializer, and the
        #   field-by-field local (`RepaintSurfaces s; s.oled = flashing;`). The lookahead is `(?!\s*false\b)` and not `(?!false\b)`: `\s*` before it can
        #   backtrack to zero width, so the shorter form matches `.oled = false` and reports it.
        # * a POSITIONAL brace, `{true, ...}`, whose first member is `oled` -- `return {true, ...}`
        #   in a body, `RepaintSurfaces{true, ...}` anywhere.
        #
        # What it still cannot see is a claim computed elsewhere and returned by name
        # (`return my_claim;`). That is the residual, and it is why the type change is the fix and
        # this row is the tripwire rather than the other way round.
        "services-no-oled-timer-claim",
        ("src/{ui/shell,ui/scenes/services}/**", "src/ui/shell/scene.h"),
        "symbol",
        r"\.oled\s*=\s*(?!\s*false\b)|RepaintSurfaces\s*\{\s*true\b|return\s*\{\s*true\b",
        "2026-08-26-ui-shell-design.md",
    ),
]
# Every row is a symbol row: the include rows live in scripts/layers.toml, which scripts/check_layers.py checks.
assert all(kind == "symbol" for _, _, kind, _, _ in RULES)

# A class declaring itself a `shell::Service`, in any of the spellings the source uses:
# `: public Service`, `: public deluge::ui::shell::Service`, and as the second base
# (`MidiLearn` is `: public deluge::ui::shell::Service, public LearnMode`).
SERVICE_DECL_RE = re.compile(r"public\s+(?:\w+\s*::\s*)*Service\b")
# The row `audit_service_scope` guards, and which files it is allowed not to cover.
SERVICE_ROW = "services-no-oled-timer-claim"
# Matches both #include "path" and #include <path>: a symbol row skips every include line, whichever
# spelling it uses (`count_file`).
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')


def glob_match(rel: str, pattern: str) -> bool:
    # expand {a,b}
    m = re.search(r"\{([^}]*)\}", pattern)
    if m:
        return any(
            glob_match(rel, pattern[: m.start()] + alt + pattern[m.end() :])
            for alt in m.group(1).split(",")
        )
    return fnmatch(rel, pattern)


def rule_covers(rel: str, frm) -> bool:
    """Does rule scope `frm` cover file `rel`?

    A plain string is one include glob. A tuple is `(include, *excludes)`: the file must match
    the include glob and none of the excludes -- for a rule that holds across a directory
    except for the file that implements the thing being ratcheted.
    """
    if isinstance(frm, tuple):
        include, *excludes = frm
        return glob_match(rel, include) and not any(
            glob_match(rel, e) for e in excludes
        )
    return glob_match(rel, frm)


def is_comment_line(line: str) -> bool:
    """Is this line nothing but comment text, for the purposes of a SYMBOL rule?

    `//` and `///` cover the line-comment spellings. ` * ` and ` */` cover a C/Doxygen block
    comment's continuation and close, so a `/** ... * waveformRenderer ... */` block does not
    count against a symbol row.

    Deliberately NOT a general comment stripper. A `/*`...`*/` state machine, or removing the
    trailing text of a `code(); // mentions waveformRenderer` line, would change what the *existing*
    baseline counts -- and this checker's baseline may only shrink, so a change that moves a count
    is a change that has to be argued separately. `*` is matched only when the line is bare `*` or
    the star is followed by a space or a slash, so a dereference (`*prevPointer = ...`) stays code.
    """
    s = line.lstrip()
    if s.startswith("//"):
        return True
    if s.startswith("/*"):
        # A `/*` that closes on the same line is a comment *and*, possibly, code after it:
        # `/* why */ waveformRenderer.render(...)` mentions the symbol in a call. Only a line that
        # leaves the comment open is certainly comment all the way to its end. (`*/ code()` has
        # the same shape and is left counted, which is the safe direction.)
        return "*/" not in s[2:]
    return s == "*" or s.startswith(("*/", "* "))


def count_file(path: Path) -> dict[str, int]:
    """Count each rule's hits in one file.

    A SYMBOL rule skips two kinds of line: a comment (`is_comment_line` above) and an
    `#include` directive. A symbol row matches whole words, so without the include skip a path
    with a `slicer/` directory in it would trip `scenes-no-root-ui`'s `slicer` -- a
    directory name, not the coupling the row exists to forbid.

    It loses nothing, and that is checkable rather than argued. Every symbol row here is
    scoped to a directory that an include rule in scripts/layers.toml already gates: `scenes-no-root-ui` and
    `scenes-no-legacy-waveform` cover `src/ui/scenes/**`, where `scenes-includes-gui` already
    forbids `gui/` and `hid/` outright, so a real `#include "gui/ui/slicer.h"` still fails
    scripts/check_layers.py -- under the rule that names the actual violation, with the actual spec. What
    a symbol row uniquely catches is the spelling an include row cannot see: the bare
    `extern Slicer slicer;` a scene would write precisely BECAUSE the header is forbidden.
    That spelling is untouched by this skip.

    Measured, not assumed: applying the skip moved ZERO of the baseline's 120 (file, rule)
    counts, tree-wide. Re-run it the same way if this predicate changes --
    `--list` before, `--list` after, `diff`.
    """
    rel = str(path.relative_to(REPO))
    counts: dict[str, int] = {}
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for name, frm, kind, rx, _ in RULES:
        if not rule_covers(rel, frm):
            continue
        r = re.compile(rx)
        n = 0
        for line in text:
            if (
                r.search(line)
                and not is_comment_line(line)
                and not INCLUDE_RE.match(line)
            ):
                n += 1
        if n:
            counts[name] = n
    return counts


# The trees the rules are counted over: the firmware, and the specs and tests that drive it (only
# `no-legacy-effects-owners` and `no-mod-controllable` have rows outside `src/`).
SCAN_ROOTS = ("src", "tests")


def current() -> dict[tuple[str, str], int]:
    out = {}
    vendored = fetched_vendor(REPO)
    for p in sorted(p for root in SCAN_ROOTS for p in (REPO / root).rglob("*")):
        if p.suffix in SUFFIXES and p.is_file() and not vendored(p):
            for rule, n in count_file(p).items():
                out[(str(p.relative_to(REPO)), rule)] = n
    return out


def load_baseline() -> dict[tuple[str, str], int]:
    out = {}
    if BASELINE.exists():
        for line in BASELINE.read_text().splitlines():
            if line and not line.startswith("#"):
                f, r, n = line.split("\t")
                out[(f, r)] = int(n)
    return out


# Every named-identifier row, as (rule name, names, the directory whose declarations they came
# from). `--audit-symbols` reads each directory and checks each name really appears there.
AUDITED_SYMBOLS = (
    ("scenes-no-root-ui", (*ROOT_UI_GLOBALS, *UI_STACK_CALLS), "src/deluge/gui"),
    ("scenes-no-legacy-waveform", LEGACY_WAVEFORM_GLOBALS, "src/deluge/gui/waveform"),
    ("scenes-listing-through-the-seam", CATALOGUE_DISPATCH_CALLS, "src/deluge/storage"),
    (
        "scenes-no-stem-export-process",
        STEM_EXPORT_PROCESS_CALLS,
        "src/deluge/processing",
    ),
    # `src/deluge` rather than a narrower root, and deliberately: the five span three legacy
    # directories -- `indicator_leds`/`PadLEDs` are `hid/led/`, `uiTimerManager` is
    # `gui/ui_timer_manager.h` but is used throughout `hid/display/`, and `uiNeedsRendering`/
    # `renderUIsForOled` are `gui/ui/ui.h`. No single directory declares all five, and a root
    # that covered only some of them would report the rest as "declared nowhere".
    ("scenes-no-legacy-hid-drivers", LEGACY_HID_DRIVERS, "src/deluge"),
)


def audit_symbols() -> int:
    """Check every named row's alternatives are symbols the source actually declares.

    A ratchet row that lists a name nothing declares reads as protection and provides none --
    as a misspelt `performanceView` or `readFileItemsForFolder` would silently be. The test is deliberately crude (does
    the identifier appear anywhere under the directory it was copied from?) because the failure it
    catches is crude: a name invented rather than copied.
    """
    bad = 0
    total = 0
    vendored = fetched_vendor(REPO)
    for rule, names, root in AUDITED_SYMBOLS:
        haystack = "\n".join(
            p.read_text(encoding="utf-8", errors="replace")
            for p in sorted((REPO / root).rglob("*"))
            if p.suffix in SUFFIXES and p.is_file() and not vendored(p)
        )
        for sym in names:
            total += 1
            if not re.search(rf"\b{re.escape(sym)}\b", haystack):
                print(f"{rule} lists '{sym}', which {root} declares nowhere")
                bad += 1
    print(f"audited {total} named ratchet symbols, {bad} not found")
    return 1 if bad else 0


def audit_service_scope() -> int:
    """Check `services-no-oled-timer-claim`'s glob still covers every `Service` in the tree.

    Services live in both `src/ui/scenes/services/` and `src/ui/shell/` (`shell::Notices`,
    `shell::Screensaver`); a glob that missed one of them would protect only some of the
    services. This makes a service declared outside the glob a build failure instead of a
    discovery.

    Deliberately crude, in the same way `audit_symbols` is: it asks whether a file that declares a
    `Service` subclass is covered by the row's scope, not whether the class is correct. A new
    service in a directory the row cannot see is exactly the failure it catches.

    Scope note: only `src/` is walked, matching `current()`. Test doubles under `tests/` derive from
    `Service` too and are intentionally out of the ratchet -- a spec claiming
    the OLED is how the OLED branch gets covered at all (`tests/unit/ui/shell/flush_spec.cpp`).
    """
    frm = next(f for name, f, *_ in RULES if name == SERVICE_ROW)
    bad = 0
    found = 0
    vendored = fetched_vendor(REPO)
    for p in sorted((REPO / "src").rglob("*")):
        if p.suffix not in SUFFIXES or not p.is_file() or vendored(p):
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        if not any(
            SERVICE_DECL_RE.search(line)
            for line in text.splitlines()
            if not is_comment_line(line)
        ):
            continue
        found += 1
        rel = str(p.relative_to(REPO))
        if not rule_covers(rel, frm):
            print(
                f"{rel} declares a shell::Service but is outside {SERVICE_ROW}'s scope "
                f"({frm}) -- widen the glob in scripts/check_layer_rules.py"
            )
            bad += 1
    print(
        f"audited {found} file(s) declaring a shell::Service, {bad} outside the row's scope"
    )
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--audit-symbols", action="store_true")
    a = ap.parse_args()
    if a.audit_symbols:
        return audit_symbols() or audit_service_scope()
    if not a.list and audit_service_scope() != 0:
        return 1
    cur = current()
    if a.list:
        for (f, r), n in sorted(cur.items()):
            print(f"{f}\t{r}\t{n}")
        return 0
    if a.update:
        BASELINE.write_text(
            "# Layer-rule baseline: file<TAB>rule<TAB>count. May only shrink.\n"
            + "".join(f"{f}\t{r}\t{n}\n" for (f, r), n in sorted(cur.items()))
        )
        print(f"wrote {len(cur)} entries")
        return 0
    base = load_baseline()
    bad = 0
    for key, n in cur.items():
        if n > base.get(key, 0):
            f, r = key
            # By (name, scope), not by name alone: a rule that needs more than one glob is spelled
            # as several rows sharing a name (`shell-no-clock` is three, `scenes-no-legacy-waveform`
            # two), and those rows may cite different specs. Matching on the name alone always
            # reports the FIRST row's spec, which is wrong for every later row.
            spec = next(
                s for name, frm, *_, s in RULES if name == r and rule_covers(f, frm)
            )
            print(
                f"{f}: rule '{r}' count {n} > baseline {base.get(key, 0)} (spec {spec})"
            )
            bad += 1
    if bad:
        print(
            f"{bad} layer-rule regression(s). Remove the new dependency, or (only for an agreed move) run "
            "--update.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
