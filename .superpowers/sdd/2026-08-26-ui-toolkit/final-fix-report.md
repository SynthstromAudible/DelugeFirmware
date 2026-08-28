# Final-review fix wave — `ui/toolkit`, on top of `e3cc41bff`

Two commits: code (I1–I5 + minors), then docs (I6). Rulings applied exactly as given; everything
left undone is listed with a reason in the minors table.

---

## I1 — `Response::velocity` carried three meanings

| finding | change |
|---|---|
| `response.h:9` — `uint8_t velocity` with no per-widget semantics | `src/ui/core/response.h:11-30` — struct re-documented; `velocity` is now spelled "the *pad* velocity, 0..127, and nothing else"; new `int16_t index = -1` documented as "widget-defined selection index: `row`/`clip_row` → pressed column x; `text_keyboard` → typed character code; −1 = none" |
| `rows.cpp:16` — `accumulate` put `x + 1` in `velocity` | `src/ui/grid/rows.cpp:14-24` — `out.index = x`, `out.velocity = pad.velocity`; the +1 bias is gone (−1 is the sentinel) |
| `rows.h:39-44`, `:225` — headers documented the `x + 1` encoding | `src/ui/grid/rows.h:36-43` and `:227-228` — both rewritten; they now point at `f.first_press_in()` for the per-pad case |
| `keyboard.cpp:174` — typed character in `velocity` | `src/ui/grid/keyboard.cpp:142,166,176-181` — character goes to `out.index`; a new `pressed_velocity` local carries the typing pad's own velocity into `out.velocity` |
| `keyboard.h:114-118` — "per the brief" | `src/ui/grid/keyboard.h:114-123` — rewritten; states why a character code is not a velocity |
| reviewer's recommendation: a `first_press_in` convenience | `src/ui/grid/frame.h:114-122` (decl) / `src/ui/grid/frame.cpp:92-105` (impl) — `std::optional<PadEvent> first_press_in(gfx::grid::Rect local) const`, local coords in and out, clipped to `bounds()`, releases skipped |
| open question: `void` vs `Response` for grid columns | **closed as `void`**, noted at `src/ui/grid/columns.h:20-33` with the reasoning and the `first_press_in` one-liner |

No call site in `src/ui/**` hand-rolled a `local_events()` scan (checked by grep), so
`first_press_in` has no in-tree caller to convert yet; it exists for the scenes the `void` return
sends to `local_events()`.

Spec cases: `tests/spec/ui/rows_spec.cpp:201-202`, `:254-268`, `:277-278`;
`tests/spec/ui/keyboard_spec.cpp:217-224`, `:233`, `:242`, `:257`, `:266`;
`tests/spec/ui/grid_frame_spec.cpp` gained "finds the first press inside a rectangle, in frame
coordinates" (rectangle filtering, event order beats scan order, region-local coordinates, empty
and off-frame rectangles).

## I2 — edge vs. level

- `src/ui/core/input.h:52-67` — `select_pressed`/`back_pressed` documented as **edges** (true only
  on the tick the press happened), `shift` as the level it sits beside; states that the shell
  lowers edges, that widgets deliberately do not latch, and why they cannot (an immediate-mode
  widget has no per-caller identity to hang a "already reported" bit on).
- `tests/spec/ui/list_spec.cpp` — new case *"does not latch select_pressed: the caller supplies
  edges (ui/core/input.h)"*: two consecutive ticks with `select_pressed = true` report `entered`
  **twice**, and clearing the flag is what stops it. The contract is pinned as the un-defended
  behaviour, exactly as ruled.

## I3 — frame lifetime and the `local_events()` cache

| finding | change |
|---|---|
| `grid/frame.h:44-49` said the shell builds frames once and refreshes the snapshot | `src/ui/grid/frame.h:40-46` — "**A `GridFrame` is a per-tick object**"; deviation 2 no longer cites that as the shell's shape, only as a rig's |
| the cache latched `cached_filled_` forever | `src/ui/grid/frame.h:150-155` + `src/ui/grid/frame.cpp:70-76` — `cached_filled_` replaced by `cached_pads_` (`const PadEvent*`) and `cached_pads_size_`; a refill happens whenever the snapshot's `pads` `data()`/`size()` differ |
| `effects_spec.cpp:44-51`'s comment documented the opposite lifetime | `tests/spec/ui/effects_spec.cpp:44-50` — rewritten: a fresh frame per tick is what the *shell* does, not a workaround; reusing the rig's frame would now work too |
| `oled/frame.h` | checked — it already says "built per frame on the stack… owns nothing and outlives nothing"; no correction needed |

Spec case: `grid_frame_spec` *"re-filters when the snapshot's events are swapped under a reused
frame"* — one long-lived frame, three successive `pads` spans (1 event → 2 events → empty), each
reflected.

## I4 — `Style` fields

Every field is now read by the widget that describes it, and the shipped profiles' values are
pinned to the legacy metrics by `static_assert` (`src/ui/core/style.h:73-78`), so a profile edit
that would move pixels fails to compile rather than failing a golden.

| field | wired at |
|---|---|
| `title_height` | `src/ui/oled/text.cpp:162-166` — `title()`'s separator row (`= kTitleSeparatorY`) |
| `row_height` | `src/ui/oled/list.cpp:28-33` (`list_visible_rows`' divisor), `:126`, `:139-140`, `:154` (row pitch, row clear, label box) (`= kTextSpacingY`) |
| `inset_x` | `src/ui/oled/list.cpp:128` — the list label's x (`= kTextSpacingX`) |
| `scrollbar_w` | `src/ui/oled/list.cpp:196-205` — the thumb is `clamp(style.scrollbar_w, 1, rect.w)` wide, right-aligned; identical pixels for the 4-wide rects every caller passes |
| `big_font` | `src/ui/oled/values.cpp:188-198` — `decimal_value`'s huge digit face; `digit_width`/`digit_height` come from it instead of `kTextHugeSpacingX`/`kTextHugeSizeY`, with two `static_assert`s pinning MetricBold20's metrics to those constants |
| **`menu_base_y` (new)** | `src/ui/core/style.h:37-42` (14 in `kDelugeStyle48`, 15 in `kDelugeStyle64`); replaces the canvas-geometry branch in `src/ui/oled/list.cpp:90-96` (`menu_rect`, the old `base_y_for` helper is deleted) and in `src/ui/oled/misc.cpp:50-59` (`output_name`'s `tall`) |

`popup.cpp:103`'s `panel_of()` is untouched — Task 9 I1 ruled it as legacy parity.
`kHorizontalMenuBaseY` stays a free constant: it is 14 on every panel and follows the horizontal
menu's rule, not the menu item's; folding it in would conflate two rules that only coincide at 48
rows. `kOutputTypeY48/64` / `kOutputNameY48/64` stay literals per the existing ruling in
`metrics.h:44-49`.

Docs: `src/ui/core/style.h:8-19` (struct doc, the `(Style, Geometry)` pairing), spec §3's code
block and prose, `src/ui/README.md:136-142` (the "menu_base_y into Style (open)" item is now
**closed**).

## I5 — the ratchet

`scripts/check_layer_rules.py:61-71` — `ui-includes-outside`'s regex becomes

```
^(?!ui/|gfx/)(?:.*/|definitions.*|board_config\.h|board_layout\.hpp)
```

Baseline unchanged (still no `ui-includes-outside` rows, i.e. 0). Probe, as ruled:

```
$ printf '#include "definitions_cxx.hpp"\n' >> src/ui/core/response.h
$ python3 scripts/check_layer_rules.py; echo $?
1 layer-rule regression(s). Remove the new dependency, or (only for an agreed move) run --update.
src/ui/core/response.h: rule 'ui-includes-outside' count 1 > baseline 0 (…)
1
$ # line removed
$ python3 scripts/check_layer_rules.py; echo $?
0
```

`src/ui/README.md:40` and its probe paragraph updated to describe both arms.

## I6 — spec (`docs/superpowers/specs/2026-08-26-ui-toolkit-design.md`, `git add -f`)

- §6 `waveform` row → "**deferred to spec E's sample-view scene; `WaveformState` retained for
  it**", with the model-side `WaveformRenderData` reason.
- §6 shape line → "`Response` for interactive widgets; `void` for pure draws (meters, misc, grid
  columns)", naming both sets.
- §12 Amendments gained: a **§2 layout** block (PUBLIC-not-PRIVATE, the dropped gfx-grid dep, the
  ratchet regex); five **§3** entries (`Response::index`, the edge contract, `Style::menu_base_y`
  plus the four revived fields, the placeholder note for `Timer`/`EncoderRole`/`ListState::editing`);
  a **§4** entry (per-tick frames, the span-keyed cache, `first_press_in`); two **§6** entries (the
  deferred `waveform`, the `Response`/`void` split); and the **§7** column-component entry rewritten
  from "left open (M6)" to closed.
- §3's code block now shows `Response::index`, the edge comment on `select_pressed`, and
  `Style::menu_base_y`; §7's open-question paragraph and `row`'s table row rewritten.

---

## Minors

| # | review's minor | verdict |
|---|---|---|
| 1 | spec §2 says `PRIVATE` deps; CMake uses `PUBLIC` (correct) | **fixed** — spec §2 no longer says `PRIVATE`; §12 records why `PUBLIC` is right |
| 2 | `deluge_ui_core` links `deluge_gfx_grid` but includes nothing from it | **fixed** — `src/ui/CMakeLists.txt:9` drops it (`deluge_ui_grid` links it directly); host + device links verified |
| 3 | `GridFrame` is 176 B; the header claims the cache costs "per frame that actually asks" | **fixed (doc)** — `src/ui/grid/frame.h:17-24` states the real cost and that spec §8's "~40 bytes" does not describe this frame. Not dropped: the buffer is what keeps `GridFrame` an aggregate with no heap and no caller scratch, and the ruling did not sanction removing it |
| 4 | `ListState::editing` read by nothing | **fixed (doc)** — `src/ui/core/state.h:8-13` says so, names spec E as the caller, and says to delete it if none appears |
| 5 | `Timer` / `EncoderRole` declared and unused | **fixed (doc)** — `src/ui/core/state.h:61-65` and `src/ui/core/input.h:7-11`, same treatment |
| 6 | two hand-built `OledFrame` aggregates (`render_slot`, `without_turn`); reviewer suggests `subframe`/`with_input` helpers | **left** — a new-API refactor of two widgets, outside I1–I6 and not ruled; both sites are already documented (Task 8 ruled `render_slot` a non-finding) and `frame.h:132-134` still carries the warning. Carry to spec D, where a third site would justify it |
| 7 | `decimal_value` repaints every frame regardless of `focused` | **left** — a behaviour change (idle cadence), not a doc fix; it needs its own ruling and a `RepaintGate` spec case, and the ruling here is explicitly parity-preserving |
| 8 | `Blink::period_ms` doubles as an "armed" sentinel | **left** — already documented at `state.h:37` ("one `Blink` serves one blinker"); enforcing it means changing the type, which no ruling covers |
| 9 | `Response::focused` set by 2 of 7 widgets | **left** — each of the five documents that it leaves the field false (`list.h:101`, `columns.h:135`, `values.h:171`); deferred twice already (ledger Tasks 5, 6) and unchanged by I1 |
| 10 | plan lists `bipolar_bar` in `meters.h` and `waveform` in `misc.h`; neither exists | **fixed** — `docs/superpowers/plans/2026-08-26-ui-toolkit.md:44,47` corrected, with the `waveform` line pointing at the Amendments |
| 11 | `layer_rules_baseline.txt` shrank 51→48 in a docs commit | **left** — historical, already landed, direction correct and the checker passes; nothing to change now |
| 12 | `README.md:150-156` pastes an `ls -l` transcript with local paths | **fixed** — transcript removed; `src/ui/README.md:150-153` keeps the per-object `size` table with a note on how it was taken and why archive byte counts are not recorded |

Also updated while in the file: `src/ui/README.md` — the grid-catalogue row for `GridFrame` lists
`first_press_in`; the "`Response` for grid columns (open)" item is closed as `void`; the OLED
`waveform` deferral now names spec E and `WaveformState`; the dependency block no longer claims
ui-core depends on gfx-grid.

---

## Verification (tails)

```
$ cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug --output-on-failure
...
71/71 Test #71: UnitTests ..........................   Passed    0.13 sec
100% tests passed out of 71
Total Test time (real) =   3.08 sec
```

71 targets, all green; three new `it` cases inside existing spec binaries (`list_spec`,
`grid_frame_spec` ×2) plus rewritten assertions in `rows_spec` / `keyboard_spec`.

```
$ git diff e3cc41bff --stat -- tests/golden
(empty)
```

No golden moved — no pixel moved.

```
$ python3 scripts/check_layer_rules.py ; echo $?
0
$ python3 scripts/check_gfx_layer.py ; echo $?
0
```

```
$ cmake --build build-sim-gfx --target deluge_host
[22/25] Linking CXX executable deluge_host
```

```
$ ./dbt build Debug
...
ld.lld: error: …/deluge_rust.0ug062106x6xzuo2cb699kjlj.0ztmbw8.rcgu.o: Invalid record
clang++: error: linker command failed with exit code 1
```

The known pre-existing device-link failure and nothing else: every C++ TU compiled and all three
`libdeluge_ui_*.a` archives were produced and appear on the link line.

```
$ ./dbt format
Done, formatting mismatch detected   # heaps.cpp / deluge_run.py only, both reverted
```

`src/deluge/memory/heaps.cpp`, `tools/deluge_run.py` and `src/bsp/rust/Cargo.lock` were touched by
the build/format tooling and checked out again; `src/bsp/rza1` untouched. Working tree carries only
the intended files plus the pre-existing untracked `release-blockers.md`.
