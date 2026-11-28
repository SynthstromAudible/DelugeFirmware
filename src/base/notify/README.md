# deluge_base_notify

The model→UI notification channel: `DirtySet` (facts, pointer-free) and `NoticeQueue`
(presentation requests, copied payloads).

Rules: include nothing but `base/notify/**` and the standard library; `raise*`/`push` are [any]
(atomic, allocation-free, no UI symbols); `drain`/`pop` are [task]. `deluge_base_notify` stores
`int16_t error_code` / `uint16_t string_id`; the typed conversions live in
`src/deluge/util/notify.h` because `gui/l10n/strings.h` is not a leaf header (it includes
`io/midi/midi_device.h`).

## Usage

**Facts** (`base/notify/dirty_set.h`) — a model-side fact that a redraw is owed, raised from any
context:

```cpp
#include "base/notify/dirty_set.h"

deluge::base::notify::raise(deluge::base::notify::Dirty::GridMain);
deluge::base::notify::raise_clip(deluge::base::notify::Dirty::ClipContent, session_clip_index);
```

`Clip::notify_content_changed()` (`src/deluge/model/clip/clip.h`) is the usual way to raise
`Dirty::ClipContent`: it looks the clip up in `currentSong->sessionClips` and calls
`raise_clip()` when it finds a session index, `raise()` otherwise.

**Notices** (`base/notify/notice_queue.h`) are presentation requests with a copied payload. The
library's own entry points store plain integers:

```cpp
#include "base/notify/notice_queue.h"

deluge::base::notify::notice_error_code(static_cast<int16_t>(Error::FILE_NOT_FOUND));
deluge::base::notify::notice_text("Undo", deluge::base::notify::Notice::Kind::Console);
deluge::base::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_UNDO),
                                    deluge::base::notify::Notice::Kind::Text,
                                    static_cast<uint8_t>(PopupType::SWING));
```

Callers almost always want the typed layer instead — `src/deluge/util/notify.h`, which knows
`Error` and `l10n::String` so callers don't cast by hand:

```cpp
#include "gui/l10n/strings.h" // deluge::l10n::String::* -- util/notify.h only forward-declares String
#include "util/notify.h"

deluge::base::notify::notice(Error::INSUFFICIENT_RAM);                        // -> error popup + console line
deluge::base::notify::notice(deluge::l10n::String::STRING_FOR_UNDO, deluge::base::notify::Notice::Kind::Console);
deluge::base::notify::notice("Loading sample failed");                        // computed/literal text, Kind::Text popup
deluge::base::notify::notice_cancel_popup();
deluge::base::notify::notice_working("Loading");
deluge::base::notify::notice_clear_working();
```

A popup type (`hid/display/display.h`'s `PopupType`) is passed as `static_cast<uint8_t>(PopupType::X)`
— `util/notify.h` deliberately does not include that header; it pulls in `l10n.h` and more, and
the cast moves to a typed one once `PopupType` moves into `notify`. `popup_type == 0` means
"unspecified"; the drain maps that to `PopupType::GENERAL`, not `PopupType::NONE`.

## The drain

`drain_notifications()` (`src/deluge/gui/ui/notify_drain.cpp`) runs in task context, inside
`doAnyPendingUIRendering()`. It's gated by the same re-entrancy lock and render-budget early-out
as grid/OLED rendering (a skipped drain is picked up on the next pass), but once past those it's
the first thing done under `pendingUIRenderingLock` — before grid/OLED rendering — so a render
the drain itself triggers can't re-enter this function and drain a second time within the same
pass. Besides draining `DirtySet` and `NoticeQueue`, every pass it also:

- polls `playbackHandler.count_in_visual()` and `AudioEngine::cull_alert_pending` — these have
  no `Dirty` bit; the drain just checks them unconditionally;
- re-dispatches the legacy `RootUI::notifyPlaybackBegun()`/`playbackEnded()` hooks on the
  playback-state edge (guarded exactly as the legacy call site was: only when the root UI is on
  screen, or the sound editor's note/note-row editor is open), because those overrides mutate
  state beyond a redraw and a bare `Dirty::PlaybackState` can't reproduce that;
- calls `ArrangerView::refresh_held_row_mod_target()` when `Dirty::OutputChanged` is set and
  `ArrangerView` is the root UI;
- for `Dirty::ClipContent`, folds the fact into both main and sidebar rows (the audio clip view's scene takes the fact
  itself through the shell's own consumer, scroll-back included).

A `Notice` with `Severity::Warn` becomes a persistent popup (`numFlashes 0`, i.e. it stays until
something removes it) instead of the default 3-flash popup. No raiser sets `Severity::Warn`
today (`notice_text`/`notice_string`/`notice_kind` never touch it; only `notice_error_code` sets
`Error`), so this path is currently unreachable, not merely undocumented.

## Deferred sites

Some `display->`/UI-hook call sites in the model are deliberately not migrated yet; each is
marked `// NOTIFY-DEFERRED: <reason>` and is counted by `scripts/check_layer_rules.py`'s
`model-calls-display` rule so the count can't silently grow. `instrument_clip_minder.cpp` is a
separate, unmarked exception: it's the accepted model/UI mixin (spec §1/§5), so its
`display->displayPopup` sites are left as-is rather than tagged. The working-animation pairs that
bracket a synchronous load, and errors on a path that never returns to the tick, stay as direct
`display->` calls until the shell can tick during `sd_busy` (spec D).

Two ratchets track the direction per file, and a count may only shrink: `scripts/layers.toml`'s
`model-includes-ui` and `notify-is-leaf` include rules (`scripts/check_layers.py`, baseline
`scripts/layers_baseline.txt`, which also holds `src/ui/gfx` to the standard library), and
`scripts/check_layer_rules.py`'s `model-calls-ui` and `model-calls-display` symbol rows (baseline
`scripts/layer_rules_baseline.txt`).
