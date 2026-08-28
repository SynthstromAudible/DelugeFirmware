# UI test harness

Level 0 of the UI test harness (spec G, `docs/superpowers/specs/2026-08-26-ui-test-harness-design.md`):
one script format, one snapshot format, dependency-free so they can be shared by levels 1-3
and compiled directly into the host BSP. **This directory must not include anything from
`src/shell`** -- it is compiled into the host BSP, which the shell is not.

- `ui_script.{h,cpp}` -- the script parser (`deluge::harness::parse_script`), plus a C façade
  (`ui_script_load` / `ui_script_next_due`) for the host BSP.
- `snapshot.{h,cpp}` -- the `.state` formatter (`StateLines`), an `oled_pbm` PBM writer, and
  `write_file`.
- `fixed_vector.h` -- a standalone copy of `src/shell/fixed_vector.h`'s shape (see above for why
  it isn't shared).

## Script format

(Copied from spec G §2.)

deluge-sdk's headless format, one step per line: `<at_ms> <command> <args…>`; `#` comments.

```
0       board 128x48+5,128x64+0   # host-app level only: select the panel set
0       snapshot boot
500     button Clip down          # ButtonId names (spec D §2)
520     button Clip up
600     encoder Select +1         # EncoderRole names; signed detents
700     pad 3 5 down 100          # grid coords; velocity optional (default 255)
750     pad 3 5 up
800     released                  # DELUGE_EVENT_NO_PRESSES
900     model ClipContent 3       # shell level only: raise Dirty::ClipContent for clip 3
950     notice Error 248          # shell level only: push a Notice
1000    snapshot after-clip
1200    quit
```

- Times are milliseconds on the harness clock: the shell's `now_ms` argument at level 2;
  the deterministic frame-count clock at level 3 (`ms = frames * 1000 / sample_rate`).
- Parsing is one dependency-free unit, `tests/harness/ui_script.{h,cpp}`
  (`parse_script(string_view) -> FixedVector<Step, 256>`; `Step{at_ms, kind, args}`),
  shared by levels 1-2 and compiled into the host BSP for level 3.
- Unknown commands are errors, not warnings.

## Snapshot format

(Copied from spec G §3.)

Per `snapshot <name>`, written into the output directory:

- `<name>.oled<N>.pbm` -- one **P1 PBM** per OLED surface, via `page_major_to_pbm`. A
  deliberate deviation from the SDK's PNG: no image codec in C, text diffs read in PRs.
  `scripts/pbm2png.py` converts for viewing.
- `<name>.state` -- text, one item per line, sorted so diffs are stable:

```
scene Session
scene Popup/General
pad 0 0 255 0 0
pad 4 3 10 20 30
led Play on
led Record blink 500
indicator upper 3 3 2 0
lock loading
notice Error 248
```

Only non-black pads, only lit/blinking LEDs, `scene` lines top→bottom, `lock` and
`notice` only when present.

`diff -r got/ golden/` is the assertion. `GFX_GOLDEN_UPDATE=1` re-records (the helper from
the gfx plan). A snapshot that is identical to the previous one in the same script is
still written -- silence is not a pass.

### `sorted_text()` ordering, precisely

`scene` lines come first, in insertion order. `indicator`/`lock`/`notice` lines always come
last, in insertion order (they are singleton-ish status lines, not meant to interleave
alphabetically with the rest). Everything else (`pad`, `led`, and any other line added
directly via `StateLines::add`) is ASCII-sorted in between.
