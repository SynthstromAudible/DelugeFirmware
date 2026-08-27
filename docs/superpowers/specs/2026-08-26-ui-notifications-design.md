# Model → UI notifications: `DirtySet` and `NoticeQueue`

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec C of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md`); realises `target_architecture.md` §4.3.

## 1. Why

Outside `src/deluge/gui/` and `src/deluge/hid/`, **652 lines in 44 files** reach into
the UI layer (census, 2026-08-26, appendix A). They fall into four groups that need
four different treatments:

| Group | Lines | Treatment |
|---|---|---|
| Presentation requests — the model deciding what the screen says (`display->displayPopup` ×73, `displayError` ×25, `consoleText` ×13, loading animations, `cancelPopup`, raw `"E248"` codes) | ~330 | §3 `NoticeQueue`, or §4 "becomes a fact" |
| Reverse coupling — the model *reading or writing* UI state (`currentUIMode` incl. 10 writes, `view.modPos/modLength`, `view.activeModControllableModelStack`, `soundEditor.current*`, `display->hasPopupOfType`, `instrumentClipView.getPosFromSquare`) | ~140 | §5 ownership map |
| LED / timer / pad primitives (`indicator_leds::` ×32, `PadLEDs::` ×16, `uiTimerManager.` ×12) | 86 | facts + shell-side derivation; three re-entrant `uiTimerManager.routine()` "yields" become scheduler yields |
| Genuine model facts (grid dirty, clip re-render, note row changed, sample ready, playback begun/ended, output changed, param automation) | **~46** | §2 `DirtySet` |
| Misfiled UI code under `model/` (`instrument_clip_minder.cpp`, `clip_minder.cpp` implement `buttonAction`/`setLedStates`; `action_logger.cpp` restores a `UI*` on undo) | ~146 | reclassified, not notified |

Two facts drive the design:

- **~24 sites are raised from `[audio]`.** `deluge_app_render → tickSongFinalizeWindows →
  actionSwungTick/actionTimerTick` runs the whole sequencer tick tree inside the render
  callback. Today that includes `display->displayPopup(buffer, …)` with a string
  formatted on the audio path (count-in digit), `display->cancelPopup()`,
  `currentUIMode &= UI_MODE_AUDITIONING` (a write to UI state), and
  `indicateAlertOnLed(PLAY)` from the voice-cull loop. `uiNeedsRendering()` itself walks
  the UI stack calling virtual `renderMainPads()` — from audio context.
- **No external caller ever passes a row-granular mask** to `uiNeedsRendering`. All 20
  are all-main / all-sidebar / both. Row granularity is a gui-internal concern.

## 2. `DirtySet` — model facts

A leaf library `deluge-notify` (no dependencies; `[any]`-safe), raised by the model,
drained once per UI tick by the shell (spec D) — or, until the shell exists, by a
temporary drain in `doAnyPendingUIRendering()`.

```cpp
namespace deluge::notify {

enum class Dirty : uint8_t {
	GridMain, GridSidebar, Oled,            // coarse: "something you draw changed"
	ClipContent, NoteRows,                  // + clip-index bits (below)
	SampleReady, PlaybackState, SongSwapped,
	OutputChanged, ParamAutomation, ModTarget, SelectedDrum, MidiLearn,
	Count
};

using ClipBits = std::bitset<256>;          // indexed by position in song->sessionClips

class DirtySet {
public:
	void raise(Dirty d);                                   // [any] atomic fetch_or
	void raise_clip(Dirty d, uint16_t session_clip_index);    // [any] sets d + one clip bit
	DirtySet drain();                                      // [task] atomic exchange with zero
	bool has(Dirty d) const;
	bool any() const;
	const ClipBits& clips(Dirty d) const;                  // only ClipContent / NoteRows carry bits
};

DirtySet& dirty();                                       // the one instance (owned by deluge-app later)
inline void raise(Dirty d) { dirty().raise(d); }
inline void raise_clip(Dirty d, uint16_t i) { dirty().raise_clip(d, i); }

} // namespace deluge::notify
```

Rules:

1. **No pointers, ever.** Clip-scoped facts carry the clip's *index* in
   `song->sessionClips` as a bit. A stale index after a delete or reorder only
   over-invalidates (an extra redraw); it can never dereference freed memory.
   Arranger-only clips and anything not indexable raise the coarse bit.
2. **No row masks from the model.** The shell/scene computes row dirtiness from
   `ClipContent`/`NoteRows` bits (the scene knows which rows show which clip). The
   pad row-bitmask protocol survives, inside the UI.
3. `raise*` is `fetch_or` on a `std::atomic<uint32_t>` (and on one 32-bit word of the
   bitset for clip bits). No allocation, no lock, no UI-stack walk, no virtual call.
4. `drain()` exchanges the clip words first, then the coarse bits, and **derives** the
   `ClipContent`/`NoteRows` flags from any non-zero clip word in the snapshot — so a clip
   bit can never appear without its flag. The one remaining interleaving (a raise's coarse
   bit landing after its word was drained) yields a snapshot whose flag is set with an
   empty clip set; that means "which clips is unknown — treat as all" (over-invalidation,
   never loss). A raise that lands during a drain is seen on that drain or the next.
5. Raising during a mutation cannot re-enter the UI, by construction: the model has no
   UI symbol to call.

Bit → effect mapping (the shell's job, listed here so the vocabulary is complete):

| Dirty | Shell effect |
|---|---|
| `GridMain` / `GridSidebar` / `Oled` | `RepaintGate::request_repaint()` on the grid / grid / all OLED surfaces |
| `ClipContent{bits}` / `NoteRows{bits}` | `on_model_changed` on the stack; grid rows showing those clips marked dirty |
| `SampleReady` | waveform views re-read; grid + OLED repaint |
| `PlaybackState` | transport LEDs re-derived; playhead views repaint; count-in overlay shown/hidden |
| `SongSwapped` | stack reset to the root scene for the new song |
| `OutputChanged` / `SelectedDrum` | sidebar + OLED name repaint |
| `ParamAutomation` / `ModTarget` | `ModKnobs` rings re-derived; automation views repaint |
| `MidiLearn` | `MidiLearn` service LED blink state re-derived |

## 3. `NoticeQueue` — presentation requests that remain requests

```cpp
namespace deluge::notify {

struct Notice {
	enum class Kind : uint8_t { Error, Text, Console, Working, ClearWorking, CancelPopup };
	enum class Severity : uint8_t { Info, Warn, Error };
	Kind kind;
	Severity severity;
	::Error error;            // Kind::Error
	l10n::String string;      // Text / Console with an l10n id; else String::EMPTY
	char text[24];            // copied short text (names, "E248"); NUL-terminated, truncated
	uint8_t popup_type;        // today's PopupType, for like-for-like replacement/dedupe
};

class NoticeQueue {
public:
	bool push(const Notice&);   // [any]; overwrite-oldest; capacity 16; returns false if it overwrote
	bool pop(Notice&);          // [task]
};

NoticeQueue& notices();
void notice(::Error e);                                        // convenience raisers
void notice(l10n::String s, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
void notice(std::string_view short_text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);

} // namespace deluge::notify
```

- **Payloads are copied.** 21 of 73 `displayPopup` calls pass a stack-local buffer today;
  a deferred pointer would dangle.
- **The shell chooses the widget.** `Error` → popup and a console line; `Text` → popup;
  `Console` → console overlay; `Working`/`ClearWorking` → loading overlay; `CancelPopup`
  → dismiss the current popup. The model no longer names a widget.
- `popup_type` lets the shell replace a like-typed popup in place (today's
  `popupTextTemporary(PopupType::SWING)` semantics) and lets `CancelPopup` target a type.
- `display->hasPopupOfType()` (5 sites where playback logic branches on UI state) is
  removed: the state those sites test becomes a model fact (§4).
- `deluge-notify` stores `int16_t error_code` / `uint16_t string_id`; the typed conversions
  live in `src/deluge/util/notify.h` because `gui/l10n/strings.h` is not a leaf header (it
  includes `io/midi/midi_device.h`).

## 4. Presentation that becomes a fact

Every `[audio]`-raised presentation request is state the UI can render itself. These are
not migrated to notices; they are turned into model/playback state plus a `Dirty` raise.

| Today | Becomes |
|---|---|
| count-in digit `displayPopup(buffer)` + `cancelPopup()` in `actionSwungTick` | `PlaybackHandler::countInBarsRemaining()` + `raise(PlaybackState)`; a `CountInScene` overlay renders it |
| swing / tempo `popupTextTemporary(PopupType::SWING/TEMPO)` + `hasPopupOfType(SWING)` | transient-value state in the shell's `ModKnobs`; playback reads its own swing value, not the popup |
| `"E336"` from `attemptLateStartOfNextNoteToPlay` (`[audio]`) | `notice(Error::…)`; push is `[any]`-safe |
| `indicateAlertOnLed(PLAY)` from the voice-cull loop | `AudioEngine::cullAlertPending` + `raise(PlaybackState)`; `GlobalControls` blinks the LED |
| `currentUIMode &= UI_MODE_AUDITIONING` written from audio | deleted; audio raises `PlaybackState`; the shell owns its mode |
| `currentUIMode = UI_MODE_RECORD_COUNT_IN / LOADING_SONG_*` | `playbackHandler.count_in_active`, `song.load_state` (model/playback state; the shell mirrors it) |
| `displayLoadingAnimationText("Loading")` / `removeLoadingAnimation` (task only) | `Working` / `ClearWorking` notices |
| `PadLEDs::clearTickSquares()` from `endPlayback` | `raise(PlaybackState)`; the scene stops drawing the playhead |

## 5. Reverse coupling: ownership map

Catalogued here; relocated by the shell (D), scenes (E) and commands (I) specs.

| State living in the UI today | Readers | New owner |
|---|---|---|
| `view.activeModControllableModelStack`, `view.modPos/modLength/modNoteRowId` (37) | param write paths, `NoteRow`, `AutoParam` | **model**: `ModTarget{ModelStack, region}` on `Song` — "what the gold knobs point at" is model state. `ModKnobs` (shell) edits it; param code reads it. |
| `soundEditor.currentMultiRange*` (7) | `Source`, `Kit` | **model**: the selected multi-range is a selection on the sound |
| `soundEditor.currentModControllable/currentParamManager/inNoteEditor` (8) | `NoteRow`, `Kit` | **`ModTarget`** (above); `inNoteEditor` becomes a `MenuScene` state read via `ModTarget.region` |
| `soundEditor.shortcutsVersion`, `PadLEDs::flashCursor` persisted by `FlashStorage` (4) | settings | **settings store** owns the values; the UI reads them |
| `currentUIMode` reads (61), `isUIModeActive` (29), writes (10) | playback, MIDI instrument, actions, `util/functions.cpp` | the ~8 modes the model tests get **proper model/playback state**: `count_in_active`, `load_state`, `midiInstrument.awaiting_cc`, `audition_state`, `holdingSectionPad`. The shell's mode enum becomes private. |
| `instrumentClipView.getPosFromSquare/getSquareFromPos` (4) | `NoteRow::getSquareInfo` | **model**: `Clip::timelineWindow{scroll, zoom}` (already persisted in the song file); the square math is a pure function on it in `model/` |
| `midiFollow.previousKnobPos` (3) | MIDI follow | **`io/midi`**: `MidiFollow` state moves out of `gui/` |
| `sampleBrowser.lastFilePathLoaded` (2) | recorder, sample holder | **`AudioFileManager`** last-path memo |
| `getRootUI()->renderSidebar(...)` in `instrument_clip.h:237`, `audio_clip.h:119` | model headers | deleted; the scene's grid view renders the sidebar |
| `changeRootUI(...)` ×14 and `action->view` (`UI*`) in `action_logger.cpp` | undo restore | **commands track**: `Action` records a `SceneId` POD tag. Until then `LegacySceneAdapter` maps `UI*` → `SceneId`. |
| `instrument_clip_minder.cpp`, `clip_minder.cpp` | — | **moved to `deluge-scenes`** wholesale; they are scene code |
| `uiTimerManager.routine()` re-entered from SD loops (`sample_recorder.cpp:1386`, `storage_manager.cpp:858`, `deluge.cpp:596`) | recorder, storage | **scheduler yield**; the UI tick is a scheduler task, not something storage pumps |

## 6. Context rules

- `DirtySet::raise*`, `NoticeQueue::push`: `[any]`. `drain`, `pop`: `[task]`.
- Both are allocation-free and lock-free. Raises are idempotent ORs. The notice ring has a
  32-bit head/tail; `[audio]` and `[task]` producers are serialised today by cooperative
  scheduling — marked `// COOP-WRITE` and replaced by the SPSC queue of
  `target_architecture.md` §4.2 when it lands.
- Debug/sim builds assert `[task]` on `drain`/`pop` via the context tracker
  (`target_architecture.md` §4.1) once it exists; until then a no-op macro.
- The model never formats a string on the audio path. A `Notice` from `[audio]` carries an
  `Error` or an l10n id, never `text`.

## 7. Migration and enforcement

1. Land `deluge-notify` (`src/notify/`, CMake target `deluge_notify`, unit tests). No callers.
2. Temporary drain in `doAnyPendingUIRendering()`: `Dirty` bits → today's
   `uiNeedsRendering(getRootUI(), …)` / `renderUIsForOled()`; notices → `display->…`.
   This lets the model side migrate before the shell exists; the shell replaces the drain.
3. Burn down by category, each a mechanical PR with a codemod where possible:
   RootUI hooks → `raise*`; `display->` → `notice()` or a fact (§4);
   `uiNeedsRendering`/`renderUIsForOled` → `raise`; `indicator_leds::`/`PadLEDs::` → facts;
   reverse reads → §5 owners; misfiled files → `deluge-scenes`; the three yields → scheduler.
4. Ratchet: `scripts/check_layer_rules.py` (generalising `check_platform_boundary.py`) with a
   committed baseline of `gui/`/`hid/` includes and symbol uses per non-UI file. CI fails on
   increase; the 652 is the visible burn-down.
5. Delete when zero: `RootUI`'s hook virtuals, the public `uiNeedsRendering`, `Display`'s
   presentation methods, `getRootUI()/getCurrentUI()` exports outside `gui/`.

## 8. Testing

- `deluge-notify` unit tests (host, CppSpec): raise/drain atomicity (a raise interleaved
  with a drain is not lost — exercised with a second thread on the host); clip-bit set and
  clear; notice overwrite-oldest and the `push` return value; text truncation and
  NUL-termination; `drain()` twice yields empty.
- Shell integration (spec G): script `model.raise ClipContent 3` → only the rows showing
  clip 3 repaint; `model.notice Error 248` → popup + console line in the snapshots.

## 9. Open questions

1. `ClipBits` width (256) versus the practical maximum session clip count — verify against
   `Song::sessionClips` limits before choosing; a smaller bitset shrinks the drain.
2. Whether `Notice::text` at 24 bytes covers the dynamic-name popups (preset/output names
   can be longer); the shell may need to look names up by id instead — decide during the
   `display->` burn-down when the real distribution is visible.

## Appendix A — census summary (2026-08-26)

652 UI-coupled lines in 44 files. By construct: `display->` 149; UI-state reads/nav
(`getCurrentUI` 77, `getRootUI` 69, `currentUIMode` 42, `isUIModeActive` 29,
`changeRootUI` 14, `openUI` 10) 220; view-singleton member access 209; LED/timer/render
primitives 86; `uiNeedsRendering` 20; RootUI hooks 20. Top files:
`playback/playback_handler.cpp` 97, `model/action/action_logger.cpp` 75,
`model/clip/instrument_clip_minder.cpp` 71, `model/song/song.cpp` 43,
`playback/mode/session.cpp` 42. `[audio]`-raised: ~24. `[isr]`-raised: none.
`freezeWithError` (214 sites) is already a foundation-tier symbol and is excluded.
