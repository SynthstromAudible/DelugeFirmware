# `deluge.cpp` HAL relocation — board bring-up + the control-surface input model

**Status:** DONE on `feat/libdeluge-main-relocation` — all of C1–C5 + the
include drop landed, build-green (Debug+Release link clean). `deluge.cpp` is off
`scripts/platform_boundary_allowlist.txt`; the offender set shrank **4 → 3**
(lint 3, as predicted). **Not yet hardware-tested** — the boot sequence and the
core input path were rewritten, so the full HW matrix below must pass before
merge.

## Outcome (one commit per phase)

- **C1** `signals: route deluge.cpp jack/LED/detect through the boundary` —
  filled in `signals.c` (BATTERY_LED open-drain inverted in the BSP,
  SPEAKER/CODEC enables); inputRoutine/battery LED now use
  `deluge_signal_read/write`.
- **C2** `board: extract one-time HW bring-up behind deluge_board_init*()` — new
  `src/bsp/rza1/board.c` with `deluge_board_init_early/_audio/_storage`,
  `deluge_board_probe_oled`, `deluge_board_unlock_data_cache`; battery ADC →
  `deluge_battery_read_raw/start_conversion` in signals.c. App keeps the
  interleaved app-init calls and the exact bring-up order.
- **C3** `display: relocate OLED panel bring-up + service into the BSP` —
  `setupOLED()` → `deluge_display_init()`; `display.c` → `display.cpp` (drives
  the PIC); `oledRoutine()` → `deluge_display_service()`.
- **C5** `control: move the boot PIC read behind deluge_control_read_boot_info` —
  returns `DelugeBootInfo{pic_firmware_version, oled_present,
  factory_reset_requested}`; app reacts (popup + settings reset).
- **C4** `input: pull the PIC pad/button decode through a BSP event queue` —
  `deluge_control_poll_event()` owns the decode (pad/button → grid x,y, the
  NEXT_PAD_OFF velocity latch, NO_PRESSES, and the OLED transfer-ack). SD
  back-pressure became an app-side held-event retry (`heldInputEvent`), not
  `uartPutCharBack`. OLED-ack surfaces via `deluge_display_consume_transfer_ack()`.
- **Drop includes** `deluge: drop all RZA1/ includes, leave the allowlist` —
  removed cpu_specific/sdif/cache/gpio/oled_low_level/rspi/spibsc (+ unused
  drivers/oled, /ssi, /uart). Only `drivers/pic` (PIC config still app-side, not
  a HAL header) and `fatfs/ff.h` remain.

### Decisions worth remembering (vs the plan's open questions)

- **Coordinate mapping (the plan's open question):** the BSP emits **grid (x,y)**
  for both pads and buttons (board-layout mapping, using `board_layout.hpp`
  dims). The app maps back: pads → `matrixDriver.padAction(x,y,vel)` directly;
  buttons → `deluge::hid::button::fromXY(x,y)` (the exact inverse of the BSP's
  decode). Event `value` carries the velocity/on-state (255 default-press / 0
  release) for both kinds.
- **D-OLED-ack:** the dynamic `oledWaitingForMessage` compare needs OLED
  low-level state, so it lives in the BSP decode; the app still schedules the
  3 ms `TimerName::OLED_LOW_LEVEL` (uiTimerManager is app infra) but off a
  boolean poll, not a raw byte. Fully BSP-owned transfer timing is left as a
  follow-up (would need a BSP-side delayed-service primitive).
- **drivers/* don't gate the lint:** `check_platform_boundary.py` only forbids
  `RZA1/`. So `drivers/pic` etc. can stay; de-allowlisting only required the
  seven `RZA1/` includes to go. The PIC *config* calls (setDebounce, setUARTSpeed,
  setupForPads, waitForFlush, flush…) remain app-side for now — a clean follow-up
  is a `deluge_control_init()`/board control-surface bring-up.

---

**Original plan below.** Suggested branch `feat/libdeluge-deluge-main`.
This takes `deluge.cpp` off `scripts/platform_boundary_allowlist.txt` (→ lint 3;
→ 2 once audio also lands). Simpler than the audio change — no realtime DSP, no
bit-identical-audio constraint — but **not small**: it is the boot file *and* the
core input path, so it needs **hardware testing** (every pad/button, the encoders,
OLED, boot, factory reset, jack detects, and input during SD activity) before
merge. There is no realtime-audio risk, which is why it's a good one to do first.

## Goal

Remove every `RZA1/` and `drivers/` include from `src/deluge/deluge.cpp`. The app's
`main`/boot becomes a thin sequence of app-init steps plus calls to BSP board
bring-up; all hardware register access, the PIC protocol, and the input decode move
into the BSP behind `<libdeluge/...>`.

Current includes: `RZA1/cpu_specific.h`, `RZA1/sdhi/inc/sdif.h`, `drivers/pic/pic.h`,
`RZA1/cache/cache.h`, `RZA1/gpio/gpio.h`, `RZA1/oled/oled_low_level.h`,
`drivers/oled/oled.h`, `drivers/ssi/ssi.h`, `drivers/uart/uart.h`, `RZA1/rspi/rspi.h`,
`RZA1/spibsc/spibsc_Deluge_setup.h`.

## The five concerns (and how much each already exists)

| # | Concern | Sites | Boundary | Difficulty |
|---|---|---|---|---|
| C1 | Jack / LED / detect polling | 111–186 | `<libdeluge/signals.h>` — **already exists** | mechanical |
| C2 | Board bring-up (GPIO mux, RSPI/CV, OLED DMA, SSI, SPIBSC, cache) | 630–725 | new `deluge_board_init*()` | mechanical bulk |
| C3 | OLED bring-up + per-frame drive | 475–495, 619 | display port + C2 | medium |
| C4 | **Runtime input decode** (`readButtonsAndPads`) | 250–313 | `deluge_control_poll_event` — **declared, not implemented** | the design |
| C5 | Boot PIC read (firmware version, factory-reset detect) | 705–760 | new BSP boot-read | medium |

**Only when all five land does `deluge.cpp` leave the allowlist** — it keeps
`pic.h` (for C4's decode) and its other `RZA1/` includes regardless of partial
work. So C4 is the gating item; the rest is bulk.

## C1 — Jack/LED/detect → `signals.h` (the easy win, do first)

`deluge.cpp` calls `setOutputState`/`readInput`/`setPinAsOutput` on the
`constexpr Pin` table (`BATTERY_LED`, `SYNCED_LED`, `CODEC`, `SPEAKER_ENABLE`,
`HEADPHONE_DETECT`, `LINE_IN_DETECT`, `MIC_DETECT`, `LINE_OUT_DETECT_L/R`). These
are **exactly** the named lines already in `<libdeluge/signals.h>`
(`DELUGE_SIGNAL_*`). `signals.c` implements only a subset today (SYNC_LED + the
detects); wire up the rest (BATTERY_LED is open-drain → invert inside the BSP, the
comment in `signals.c` already flags this) and repoint `deluge.cpp` to
`deluge_signal_read/write`. Removes the jack/LED use of `gpio.h`.

## C2 — Board bring-up → `deluge_board_init()`

Lines ~630–725 are one-time hardware bring-up: GPIO direction/mux for LEDs, detects,
analog sense, the CV SPI pins; `R_RSPI_Create/Start` for the CV DAC; `oledDMAInit`;
`ssiInit`; `setPinMux` + `initSPIBSC`; cache. This is classic `board_init()` and
belongs in the BSP.

**The catch:** boot is *interleaved* — app-init calls (`cvEngine.init()`,
`AudioEngine::init()`, `audioFileManager.init()`) sit between hardware steps, and
the ordering matters (the comment at `initSPIBSC` notes it "will run the audio
routine," so external RAM/audio must be up first). So you cannot lift the whole
function down wholesale.

**Approach:** split the hardware bring-up into a few ordered BSP entry points the
app calls at the existing points — e.g. `deluge_board_init_early()` (pins, CV SPI,
cache, before app subsystems) and `deluge_board_init_storage()` (SPIBSC, after audio
is running) — rather than one monolith. Keep the app-init calls in `deluge.cpp`. The
`constexpr Pin` map moves into the BSP (it already lives, duplicated as comments, in
`signals.c`); make the BSP the single source of truth. Removes `cache.h`, `rspi.h`,
`spibsc_Deluge_setup.h`, `sdif.h`, `ssi.h`, and the bring-up half of `gpio.h`.

## C3 — OLED bring-up + drive → display port

Lines 475–495 poke RSPI registers directly (`RSPI0.SPDCR`/`SPCMD0`/`SPBFCR`) and
drive `PIC::setDCLow/enableOLED/selectOLED/deselectOLED/flush` + `oledMainInit`
around the panel init. This is OLED panel bring-up over the shared CV/OLED SPI. Move
the register setup into the BSP (alongside `oledMainInit`, already a BSP driver) and
expose a `deluge_display_init()` (or fold into C2). The runtime OLED select/DC lines
are PIC ops → behind the display/control_surface boundary. Removes
`oled_low_level.h`, `drivers/oled/oled.h`, and the OLED use of `rspi.h`/`pic.h`.
(Note C3's transfer-completion handshake is the same `oledWaitingForMessage` ack
handled in C4/D-OLED-ack — coordinate the two.)

## C4 — The runtime input model → `deluge_control_poll_event` (the design)

`readButtonsAndPads` (250–313) reads **raw PIC bytes** off one UART channel
(`uartGetChar(UART_ITEM_PIC)`) and the app demuxes a **multiplexed** stream:

1. **pad/button events** (`value < kPadAndButtonMessagesEnd`): `Pad::isPad` →
   `matrixDriver.padAction(x,y,vel)`, else `Buttons::buttonAction(...)`.
2. **velocity state** (`NEXT_PAD_OFF` → `nextPadPressIsOn`): a flag consumed by the
   *next* pad message.
3. **`NO_PRESSES_HAPPENING`**: a periodic "all clear" → `matrixDriver`/`Buttons`
   `noPressesHappening`.
4. **the OLED transfer-ack** (`value == oledWaitingForMessage`): *display* flow
   control riding the input channel → schedules the next OLED low-level transfer.
5. **SD back-pressure**: if a pad/button action returns
   `REMIND_ME_OUTSIDE_CARD_ROUTINE`, the byte is pushed back
   (`uartPutCharBack`) and input stalls (`waitingForSDRoutineToEnd`) until the SD
   routine ends.

### Target design (pull-model event queue — same shape as card-detect/USB events)

The BSP decodes the PIC channel into a **queue of `DelugeInputEvent`** (the struct
already exists: PAD/BUTTON/ENCODER + x,y,value); the app drains via
`deluge_control_poll_event(&ev)` each tick. Resolutions:

- **D-velocity:** `NEXT_PAD_OFF` is consumed *in the BSP* — it sets the velocity for
  the next pad message, which is emitted as a PAD event with `value` = velocity (0 =
  release). The app no longer tracks `nextPadPressIsOn`.
- **D-nopresses:** surface as a distinct event kind (`DELUGE_EVENT_NO_PRESSES`) or a
  small separate poll; the app maps it to `noPressesHappening`.
- **D-OLED-ack:** the OLED transfer-ack must **not** surface as an input event. The
  BSP consumes it and drives the next OLED transfer internally (the app pushes frames
  through the display port; the BSP owns transfer completion). This removes the
  display/input channel-multiplexing from the app entirely — the cleanest outcome,
  and it's shared with C3.
- **D-backpressure:** drop `uartPutCharBack`. Decoded events live in the BSP queue,
  so there is nothing to "put back" — when the app can't process an event during SD
  activity it simply stops draining; the event waits in the queue and is re-polled
  later. The `isSDRoutineActive()` gate stays app-side (it already does, via the
  scheduler `isSDRoutineActive()` from the storage-yield work).

This removes `pic.h` and `uart.h` (input half) from `deluge.cpp`. The PIC decode
(`Pad::isPad`, `kPadAndButtonMessagesEnd`, the `Response` enum) moves into the BSP
`control_surface.cpp`, which already owns the PIC protocol.

**Open question — `Pad::isPad`/coordinate mapping:** today the app maps raw PIC pad
codes to grid (x,y) via `Pad`. Decide whether that mapping is board layout (→ BSP,
emit (x,y) directly in the event) or app convention (→ keep app-side, emit the raw
code). Board layout is the cleaner boundary (the event carries grid coords), and it
matches the `DelugeInputEvent` doc ("x,y = grid coords").

## C5 — Boot PIC read → BSP boot-read

The boot read loop (`PIC::read(0x8000, lambda)`, 705–760) reads the PIC firmware
version + OLED-present bit and detects a held select-knob for factory reset
(`RESET_SETTINGS`). This is a one-shot, different from the runtime decode. Expose a
BSP boot query — e.g. `deluge_control_read_boot_info(DelugeBootInfo*)` returning
`{pic_firmware_version, oled_present, factory_reset_requested}` — and let the app
react (popup, `FlashStorage::resetSettings`). Removes the last `pic.h` use.

## Phases (each build-green; HW-tested before merge)

1. **C1 signals** — repoint jack/LED/detect to `signals.h`; fill in `signals.c`.
2. **C2 board init** — extract `deluge_board_init*()`; move the pin map to the BSP.
3. **C3 OLED bring-up** — relocate the RSPI/OLED panel init.
4. **C5 boot-read** — `deluge_control_read_boot_info`.
5. **C4 input model** — the event queue + velocity/nopresses/OLED-ack/backpressure
   resolutions. The centerpiece; do it deliberately and test heavily.
6. **Drop includes** → off the allowlist (**lint 3**).

(C1–C3, C5 are mechanical and independently shippable; none de-allowlists alone, but
they shrink C4's surface and are individually low-risk. C4 is the gating design.)

## Hardware test matrix (before merge)

- **Every pad + button**: press/release, velocity (audition pads), shift stickiness,
  the SHIFT-LED feedback path.
- **Encoders**: detents + push (note: `hid/encoder*.cpp` stay on the allowlist /
  `feat/encoder-overhaul` — confirm the event path interoperates, don't touch them).
- **`NO_PRESSES_HAPPENING`/stuck-key recovery**.
- **Input during SD activity**: heavy card I/O while pressing pads — the
  back-pressure path (events defer, none lost, none double-fired).
- **OLED**: boot splash, refresh under load, the transfer-ack pump (C3/C4) — no
  tearing/stalls; 7SEG variant unaffected.
- **Boot**: cold boot, the firmware-version read, **factory reset** (hold select on
  boot), OLED-present detection on both OLED and 7SEG hardware.
- **Jack detects**: headphone/line-in/mic/line-out insert/remove; speaker-enable;
  battery LED.

## Risk notes

- C4 is the core input path — a dropped/duplicated/reordered event is immediately
  felt. The queue model must preserve ordering and the SD-deferral semantics exactly.
- The OLED-ack demux (D-OLED-ack) is the subtle cross-cutting piece — it couples the
  display transfer pump to what was the input channel. Getting it out of the app is
  the cleanest result but must be verified on OLED hardware (and confirmed inert on
  7SEG).
- Boot ordering (C2) is load-bearing (SPIBSC-after-audio, etc.) — preserve the exact
  sequence; split board-init at the existing app-init seams, don't reorder.
- The `constexpr Pin` table is currently shared (definitions_cxx.hpp) and mirrored in
  `signals.c` comments — make the BSP authoritative as part of C2 so there's one map.
