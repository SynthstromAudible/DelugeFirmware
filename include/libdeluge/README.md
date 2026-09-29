# `libdeluge` — the hardware boundary

These headers are the **API boundary** between the portable Deluge application
and the hardware support library (BSP).

## Rules

1. **This is a C ABI.** Every declaration is `extern "C"`. Only fixed-width
   integer types, `bool`, and POD structs cross the boundary. No STL, no
   exceptions, no C++ types, no allocation, and no panics/unwinding may cross it.
   The C ABI is the only thing a future Rust/Embassy BSP (`~/GitHub/deluge-embassy`)
   can implement, so it must stay pure C.

2. **Direction is one-way.** The application includes `<libdeluge/...>`. A BSP
   *implements* these headers and may include the HAL. The HAL includes nothing
   upward. The application must never include a HAL or BSP-implementation header
   — enforced by `scripts/check_platform_boundary.py`.

3. **Two halves:**
   - `app.h` — callbacks the **application implements** and the platform calls
     (`deluge_app_init`, `deluge_app_render`, …). The platform owns `main`; the
     app does not assume it does.
   - everything else — services the **BSP implements** and the app calls
     (audio, MIDI, control surface, display, CV/gate, storage, clock, memory).

4. **Call context is documented per function** as one of:
   - `[isr]` — may be called from interrupt context.
   - `[audio]` — called from / safe to call within the realtime audio callback;
     must not block, allocate, or lock.
   - `[task]` — called from cooperative/task context only.

## Status

Implemented by the Rust BSP (`src/bsp/rust`, on deluge-sdk's `deluge-bsp`), on the
device and on the host.
