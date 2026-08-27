# deluge_notify

The model→UI notification channel: `DirtySet` (facts, pointer-free) and `NoticeQueue`
(presentation requests, copied payloads). Spec: docs/superpowers/specs/2026-08-26-ui-notifications-design.md.

Rules: include nothing but `notify/**` and the standard library; `raise*`/`push` are [any]
(atomic, allocation-free, no UI symbols); `drain`/`pop` are [task]. `deluge_notify` stores
`int16_t error_code` / `uint16_t string_id`; the typed conversions live in
`src/deluge/util/notify.h` because `gui/l10n/strings.h` is not a leaf header (it includes
`io/midi/midi_device.h`).

## Usage

**Facts** (`notify/dirty_set.h`) — a model-side fact that a redraw is owed, raised from any
context:

```cpp
#include "notify/dirty_set.h"

deluge::notify::raise(deluge::notify::Dirty::GridMain);
deluge::notify::raise_clip(deluge::notify::Dirty::ClipContent, session_clip_index);
```

`Clip::notify_content_changed()` (`src/deluge/model/clip/clip.h`) is the usual way to raise
`Dirty::ClipContent`: it looks the clip up in `currentSong->sessionClips` and calls
`raise_clip()` when it finds a session index, `raise()` otherwise.

**Notices** (`notify/notice_queue.h`) are presentation requests with a copied payload. The
library's own entry points store plain integers:

```cpp
#include "notify/notice_queue.h"

deluge::notify::notice_error_code(static_cast<int16_t>(Error::FILE_NOT_FOUND));
deluge::notify::notice_text("Undo", deluge::notify::Notice::Kind::Console);
deluge::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_UNDO),
                               deluge::notify::Notice::Kind::Text,
                               static_cast<uint8_t>(PopupType::SWING));
```

Callers almost always want the typed layer instead — `src/deluge/util/notify.h`, which knows
`Error` and `l10n::String` so callers don't cast by hand:

```cpp
#include "gui/l10n/strings.h" // deluge::l10n::String::* -- util/notify.h only forward-declares String
#include "util/notify.h"

deluge::notify::notice(Error::INSUFFICIENT_RAM);                        // -> error popup + console line
deluge::notify::notice(deluge::l10n::String::STRING_FOR_UNDO, deluge::notify::Notice::Kind::Console);
deluge::notify::notice("Loading sample failed");                        // computed/literal text, Kind::Text popup
deluge::notify::notice_cancel_popup();
deluge::notify::notice_working("Loading");
deluge::notify::notice_clear_working();
```

A popup type (`hid/display/display.h`'s `PopupType`) is passed as `static_cast<uint8_t>(PopupType::X)`
— `util/notify.h` deliberately does not include that header; it pulls in `l10n.h` and more, and
the cast moves to a typed one once `PopupType` moves into `notify`. `popup_type == 0` means
"unspecified"; the drain maps that to `PopupType::GENERAL`, not `PopupType::NONE`.

## The drain

`drain_notifications()` (`src/deluge/gui/ui/notify_drain.cpp`) runs first, in task context,
inside `doAnyPendingUIRendering()` — before grid/OLED rendering and before the render-budget
early-outs. Besides draining `DirtySet` and `NoticeQueue`, every pass it also:

- polls `playbackHandler.count_in_visual()` and `AudioEngine::cull_alert_pending` — these have
  no `Dirty` bit; the drain just checks them unconditionally;
- re-dispatches the legacy `RootUI::notifyPlaybackBegun()`/`playbackEnded()` hooks on the
  playback-state edge (guarded exactly as the legacy call site was: only when the root UI is on
  screen, or the sound editor's note/note-row editor is open), because those overrides mutate
  state beyond a redraw and a bare `Dirty::PlaybackState` can't reproduce that;
- calls `ArrangerView::refresh_held_row_mod_target()` when `Dirty::OutputChanged` is set and
  `ArrangerView` is the root UI;
- for `Dirty::ClipContent`, calls `audioClipView.clipNeedsReRendering(clip)` on the current clip
  when `AudioClipView` is the root UI (its override has a scroll-position side effect a bare
  redraw would lose), and folds the fact into both main and sidebar rows otherwise.

A `Notice` with `Severity::Warn` becomes a persistent popup (`numFlashes 0`, i.e. it stays until
something removes it) instead of the default 3-flash popup.

## Deferred sites

Some `display->`/UI-hook call sites in the model are deliberately not migrated yet; each is
marked `// NOTIFY-DEFERRED: <reason>` and is counted by `scripts/check_layer_rules.py`'s
`model-calls-display` rule so the count can't silently grow. The working-animation pairs that
bracket a synchronous load, and errors on a path that never returns to the tick, stay as direct
`display->` calls until the shell can tick during `sd_busy` (spec D;
`docs/superpowers/specs/2026-08-26-ui-notifications-design.md`).

The ratchet (`scripts/check_layer_rules.py`, baseline `scripts/layer_rules_baseline.txt`; wired
into the `check-layer-rules` pre-commit hook and the `layer_rules` ctest) tracks
`model-includes-ui`, `model-calls-ui`, `model-calls-display`, `gfx-includes-outside` and
`notify-is-leaf` per file; a count may only shrink.
