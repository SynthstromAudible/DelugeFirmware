# Migrating `StringBuf` → `etl::string` / owned `std::string`

Follow-up to [`string_migration.md`](string_migration.md). That effort removed the custom
heap `String` class in favour of `std::string`, and deliberately left **`StringBuf`** in
place. This effort revisits that: `StringBuf` is replaced by `etl::string` (and, where a
short value is simply returned to a caller, by an owned `std::string`).

## Why change a non-allocating buffer

`StringBuf` ([`util/d_stringbuf.h`](../../src/deluge/util/d_stringbuf.h)) is a non-owning,
fixed-capacity formatter over a caller-supplied `char[]` (via `DEF_STACK_STRING_BUF`). It is
non-allocating — which is correct for the OLED/menu render paths — but:

- It does not own its storage, so it cannot be returned by value or stored; callers must
  declare a separate `char[]` and thread the buffer through.
- Its `appendInt` / `appendHex` / `appendFloat` are **unbounded and can overflow**
  (`d_stringbuf.h:41` TODO); only `append(string_view)` is length-clamped.
- It is a bespoke type rather than a standard-shaped string.

`etl::string<N>` (Embedded Template Library, already a project dependency — used for
`etl::vector` throughout the menu layer) is an **owned, inline, non-allocating** string with
a full `std::string`-like API and **bounds-checked** append/format. It keeps StringBuf's
no-heap property while fixing all three points above, and the capacity-erased base
`etl::istring&` replaces the `StringBuf&` output-parameter idiom.

## Type taxonomy (decided)

| Need | Type | Allocates? |
| --- | --- | --- |
| Build a **short** value (≤15 chars, SSO) and return it to a caller | `std::string` by value | no (SSO) |
| Non-allocating bounded builder: output params, hot render paths, content that can exceed 15 chars | `etl::string<N>` / `etl::istring&` | no (inline) |
| Read-only borrow | `std::string_view` | no |
| Persistent owned member (filenames, paths, song name) | `std::string` (already migrated) | heap (external SDRAM) + SSO |

`std::string` heap data lands in external SDRAM via the global `operator new` override
([`memory/operators.cpp`](../../src/deluge/memory/operators.cpp)); 15-char SSO covers most
short names. `etl::string` never allocates, so it is preferred for per-frame display code.

## Helpers (foundation — landed)

[`util/etl_string.h`](../../src/deluge/util/etl_string.h) adds, in `deluge::string`:

- `append(etl::istring&, std::string_view)` — bridges the codebase's `std::string_view`
  vocabulary type onto etl's `string_view`-based append (etl does not accept
  `std::string_view` directly). `etl::string`'s own `append` already covers `const char*`
  (e.g. `l10n::get(...)`), `char`, literals, and `etl::string_view`.
- `appendInt` / `appendHex` / `appendFloat(etl::istring&, …)` — wrap the existing
  `intToString` / `intToHex` / `floatToString` formatters, so output is **byte-identical** to
  the old `StringBuf::append*` (notably `appendFloat`'s min/max trailing-zero trimming, which
  a plain `etl::to_string` / `fromFloat` would not reproduce).

## Call-site translation

| `StringBuf` | `etl::string` |
| --- | --- |
| `DEF_STACK_STRING_BUF(s, N)` | `etl::string<N> s;` |
| `void f(StringBuf&)` | `void f(etl::istring&)` |
| `s.append(cstr)` / `s.append("lit")` | unchanged (etl accepts `const char*`) |
| `s.append(std::string_view sv)` | `deluge::string::append(s, sv)` |
| `s.appendInt(i, m)` | `deluge::string::appendInt(s, i, m)` |
| `s.appendHex(i, m)` | `deluge::string::appendHex(s, i, m)` |
| `s.appendFloat(f, mn, mx)` | `deluge::string::appendFloat(s, f, mn, mx)` |
| `s.clear()` / `s.empty()` / `s.size()` / `s.c_str()` | unchanged |
| `s.truncate(n)` | `s.resize(n)` (call sites only ever shrink) |
| `s.data()` passed to a `char const*` sink | `s.c_str()` |
| implicit `operator std::string_view()` | `std::string_view{s.c_str(), s.size()}` |

When a helper merely builds a short value and returns it, prefer a `std::string` return via
the existing `deluge::string::fromInt/fromFloat/fromSlot/fromNoteCode`
([`util/string.h`](../../src/deluge/util/string.h)) instead of an `etl::istring&` out-param.

## Sequencing

Separate branch (`stringbuf-etl-migration`), staged into bisectable, build-green waves:

1. [x] Foundation: `etl_string.h` helpers; proof conversion (`decimal.cpp` local buffers).
2. [x] Self-contained popup/status local buffers (not passed to a `StringBuf&` helper):
   `global_effectable.cpp`, `mod_controllable_audio.cpp`, `song.cpp`, `stem_export.cpp`.
3. [ ] `std::string`-return conversions (helpers that build + hand back a short value).
4. [ ] **Output-parameter surface** — the coupled bulk. Functions taking `StringBuf&`
   (`MenuItem::getColumnLabel` / `getNotificationValue` and all overrides;
   `Song::getNoteLengthName` / `getCurrentRootNoteAndScaleName`;
   `View::displayCurrentRootNoteAndScaleName`; etc.) → `etl::istring&`, **together with**
   their local-buffer callers. These cannot move independently: a local buffer passed to such
   a helper must change type when the helper does. Largest wave; mostly mechanical.
5. [ ] Remaining standalone locals → `etl::string<N>`.
6. [ ] `std::string_view` for the read-only consumer; then delete `StringBuf`,
   `DEF_STACK_STRING_BUF`, and `d_stringbuf.{h,cpp}`.

Build ARM hardware per wave (and the host-sim, on branches where it is present); the
`operator new` routing differs between unit tests (`IN_UNIT_TESTS`) and hardware.
```
ARM debug elf .text size: 1689892 (baseline) → 1690464 (after waves 1–2).
```
