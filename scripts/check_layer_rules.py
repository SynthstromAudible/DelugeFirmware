#!/usr/bin/env python3
"""Layer-rule ratchet enforcing the UI redesign's dependency direction.

Each rule is (name, from_glob, kind, regex, spec). `kind` is "include" (match the include
path) or "symbol" (match any source line). The baseline holds counts per (file, rule) and
may only shrink: a PR that raises a count, or adds a new (file, rule) pair, fails.

  scripts/check_layer_rules.py            # check (exit 1 on regression)
  scripts/check_layer_rules.py --list     # print current counts
  scripts/check_layer_rules.py --update   # rewrite the baseline from the tree
  scripts/check_layer_rules.py --audit-symbols  # every named row's identifiers really exist
"""

import argparse
import re
import sys
from fnmatch import fnmatch
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "scripts" / "layer_rules_baseline.txt"
SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
NON_UI = "src/deluge/{model,playback,processing,storage,io,modulation,memory,sync,util,dsp}/**"

# The `scenes-no-root-ui` row's alternatives, named rather than inlined so `--audit-symbols` can
# check that each one is a symbol this tree actually has. Every entry below was taken from an
# `extern <T> <name>;` declaration in `src/deluge/gui/views/*.h`, `src/deluge/gui/ui/**/*.h` or
# `src/deluge/gui/context_menu/*.h` whose type is a `UI` subclass -- i.e. from the declarations,
# not from memory. (The third directory is F2 task 9's; see the two entries at the end.)
#
# `view` (`View view`, `gui/views/view.h`) is deliberately NOT here: as a whole-word pattern it
# would match the English word in prose and any local named `view`, and its false-positive rate
# would make the row unusable. `soundEditor` is here even though it is not a root UI, because it
# is the legacy menu driver this whole library replaces; naming it is always a mistake.
ROOT_UI_GLOBALS = (
    # the root views (`gui/views/`)
    "sessionView",
    "arrangerView",
    "automationView",
    "performanceView",
    "instrumentClipView",
    "audioClipView",
    "keyboardScreen",
    # the legacy menu driver (`gui/ui/sound_editor.h:189`)
    "soundEditor",
    # the rename UIs `rename::RenameScene` replaces (`gui/ui/rename/`)
    "renameClipUI",
    "renameOutputUI",
    "renameDrumUI",
    "renameMidiCCUI",
    # the browsers, savers and recorders the eleven `kPendingCustomNodes` wanted when E3 implemented
    # the file/sample family -- listed ahead of that work so the ratchet was already in place for the
    # exact group most likely to reach for them (`gui/ui/browser/`, `gui/ui/save/`, `gui/ui/load/`,
    # `gui/ui/audio_recorder.h`, `gui/ui/sample_marker_editor.h`, `gui/ui/slicer.h`). It worked:
    # E3 tasks 3-7 landed all eleven and named none of these.
    "sampleBrowser",
    "audioRecorder",
    "sampleMarkerEditor",
    "dxBrowser",
    "slicer",
    "loadSongUI",
    "saveSongUI",
    "loadInstrumentPresetUI",
    "saveInstrumentPresetUI",
    "loadPatternUI",
    "savePatternUI",
    "saveKitRowUI",
    "loadMidiDeviceDefinitionUI",
    "saveMidiDeviceDefinitionUI",
    # The two `gui/context_menu/` singletons `dispatchShellActions`' fourth and fifth arms open
    # (F2 tasks 2 and 4). Added by F2 task 9, and the argument is the one this row was built on:
    # `MenuAction::OpenSongMidiLearn` and `OpenSongMacros` exist BECAUSE the scenes layer may not
    # name these two, and until now nothing checked that it did not. Probed rather than assumed --
    # a `namespace probe { extern int midiLearnMode, configureSongMacros; }` plus a use, dropped
    # into `menu/nodes/roots.cpp`, left `check_layer_rules.py` reporting **zero** regressions
    # before this entry and reports both after it.
    #
    # They are `UI` subclasses like every other name here (`ContextMenu : public UI`,
    # `gui/context_menu/context_menu.h:27`), just from a third directory -- which is why the
    # comment above now names three rather than two. `--audit-symbols`' root for this row is
    # `src/deluge/gui`, so both are already inside what it reads.
    #
    # Measured on the tree at the time of writing: zero non-comment hits in `src/scenes/**`,
    # `src/shell/**` or `src/ui/**`, so the baseline does not move. The four hits under
    # `src/scenes` (`menu/action.h:18`, `:19`, `:140`, `:143`, `context_menu/context_menu_scene.h`)
    # are all whole-line `///` comments, which `is_comment_line` skips -- they are the seam's own
    # description of what the dispatcher does, and the row is what keeps them descriptions.
    "midiLearnMode",
    "configureSongMacros",
)
# The `StemExport` PROCESS entry points -- the `scenes-no-stem-export-process` row's alternatives,
# named here for the same reason `CATALOGUE_DISPATCH_CALLS` is: `--audit-symbols` checks each is a
# method this tree really declares (`processing/stem_export/stem_export.h:36`, `:39`, `:40`, `:41`).
#
# Why this needs a row of its own, and why it is the only arm of `dispatchShellActions` that does.
# Every other verb's legacy target is already unreachable from `src/scenes`: `performanceView`,
# `audioClipView`, `soundEditor`, `midiLearnMode` and `configureSongMacros` are `ROOT_UI_GLOBALS`
# above, and `Screensaver::settingsChanged` lives in `hid/display/screensaver.h`, which
# `scenes-includes-gui` and `scenes-includes-outside` BOTH refuse (probed: adding that include to
# `menu/nodes/settings.cpp` raises two regressions). `stemExport` is the exception --
# `processing/**` is on `scenes-includes-outside`' allow-list on purpose, and the scenes layer
# *does* legitimately reach `stemExport`'s members: the seven `ToggleBool`s at
# `menu/nodes/roots.cpp:354-360` read and write `allowNormalization`, `exportMixdown` and five more
# through one macro (`:348`, `:351`), exactly as `menus.cpp:2020-2036` does. So
# `MenuAction::StartStemExport` is the one
# verb whose target the scenes layer could reach through an allowed include, and nothing saw it.
#
# The split is `CATALOGUE_DISPATCH_CALLS`' split, for the same reason: the *settings* stay readable
# and writable, the calls that START, RUN, STOP or ABORT the export are the wiring layer's. A row on
# the bare name `stemExport` would have had a baseline of two and would have banned the toggles.
#
# The five remaining public methods (`startOutputRecordingUntilLoopEndAndSilence`, `stopPlayback`,
# `stopOutputRecording`, `renderWait`, `checkForLoopEnd`) are deliberately NOT here: `stopPlayback`
# in particular is a name `PlaybackHandler` also has, and this row is a whole-word symbol match with
# no receiver in it. Four names that end in `StemExportProcess` cannot collide with anything.
STEM_EXPORT_PROCESS_CALLS = (
    "startStemExportProcess",
    "runStemExportProcess",
    "stopStemExportProcess",
    "abortStemExportProcess",
)
# The `scenes-no-legacy-waveform` row's alternatives, named here for the same reason
# `ROOT_UI_GLOBALS` is: `--audit-symbols` checks each one is a global this tree really declares.
# Both were taken from their `extern` declarations, not from memory:
# `gui/waveform/waveform_renderer.h:65 extern WaveformRenderer waveformRenderer` and
# `gui/waveform/waveform_basic_navigator.h:64 extern WaveformBasicNavigator waveformBasicNavigator`.
#
# E3 task 4 split `gui/waveform/` in two: the *arithmetic* (peak scanning, the scroll/zoom
# navigator) moved verbatim to `model/sample/waveform_scan.h` and
# `model/sample/waveform_navigator.h`, where `src/scenes` may reach it, while the *drawing* stayed
# behind in `gui/waveform/waveform_renderer.h`, which the scenes layer draws for itself through
# `ui/grid/markers.h`. This row is what keeps the split split. It is a SYMBOL row for the same
# reason `scenes-no-root-ui` is: `scenes-includes-gui` already forbids the header, so the way a
# regression would actually be spelled is a bare `extern WaveformRenderer waveformRenderer;`.
LEGACY_WAVEFORM_GLOBALS = (
    "waveformRenderer",
    "waveformBasicNavigator",
)
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
# uncovered for the scenes layer until E4 task 14, and E4 is the plan that made it matter: the
# three services here are the port of `View` (`gui/views/view.cpp`), which is *the* legacy
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
# `holding_save_ = true;  // `:240` currentUIMode = UI_MODE_HOLDING_SAVE_BUTTON` lines in
# `services/global_controls.cpp` would give this row a non-zero baseline out of pure citation
# text. They are also a different concern -- reading a mode, not driving an output -- which the
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
# `flash::Status::readCurrentValue`/`writeCurrentValue` (`gui/menu_item/flash/status.h:26-32`)
# read and write, ported as a value in `menu/nodes/settings.cpp` and recorded there
# (`nodes/settings.h:320-322`). Excluding the spelling rather than the file keeps the other four
# drivers -- and every other `PadLEDs::` member -- gated inside `settings.cpp` too.
LEGACY_HID_DRIVER_RE = (
    r"\b(uiNeedsRendering\(|renderUIsForOled\(|indicator_leds::|"
    r"PadLEDs::(?!flashCursor\b)|uiTimerManager\.)"
)
# The UI-stack entry points (`gui/ui/ui.h:184-197`), matched with a following `(`.
UI_STACK_CALLS = (
    "openUI",
    "closeUI",
    "changeRootUI",
    "getRootUI",
    "getCurrentUI",
    "swapOutRootUILowLevel",
)

# The two legacy methods F3 added FOR the guest and for nothing else -- the claim seam's
# legacy-side half -- and the invariant that keeps them dead with the toggle off.
#
# `SampleBrowser::selectFileForClaim` (`gui/ui/browser/sample_browser.h:99`, defined
# `sample_browser.cpp:401`) points the legacy browser at a directory and a filename, because
# `claimCurrentFile` reads `currentDir` and the selected `FileItem` rather than taking a path.
# `SoundEditor::adoptResolvedContext` (`gui/ui/sound_editor.h:220`, defined
# `sound_editor.cpp:2152`) writes the editor context legacy's own `setup()` would have resolved --
# which under the toggle nothing calls, and which F3 task 4 found by SIGSEGV. Eleven assignments;
# ten cite a `setup()` line and the eleventh is argued at the write.
#
# Why this needs a row rather than review. Both were argued as additive and BOTH ARE DEAD WITH
# THE TOGGLE OFF -- that is the property the whole guest rests on (spec F §2.1: with the toggle
# off the firmware runs the exact legacy statements it ran before), and it holds for exactly one
# reason: their only caller is `shell_choice_dispatch.cpp`, which only `ShellHostUI::timerCallback`
# reaches. A second caller anywhere under `src/deluge/**` -- inside `SampleBrowser` itself, in a
# view, in a future save browser -- would make them live on the OFF leg silently, and nothing
# today would say so. This is `legacy-names-port`'s own argument (`the second edge is a decision
# somebody writes down`) applied to the seam pointing the other way: `legacy-names-port` counts
# legacy files that name PORT symbols; these two are legacy symbols that exist only to be named
# BY the seam, and no existing row can see them.
#
# Spelled as a member-access CALL rather than a bare name, deliberately, and it is what keeps the
# baseline at zero without excluding the files that own the methods: `\.name\(` matches
# `sampleBrowser.selectFileForClaim(` and `soundEditor.adoptResolvedContext(` and does NOT match
# the declaration (`bool selectFileForClaim(`), the definition
# (`void SoundEditor::adoptResolvedContext(`), or a `::`-qualified mention in prose. So the row's
# scope needs exactly one exclusion -- the dispatcher -- and it names the one edge that is argued.
#
# Same ratchet-not-proof caveat as every symbol row here: a reference or pointer receiver
# (`browser.selectFileForClaim(`) still matches because the match is on the member, not the
# object, but a call through a `SampleBrowser*` written `->` would not. `->` is deliberately not
# in the pattern: both objects are global singletons this tree only ever spells with a `.`
# (`gui/ui/browser/sample_browser.h:184 extern SampleBrowser sampleBrowser`,
# `gui/ui/sound_editor.h:239 extern SoundEditor soundEditor`), and adding `->` would be widening
# the row past the mechanism that makes it checkable.
CLAIM_SEAM_CALLS = (
    "selectFileForClaim",
    "adoptResolvedContext",
)
CLAIM_SEAM_RE = r"\.(selectFileForClaim|adoptResolvedContext)\("

# The legacy files F1 authorised to reach into the ported UI -- the `legacy-includes-port` and
# `legacy-names-port` rows' exclusion list, one entry per argued seam. Everything else under
# `src/deluge/**` is held to zero.
#
# Why the list is spelled out file by file rather than as `gui/ui/shell_*`: a glob would silently
# exempt the next `shell_`-prefixed file somebody drops into `gui/ui/`, and the whole value of
# these two rows is that adding a legacy->port edge is a decision somebody has to write down here.
#
# * `deluge.cpp` -- the three boot calls (F1 task 8): `scenes::assigned_node_ids()` and the service
#   install. `src/scenes/registry.h`'s own note is the boot author's copy of this.
# * `gui/ui/shell_host_ui.{h,cpp}` -- the guest itself, `ShellHostUI : UI` (spec F §2.1).
# * `gui/ui/shell_guest_surfaces.{h,cpp}` -- `GuestSurfaceSet`, the `SurfaceSet` that copies into
#   legacy's `OLED::main` and `PadLEDs::image` (F1 task 4).
# * `gui/ui/shell_guest_services.cpp` -- where the three `scenes::services::` singletons are built
#   and handed to `Shell::set_services` (F1 task 8). Its HEADER is deliberately NOT excluded: it
#   names none of this, and leaving it in scope is what keeps that true.
# * `gui/ui/shell_action_dispatch.cpp` -- `dispatchShellActions`, the poll of
#   `MenuScene::take_action()` that `ShellHostUI::timerCallback` runs once per tick (F2 task 1). Its
#   HEADER is deliberately NOT excluded, on `shell_guest_services.h`'s terms: it declares one
#   no-arguments free function and names no port type, and leaving it in scope is what keeps that
#   true.
# * `gui/ui/shell_menu_entry.cpp` -- `openShellMenu`, the one door the fourteen entry points use.
#   Its DECLARATION (`util/shell_menu_entry.h`) is likewise not excluded, and must stay includable
#   from `model/clip/instrument_clip_minder.cpp`, which is why it names no port type (F1 task 9).
# * `gui/ui/sound_editor.cpp` -- `SoundEditor::setShortcutsVersion` mirrors flash byte 60 into
#   `menu::set_shortcuts_version`, covering all four legacy writers in one call (F1 task 8 fix
#   round 1). The one seam that is not part of the guest.
# * `gui/views/performance_view.cpp` -- `syncEditingModeMirror`, the four calls into
#   `menu::set_performance_editing_mode` (F2 task 5). The SECOND seam that is not part of the guest,
#   and it is `sound_editor.cpp`'s exactly: a piece of legacy state the ported menu has to display
#   but may not read, mirrored by its writers. `PerformEditorItem` shows
#   `performanceView.defaultEditingMode`/`editingParam` (`gui/menu_item/performance_session_view/
#   editing_mode.h:35-46`), which are members of a class `scenes-includes-gui` forbids -- and unlike
#   `shortcutsVersion` there is no bare-`extern` escape from a class member, which is what makes the
#   mirror the only shape available rather than the convenient one. Four call sites in one file,
#   each writing nothing but the mirror; `scenes/menu/action.h` is the contract they answer to.
# * `gui/menu_item/performance_session_view/editing_mode.h` -- the FIFTH call into
#   `menu::set_performance_editing_mode` (F2 task 5, review round 1), and the only one outside
#   `PerformanceView`. It is here because the mirror has to survive the toggle being flipped ON:
#   `EditingMode::selectButtonPress` is legacy's own way into an editing mode, it runs with the
#   toggle OFF, and it writes both bools. Without this call the mirror rots while the port is off
#   and `PerformEditorItem` seeds its cursor from the rot at the next flip-on.
#
#   A HEADER rather than a `.cpp`. **Not the first on this list** -- `shell_host_ui.h` and
#   `shell_guest_surfaces.h` are both headers and both predate it, and `shell_host_ui.h` reaches
#   eighteen files under `src`/`sim`/`tests`, far wider than this. (An earlier version of this
#   comment claimed "the first header", which was asserted rather than grepped -- exactly the shape
#   of mistake F2 task 5 exists to correct. The recipe is one line:
#   `grep -c '\.h",' <this list>`.) What is worth stating is the narrow fact rather than a
#   superlative: exactly two translation units include this header (`gui/ui/menus.cpp:154` and
#   `sim/scenes_spec/menu_actions_spec.cpp:62`, with no transitive includers), so the port include
#   it carries is not being smuggled into the tree at large. If it ever gains a `.cpp`, the call
#   belongs there and this entry should move with it.
LEGACY_PORT_SEAMS = (
    "src/deluge/deluge.cpp",
    "src/deluge/gui/ui/shell_host_ui.h",
    "src/deluge/gui/ui/shell_host_ui.cpp",
    "src/deluge/gui/ui/shell_guest_surfaces.h",
    "src/deluge/gui/ui/shell_guest_surfaces.cpp",
    "src/deluge/gui/ui/shell_guest_services.cpp",
    "src/deluge/gui/ui/shell_action_dispatch.cpp",
    # F3 task 1: the noun half of the same seam. It names
    # `deluge::scenes::browser::browser_scene()` for exactly the reason the row above does -- the
    # scene parks a chosen file it may not load, and this is the side of the wall that gets to name
    # `soundEditor`, `AudioEngine`, `slicer`, `PadLEDs` and `instrumentClipView`. Its HEADER
    # (`shell_choice_dispatch.h`) is deliberately NOT listed: it names no `scenes/` type, taking
    # `std::string_view` where the choice's path would go, so the seam is one translation unit wide.
    "src/deluge/gui/ui/shell_choice_dispatch.cpp",
    # This plan's task 2: the pad half of the same seam. It names `keyboardScreen`,
    # `instrumentClipView` and `performanceView` -- the legacy views `SoundEditor::padAction`
    # forwards to (`gui/ui/sound_editor.cpp:1474-1517`) -- for the same reason the row above names
    # `soundEditor`/`AudioEngine`/`slicer`/`PadLEDs`/`instrumentClipView`: this is the side of the
    # wall that gets to name them. Its HEADER (`shell_pad_forward.h`) is deliberately NOT listed: it
    # names no `scenes/` or `shell/` type, taking plain `int32_t` coordinates, so this seam is one
    # translation unit wide -- exactly the line `shell_choice_dispatch.h` draws above.
    #
    # Task 3 adds the sixth arm, `automationView`, and it is the one arm that cannot be a bare
    # forward: `SoundEditor::isEditingAutomationViewParam` reads `SoundEditor` state no guest path
    # writes (`gui/ui/sound_editor.cpp:1564-1590`), so the question is ported rather than called.
    # That port is why this `.cpp` now also names `deluge::scenes::menu_scene()`
    # (`scenes/registry.h`) and reads a focused node's `deluge::scenes::menu::ParamRef`
    # (`scenes/menu/node.h`) -- both `scenes/` names, both covered by this same entry.
    "src/deluge/gui/ui/shell_pad_forward.cpp",
    "src/deluge/gui/ui/shell_menu_entry.cpp",
    "src/deluge/gui/ui/sound_editor.cpp",
    "src/deluge/gui/views/performance_view.cpp",
    "src/deluge/gui/menu_item/performance_session_view/editing_mode.h",
    # This plan's task 4: the note bridge's installer. `src/scenes` may include `model/**`
    # (`src/scenes/CMakeLists.txt:3`), so `Note`/`NoteRow` are not what earns this file its place
    # here -- it is `gui/views/instrument_clip_view.h`, which `src/scenes` may NOT include, and the
    # iterance preset table, `iterancePresets` (`util/lookuptables/lookuptables.cpp:508`), which
    # `src/scenes` may not reach either (it lives in a `.cpp`, not the model header the table's
    # declaration sits in). This is the side of the wall that gets to name `instrumentClipView`
    # itself and that table, the same reason `shell_pad_forward.cpp` above gets to name
    # `instrumentClipView`/`keyboardScreen`/`performanceView`. It names
    # `deluge::scenes::menu::NoteBridge` and `deluge::scenes::menu::set_note_bridge`
    # (`scenes/menu/note_bridge.h`) to install the function pointers those legacy names sit behind.
    # Its HEADER (`shell_note_bridge.h`) is deliberately NOT listed: it names no `scenes/` type,
    # only the no-arguments `wireShellNoteBridge()` that installs the bridge, so this seam too is
    # one translation unit wide -- exactly the line `shell_choice_dispatch.h` and
    # `shell_pad_forward.h` draw above.
    "src/deluge/gui/ui/shell_note_bridge.cpp",
    # Note-family Task 7: the note modes' routing through the guest. It names
    # `deluge::scenes::menu_scene()` (`scenes/registry.h`) and reads its `menu_context().root`
    # (`scenes/menu/menu_scene.h`), and walks `shellHostUI`'s `deluge::ui::shell::SceneStack` for the
    # menu's identity -- the port's side of `SoundEditor::inNoteEditor()` / `inNoteRowEditor()`
    # (`gui/ui/sound_editor.cpp:1916-1922`). And it names `instrumentClipView`, whose note handlers
    # `SoundEditor::buttonAction`, its two encoder hooks and `exitCompletely` forward to
    # (`:617-623`, `:1047-1052`, `:1593-1598`, `:749-756`). That is the same pair of names
    # `shell_pad_forward.cpp` above is listed for, and the same side of the wall.
    #
    # Note-family Task 7a widened what it names on both sides. Legacy side: `soundEditor` (the legacy
    # half of `util/note_editor_open.h`'s two predicates, defined here), `closePortedMenu()`
    # (`gui/ui/shell_action_dispatch.h`) and `runtimeFeatureSettings` (the toggle's `HorizontalMenus`
    # read). Port side: the `deluge::scenes::menu::nodes::*` note and row nodes plus
    # `sequence_direction` (`scenes/menu/nodes/note.h`), and `MenuScene::focused`, `read_value_again`,
    # `go_up_one_level` and `enter_submenu` -- the ports of the note-mode handlers' own
    # `readValueAgain` / `goUpOneLevel` / `toggleNoteEditorParamMenu` calls. Its HEADER
    # (`shell_note_mode.h`) is deliberately NOT listed: it names no `scenes/` or `shell/` type --
    # `NoteModeKind` is its own enum, and every call takes `hid::Button`, integers and bools -- so
    # `shell_host_ui.cpp` and the host spec suite's fake reach it without the `deluge_scenes`
    # closure, and this seam too is one translation unit wide.
    "src/deluge/gui/ui/shell_note_mode.cpp",
)

RULES = [
    (
        # F1's own boundary, and the one this plan CREATED rather than inherited. Until F1 task 1
        # (`eb17d4688`) nothing linked `deluge_scenes` into the firmware or the host sim, so a
        # `#include "scenes/..."` from a legacy translation unit compiled and then failed to
        # resolve at link time -- the build was the enforcement. Task 1 put
        # `libdeluge_scenes.a` into both closures (`src/bsp/rust/build.rs`,
        # `sim/CMakeLists.txt:446`) and that enforcement is gone: any legacy file may now reach
        # into the ported UI and link.
        #
        # The rule this replaces it with is spec F §2.1's: the ported UI enters the firmware
        # through ONE guest (`ShellHostUI`), and the legacy files that may name it are the seams
        # listed in `LEGACY_PORT_SEAMS` above. Everything else under `src/deluge/**` -- 1,000-odd
        # files, `model/`, `storage/`, `playback/`, and the rest of `gui/` -- is held to zero, so
        # the second legacy->port edge is a decision somebody writes into that list.
        #
        # `ui/`, `gfx/` and `notify/` are deliberately NOT in the pattern, and the reason is that
        # they are not F1's seams to police: `hid/display/screensaver.h` includes
        # `ui/oled/rainfall.h` because E4 task 7 MOVED `Rainfall`/`Starfield` there,
        # `gui/colour/rgb.h` re-exports `gfx/grid/rgb.h`, and `util/notify.h` is the notify
        # channel's own legacy-side header. Those three are earlier plans' seams with earlier
        # plans' owners; widening this row over them would be this row claiming more than its
        # mechanism.
        "legacy-includes-port",
        ("src/deluge/**", *LEGACY_PORT_SEAMS),
        "include",
        r"^(scenes/|shell/)",
        "2026-08-26-ui-migration-design.md",
    ),
    (
        # The symbol half of `legacy-includes-port`, for exactly the reason `scenes-no-root-ui` is
        # a symbol row rather than an include row: this tree routinely names a symbol through a
        # bare declaration BECAUSE the include is forbidden (`extern uint32_t jcong;`,
        # `src/scenes/menu/nodes/settings.cpp:78`). A legacy file that wants
        # `deluge::scenes::scene_registry()` and finds the include row in its way can write
        # `namespace deluge::scenes { const SceneTable& scene_registry(); }` and link, because
        # task 1 put the archive in the closure. This row is what sees that spelling.
        #
        # Matched without a trailing `::` so that the namespace-reopening form above counts too,
        # and `\b`-anchored on `deluge::` so it cannot fire on the English words "scenes" and
        # "shell", which this tree's prose is full of. Same ratchet-not-proof caveat as every
        # symbol row here: a `using namespace` plus an unqualified type would slip past, and
        # `is_comment_line` skips only whole-line comments.
        #
        # Measured on the tree at the time of writing: zero hits outside `LEGACY_PORT_SEAMS`, so
        # the baseline does not move.
        "legacy-names-port",
        ("src/deluge/**", *LEGACY_PORT_SEAMS),
        "symbol",
        r"\bdeluge::(scenes|ui::shell)\b",
        "2026-08-26-ui-migration-design.md",
    ),
    (
        # The third row of the same wall, and the only one that points at legacy symbols rather
        # than port ones: `CLAIM_SEAM_CALLS` above is the argument, and the scope is the whole of
        # `src/deluge/**` less the one translation unit the seam was added for.
        #
        # Measured on the tree at the time of writing: `grep -rn` over `src`, `sim` and `tests`
        # finds `selectFileForClaim` and `adoptResolvedContext` at their declarations, their
        # definitions, three prose mentions and exactly two calls, both in
        # `gui/ui/shell_choice_dispatch.cpp` (`:117`, `:137`). So the baseline does not move.
        "legacy-calls-claim-seam",
        ("src/deluge/**", "src/deluge/gui/ui/shell_choice_dispatch.cpp"),
        "symbol",
        CLAIM_SEAM_RE,
        "2026-08-26-ui-migration-design.md",
    ),
    (
        "model-includes-ui",
        NON_UI,
        "include",
        r"^(gui/(?!l10n/)|hid/)",
        "2026-08-26-ui-notifications-design.md",
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
        "gfx-includes-outside",
        "src/gfx/**",
        "include",
        r"^(?!gfx/).*/",
        "2026-08-26-ui-stack-redesign-design.md",
    ),
    (
        "notify-is-leaf",
        "src/notify/**",
        "include",
        r"^(?!notify/).*/",
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        # Slashed paths outside ui/ and gfx/ -- which is every project header except the handful
        # named explicitly below (definitions*, board_config.h, board_layout.hpp): a std header
        # (<cstdint>, <span>) has no slash and no such name, so it still never matches.
        "ui-includes-outside",
        "src/ui/**",
        "include",
        r"^(?!ui/|gfx/)(?:.*/|definitions.*|board_config\.h|board_layout\.hpp)",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        "ui-oled-no-grid",
        "src/ui/oled/**",
        "include",
        r"^ui/grid/",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        "ui-grid-no-oled",
        "src/ui/grid/**",
        "include",
        r"^ui/oled/",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # The input-consumption contract (ui/core/frame_context.h): a widget reads input only
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
        ("src/ui/{oled,grid}/**", "src/ui/{oled,grid}/frame.{h,cpp}"),
        "symbol",
        r"\binput\.(turn|edit|horizontal|vertical|select_pressed|back_pressed|shift|now_ms)\b",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # Mirrors ui-includes-outside for src/shell: only shell/**, ui/**, gfx/**, notify/** and
        # libdeluge/** (the one hardware seam, board_surfaces.cpp/profile.h) may be included --
        # never model/, gui/, hid/, or the root headers (definitions*, board_config.h,
        # board_layout.hpp) that would pull the model layer in by the back door.
        "shell-includes-outside",
        "src/shell/**",
        "include",
        r"^(?!shell/|ui/|gfx/|notify/|libdeluge/)(?:.*/|definitions.*|board_config\.h|board_layout\.hpp)",
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # Spec G §5: the shell reads no wall clock of its own -- every tick and gate deadline is
        # driven by the `now_ms` the caller passes in, so the shell can be driven by a fake clock
        # in a spec exactly as it is driven by the real one on device. Two rows, one glob each
        # (this checker matches one glob per rule row): src/shell/** and src/ui/** both apply.
        "shell-no-clock",
        "src/shell/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # Same rule, src/ui/** side -- the scenes and widgets that draw on the shell's ticks must
        # be exactly as clock-free as the shell that drives them (task-5 ratchet extension).
        "shell-no-clock",
        "src/ui/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # Mirrors shell-includes-outside for src/scenes: the layer's own CMakeLists.txt
        # (src/scenes/CMakeLists.txt) is the authority -- deluge_scenes may include scenes/**,
        # shell/**, ui/**, gfx/**, notify/**, libdeluge/**, and -- being the first UI layer
        # allowed to reach into the model -- model/**, modulation/**, playback/**, processing/**,
        # storage/**, gui/l10n/**, definitions_cxx.hpp. Never gui/ otherwise (gui/l10n/ is the
        # sole exception), never hid/. `definitions_cxx.hpp` sits in the allow-list itself (not
        # the trailing catch-all group, unlike shell-includes-outside's `definitions.*`, which
        # shell must NOT reach): scenes is deliberately the first model-aware UI layer, so the
        # header that pulls in `params::Kind` and friends is expected, not a back door.
        #
        # `dsp/**` joined the list in task 7 (spec E1), on exactly the argument task 3 made for
        # `processing/**` below: `check_layer_rules`'s own NON_UI glob already counts
        # `src/deluge/dsp/` as non-UI, held to the same no-`gui/`, no-`hid/` rules as `model/`. It
        # is needed because the reverb menu's relevance predicates ask
        # `AudioEngine::reverb.getModel()` (`gui/menu_item/reverb/hpf.h:27`) and
        # `processing/engines/audio_engine.h` only forward declares `deluge::dsp::Reverb` -- no
        # `model/` or `processing/` header completes it, so `menu/relevance.cpp` cannot ask the
        # question at all without this.
        #
        # `io/**` joined the list in task 8 (spec E1), on that same argument: `check_layer_rules`'s
        # own NON_UI glob counts `src/deluge/io/` as non-UI, held to the same no-`gui/`, no-`hid/`
        # rules as `model/`. The settings root is where it became unavoidable -- `midiEngine`
        # (`io/midi/midi_engine.h`), `MIDIDeviceManager` (`io/midi/midi_device_manager.h`) and
        # `MIDITranspose` (`io/midi/midi_transpose.h`) hold the state seven of the MIDI page's
        # items edit, and there is no `model/` spelling of any of them.
        #
        # `processing/**` joined the list in task 3 (spec E1). It is not a widening of what
        # "model-aware" means -- `check_layer_rules`'s own NON_UI glob above already counts
        # `src/deluge/processing/` as non-UI, held to the same no-`gui/`, no-`hid/` rules as
        # `model/` -- it is where two of the types spec E §4's `MenuContext` is *defined* in terms
        # of happen to live: `Sound` (`processing/sound/sound.h`) and `Source`
        # (`processing/source.h`). `menu/context.cpp` cannot resolve `sound->sources[i]`,
        # `source->ranges` or `sound->voicePriority` from a forward declaration, so without this
        # the ported `SoundEditor::setup` could not be written at all.
        # NOTE (E2 task 6 fix round 1, Minor 2): the pattern's `(?:.*/)` tail means this row only
        # ever inspects include paths that contain a `/`. A ROOT-LEVEL header -- `definitions_cxx.hpp`,
        # `deluge.h`, `<version.h>` -- is therefore not gated here at all, whether or not it is
        # allowed. That is deliberate rather than merely accidental now: dropping the tail would
        # also flag every standard-library include, and this row has no way to tell `<cstdint>`
        # from `<stdint.h>`. The authoritative list of root-level headers `src/scenes` may include
        # is the comment at the top of src/scenes/CMakeLists.txt, enforced by review.
        "scenes-includes-outside",
        "src/scenes/**",
        "include",
        r"^(?!scenes/|shell/|ui/|gfx/|notify/|libdeluge/|dsp/|io/|model/|modulation/|playback/|processing/|storage/|gui/l10n/|definitions_cxx\.hpp)(?:.*/)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Narrower than the rule above, and the reason `menu::model_stack_for()` exists (spec E1
        # task-3 ruling 6): `model/model_stack.h` may be reached only by the two files that own the
        # model-stack boundary. Everything else in src/scenes goes through `model_stack_for`, so
        # there is exactly one place that knows how a menu's stack is assembled.
        #
        # `menu/bindings.cpp` does not exist yet -- Task 4 adds it; excluding it now means that task
        # needs no edit here. Added in fix round 1 (Minor 8): the restriction was prose in
        # src/scenes/README.md while every other rule in this stack is machine-checked.
        "scenes-model-stack-outside-context",
        (
            "src/scenes/**",
            "src/scenes/menu/context.cpp",
            "src/scenes/menu/bindings.cpp",
        ),
        "include",
        r"^model/model_stack\.h$",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Mirrors ui-includes-outside/shell-includes-outside for the gui/hid split specifically:
        # scenes-includes-outside above already forbids gui/ (except gui/l10n/) and hid/ wholesale,
        # but names every other allowed directory explicitly rather than the two forbidden ones --
        # this row restates the gui/hid half on its own so a reviewer scanning by rule name finds
        # it without re-deriving the negation. The (include, *excludes) glob tuple already exists
        # (`rule_covers`, check_layer_rules.py:775-787, as ui-widgets-read-input-directly uses at
        # :319) -- no checker change needed; this row needs no `except` field of its own.
        #
        # Both numbers in that parenthetical were wrong before F1 task 12 as well as after it, and
        # they are why `SCAN_ROOTS` now reaches `scripts/`: nothing has ever read a citation written
        # in a Python file. At 32f4d4551 `:214-226` was the `ui-grid-no-oled` row and `:98` was a
        # `CATALOGUE_DISPATCH_CALLS` entry -- neither has been `rule_covers` or a glob tuple for
        # several plans. They are left wrong on purpose: the wrongness IS the example, and
        # correcting them would delete it. What a reader should know is that they have since drifted
        # further -- F3 task 5's `CLAIM_SEAM_CALLS` insert moved `rule_covers` from `:885` to `:946`
        # -- and that `check_citation_shift.py` cannot see this, because a self-citation inside the
        # file it cites is compared against the same file's own new text.
        "scenes-includes-gui",
        "src/scenes/**",
        "include",
        r"^(gui/(?!l10n/)|hid/)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Narrower still than scenes-model-stack-outside-context: `modulation/automation/auto_param.h`
        # carries the same "reach into the model stack machinery directly" risk as `model_stack.h`
        # itself, so it is confined to the same two files.
        "scenes-model-stack",
        (
            "src/scenes/**",
            "src/scenes/menu/bindings.cpp",
            "src/scenes/menu/context.cpp",
        ),
        "include",
        r"^(model/model_stack\.h|modulation/automation/auto_param\.h)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Spec E §3 rule 4: `currentSong` is confined to the two files that resolve a MenuContext
        # (context.cpp, bindings.cpp) and even there only inside doc comments describing what was
        # *not* ported -- the live code takes the Song& from MenuContext. Baseline is the counted
        # number of currentSong uses in context.cpp after Task 3: 0 (shrink-only).
        "scenes-currentSong",
        "src/scenes/**",
        "symbol",
        r"\bcurrentSong\b",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Menus draw only through widgets: no direct OledFrame primitive calls from the scene
        # layer. The real risk is `f.<primitive>`, not `canvas.` -- OledFrame exposes
        # pixel/hline/rect/glyphs directly (ui/oled/frame.h:165-269).
        #
        # A ratchet against drift, not a proof: this only sees the two local names it lists, so a
        # primitive reached through some other local (`region.pixel(...)`, say) slips past. Same
        # caveat as ui-widgets-read-input-directly -- the row catches the habit, not every spelling.
        "scenes-no-frame-primitives",
        # `src/scenes/**`, not just `menu/**`: the rule is about the scenes layer, and E2 task 5
        # added a second scene directory (`src/scenes/rename/`) that draws its box through
        # `ui::oled::popup_frame` rather than `f.rect`, exactly as this row asks.
        "src/scenes/**",
        "symbol",
        r"\b(canvas|f)\.(pixel|hline|vline|line|rect|invert|clear|glyph)",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # A THIRD shell-no-clock row: the checker matches one glob per row, which is why this rule
        # is already two rows (src/shell/** above, src/ui/** above). The scenes layer must be
        # exactly as clock-free as the shell and toolkit layers it sits on top of.
        "shell-no-clock",
        "src/scenes/**",
        "symbol",
        r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
        "2026-08-26-ui-test-harness-design.md",
    ),
    (
        # E2 task 11b's action seam, stated as a rule rather than as a convention. Five menu items
        # perform an action that legacy does by opening or reconfiguring a ROOT UI --
        # `performanceView`, `sessionView`'s macro editor, the MIDI-learn overlay. The
        # scenes layer may not do that: it files a `MenuAction` (`menu/action.h`) into
        # `MenuState`, drained where `pending_scene` drains, and the application-side wiring
        # performs it. This row is what makes the seam necessary rather than merely tidy.
        #
        # It must be a SYMBOL row, not an include row: `scenes-includes-gui` above already
        # forbids `#include "gui/ui/session_view.h"`, but this library routinely names a legacy
        # global through a bare `extern` declaration instead of its header (`extern uint32_t
        # jcong;`, `nodes/settings.cpp:78`) precisely because the include rule forbids the
        # header -- so an include row alone cannot see the call.
        #
        # `getRootUI()`/`getCurrentUI()` are listed alongside the view globals because reading
        # which root UI is current is the same coupling as calling into it: legacy's
        # `isRelevant() == (getRootUI() == &sessionView)` predicates (`song/midi_learn.h:34-36`)
        # are ported as `MenuContext` questions, not as root-UI reads. Task 11b's deferred
        # session-vs-arranger split is the one place that costs something, and it is recorded in
        # the spec's E3 carry list rather than papered over here.
        #
        # Same ratchet-not-proof caveat as the rows above: it sees these spellings, not every
        # possible one.
        #
        # The identifier list is `ROOT_UI_GLOBALS` / `UI_STACK_CALLS` below, and `--audit-symbols`
        # is why: E2 task 13 shipped this row spelling the performance view
        # `performanceSessionView`, which is the name of no symbol in this tree (the global is
        # `performanceView`, `gui/views/performance_view.h:207 extern PerformanceView
        # performanceView`) -- so the very global `menu/action.h`'s mirror apparatus exists to
        # avoid touching was the one the row could not see. A ratchet listing a name that does not
        # exist guards nothing while reading as though it does.
        "scenes-no-root-ui",
        "src/scenes/**",
        "symbol",
        r"\b("
        + "|".join(ROOT_UI_GLOBALS)
        + r")\b|\b("
        + "|".join(UI_STACK_CALLS)
        + r")\(",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Same rule, `src/shell/**` side -- one glob per row, as `shell-no-clock` is three rows and
        # `scenes-no-legacy-waveform` two. Added by F1 task 12, and the argument is NOT that F1
        # created the exposure. It did not, and that was checked before the row was written:
        # `deluge_app` has `PUBLIC`-linked `deluge_ui_shell` since `6bf7f1dbf` (plan D's shell
        # skeleton, `src/deluge/CMakeLists.txt:47-62`), an ancestor of F1's own base -- so a bare
        # `extern SoundEditor soundEditor;` in `src/shell` has resolved in every binary that
        # matters for several plans. What is true is that **nothing has ever checked it**:
        # `scenes-no-root-ui` and `scenes-no-legacy-hid-drivers` are scoped to `src/scenes/**` and
        # `src/ui/**` and to neither is `src/shell/**` added, while `shell-includes-outside` is an
        # INCLUDE row that a bare `extern` walks straight past -- which is the same gap, in the same
        # spelling, that `scenes-no-root-ui` exists for one layer up.
        #
        # F1 is only why it became worth closing: the shell now runs inside the shipping firmware as
        # `ShellHostUI`'s guest, so a shell file naming `soundEditor` would be naming the very object
        # the guest replaces, in the same image, at run time.
        #
        # Measured on the tree at the time of writing: zero hits, so the baseline does not move.
        "scenes-no-root-ui",
        "src/shell/**",
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
        # model layer to this; NON_UI does not cover `src/scenes`, so until now nothing did.
        # A scene reports to the user through `notify::` and `Action::Kind::Popup` (E2 task 5's
        # rename-refusal path), never by reaching for the global `display`.
        "scenes-no-display",
        "src/scenes/**",
        "symbol",
        r"\bdisplay->",
        "2026-08-26-ui-notifications-design.md",
    ),
    (
        # F2's own boundary, and the ONE arm of `dispatchShellActions` no existing row could see.
        # See `STEM_EXPORT_PROCESS_CALLS` above for why: `processing/**` is on
        # `scenes-includes-outside`' allow-list, so unlike every other verb's target this one is
        # reachable from `src/scenes` through an include the rules already permit -- and the layer
        # really does use it, for the seven stem-export toggles at `menu/nodes/roots.cpp:354-360`.
        #
        # `src/scenes/**` only, and one glob per row as ever. `src/shell/**` is not in scope
        # because `shell-includes-outside` refuses `processing/` outright and the shell has no
        # `StemExport` declaration to name; `src/ui/**` likewise.
        #
        # Same ratchet-not-proof caveat as every symbol row here: it sees these four spellings, a
        # call through a member function pointer slips past, and `is_comment_line` skips only
        # whole-line comments. What it catches is a scene deciding to do the export itself instead
        # of filing `MenuAction::StartStemExport` -- which is the habit the seam exists to prevent.
        #
        # Measured on the tree at the time of writing: zero hits, so the baseline does not move.
        "scenes-no-stem-export-process",
        "src/scenes/**",
        "symbol",
        r"\b(" + "|".join(STEM_EXPORT_PROCESS_CALLS) + r")\b",
        "2026-08-26-ui-migration-design.md",
    ),
    (
        # E3's async listing seam, stated as a rule. `storage::FileCatalogue` holds ONE shared
        # `fileItems` vector and ONE single-flight claim, and the claim is what stops a scenes
        # listing and a legacy `Browser` listing from interleaving over it (task 2 fix round 2 moved
        # the guard off `Browser` and onto the resource so both dispatch paths claim through the
        # same object). `browser::Listing` is the only thing in this layer that claims, so it must
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
            "src/scenes/**",
            "src/scenes/browser/listing.cpp",
            "src/scenes/browser/listing.h",
        ),
        "symbol",
        r"\b(" + "|".join(CATALOGUE_DISPATCH_CALLS) + r")\b",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # E3 task 4's waveform split, scenes side. See `LEGACY_WAVEFORM_GLOBALS` above for why the
        # split exists and why this must be a symbol row.
        "scenes-no-legacy-waveform",
        "src/scenes/**",
        "symbol",
        r"\b(" + "|".join(LEGACY_WAVEFORM_GLOBALS) + r")\b",
        "2026-08-26-ui-scenes-menus-design.md",
    ),
    (
        # Same rule, `src/ui/**` side -- one glob per row, as `shell-no-clock` is three rows. The
        # toolkit is where the shortcut would be most tempting: `ui/grid/markers.cpp` and
        # `ui/oled/sample_position.cpp` draw exactly what `WaveformRenderer` draws, and calling the
        # legacy renderer instead would be both a layer violation and a silent un-porting.
        "scenes-no-legacy-waveform",
        "src/ui/**",
        "symbol",
        r"\b(" + "|".join(LEGACY_WAVEFORM_GLOBALS) + r")\b",
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # E4's service boundary, stated as a rule. See `LEGACY_HID_DRIVERS` above for what the
        # five spellings are, why the other seven of `model-calls-ui`'s alternatives are not
        # repeated here, and why `PadLEDs::flashCursor` is excluded by name.
        #
        # Same ratchet-not-proof caveat as every symbol row: it sees these spellings, not every
        # possible one, and `is_comment_line` skips only whole-line comments.
        "scenes-no-legacy-hid-drivers",
        "src/scenes/**",
        "symbol",
        LEGACY_HID_DRIVER_RE,
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # Same rule, `src/ui/**` side -- one glob per row, as `shell-no-clock` is three rows and
        # `scenes-no-legacy-waveform` is two. E4 is why the toolkit half is needed: task 7 moved
        # `Rainfall` and `Starfield` out of `hid/display/` into `src/ui/oled/`, so the toolkit now
        # contains code whose ORIGINAL home was the same directory as `indicator_leds` and
        # `PadLEDs`. A future edit restoring one of those calls would be restoring it to a file
        # that used to be allowed to make it.
        "scenes-no-legacy-hid-drivers",
        "src/ui/**",
        "symbol",
        LEGACY_HID_DRIVER_RE,
        "2026-08-26-ui-toolkit-design.md",
    ),
    (
        # ...and the `src/shell/**` side, F1 task 12, on the argument the `scenes-no-root-ui` shell
        # row above sets out in full: the exposure is plan D's, not F1's, and what was missing is
        # the check. It matters more here than the root-UI half, because these five are exactly what
        # the shell's own seams replace -- `SurfaceSet::set_led`/`set_indicator` for
        # `indicator_leds::` and `PadLEDs::`, `Service::next_timer()` for `uiTimerManager.`, and the
        # `RepaintGate`s for `uiNeedsRendering`/`renderUIsForOled`. A shell file reaching for one is
        # reaching around its own hardware seam, which is the one thing `src/shell` is for.
        #
        # `src/shell/board_surfaces.cpp` is deliberately NOT excluded: it binds the sinks to
        # `libdeluge`, not to legacy's `hid/`, and it has zero hits.
        #
        # Measured on the tree at the time of writing: zero hits, so the baseline does not move.
        "scenes-no-legacy-hid-drivers",
        "src/shell/**",
        "symbol",
        LEGACY_HID_DRIVER_RE,
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # Global Constraint 5, which every plan since E2 has restated in prose and no checker has
        # ever held anyone to: no scene raises `Action::Kind::LockInput`. It is the shell's own
        # word (`shell/scene.h`'s `Action` enum carries the warning), and the scenes library has
        # six @warnings saying "must not, and does not" -- `browser/listing.h:83-87` and `:335`,
        # `browser/browser_scene.h:59`, `marker/marker_scene.h:73`, `recorder/recorder_scene.h:144`,
        # `overlay/count_in_scene.h:97`. Six repetitions of a claim with no mechanism behind it is
        # the shape this plan kept finding and fixing, so it gets the mechanism.
        #
        # The reason it matters is in `listing.h:83-87`: a scene that takes the lock across an
        # operation with no cancel and no timeout leaves the whole UI deaf, which E3 shipped once.
        # `src/shell/**` is NOT in scope -- the shell defines the enum and `Router::route` is what
        # honours it (`router.cpp`'s `case Action::Kind::LockInput`).
        "scenes-no-input-lock",
        "src/scenes/**",
        "symbol",
        r"\bLockInput\b",
        "2026-08-26-ui-shell-design.md",
    ),
    (
        # E4 task 5's residual, made into a tripwire rather than left as a docstring.
        #
        # `Shell::tick`'s step 8b skips a SCENE whose surface is covered (`Shell::oled_paintable`),
        # so a scene's OLED claim is checked by machine. Step 8c cannot do that for a SERVICE --
        # a service has no stack index and draws nothing -- so a service claiming `oled` arms
        # EVERY OLED surface, every period, unconditionally. An OLED surface blits whenever it
        # rendered (`Shell::tick` step 5 sets `blit_pending` on any successful `render_oled`; there
        # is no content diff, unlike the grid's `last_sent`), so the claim IS the blit rate. That
        # branch is the one whose scene-side twin shipped a 50 Hz and then a 100 Hz two-panel blit
        # in E3, and its only machine check is one leg of `tests/spec/shell/flush_spec.cpp`.
        #
        # `Service::timer_repaints`'s doc (`src/shell/service.h`) says all of that and says "claim
        # `oled` only for a timer whose deadline really does change OLED content" -- but nothing
        # made the next service author read it, which is exactly the gap this row closes. It is a
        # RATCHET, not a ban: a service that genuinely needs an OLED repaint gets one, by an agreed
        # `--update` with the argument in the commit message and a `flush_spec` leg to match. What
        # it stops is the claim arriving as a habit.
        #
        # SCOPE (E4 final review, I1 -- the row as first written had two holes and caught nothing
        # it was written for). It is `src/shell/**` AND `src/scenes/services/**`, less
        # `src/shell/scene.h`:
        #
        # * `src/scenes/services/**` alone left `shell::Notices` and `shell::Screensaver` -- two
        #   live `Service` implementations -- outside the row entirely, and any future service the
        #   shell owns with them. `audit_service_scope()` below now makes that hole a build
        #   failure rather than something a reviewer has to notice.
        # * `src/shell/scene.h` is excluded because it DEFINES `RepaintSurfaces` and is where
        #   `Scene::timer_repaints()`'s `{true, true}` base body lives. It declares no `Service`.
        #
        # Still NOT `src/scenes/**`: four SCENES (`ParamPopupScene`, `CountInScene`,
        # `ScreensaverScene`, and the menu's own) legitimately claim `{.oled = true}` and have
        # `oled_paintable` standing behind them.
        #
        # REGEX. The first cut was `\.oled\s*=\s*true`, which matched only the spelling nobody was
        # going to get wrong. What it missed was everything the type's own defaults used to arm for
        # you -- `return {.grid = true};` and `return {};` both carried `oled == true` while
        # `RepaintSurfaces`' members defaulted to `true`. Those two spellings are now SAFE rather
        # than merely unmatched (the members are `false`; `shell/scene.h`'s `static_assert`s pin
        # it), so this row does not have to catch them. What it must catch is every remaining way
        # to arm the OLED:
        #
        # * `.oled = <anything not literally false>` -- the designated initializer, and the
        #   field-by-field local (`RepaintSurfaces s; s.oled = flashing;`) the old regex also
        #   missed. The lookahead is `(?!\s*false\b)` and not `(?!false\b)`: `\s*` before it can
        #   backtrack to zero width, so the shorter form matches `.oled = false` and reports it.
        #   (Found by measuring: the shorter form hit `service.h` and `midi_learn.h`, both of which
        #   spell `false`.)
        # * a POSITIONAL brace, `{true, ...}`, whose first member is `oled` -- `return {true, ...}`
        #   in a body, `RepaintSurfaces{true, ...}` anywhere.
        #
        # What it still cannot see is a claim computed elsewhere and returned by name
        # (`return my_claim;`). That is the residual, and it is why the type change is the fix and
        # this row is the tripwire rather than the other way round.
        #
        # Measured on the tree at the time of writing: zero hits, so the baseline does not move.
        "services-no-oled-timer-claim",
        ("src/{shell,scenes/services}/**", "src/shell/scene.h"),
        "symbol",
        r"\.oled\s*=\s*(?!\s*false\b)|RepaintSurfaces\s*\{\s*true\b|return\s*\{\s*true\b",
        "2026-08-26-ui-shell-design.md",
    ),
]

# A class declaring itself a `shell::Service`, in any of the spellings this tree uses:
# `: public Service`, `: public deluge::ui::shell::Service`, and as the second base
# (`MidiLearn` is `: public deluge::ui::shell::Service, public LearnMode`).
SERVICE_DECL_RE = re.compile(r"public\s+(?:\w+\s*::\s*)*Service\b")
# The row `audit_service_scope` guards, and which files it is allowed not to cover.
SERVICE_ROW = "services-no-oled-timer-claim"
# Matches both #include "path" and #include <path>, capturing only the path (the delimiter
# itself is stripped either way). Angle-bracket project includes (e.g. #include <hid/buttons.h>)
# are real layer violations and must count the same as the quoted spelling -- mirrors the
# include regex in scripts/check_gfx_layer.py, which treats angle-bracket project includes
# the same way.
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
    comment's continuation and close, which the first cut of this skip missed: a
    `/** ... * waveformRenderer ... */` block would have counted against the symbol rows E3 task 12
    added (found by the E3 final review, A8; every such mention in the tree happens to use `///`
    today, so no count moved when this was fixed).

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
        # leaves the comment open is certainly comment all the way to its end. (E3 final review
        # residual 1; `*/ code()` has the same shape and is left counted, which is the safe
        # direction -- this predicate may only ever skip *less* than it did.)
        return "*/" not in s[2:]
    return s == "*" or s.startswith(("*/", "* "))


def count_file(path: Path) -> dict[str, int]:
    """Count each rule's hits in one file.

    A SYMBOL rule skips two kinds of line: a comment (`is_comment_line` above) and an
    `#include` directive. The include skip was added in E4 task 14, for a cost this plan
    actually paid: `scenes-no-root-ui` lists `slicer` as a whole word, so the mandated path
    `src/scenes/slicer/slicer_scene.h` tripped the row from its own `#include` lines --
    2 violations in 2 files, `registry.cpp` and `slicer_scene.cpp`, 1 each -- and the
    directory was renamed to `slicing/` to get around a rule that was not describing the
    coupling it exists to forbid.

    It loses nothing, and that is checkable rather than argued. Every symbol row here is
    scoped to a directory that an INCLUDE row already gates: `scenes-no-root-ui` and
    `scenes-no-legacy-waveform` cover `src/scenes/**`, where `scenes-includes-gui` already
    forbids `gui/` and `hid/` outright, so a real `#include "gui/ui/slicer.h"` still fails
    the checker -- under the row that names the actual violation, with the actual spec. What
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
            if kind == "include":
                m = INCLUDE_RE.match(line)
                if m and r.search(m.group(1)):
                    n += 1
            elif (
                r.search(line)
                and not is_comment_line(line)
                and not INCLUDE_RE.match(line)
            ):
                n += 1
        if n:
            counts[name] = n
    return counts


def current() -> dict[tuple[str, str], int]:
    out = {}
    for p in sorted((REPO / "src").rglob("*")):
        if p.suffix in SUFFIXES and p.is_file():
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
    # `src/deluge/gui/ui` rather than a narrower root: the two are declared in different
    # subtrees of it (`browser/sample_browser.h` and `sound_editor.h`), and no directory below
    # that declares both.
    ("legacy-calls-claim-seam", CLAIM_SEAM_CALLS, "src/deluge/gui/ui"),
)


def audit_symbols() -> int:
    """Check every named row's alternatives are symbols this tree actually declares.

    A ratchet row that lists a name nothing declares reads as protection and provides none --
    which is exactly what `performanceSessionView` was between E2 task 13 and its fix, and what a
    mistyped `readFileItemsForFolder` would silently be here. The test is deliberately crude (does
    the identifier appear anywhere under the directory it was copied from?) because the failure it
    catches is crude: a name invented rather than copied.
    """
    bad = 0
    total = 0
    for rule, names, root in AUDITED_SYMBOLS:
        haystack = "\n".join(
            p.read_text(encoding="utf-8", errors="replace")
            for p in sorted((REPO / root).rglob("*"))
            if p.suffix in SUFFIXES and p.is_file()
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

    This is the second half of the E4 final review's I1, and it exists because the first half
    alone would rot. The row was written scoped to `src/scenes/services/**` while two live
    `Service` implementations -- `shell::Notices` and `shell::Screensaver` -- sat in `src/shell/`,
    outside it, so the row protected three of the five services that existed and nobody noticed
    for a whole plan. Widening the glob fixes today; this makes tomorrow's service a build failure
    instead of a discovery.

    Deliberately crude, in the same way `audit_symbols` is: it asks whether a file that declares a
    `Service` subclass is covered by the row's scope, not whether the class is correct. A new
    service in a directory the row cannot see is exactly the failure it catches.

    Scope note: only `src/` is walked, matching `current()`. Test doubles under `tests/` and
    `sim/` derive from `Service` too and are intentionally out of the ratchet -- a spec claiming
    the OLED is how the OLED branch gets covered at all (`tests/spec/shell/flush_spec.cpp`).
    """
    frm = next(f for name, f, *_ in RULES if name == SERVICE_ROW)
    bad = 0
    found = 0
    for p in sorted((REPO / "src").rglob("*")):
        if p.suffix not in SUFFIXES or not p.is_file():
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
                f"{f}: rule '{r}' count {n} > baseline {base.get(key, 0)} (see docs/superpowers/specs/{spec})"
            )
            bad += 1
    if bad:
        print(
            f"{bad} layer-rule regression(s). Remove the new dependency, or (only for an agreed move) run --update.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
