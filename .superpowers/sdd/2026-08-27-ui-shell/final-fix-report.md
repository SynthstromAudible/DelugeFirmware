# Final-review fix wave — `ui/shell` (plan D)

Applied on top of `72ddaa4d3` against
`.superpowers/sdd/2026-08-27-ui-shell/final-review.md`. Every finding in that review is addressed;
each one below names what changed, the spec that pins it, and the review citation it answers.

Every new spec was verified **failing first** by temporarily reverting its fix in place and
re-running the suite — the observed pre-fix failures are quoted per finding.

---

## Critical

### C1 — unregistered `Screensaver` scene turned the shell into a busy loop

*Review: "C1. An unregistered `Screensaver` scene turns the whole shell into a busy loop that
repaints every surface every tick" (`screensaver.cpp:36-40`, `scene_stack.cpp:19-22`,
`shell.cpp:307-319`, `:363`).*

**Changed — `src/shell/screensaver.{h,cpp}`.** `Screensaver::tick` latches itself off the first
time `SceneStack::push` refuses `SceneKind::Screensaver`: `refused_` is set and never cleared, so
`active()` stays false, `next_timer()` returns `std::nullopt` (no longer pinning `next_wake_` to
now), and no further push is attempted. The refusal is counted by `uint32_t push_refused() const`,
surfaced on the shell as `Shell::screensaver_push_refused()` alongside `leds_dropped()`. The class
note documents why a refusal is permanent rather than recoverable: the idle deadline stays past for
ever, so a retry cannot ever succeed for a different reason than the first attempt failed.

**Changed — `src/shell/scene_stack.{h,cpp}`.** `changed_` is now set only by an operation that
touched something. The early refusal in `push` (unregistered kind, or a full stack), `pop`'s
"no scene of that kind is stacked" return, and `replace_root`/`replace_top`'s unregistered-kind
returns all leave the flag alone. A push whose `on_enter` refused still sets it, deliberately and
with a comment saying so — `on_enter` and `on_resume` both ran, and either may have moved a greyout
mask. `changed_since_last_query`'s doc note was rewritten to state both halves.

**Spec added — `tests/spec/shell/lifecycle_spec.cpp`, "an unregistered screensaver kind latches the
service off instead of retrying every tick".** `Rig` gained a `register_screensaver` flag so a rig
can leave `SceneKind::Screensaver` out of the registry — the branch's documented default per §13's
§9 amendment, and the exact case the old rig always installed a stub for. With
`screensaver_idle_ms = 1000` and no scene registered: the idle deadline produces exactly one
counted refusal, ninety further ticks past it produce no additional blits, and `next_wake()` is
`std::nullopt`.

**Failing-first evidence** (fix reverted): `expected 2, got 184` blits; `expected 1, got 91`
refusals; `next_wake().has_value()` `expected false, got true`.

### C2 — `Repaint` from `after_render()` / post-render delivery was lost

*Review: "C2. `Action::Repaint` / `RepaintAll` issued from `after_render()` or post-render delivery
is cleared before anything can read it" (`shell.cpp:334`, `:354-356`, `:358`, `:362`).*

**Changed — `src/shell/shell.cpp`, `src/shell/shell.h`.** `router_.clear_repaint_requests()` moved
from the bottom of `tick()` to immediately after the grid render decision (between steps 6 and 7),
so anything steps 7 and 8 request survives to be read by the *next* tick's step-5 loop.
Additionally, after step 8, any surface whose role is still repaint-requested has its own
`RepaintGate` armed with `request_repaint_after(0, now_ms)`. That is what makes the next tick
happen at all: `compute_next_wake` reads gate deadlines, so `next_wake()` reports `now_ms` and a
scheduler that would otherwise sleep until the next input wakes immediately. A bare
`request_repaint()` would fire the gate but arm no deadline, leaving the frame waiting on an
unrelated wake — noted in place. The invariant is documented as a new "# Repaint requests" section
on `Shell`'s class doc in `shell.h`, and in the README's tick walkthrough.

**Specs added — `tests/spec/shell/tick_spec.cpp`.** `BaseScene` gained one-shot
`emit_repaint_from_after_render` and `emit_repaint_from_handle` flags.
- *"a Repaint from after_render is honoured on the next tick"*: an OLED dirty fact gives the tick a
  reason to render, `after_render()` returns `Repaint{Main}`, `next_wake()` is `now`, and the next
  tick — with no input, no fact, no stack change — renders and blits Main only, once.
- *"a Repaint from a post-render-delivered event is honoured on the next tick"*: an unclaimed select
  detent is held back and delivered at step 8, the scene's `handle()` returns `Repaint{Main}` (the
  review's menu-scene scenario), and the next tick renders and blits Main.

**Failing-first evidence** (fix reverted): `next_wake()` `expected 10, got 0` and `expected 30, got
0`; `oled_renders` `expected 5, got 4`; `blits` `expected 5, got 4`.

---

## Important

### I1 — absolute deadline comparisons broke at the `uint32_t` wrap

*Review: "I1. Deadline comparisons in the shell's own services break at the `uint32_t` wrap"
(`notices.cpp:75`, `:108`, `screensaver.cpp:37`, `:48`, `leds.cpp:17`, `shell.cpp:805-808`).*

**Changed** — every site converted to the difference form the toolkit already uses
(`RepaintGate::needs_frame`, `ui/core/repaint_gate.cpp:13`), each with a comment naming the concrete
failure it prevents:

| site | now |
|---|---|
| `notices.cpp` `PopupScene::expired` | `static_cast<int32_t>(now_ms - deadline_ms_) >= 0` |
| `notices.cpp` `ConsoleScene::render_oled` | `static_cast<int32_t>(frame.now_ms() - line.expire_ms) >= 0` |
| `screensaver.cpp` `tick` | `static_cast<int32_t>(now_ms - deadline_ms_) >= 0` |
| `screensaver.cpp` `next_timer` | `const int32_t remaining = static_cast<int32_t>(deadline_ms_ - now_ms_);` |
| `leds.cpp` `led_is_on` | `static_cast<int32_t>(now_ms - s.until_ms) >= 0` |
| `shell.cpp` `compute_next_wake`'s `consider` | `static_cast<int32_t>(*at - *best) < 0` |

`led_is_on`'s blink *phase* (`(now_ms / s.period_ms) & 1`) is left alone, per the review's own note.

**Specs added.**
- `lifecycle_spec.cpp`, *"the screensaver idle deadline survives the uint32 clock wrap"*: ticks from
  `0xFFFFFF00`, notes activity 10 ms before the wrap so the deadline lands past it, and asserts the
  screensaver is not pushed at +20 ms or +500 ms, that `next_wake()` reports the wrapped deadline
  itself, and that it does fire at +1010 ms.
- `notices_spec.cpp`, *"the popup and console lifetimes survive the uint32 clock wrap"*: a popup
  shown at `0xFFFFFF00` is still up 10 ms later and 1 ms before its deadline, and does expire 1 ms
  after it; a console line appended at the same clock still draws on the tick after the wrap.

**Failing-first evidence** (fix reverted): the screensaver fires immediately after the wrap
(`enters` `expected 0, got 1`); the popup is gone on the next tick and the console line never
renders (three `expected true, got false`).

### I2 — the documented `nullptr → digits` text fallback did not exist

*Review: "I2. The documented `nullptr → render the id as digits` text fallback does not exist, and
the real Deluge profile relies on it" (`profile.h:91-93`, `shell.cpp:297-302`,
`notices.cpp:148-151`, `:158-161`, `profiles/deluge.cpp`).*

**Changed — `src/shell/profile.{h,cpp}`.** Added `TextScratch` (an 8-byte caller-owned buffer,
sized for `int16_t`'s `"-32768"` plus a terminator) and two accessors on `BoardProfile`:
`text_or_digits(uint16_t, TextScratch&)` and `error_text_or_digits(int16_t, TextScratch&)`. Both
delegate to one allocation-free `render_digits` helper — hand-rolled rather than `std::to_string`
(allocates) or `snprintf` (pulls in the formatting machinery for two call sites), building the
digits into the caller's buffer through `uint32_t` so the most negative input has no magnitude to
overflow into. The header states the contract: this is the *one* place those two hooks are read.

**Changed — `src/shell/shell.cpp`, `src/shell/notices.cpp`.** All four consumers (the popup bridge,
and `drain()`'s `Error`, `Text` and `Console` cases) now call the accessors instead of testing the
hook and substituting `std::string_view{}`. `drain()` declares one `TextScratch` per notice, which
outlives every view built from it — `show_popup`/`console_.append` copy before the case ends.

**Changed — `src/shell/profiles/deluge.cpp`.** `.shortcuts`, `.text` and `.error_text` are now
stated as unset with a comment pointing at spec E (the `l10n::String` table) and spec F (wiring the
firmware's copy of it), rather than left to the aggregate defaults.

**Specs added.**
- `profile_spec.cpp`, *"resolves text through the profile's hooks, and as digits when they are
  null"*: wired hooks win; a bare profile spells 42, 0, 65535, -1, -32768 and 32767 — both extremes
  of each id type, which is what the scratch buffer is sized for.
- `notices_spec.cpp`, *"a null text/error lookup shows the id as digits, not an empty box"*: a Text
  notice with `string_id 42` shows `"42"`, an Error with code 7 shows `"7"`, and a negative code
  keeps its sign. `Rig` gained a profile parameter so a spec can run against `BoardProfile{}`.
- `profiles_spec.cpp`, *"Deluge profile has no l10n bridge yet, and falls back to digits"*: pins
  both hooks null on the shipping profile and both fallbacks through it, so a future wiring is a
  deliberate edit rather than a silent change to what an error popup says.

**Failing-first evidence** (fix reverted): every digit expectation `got ""`.

---

## Minor

**M1 — README's false libdeluge claim.** *(Review: `README.md:231`.)* The sentence now says
`board_surfaces.cpp` is the only `.cpp` that includes `libdeluge/display.h` or
`libdeluge/control_surface.h` — the only `.cpp`, not the only file, since `input_lowering.h`
includes `control_surface.h` — and states explicitly that the `shell-includes-outside` ratchet
enforces the *layer* rule (no `model/`, `gui/`, `hid/`, `definitions*`) and allows `libdeluge/` from
anywhere, so the narrowing is a recorded convention rather than a checked one. The table row at
line 51 was already the narrow, true statement and is unchanged.

**M2 — `Notices::tick` only expired notices while they were `top()`.** *(Review: `notices.cpp:214-221`,
`:203-207`.)* Popup expiry, working-scene expiry and `Kind::CancelPopup` all pop by kind now, which
also takes down anything pushed above the notice — the shape `Screensaver::note_activity` already
used, documented on both `Notices::tick` and `drain()`. Neither pop is guarded on the scene being
stacked: `SceneStack::pop` for an absent kind touches nothing and (after C1) does not flag the
stack either. **Spec added** — `notices_spec.cpp`, *"an expired popup is popped even with a scene
above it"*: a Console scene pushed over a live popup no longer strands it. **Failing-first
evidence**: `depth()` `expected 0, got 2`.

**M3 — a `Popup` from `after_render()`/post-render delivery is one tick late.** *(Review:
`shell.cpp:297-302`.)* Behaviour unchanged (it is late, never lost, since `popup_string_` is only
cleared on consumption); the README's tick walkthrough step 3 now says so, and says why.

**M4 — an odd total column count silently drops the last column.** *(Review: `surfaces.cpp:30`.)*
`SurfaceSet::build`'s preconditions in `surfaces.h` now require an even total column count and say
what an odd one would cost and what fixing it would take (a single-column send path, not a rounding
change); the `pairs` computation carries the matching comment.

**M5 — `GridSurface::dirty` latched forever if flushed before `build()`.** *(Review:
`shell.cpp:724`, `:746`.)* `Shell::flush` clears `dirty` when there is no `grid_storage` instead of
skipping the block that would have cleared it. Not reachable today; the set and the clear live in
different functions and only one was guarded, so the pair is now closed rather than closed by
coincidence.

---

## Documentation

- `src/shell/README.md`: the board-profile table gained a `TextScratch`/`text_or_digits` row and a
  paragraph on `deluge_profile()`'s deliberate nulls; the services section gained the screensaver
  latch and a statement of the difference-form deadline rule; the tick walkthrough gained the
  repaint-clear ordering, the `Popup` one-tick-late note, and the corrected libdeluge sentence.
- `docs/superpowers/specs/2026-08-26-ui-shell-design.md` §13: a **Final whole-branch review fix wave
  (2026-08-28)** block records each change that alters a documented claim — §7 step 7's "same as a
  `handle()` result" is now true; the screensaver latch and the `changed_` narrowing; the
  difference-form rule; the digits fallback and the profile's stated nulls; pop-by-kind; and the two
  latent hazards (M4, M5).

---

## Verification

| check | result |
|---|---|
| `cmake --build build/tests --config Debug` | exit 0 |
| `ctest --test-dir build/tests -C Debug` | **85/85 passed**, 3.78 s |
| `cmake --build build-sim-gfx --target deluge_host` | links, exit 0 |
| `./dbt build Debug` | all C++ compiles; fails at the known `ld.lld: … Invalid record` on the Rust BSP link (pre-existing, not from this wave) |
| `python3 scripts/check_layer_rules.py` | exit 0 |
| `python3 scripts/check_gfx_layer.py` | exit 0 |
| `./dbt format` | no change to any `src/shell` or `tests/spec/shell` file |
| `Cargo.lock` / `heaps.cpp` / `deluge_run.py` in the diff | none (build-dirtied, `git checkout --`'d) |

Each new spec was additionally run against a temporarily reverted fix; the failures are quoted per
finding above.
