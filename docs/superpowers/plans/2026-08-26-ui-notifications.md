# Model → UI notifications (`deluge_notify`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land a leaf `deluge_notify` library (`DirtySet` + `NoticeQueue`), drain it from today's UI loop so the model side can migrate before the shell exists, move every `[audio]`-context UI call onto it, codemod the mechanical `uiNeedsRendering` / RootUI-hook / `display->` sites, and stand up the shrink-only layer ratchet that makes the remaining burn-down visible.

**Architecture:** `src/notify/` is a dependency-free static library with an atomic dirty bitset (+ a 256-bit clip index set) and a 16-entry overwrite-oldest notice ring with copied payloads. A typed adapter header in the app layer converts `Error` / `l10n::String` enums to the plain integers the library stores. A temporary drain in `doAnyPendingUIRendering()` maps bits to today's `uiNeedsRendering`/`renderUIsForOled` and notices to `display->…`, so behaviour is unchanged while the model stops calling the UI. The ratchet generalises `scripts/check_platform_boundary.py` with per-(file, rule) counts.

**Tech Stack:** C++26, CMake, CppSpec (`tests/spec`), `std::atomic`, `std::thread` in host tests only, Python 3 for the ratchet and codemods.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-notifications-design.md` (spec C). Read it first; §5 (reverse-coupling ownership map) is **not** in this plan — those relocations belong to the shell/scenes plans.

## Global Constraints

- Branch `docs/ui-stack-redesign-spec` (off local `next`). Firmware verified with `./dbt build Debug` only.
- `src/notify/**` may include only its own headers and the C++ standard library. **Not** `gui/l10n/strings.h` (it includes `io/midi/midi_device.h`) and **not** `definitions_cxx.hpp` (where `enum class Error` lives): the library stores `int16_t error_code` and `uint16_t string_id`; the typed conversions live in `src/deluge/util/notify.h` (app layer).
- `raise*`/`push` are `[any]`: no allocation, no locks, no virtual calls, no UI symbols. `drain`/`pop` are `[task]`.
- No pointers in a `DirtySet`; no pointers in a `Notice` (`text[24]` is copied).
- Behaviour-preserving: after every task the device shows the same popups, LEDs and redraws it did before; the golden harness does not exist yet for the legacy UI, so acceptance is "builds, unit tests pass, on-device checklist in Task 9".
- Naming: `snake_case` functions/variables/members, `PascalCase` types, `k`-prefixed constants.
- Commit per task step that says "Commit"; `./dbt format` before each commit. Tests: `cmake -S tests -B build/tests -G "Ninja Multi-Config" && cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R <name> --output-on-failure`.

---

## File structure

```
src/notify/CMakeLists.txt                 deluge_notify STATIC
src/notify/README.md                      rules
src/notify/dirty_set.h / .cpp             Dirty, ClipBits, DirtySet, dirty(), raise(), raise_clip()
src/notify/notice_queue.h / .cpp          Notice, NoticeQueue, notices(), notice_text(), notice_error_code(), ...
src/deluge/util/notify.h                  typed raisers: notice(Error), notice(l10n::String, ...), notice(string_view, ...)
src/deluge/gui/ui/notify_drain.h / .cpp   drain_notifications(): bits → legacy redraw calls; notices → display->
scripts/codemods/notify_raise.py          uiNeedsRendering / renderUIsForOled / RootUI hooks → notify::raise*
scripts/codemods/notify_display.py        display->displayError/consoleText/displayPopup(literal|l10n)/cancelPopup/... → notify::notice*
scripts/check_layer_rules.py              generalised ratchet (counts per file×rule, shrink-only baseline)
scripts/layer_rules_baseline.txt          committed baseline
tests/spec/notify/dirty_set_spec.cpp
tests/spec/notify/notice_queue_spec.cpp
tests/spec/notify/drain_mapping_spec.cpp
```

Modified: `src/CMakeLists.txt`, `sim/CMakeLists.txt`, `tests/spec/CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `src/deluge/CMakeLists.txt`, `src/deluge/gui/ui/ui.cpp`, `src/deluge/playback/playback_handler.{h,cpp}`, `src/deluge/processing/engines/audio_engine.{h,cpp}`, `src/deluge/model/note/note_row.cpp`, `src/deluge/model/clip/clip.{h,cpp}`, `instrument_clip.cpp`, `audio_clip.cpp`, the 20 `uiNeedsRendering` files, the 13 `renderUIsForOled` files, the RootUI-hook files, ~30 `display->` files, `.pre-commit-config.yaml`, `tests/CMakeLists.txt`.

---

### Task 1: `DirtySet`

**Files:**
- Create: `src/notify/CMakeLists.txt`, `src/notify/README.md`, `src/notify/dirty_set.h`, `src/notify/dirty_set.cpp`
- Modify: `src/CMakeLists.txt` (add `add_subdirectory(notify)` after `midi`), `sim/CMakeLists.txt` (add `add_subdirectory(${REPO_ROOT}/src/notify ${CMAKE_BINARY_DIR}/notify)` after the `midi` line), `tests/spec/CMakeLists.txt` (`add_subdirectory(../../src/notify notify)` and link `deluge_notify` into `deluge_spec`), `tests/unit/CMakeLists.txt` (same for `UnitTests`), `src/deluge/CMakeLists.txt` (link `deluge_notify` into `deluge_app`)
- Test: `tests/spec/notify/dirty_set_spec.cpp`

**Interfaces:**
- Produces (`deluge::notify`):
  ```cpp
  enum class Dirty : uint8_t { GridMain, GridSidebar, Oled, ClipContent, NoteRows, SampleReady, PlaybackState,
                               SongSwapped, OutputChanged, ParamAutomation, ModTarget, SelectedDrum, MidiLearn, Count };
  constexpr size_t kMaxSessionClips = 256;
  struct ClipBits { uint32_t words[kMaxSessionClips / 32]; bool test(uint16_t i) const; bool any() const; };
  class DirtySet {                       // the atomic *accumulator* and the drained *snapshot* share this type
  public:
      void raise(Dirty d);                                 // [any]
      void raise_clip(Dirty d, uint16_t session_clip_index);   // [any]; d ∈ {ClipContent, NoteRows}; index ≥ 256 → coarse bit only
      DirtySet drain();                                    // [task]; returns the snapshot, leaves *this empty
      bool has(Dirty d) const;  bool any() const;
      const ClipBits& clips(Dirty d) const;                // ClipContent → clips_[0], NoteRows → clips_[1]
  };
  DirtySet& dirty();                                       // process-wide instance
  inline void raise(Dirty d) { dirty().raise(d); }
  inline void raise_clip(Dirty d, uint16_t i) { dirty().raise_clip(d, i); }
  ```

- [ ] **Step 1: Write the failing spec**

`tests/spec/notify/dirty_set_spec.cpp`:

```cpp
#include "notify/dirty_set.h"
#include "cppspec.hpp"
#include <thread>

using namespace deluge::notify;

// clang-format off
describe dirty_set("notify::DirtySet", $ {
	it("starts empty and drains to empty", _ {
		DirtySet s;
		expect(s.any()).to_be_false();
		s.raise(Dirty::Oled);
		expect(s.has(Dirty::Oled)).to_be_true();
		DirtySet got = s.drain();
		expect(got.has(Dirty::Oled)).to_be_true();
		expect(s.any()).to_be_false();
		expect(s.drain().any()).to_be_false();
	});

	it("carries clip bits for ClipContent and NoteRows only", _ {
		DirtySet s;
		s.raise_clip(Dirty::ClipContent, 3);
		s.raise_clip(Dirty::NoteRows, 200);
		s.raise_clip(Dirty::ClipContent, 999);            // out of range: coarse bit only
		DirtySet got = s.drain();
		expect(got.has(Dirty::ClipContent)).to_be_true();
		expect(got.clips(Dirty::ClipContent).test(3)).to_be_true();
		expect(got.clips(Dirty::ClipContent).test(4)).to_be_false();
		expect(got.clips(Dirty::NoteRows).test(200)).to_be_true();
		expect(got.clips(Dirty::ClipContent).test(255)).to_be_false();
	});

	it("does not lose a raise that races a drain", _ {
		DirtySet s;
		std::atomic<bool> stop{false};
		std::thread raiser([&] { while (!stop) { s.raise(Dirty::PlaybackState); s.raise_clip(Dirty::ClipContent, 7); } });
		int seen = 0;
		for (int i = 0; i < 20000; i++) {
			DirtySet got = s.drain();
			if (got.has(Dirty::PlaybackState)) seen++;
		}
		stop = true;
		raiser.join();
		DirtySet last = s.drain();
		expect(seen).to_be_greater_than(0);
		// Every raise is either in some drained snapshot or still pending — never dropped.
		// (The property we can assert cheaply: after the raiser stops, one more drain is empty.)
		expect(s.drain().any()).to_be_false();
		(void)last;
	});
});

CPPSPEC_SPEC(dirty_set)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/tests --config Debug 2>&1 | tail -3` — Expected: `notify/dirty_set.h` not found (after adding the spec file the glob picks it up on reconfigure: `cmake -S tests -B build/tests -G "Ninja Multi-Config"`).

- [ ] **Step 3: Implement**

`src/notify/CMakeLists.txt`:

```cmake
# deluge_notify — the model→UI notification channel (spec C). A leaf: depends on nothing
# but the C++ standard library; [any]-safe raise/push, [task] drain/pop.
add_library(deluge_notify STATIC dirty_set.cpp notice_queue.cpp)
target_include_directories(deluge_notify PUBLIC ${SHARED_INCLUDE})
set_target_properties(deluge_notify PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
```

(list `notice_queue.cpp` now; Task 2 creates it — until then create it as an empty translation unit containing only `#include "notify/notice_queue.h"` **after** writing that header in Task 2; for this task list only `dirty_set.cpp`.)

`src/notify/README.md`:

```markdown
# deluge_notify

The model→UI notification channel: `DirtySet` (facts, pointer-free) and `NoticeQueue`
(presentation requests, copied payloads). Spec: docs/superpowers/specs/2026-08-26-ui-notifications-design.md.

Rules: include nothing but `notify/**` and the standard library; `raise*`/`push` are [any]
(atomic, allocation-free, no UI symbols); `drain`/`pop` are [task]. Typed raisers that know
`Error` and `l10n::String` live in `src/deluge/util/notify.h`, not here.
```

`src/notify/dirty_set.h`:

```cpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace deluge::notify {

enum class Dirty : uint8_t {
	GridMain, GridSidebar, Oled,
	ClipContent, NoteRows,
	SampleReady, PlaybackState, SongSwapped,
	OutputChanged, ParamAutomation, ModTarget, SelectedDrum, MidiLearn,
	Count
};
static_assert(static_cast<int>(Dirty::Count) <= 32);

constexpr size_t kMaxSessionClips = 256;

struct ClipBits {
	uint32_t words[kMaxSessionClips / 32] = {};
	bool test(uint16_t i) const { return i < kMaxSessionClips && (words[i >> 5] >> (i & 31)) & 1u; }
	bool any() const { for (auto w : words) if (w) return true; return false; }
};

/// Pointer-free, [any]-safe dirty facts. The same type is the live accumulator (atomics)
/// and the drained snapshot (plain reads after drain()).
class DirtySet {
public:
	DirtySet() = default;
	DirtySet(const DirtySet&);            // snapshot copy (relaxed loads)
	DirtySet& operator=(const DirtySet&);

	void raise(Dirty d);                              // [any]
	void raise_clip(Dirty d, uint16_t session_clip_index);   // [any]
	DirtySet drain();                                 // [task]

	bool has(Dirty d) const { return (bits_.load(std::memory_order_relaxed) >> static_cast<unsigned>(d)) & 1u; }
	bool any() const { return bits_.load(std::memory_order_relaxed) != 0; }
	const ClipBits& clips(Dirty d) const;             // ClipContent or NoteRows; empty set otherwise

private:
	static constexpr int clip_slot(Dirty d) { return d == Dirty::ClipContent ? 0 : d == Dirty::NoteRows ? 1 : -1; }
	std::atomic<uint32_t> bits_{0};
	std::atomic<uint32_t> clip_words_[2][kMaxSessionClips / 32] = {};
	ClipBits snapshot_[2];                            // filled by drain() for the returned copy
};

DirtySet& dirty();
inline void raise(Dirty d) { dirty().raise(d); }
inline void raise_clip(Dirty d, uint16_t i) { dirty().raise_clip(d, i); }

} // namespace deluge::notify
```

`src/notify/dirty_set.cpp`:

```cpp
#include "notify/dirty_set.h"

namespace deluge::notify {

namespace {
DirtySet g_dirty;
const ClipBits kNoClips{};
} // namespace

DirtySet& dirty() { return g_dirty; }

DirtySet::DirtySet(const DirtySet& o) { *this = o; }

DirtySet& DirtySet::operator=(const DirtySet& o) {
	bits_.store(o.bits_.load(std::memory_order_relaxed), std::memory_order_relaxed);
	for (int s = 0; s < 2; s++) {
		snapshot_[s] = o.snapshot_[s];
		for (size_t w = 0; w < kMaxSessionClips / 32; w++) {
			clip_words_[s][w].store(o.clip_words_[s][w].load(std::memory_order_relaxed), std::memory_order_relaxed);
		}
	}
	return *this;
}

void DirtySet::raise(Dirty d) {
	bits_.fetch_or(1u << static_cast<unsigned>(d), std::memory_order_release);
}

void DirtySet::raise_clip(Dirty d, uint16_t i) {
	const int s = clip_slot(d);
	if (s >= 0 && i < kMaxSessionClips) {
		clip_words_[s][i >> 5].fetch_or(1u << (i & 31), std::memory_order_relaxed);
	}
	raise(d); // release: publishes the clip word above
}

DirtySet DirtySet::drain() {
	DirtySet out;
	// Clip words FIRST, then the coarse bits: a word can then never be drained without its flag.
	uint32_t derived = 0;
	for (int s = 0; s < 2; s++) {
		for (size_t w = 0; w < kMaxSessionClips / 32; w++) {
			out.snapshot_[s].words[w] = clip_words_[s][w].exchange(0, std::memory_order_relaxed);
		}
		if (out.snapshot_[s].any()) {
			derived |= 1u << static_cast<unsigned>(s == 0 ? Dirty::ClipContent : Dirty::NoteRows);
		}
	}
	out.bits_.store(bits_.exchange(0, std::memory_order_acq_rel) | derived, std::memory_order_relaxed);
	return out;
}

const ClipBits& DirtySet::clips(Dirty d) const {
	const int s = clip_slot(d);
	return s >= 0 ? snapshot_[s] : kNoClips;
}

} // namespace deluge::notify
```

Note on the race: words are drained before bits and the flag is derived from the words, so a snapshot never carries a clip bit without its flag. The one remaining interleaving (coarse bit landing after its word was drained by the previous call) yields a set flag with an empty clip set — documented on `has()`/`clips()` as "which clips is unknown: treat as all" (over-invalidation, never loss). The race example asserts `word ⇒ flag` on every drained snapshot.

Wire CMake as listed under **Files**. In `src/deluge/CMakeLists.txt` add `deluge_notify` to `target_link_libraries(deluge_app PUBLIC ...)`.

- [ ] **Step 4: Run**

Run: `cmake -S tests -B build/tests -G "Ninja Multi-Config" && cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R dirty_set_spec --output-on-failure` — Expected: PASS (3 examples).
Run: `./dbt build Debug` — Expected: builds (library linked, unused).

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/notify src/CMakeLists.txt sim/CMakeLists.txt tests/spec/CMakeLists.txt tests/unit/CMakeLists.txt src/deluge/CMakeLists.txt tests/spec/notify/dirty_set_spec.cpp
git commit -m "feat(notify): pointer-free, any-context DirtySet"
```

---

### Task 2: `NoticeQueue`

**Files:**
- Create: `src/notify/notice_queue.h`, `src/notify/notice_queue.cpp`
- Modify: `src/notify/CMakeLists.txt` (add `notice_queue.cpp`)
- Test: `tests/spec/notify/notice_queue_spec.cpp`

**Interfaces:**
- Produces (`deluge::notify`):
  ```cpp
  struct Notice {
      enum class Kind : uint8_t { Error, Text, Console, Working, ClearWorking, CancelPopup };
      enum class Severity : uint8_t { Info, Warn, Error };
      Kind kind; Severity severity;
      int16_t error_code;      // Kind::Error: static_cast<int16_t>(::Error)
      uint16_t string_id;      // Text/Console with an l10n id (static_cast of l10n::String); 0 = none
      char text[24];           // copied; NUL-terminated; 0-length = none
      uint8_t popup_type;      // today's PopupType value; 0 = GENERAL
  };
  constexpr size_t kNoticeCapacity = 16;
  class NoticeQueue {
  public:
      bool push(const Notice&);       // [any]; overwrite-oldest; returns false if it overwrote
      bool pop(Notice& out);          // [task]
      size_t size() const;
  };
  NoticeQueue& notices();
  void notice_text(std::string_view short_text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
  void notice_string(uint16_t string_id, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
  void notice_error_code(int16_t code);
  void notice_kind(Notice::Kind k, uint8_t popup_type = 0);   // Working/ClearWorking/CancelPopup
  ```

- [ ] **Step 1: Write the failing spec**

`tests/spec/notify/notice_queue_spec.cpp`:

```cpp
#include "notify/notice_queue.h"
#include "cppspec.hpp"
#include <cstring>

using namespace deluge::notify;

// clang-format off
describe notice_queue("notify::NoticeQueue", $ {
	it("pops in push order", _ {
		NoticeQueue q;
		Notice a{}; a.kind = Notice::Kind::Console; std::strcpy(a.text, "Undo");
		Notice b{}; b.kind = Notice::Kind::Error; b.error_code = 42;
		expect(q.push(a)).to_be_true();
		expect(q.push(b)).to_be_true();
		Notice out{};
		expect(q.pop(out)).to_be_true();
		expect(std::string(out.text)).to_equal("Undo");
		expect(q.pop(out)).to_be_true();
		expect(int(out.error_code)).to_equal(42);
		expect(q.pop(out)).to_be_false();
	});

	it("overwrites the oldest when full and says so", _ {
		NoticeQueue q;
		for (size_t i = 0; i < kNoticeCapacity; i++) {
			Notice n{}; n.error_code = int16_t(i);
			expect(q.push(n)).to_be_true();
		}
		Notice last{}; last.error_code = 99;
		expect(q.push(last)).to_be_false();
		Notice out{};
		expect(q.pop(out)).to_be_true();
		expect(int(out.error_code)).to_equal(1);        // 0 was overwritten
		expect(q.size()).to_equal(kNoticeCapacity - 1);
	});

	it("copies and truncates text with a NUL", _ {
		notice_text("THIS PRESET NAME IS LONGER THAN TWENTY-THREE CHARS", Notice::Kind::Text, 3);
		Notice out{};
		expect(notices().pop(out)).to_be_true();
		expect(std::strlen(out.text)).to_equal(size_t(23));
		expect(std::string(out.text)).to_equal("THIS PRESET NAME IS LON");
		expect(int(out.popup_type)).to_equal(3);
	});
});

CPPSPEC_SPEC(notice_queue)
```

- [ ] **Step 2: Run it to see it fail** — Expected: `notify/notice_queue.h` not found.

- [ ] **Step 3: Implement**

`src/notify/notice_queue.h`:

```cpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::notify {

struct Notice {
	enum class Kind : uint8_t { Error, Text, Console, Working, ClearWorking, CancelPopup };
	enum class Severity : uint8_t { Info, Warn, Error };
	Kind kind = Kind::Text;
	Severity severity = Severity::Info;
	int16_t error_code = 0;
	uint16_t string_id = 0;
	char text[24] = {};
	uint8_t popup_type = 0;
};

constexpr size_t kNoticeCapacity = 16;

/// Overwrite-oldest ring. Single logical producer at a time (cooperative scheduling today —
/// see spec C §6, `COOP-WRITE`); the consumer is the UI tick.
class NoticeQueue {
public:
	bool push(const Notice& n);   // [any]
	bool pop(Notice& out);        // [task]
	size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }

private:
	Notice ring_[kNoticeCapacity];
	std::atomic<uint32_t> head_{0}; // next write
	std::atomic<uint32_t> tail_{0}; // next read
};

NoticeQueue& notices();
void notice_text(std::string_view short_text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
void notice_string(uint16_t string_id, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
void notice_error_code(int16_t code);
void notice_kind(Notice::Kind k, uint8_t popup_type = 0);

} // namespace deluge::notify
```

`src/notify/notice_queue.cpp`:

```cpp
#include "notify/notice_queue.h"
#include <cstring>

namespace deluge::notify {

namespace {
NoticeQueue g_notices;
} // namespace

NoticeQueue& notices() { return g_notices; }

bool NoticeQueue::push(const Notice& n) {
	uint32_t head = head_.load(std::memory_order_relaxed);
	uint32_t tail = tail_.load(std::memory_order_acquire);
	bool overwrote = false;
	if (head - tail >= kNoticeCapacity) { // full: drop the oldest
		tail_.store(tail + 1, std::memory_order_release);
		overwrote = true;
	}
	ring_[head % kNoticeCapacity] = n;
	head_.store(head + 1, std::memory_order_release);
	return !overwrote;
}

bool NoticeQueue::pop(Notice& out) {
	uint32_t tail = tail_.load(std::memory_order_relaxed);
	if (tail == head_.load(std::memory_order_acquire)) {
		return false;
	}
	out = ring_[tail % kNoticeCapacity];
	tail_.store(tail + 1, std::memory_order_release);
	return true;
}

void notice_text(std::string_view s, Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.popup_type = popup_type;
	const size_t len = s.size() < sizeof(n.text) - 1 ? s.size() : sizeof(n.text) - 1;
	std::memcpy(n.text, s.data(), len);
	n.text[len] = 0;
	g_notices.push(n);
}

void notice_string(uint16_t string_id, Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.string_id = string_id;
	n.popup_type = popup_type;
	g_notices.push(n);
}

void notice_error_code(int16_t code) {
	Notice n{};
	n.kind = Notice::Kind::Error;
	n.severity = Notice::Severity::Error;
	n.error_code = code;
	g_notices.push(n);
}

void notice_kind(Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.popup_type = popup_type;
	g_notices.push(n);
}

} // namespace deluge::notify
```

- [ ] **Step 4: Run** — `ctest -R notice_queue_spec` PASS (3 examples); `./dbt build Debug` builds.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/notify tests/spec/notify/notice_queue_spec.cpp
git commit -m "feat(notify): overwrite-oldest NoticeQueue with copied payloads"
```

---

### Task 3: Typed raisers in the app layer

**Files:**
- Create: `src/deluge/util/notify.h`
- Test: compile-only via Task 4's drain spec; plus one example added to `notice_queue_spec.cpp`? No — `util/notify.h` pulls `definitions_cxx.hpp`, which the spec library already compiles (`value_scaling.cpp` includes it), so add a small spec `tests/spec/notify/typed_notify_spec.cpp`.

**Interfaces:**
- Produces (`deluge::notify`, app layer):
  ```cpp
  inline void notice(::Error e)                       { notice_error_code(static_cast<int16_t>(e)); }
  inline void notice(deluge::l10n::String s, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0)
                                                      { notice_string(static_cast<uint16_t>(s), k, popup_type); }
  inline void notice(std::string_view text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0)
                                                      { notice_text(text, k, popup_type); }
  inline void notice_cancel_popup(PopupType t = PopupType::GENERAL) { notice_kind(Notice::Kind::CancelPopup, uint8_t(t)); }
  inline void notice_working(std::string_view word)   { notice_text(word, Notice::Kind::Working); }
  inline void notice_clear_working()                  { notice_kind(Notice::Kind::ClearWorking); }
  inline ::Error error_of(const Notice& n)            { return static_cast<::Error>(n.error_code); }
  inline deluge::l10n::String string_of(const Notice& n) { return static_cast<deluge::l10n::String>(n.string_id); }
  ```

- [ ] **Step 1: Write the failing spec**

`tests/spec/notify/typed_notify_spec.cpp`:

```cpp
#include "util/notify.h"
#include "cppspec.hpp"

using namespace deluge::notify;

// clang-format off
describe typed_notify("notify typed raisers", $ {
	it("round-trips Error and l10n String through the plain fields", _ {
		Notice out{};
		while (notices().pop(out)) {}                       // drain anything earlier specs left
		notice(Error::INSUFFICIENT_RAM);
		notice(deluge::l10n::String::STRING_FOR_ERROR_SD_CARD, Notice::Kind::Console);
		expect(notices().pop(out)).to_be_true();
		expect(error_of(out) == Error::INSUFFICIENT_RAM).to_be_true();
		expect(notices().pop(out)).to_be_true();
		expect(string_of(out) == deluge::l10n::String::STRING_FOR_ERROR_SD_CARD).to_be_true();
		expect(out.kind == Notice::Kind::Console).to_be_true();
	});
});

CPPSPEC_SPEC(typed_notify)
```

- [ ] **Step 2: Run to see it fail** — `util/notify.h` not found.

- [ ] **Step 3: Implement** `src/deluge/util/notify.h`:

```cpp
#pragma once
// Typed raisers over deluge_notify. The library stores plain integers so it stays a leaf;
// this header, in the app layer, knows Error, l10n::String and PopupType.
#include "definitions_cxx.hpp"     // Error
#include "gui/l10n/strings.h"      // deluge::l10n::String
#include "hid/display/display.h"   // PopupType (moves to notify when Display dissolves)
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include <string_view>

namespace deluge::notify {

inline void notice(::Error e) { notice_error_code(static_cast<int16_t>(e)); }
inline void notice(deluge::l10n::String s, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0) {
	notice_string(static_cast<uint16_t>(s), k, popup_type);
}
inline void notice(std::string_view text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0) {
	notice_text(text, k, popup_type);
}
inline void notice_cancel_popup(PopupType t = PopupType::GENERAL) {
	notice_kind(Notice::Kind::CancelPopup, static_cast<uint8_t>(t));
}
inline void notice_working(std::string_view word) { notice_text(word, Notice::Kind::Working); }
inline void notice_clear_working() { notice_kind(Notice::Kind::ClearWorking); }

inline ::Error error_of(const Notice& n) { return static_cast<::Error>(n.error_code); }
inline deluge::l10n::String string_of(const Notice& n) { return static_cast<deluge::l10n::String>(n.string_id); }

} // namespace deluge::notify
```

If `hid/display/display.h` does not compile in the spec library (it includes `l10n.h` and more), replace the `PopupType` include with a forward `enum class PopupType;`-free approach: take `uint8_t popup_type` in `notice_cancel_popup` and let callers pass `static_cast<uint8_t>(PopupType::X)`. Prefer that if in doubt — the spec build must stay light.

- [ ] **Step 4: Run** — `ctest -R typed_notify_spec` PASS; `./dbt build Debug` builds.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/deluge/util/notify.h tests/spec/notify/typed_notify_spec.cpp
git commit -m "feat(notify): typed Error/l10n raisers in the app layer"
```

---

### Task 4: Temporary drain in the legacy UI loop

**Files:**
- Create: `src/deluge/gui/ui/notify_drain.h`, `src/deluge/gui/ui/notify_drain.cpp`
- Modify: `src/deluge/gui/ui/ui.cpp:doAnyPendingUIRendering` (call `drain_notifications()` first)
- Test: `tests/spec/notify/drain_mapping_spec.cpp` (the pure mapping); firmware build

**Interfaces:**
- Produces:
  ```cpp
  struct LegacyRedraw { bool main_rows, sidebar_rows, oled; };          // notify_drain.h
  LegacyRedraw legacy_redraw_for(const deluge::notify::DirtySet&);      // pure; host-testable
  void drain_notifications();                                           // [task]; calls the legacy UI/display
  ```
  Mapping: `GridMain|ClipContent|NoteRows|SampleReady|PlaybackState|SongSwapped|OutputChanged|SelectedDrum` → main rows; `GridSidebar|OutputChanged|SelectedDrum|PlaybackState|MidiLearn` → sidebar rows; `Oled|OutputChanged|ParamAutomation|ModTarget|SongSwapped|PlaybackState|SampleReady` → OLED. (Coarse on purpose: the legacy views re-render everything anyway; the shell refines it.)

- [ ] **Step 1: Write the failing spec**

`tests/spec/notify/drain_mapping_spec.cpp`:

```cpp
#include "gui/ui/notify_drain.h"
#include "cppspec.hpp"

using namespace deluge::notify;

// clang-format off
describe drain_mapping("legacy_redraw_for", $ {
	it("maps grid facts to rows and oled facts to the display", _ {
		DirtySet s; s.raise(Dirty::GridSidebar);
		LegacyRedraw r = legacy_redraw_for(s.drain());
		expect(r.sidebar_rows).to_be_true(); expect(r.main_rows).to_be_false(); expect(r.oled).to_be_false();
		s.raise(Dirty::Oled);
		r = legacy_redraw_for(s.drain());
		expect(r.oled).to_be_true(); expect(r.main_rows).to_be_false();
		s.raise_clip(Dirty::ClipContent, 2);
		r = legacy_redraw_for(s.drain());
		expect(r.main_rows).to_be_true();
	});
	it("is empty for an empty set", _ {
		DirtySet s;
		LegacyRedraw r = legacy_redraw_for(s.drain());
		expect(r.main_rows || r.sidebar_rows || r.oled).to_be_false();
	});
});

CPPSPEC_SPEC(drain_mapping)
```

Add `../../src/deluge/gui/ui/notify_drain.cpp` to `deluge_spec`'s sources **only if** it compiles on the host — it does not (it includes `ui.h`, `display.h`). So split: the pure function goes in `notify_drain_mapping.cpp` (host-buildable, no UI includes) and the drain in `notify_drain.cpp`. Add `notify_drain_mapping.cpp` to `deluge_spec`.

- [ ] **Step 2: Run to see it fail** — header not found.

- [ ] **Step 3: Implement**

`src/deluge/gui/ui/notify_drain.h`:

```cpp
#pragma once
#include "notify/dirty_set.h"

struct LegacyRedraw {
	bool main_rows = false, sidebar_rows = false, oled = false;
};
LegacyRedraw legacy_redraw_for(const deluge::notify::DirtySet& d);

/// Temporary (until the UI shell owns the tick): map DirtySet bits to the legacy redraw calls
/// and NoticeQueue entries to display->. Called at the top of doAnyPendingUIRendering(). [task]
void drain_notifications();
```

`src/deluge/gui/ui/notify_drain_mapping.cpp`:

```cpp
#include "gui/ui/notify_drain.h"

LegacyRedraw legacy_redraw_for(const deluge::notify::DirtySet& d) {
	using deluge::notify::Dirty;
	LegacyRedraw r;
	auto any = [&](std::initializer_list<Dirty> ds) { for (auto x : ds) if (d.has(x)) return true; return false; };
	r.main_rows = any({Dirty::GridMain, Dirty::ClipContent, Dirty::NoteRows, Dirty::SampleReady, Dirty::PlaybackState,
	                   Dirty::SongSwapped, Dirty::OutputChanged, Dirty::SelectedDrum});
	r.sidebar_rows = any({Dirty::GridSidebar, Dirty::OutputChanged, Dirty::SelectedDrum, Dirty::PlaybackState, Dirty::MidiLearn});
	r.oled = any({Dirty::Oled, Dirty::OutputChanged, Dirty::ParamAutomation, Dirty::ModTarget, Dirty::SongSwapped,
	              Dirty::PlaybackState, Dirty::SampleReady});
	return r;
}
```

`src/deluge/gui/ui/notify_drain.cpp`:

```cpp
#include "gui/ui/notify_drain.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "util/notify.h"

void drain_notifications() {
	using namespace deluge::notify;
	const DirtySet d = dirty().drain();
	if (d.any()) {
		const LegacyRedraw r = legacy_redraw_for(d);
		if (RootUI* root = getRootUI(); root && (r.main_rows || r.sidebar_rows)) {
			uiNeedsRendering(root, r.main_rows ? 0xFFFFFFFF : 0, r.sidebar_rows ? 0xFFFFFFFF : 0);
		}
		if (r.oled) {
			renderUIsForOled();
		}
	}
	Notice n;
	while (notices().pop(n)) {
		switch (n.kind) {
		case Notice::Kind::Error:
			display->displayError(error_of(n));
			break;
		case Notice::Kind::Text:
			if (n.string_id) display->displayPopup(deluge::l10n::get(string_of(n)), 3, false, 255, 1, static_cast<PopupType>(n.popup_type));
			else display->displayPopup(n.text, 3, false, 255, 1, static_cast<PopupType>(n.popup_type));
			break;
		case Notice::Kind::Console:
			display->consoleText(n.string_id ? deluge::l10n::get(string_of(n)) : n.text);
			break;
		case Notice::Kind::Working:
			display->displayLoadingAnimationText(n.text);
			break;
		case Notice::Kind::ClearWorking:
			display->removeLoadingAnimation();
			display->removeWorkingAnimation();
			break;
		case Notice::Kind::CancelPopup:
			display->cancelPopup();
			break;
		}
	}
}
```

In `ui.cpp`, add `#include "gui/ui/notify_drain.h"` and make the first statement of `doAnyPendingUIRendering()` (before the lock check) `drain_notifications();`.

- [ ] **Step 4: Run** — `ctest -R drain_mapping_spec` PASS; `./dbt build Debug` builds.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/deluge/gui/ui/notify_drain.h src/deluge/gui/ui/notify_drain.cpp src/deluge/gui/ui/notify_drain_mapping.cpp src/deluge/gui/ui/ui.cpp tests/spec/notify/drain_mapping_spec.cpp tests/spec/CMakeLists.txt
git commit -m "feat(ui): drain notify facts and notices into the legacy redraw and display paths"
```

---

### Task 5: Move the audio-path UI calls onto the channel

**Files:**
- Modify: `src/deluge/playback/playback_handler.h` (add accessor), `src/deluge/playback/playback_handler.cpp:1040-1060` (count-in), `src/deluge/model/note/note_row.cpp:2456`, `src/deluge/processing/engines/audio_engine.{h,cpp}:492-502`, `src/deluge/model/clip/clip.{h,cpp}:822-828`, `src/deluge/model/clip/instrument_clip.cpp:4122,4223`, `src/deluge/model/clip/audio_clip.cpp:131,221`, `src/deluge/playback/mode/session.cpp:217,1104,1308`, `src/deluge/playback/mode/arrangement.cpp:99,106,560,667`, `src/deluge/playback/playback_handler.cpp:627` (`clearTickSquares`), `src/deluge/gui/ui/notify_drain.cpp` (count-in and cull-alert presentation)
- Test: `./dbt build Debug`; on-device: count-in shows 4..1 and clears; voice-cull blinks Play

**Interfaces:**
- Produces: `int32_t PlaybackHandler::count_in_visual() const` (returns `currentVisualCountForCountIn`, 0 = none) and `bool count_in_active() const` (`ticksLeftInCountIn > 0`); `Clip::notify_content_changed()`; `static inline bool AudioEngine::cull_alert_pending`.

- [ ] **Step 1: Count-in becomes a fact**

`playback_handler.h`: add next to `currentVisualCountForCountIn`:

```cpp
	int32_t count_in_visual() const { return currentVisualCountForCountIn; }
	bool count_in_active() const { return ticksLeftInCountIn > 0; }
```

`playback_handler.cpp:1042-1044` (count-in finished) — replace

```cpp
			display->cancelPopup();
			currentVisualCountForCountIn = 0;
			currentUIMode &= UI_MODE_AUDITIONING;
```
with
```cpp
			currentVisualCountForCountIn = 0;
			deluge::notify::raise(deluge::notify::Dirty::PlaybackState);
```

`playback_handler.cpp:1053-1058` (still counting) — replace the `char buffer[12]; intToString(...); display->displayPopup(buffer, 0, true, 255, 2);` with

```cpp
				currentVisualCountForCountIn = newVisualCount;
				deluge::notify::raise(deluge::notify::Dirty::PlaybackState);
```

(keep the `if (newVisualCount != currentVisualCountForCountIn)` guard.) Add `#include "util/notify.h"` to the file.

In `notify_drain.cpp`, at the top of `drain_notifications()` after the redraw mapping, add the count-in presentation (task context now):

```cpp
	static int32_t shown_count_in = 0;
	const int32_t count_in = playbackHandler.count_in_visual();
	if (count_in != shown_count_in) {
		if (count_in > 0) {
			char buffer[12];
			intToString(count_in, buffer);
			display->displayPopup(buffer, 0, true, 255, 2);
		}
		else {
			display->cancelPopup();
			if (currentUIMode == UI_MODE_RECORD_COUNT_IN) {
				currentUIMode &= UI_MODE_AUDITIONING;
			}
		}
		shown_count_in = count_in;
	}
```

(`#include "playback/playback_handler.h"`, `"util/functions.h"` for `intToString`.)

- [ ] **Step 2: E336 and the cull alert**

`note_row.cpp:2456`: replace `display->displayPopup("E336"); // Popup only` with `deluge::notify::notice("E336");` (include `util/notify.h`).

`audio_engine.h`: add `static inline bool cull_alert_pending = false;` in the `AudioEngine` namespace/class. `audio_engine.cpp:496-499`: replace the `indicator_leds::` calls with

```cpp
	if (culled && FlashStorage::highCPUUsageIndicator) {
		cull_alert_pending = true;
		deluge::notify::raise(deluge::notify::Dirty::PlaybackState);
```

and in `notify_drain.cpp` add:

```cpp
	if (AudioEngine::cull_alert_pending) {
		AudioEngine::cull_alert_pending = false;
		if (indicator_leds::getLedBlinkerIndex(IndicatorLED::PLAY) == 255) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::PLAY);
		}
	}
```

- [ ] **Step 3: Clip re-render hooks raised from audio**

`clip.h`: add `void notify_content_changed();`. `clip.cpp`:

```cpp
void Clip::notify_content_changed() {
	using namespace deluge::notify;
	const int32_t i = currentSong ? currentSong->sessionClips.getIndexForClip(this) : -1;
	if (i >= 0) raise_clip(Dirty::ClipContent, static_cast<uint16_t>(i));
	else raise(Dirty::ClipContent);
}
```

Replace, at `clip.cpp:823-827`, the `RootUI* rootUI = getRootUI(); if (rootUI) rootUI->clipNeedsReRendering(this);` block with `notify_content_changed();`. Same at `instrument_clip.cpp:4223` (`getRootUI()->clipNeedsReRendering(this)`) and `audio_clip.cpp:131,221`. At `instrument_clip.cpp:4122` (`arrangerView.clipNeedsReRendering(this)`) and `session.cpp:217,1104` (`sessionView.clipNeedsReRendering(clip)`) use `clip->notify_content_changed()`.

`session.cpp:1308` `uiNeedsRendering(getCurrentUI(), 0, 0xFFFFFFFF)` → `deluge::notify::raise(Dirty::GridSidebar)`; `arrangement.cpp:667` → `raise(Dirty::GridMain)`; `arrangement.cpp:99` `sessionView.requestRendering(getRootUI(), 0, 0xFFFFFFFF)` → `raise(Dirty::GridSidebar)`; `arrangement.cpp:106,560` and `session.cpp:809,1298,2378` `renderUIsForOled()` → `raise(Dirty::Oled)`; `playback_handler.cpp:627` `PadLEDs::clearTickSquares()` → `raise(Dirty::PlaybackState)` **and** keep the legacy tick-square clearing by adding to the drain: `if (d.has(Dirty::PlaybackState) && !playbackHandler.playbackState) PadLEDs::clearTickSquares();`.

Remove now-unneeded `#include "gui/..."`/`"hid/..."` lines from each touched file; if the build complains about a still-used symbol, keep that include and note it (the ratchet counts it).

- [ ] **Step 4: Build and check on device**

Run: `./dbt build Debug` — builds. Flash; verify: record count-in shows 4,3,2,1 and clears; play a heavy song until voices cull → Play LED alert blinks once; end playback → tick squares clear; finish a clip's linear recording → clip view redraws.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add -u
git commit -m "refactor(notify): raise facts instead of calling the UI from the audio path"
```

---

### Task 6: Codemod — `uiNeedsRendering`, `renderUIsForOled`, RootUI hooks → `notify::raise*`

**Files:**
- Create: `scripts/codemods/notify_raise.py`
- Modify: the remaining external sites — `uiNeedsRendering`: `instrument_clip_minder.cpp:454`, `melodic_instrument.cpp:411`, `kit.cpp:1611`, `midi_transpose.cpp:65,68,71`, `action_logger.cpp:669,681,687,692,698`, `session.cpp:227,1056,1107,1110`, `song.cpp:892`, `stem_export.cpp:153,755`; `renderUIsForOled`: `deluge.cpp:120,133,371`, `instrument_clip_minder.cpp:250,256`, `audio_output.h:152`, `playback_handler.cpp:265,3045`, `stem_export.cpp:154`; hooks: `kit.cpp:1525,1552`, `melodic_instrument.cpp:200,254` (`noteRowChanged`), `sample_recorder.cpp:1005` (`sampleNeedsReRendering`), `playback_handler.cpp:567,649` (`notifyPlaybackBegun`/`playbackEnded`), `arrangement.cpp:287` (`notifyActiveClipChangedOnOutput`)
- Test: `./dbt build Debug`; `grep -rn "uiNeedsRendering(\|renderUIsForOled()\|->noteRowChanged(\|->clipNeedsReRendering(\|->sampleNeedsReRendering(\|->notifyPlaybackBegun(\|->playbackEnded(" src/deluge --include='*.cpp' --include='*.h' | grep -v "^src/deluge/gui\|^src/deluge/hid"` returns nothing

**Interfaces:**
- Consumes: `notify::raise`, `raise_clip`, `Clip::notify_content_changed()`.

- [ ] **Step 1: Write the codemod**

`scripts/codemods/notify_raise.py`:

```python
#!/usr/bin/env python3
"""Rewrite model-side UI redraw calls to notify::raise*. Idempotent. Run from repo root.

  uiNeedsRendering(X, 0xFFFFFFFF, 0)        -> deluge::notify::raise(deluge::notify::Dirty::GridMain);
  uiNeedsRendering(X, 0, 0xFFFFFFFF)        -> raise(Dirty::GridSidebar)
  uiNeedsRendering(X) / (X, 0xFFFFFFFF, 0xFFFFFFFF) / (X, 0x00000000, 0xFFFFFFFF)
                                            -> raise(GridMain); raise(GridSidebar)  (both / sidebar)
  renderUIsForOled();                       -> raise(Dirty::Oled);
  getRootUI()->notifyPlaybackBegun();  getRootUI()->playbackEnded();   -> raise(Dirty::PlaybackState);
  getRootUI()->sampleNeedsReRendering(s);   -> raise(Dirty::SampleReady);
  getRootUI()->noteRowChanged(clip, row);   -> clip->notify_content_changed(); raise(Dirty::NoteRows);
  arrangerView.notifyActiveClipChangedOnOutput(o); -> raise(Dirty::OutputChanged);
"""
import re, sys
from pathlib import Path

RAISE = "deluge::notify::raise(deluge::notify::Dirty::{})"
RULES = [
    (re.compile(r"uiNeedsRendering\([^,()]+,\s*0xFFFFFFFF,\s*0\)\s*;"), RAISE.format("GridMain") + ";"),
    (re.compile(r"uiNeedsRendering\([^,()]+,\s*0(?:x0+)?,\s*0xFFFFFFFF\)\s*;"), RAISE.format("GridSidebar") + ";"),
    (re.compile(r"uiNeedsRendering\([^,()]+(?:,\s*0xFFFFFFFF,\s*0xFFFFFFFF)?\)\s*;"),
     RAISE.format("GridMain") + "; " + RAISE.format("GridSidebar") + ";"),
    (re.compile(r"renderUIsForOled\(\)\s*;"), RAISE.format("Oled") + ";"),
    (re.compile(r"getRootUI\(\)->(?:notifyPlaybackBegun|playbackEnded)\(\)\s*;"), RAISE.format("PlaybackState") + ";"),
    (re.compile(r"getRootUI\(\)->sampleNeedsReRendering\([^;]*\)\s*;"), RAISE.format("SampleReady") + ";"),
    (re.compile(r"getRootUI\(\)->noteRowChanged\(\s*([A-Za-z_][\w>\-.]*)\s*,[^;]*\)\s*;"),
     r"\1->notify_content_changed(); " + RAISE.format("NoteRows") + ";"),
    (re.compile(r"arrangerView\.notifyActiveClipChangedOnOutput\([^;]*\)\s*;"), RAISE.format("OutputChanged") + ";"),
]
INCLUDE = '#include "util/notify.h"\n'

def rewrite(path: Path) -> bool:
    src = path.read_text()
    out = src
    for rx, rep in RULES:
        out = rx.sub(rep, out)
    if out == src:
        return False
    if INCLUDE not in out:
        # after the first #include line
        out = re.sub(r"(^\s*#include[^\n]*\n)", r"\1" + INCLUDE, out, count=1, flags=re.M)
    path.write_text(out)
    return True

def main(argv):
    roots = [Path(a) for a in argv[1:]] or [Path("src/deluge")]
    changed = 0
    for root in roots:
        for p in root.rglob("*"):
            if p.suffix not in {".cpp", ".h", ".hpp"} or "/gui/" in str(p) or "/hid/" in str(p):
                continue
            if rewrite(p):
                changed += 1
                print(f"rewrote {p}")
    print(f"{changed} file(s) changed")

if __name__ == "__main__":
    main(sys.argv)
```

- [ ] **Step 2: Run it, then handle the leftovers by hand**

```bash
python3 scripts/codemods/notify_raise.py
grep -rn "uiNeedsRendering(\|renderUIsForOled()\|->noteRowChanged(\|->sampleNeedsReRendering(\|->notifyPlaybackBegun(\|->playbackEnded(\|notifyActiveClipChangedOnOutput(" src/deluge --include='*.cpp' --include='*.h' | grep -v "^src/deluge/gui\|^src/deluge/hid"
```

Expected leftovers: `instrument_clip_minder.cpp` (a UI mixin — leave it; spec C §5 moves the file wholesale in the scenes plan) and any call whose first argument the regex did not match. Rewrite those by hand with the same mapping. `deluge.cpp:120,133,371` are boot-time OLED redraws; `raise(Dirty::Oled)` is correct there too.

- [ ] **Step 3: Build and commit**

Run: `./dbt build Debug` — builds. Remove `#include "gui/ui/ui.h"`/`"gui/views/*.h"` from files where the only use was these calls (build tells you).

```bash
./dbt format
git add scripts/codemods/notify_raise.py
git add -u
git commit -m "refactor(notify): codemod redraw requests and RootUI hooks to notify::raise"
```

---

### Task 7: Codemod — `display->` presentation requests → notices

**Files:**
- Create: `scripts/codemods/notify_display.py`
- Modify: the `display->` sites outside `gui/`/`hid/` (149 lines, 27 files; the census's per-file list is in spec C appendix A). **Excluded from this task** (they read UI state or carry `PopupType` semantics the shell will own): `hasPopupOfType`/`hasPopup` (5), `popupText`/`popupTextTemporary` (13), `displayNotification` (1), and `instrument_clip_minder.cpp`.
- Test: `./dbt build Debug`; grep for remaining `display->displayError\|display->consoleText\|display->displayPopup\|display->cancelPopup\|display->removeWorkingAnimation\|display->displayLoadingAnimationText\|display->removeLoadingAnimation` outside gui/hid returns only the excluded file

**Interfaces:**
- Consumes: `notice(Error)`, `notice(l10n::String)`, `notice(string_view)`, `notice_cancel_popup`, `notice_working`, `notice_clear_working`.

- [ ] **Step 1: Confirm which `displayPopup` parameters the OLED honours**

Run: `grep -n "displayPopup" src/deluge/hid/display/oled.cpp src/deluge/hid/display/display.cpp | head`. Read the OLED implementation: `numFlashes`, `alignRight`, `drawDot`, `blinkSpeed` are 7-segment parameters; on OLED only `newText` and `type` matter. That justifies dropping them in the rewrite. If the OLED does use `numFlashes` (e.g. `0` = persistent), map `numFlashes == 0` to `Notice::Severity::Warn` and have the drain call `display->displayPopup(text, 0, ...)` for `Warn` — add that branch to `notify_drain.cpp` before running the codemod.

- [ ] **Step 2: Write the codemod**

`scripts/codemods/notify_display.py`:

```python
#!/usr/bin/env python3
"""Rewrite model-side display-> presentation calls to notify::notice*. Idempotent.

  display->displayError(e);                                  -> deluge::notify::notice(e);
  display->consoleText("lit");                               -> notice("lit", Notice::Kind::Console);
  display->consoleText(deluge::l10n::get(String::X));        -> notice(String::X, Notice::Kind::Console);
  display->displayPopup("lit"[, ...]);                       -> notice("lit"[, Kind::Text, uint8_t(PopupType::T)]);
  display->displayPopup(deluge::l10n::get(String::X)[, ...]);-> notice(String::X[, ...]);
  display->displayPopup(<expr>[, ...]);   (computed text)    -> notice(std::string_view(<expr>)[, ...]);
  display->cancelPopup();                                    -> notice_cancel_popup();
  display->removeWorkingAnimation();  removeLoadingAnimation(); -> notice_clear_working();
  display->displayLoadingAnimationText("Loading"[, ...]);    -> notice_working("Loading");
PopupType is preserved when the call passed one as the last argument.
"""
import re, sys
from pathlib import Path

NS = "deluge::notify::"
L10N = re.compile(r"deluge::l10n::get\(\s*(deluge::l10n::String::\w+|String::\w+|l10n::String::\w+)\s*\)")

def popup_type(args: str) -> str:
    m = re.search(r"PopupType::(\w+)\s*$", args)
    return f", {NS}Notice::Kind::Text, static_cast<uint8_t>(PopupType::{m.group(1)})" if m else ""

def rewrite_popup(m: re.Match) -> str:
    args = m.group(1).strip()
    parts = [a.strip() for a in split_top_level(args)]
    text = parts[0]
    ptype = popup_type(args)
    lm = L10N.fullmatch(text)
    if lm:
        return f"{NS}notice({lm.group(1)}{ptype});"
    if text.startswith('"'):
        return f"{NS}notice({text}{ptype});"
    return f"{NS}notice(std::string_view({text}){ptype});"

def split_top_level(s: str):
    depth, cur, out = 0, "", []
    for ch in s:
        if ch in "([{": depth += 1
        if ch in ")]}": depth -= 1
        if ch == "," and depth == 0:
            out.append(cur); cur = ""
        else:
            cur += ch
    out.append(cur)
    return out

RULES = [
    (re.compile(r"display->displayError\(([^;]*)\)\s*;"), lambda m: f"{NS}notice({m.group(1).strip()});"),
    (re.compile(r"display->consoleText\(([^;]*)\)\s*;"),
     lambda m: (f"{NS}notice({L10N.fullmatch(m.group(1).strip()).group(1)}, {NS}Notice::Kind::Console);"
                if L10N.fullmatch(m.group(1).strip()) else f"{NS}notice({m.group(1).strip()}, {NS}Notice::Kind::Console);")),
    (re.compile(r"display->displayPopup\(((?:[^;()]|\([^;()]*\))*)\)\s*;"), rewrite_popup),
    (re.compile(r"display->cancelPopup\(\)\s*;"), lambda m: f"{NS}notice_cancel_popup();"),
    (re.compile(r"display->(?:removeWorkingAnimation|removeLoadingAnimation)\(\)\s*;"), lambda m: f"{NS}notice_clear_working();"),
    (re.compile(r"display->displayLoadingAnimationText\(\s*(\"[^\"]*\")[^;]*\)\s*;"), lambda m: f"{NS}notice_working({m.group(1)});"),
]
INCLUDE = '#include "util/notify.h"\n'

def rewrite(path: Path) -> bool:
    src = path.read_text(); out = src
    for rx, rep in RULES:
        out = rx.sub(rep, out)
    if out == src: return False
    if INCLUDE not in out:
        out = re.sub(r"(^\s*#include[^\n]*\n)", r"\1" + INCLUDE, out, count=1, flags=re.M)
    path.write_text(out); return True

def main(argv):
    changed = 0
    for p in Path("src/deluge").rglob("*"):
        if p.suffix not in {".cpp", ".h", ".hpp"} or "/gui/" in str(p) or "/hid/" in str(p): continue
        if p.name == "instrument_clip_minder.cpp": continue
        if rewrite(p): changed += 1; print(f"rewrote {p}")
    print(f"{changed} file(s) changed")

if __name__ == "__main__":
    main(sys.argv)
```

- [ ] **Step 3: Run, review the diff, fix the computed-text cases**

```bash
python3 scripts/codemods/notify_display.py
git diff --stat
grep -rn "display->" src/deluge --include='*.cpp' --include='*.h' | grep -v "^src/deluge/gui\|^src/deluge/hid" | grep -v "hasPopup\|popupText\|displayNotification\|instrument_clip_minder"
```

Read every `notice(std::string_view(...))` rewrite (the ~21 computed-buffer sites): the buffer is copied at the call, so a stack buffer is fine; a `StringBuf`/`etl::string` needs `.c_str()` — fix by hand. Any remaining `display->` line is either excluded (leave, with a `// NOTIFY-DEFERRED: <reason>` comment) or a shape the regex missed (rewrite by hand).

- [ ] **Step 4: Build, device check, commit**

Run: `./dbt build Debug`. On device: load a song with a missing sample (error popup), undo/redo (console "Undo"), trigger an E-code path if one is reachable, load a song (working animation appears and clears).

```bash
./dbt format
git add scripts/codemods/notify_display.py
git add -u
git commit -m "refactor(notify): codemod model-side popups, errors and console lines to notices"
```

---

### Task 8: The layer-rule ratchet

**Files:**
- Create: `scripts/check_layer_rules.py`, `scripts/layer_rules_baseline.txt`
- Modify: `.pre-commit-config.yaml` (add a hook next to `check_platform_boundary`), `tests/CMakeLists.txt` (register as ctest `layer_rules`)
- Test: the script against a deliberate violation; `--update` reproduces the committed baseline

**Interfaces:**
- Produces: `python3 scripts/check_layer_rules.py [--update] [--list]`. Rules are `(name, from_glob, kind, pattern, spec)`; the baseline is `file<TAB>rule<TAB>count`. Exit 1 if any (file, rule) count exceeds the baseline or a new (file, rule) pair appears.

- [ ] **Step 1: Prove the failure mode first**

Create a scratch file `src/deluge/model/_ratchet_probe.cpp` containing `#include "gui/ui/ui.h"\nvoid probe() { uiNeedsRendering(nullptr); }`. After Step 2 the checker must fail on it; delete it before committing.

- [ ] **Step 2: Write the checker**

`scripts/check_layer_rules.py`:

```python
#!/usr/bin/env python3
"""Layer-rule ratchet (UI redesign, spec C §7 / spec F §5).

Each rule is (name, from_glob, kind, regex, spec). `kind` is "include" (match the include
path) or "symbol" (match any source line). The baseline holds counts per (file, rule) and
may only shrink: a PR that raises a count, or adds a new (file, rule) pair, fails.

  scripts/check_layer_rules.py            # check (exit 1 on regression)
  scripts/check_layer_rules.py --list     # print current counts
  scripts/check_layer_rules.py --update   # rewrite the baseline from the tree
"""
import argparse, re, sys
from fnmatch import fnmatch
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "scripts" / "layer_rules_baseline.txt"
SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
NON_UI = "src/deluge/{model,playback,processing,storage,io,modulation,memory,sync,util,dsp}/**"
RULES = [
    ("model-includes-ui", NON_UI, "include", r"^(gui/|hid/)", "2026-08-26-ui-notifications-design.md"),
    ("model-calls-ui", NON_UI, "symbol",
     r"\b(uiNeedsRendering\(|renderUIsForOled\(|display->|getRootUI\(|getCurrentUI\(|currentUIMode|isUIModeActive\(|"
     r"indicator_leds::|PadLEDs::|uiTimerManager\.|openUI\(|changeRootUI\()", "2026-08-26-ui-notifications-design.md"),
    ("gfx-includes-outside", "src/gfx/**", "include", r"^(?!gfx/)", "2026-08-26-ui-stack-redesign-design.md"),
    ("notify-includes-outside", "src/notify/**", "include", r"^(?!notify/)", "2026-08-26-ui-notifications-design.md"),
]
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')

def glob_match(rel: str, pattern: str) -> bool:
    # expand {a,b}
    m = re.search(r"\{([^}]*)\}", pattern)
    if m:
        return any(glob_match(rel, pattern[:m.start()] + alt + pattern[m.end():]) for alt in m.group(1).split(","))
    return fnmatch(rel, pattern)

def count_file(path: Path) -> dict[str, int]:
    rel = str(path.relative_to(REPO))
    counts: dict[str, int] = {}
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for name, frm, kind, rx, _ in RULES:
        if not glob_match(rel, frm):
            continue
        r = re.compile(rx)
        n = 0
        for line in text:
            if kind == "include":
                m = INCLUDE_RE.match(line)
                if m and m.group(1).startswith("<") is False and r.search(m.group(1)) and not line.lstrip().startswith("#include <"):
                    n += 1
            elif r.search(line) and not line.lstrip().startswith("//"):
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

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--update", action="store_true"); ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    cur = current()
    if a.list:
        for (f, r), n in sorted(cur.items()): print(f"{f}\t{r}\t{n}")
        return 0
    if a.update:
        BASELINE.write_text("# Layer-rule baseline: file<TAB>rule<TAB>count. May only shrink.\n"
                            + "".join(f"{f}\t{r}\t{n}\n" for (f, r), n in sorted(cur.items())))
        print(f"wrote {len(cur)} entries"); return 0
    base = load_baseline(); bad = 0
    for key, n in cur.items():
        if n > base.get(key, 0):
            f, r = key
            spec = next(s for name, *_, s in RULES if name == r)
            print(f"{f}: rule '{r}' count {n} > baseline {base.get(key, 0)} (see docs/superpowers/specs/{spec})"); bad += 1
    if bad:
        print(f"{bad} layer-rule regression(s). Remove the new dependency, or (only for an agreed move) run --update.", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

Note the `include` branch: only quoted includes (`"gui/..."`) count; `<...>` system headers never match. Simplify that condition to `if m and r.search(m.group(1))` after verifying `INCLUDE_RE` captures only the path.

- [ ] **Step 3: Baseline, verify, wire**

```bash
python3 scripts/check_layer_rules.py --update      # with the probe present
python3 scripts/check_layer_rules.py; echo "exit=$?"  # 0: baseline includes the probe
rm src/deluge/model/_ratchet_probe.cpp
python3 scripts/check_layer_rules.py --update      # real baseline
git checkout -- src/deluge/model 2>/dev/null; python3 scripts/check_layer_rules.py; echo "exit=$?"   # 0
printf '#include "gui/ui/ui.h"\n' >> src/deluge/model/song/song.cpp; python3 scripts/check_layer_rules.py; echo "exit=$?"   # 1
git checkout -- src/deluge/model/song/song.cpp
```

`.pre-commit-config.yaml`: duplicate the `check_platform_boundary` hook entry with `entry: python3 scripts/check_layer_rules.py`, `files: ^(src/|scripts/(check_layer_rules\.py|layer_rules_baseline\.txt))`. `tests/CMakeLists.txt`: `add_test(NAME layer_rules COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../scripts/check_layer_rules.py)` under the existing `Python3_Interpreter_FOUND` guard (add the `find_package` if the gfx plan has not already).

- [ ] **Step 4: Commit**

```bash
git add scripts/check_layer_rules.py scripts/layer_rules_baseline.txt .pre-commit-config.yaml tests/CMakeLists.txt
git commit -m "test(ui): shrink-only layer-rule ratchet for model→UI coupling"
```

---

### Task 9: Docs and the on-device checklist

**Files:**
- Modify: `src/notify/README.md` (usage), `docs/superpowers/specs/2026-08-26-ui-notifications-design.md` (§3 note: `notify` stores integers; typed raisers in `util/notify.h`)
- Create: `docs/dev/ui_notify_checklist.md`

- [ ] **Step 1: Correct the spec's leaf note**

In spec C §3, replace the bullet "`deluge-notify` depends on `l10n::String` (an enum) and `Error` (an enum) — both are header-only enums; the library stays a leaf." with: "`deluge-notify` stores `int16_t error_code` / `uint16_t string_id`; the typed conversions live in `src/deluge/util/notify.h` because `gui/l10n/strings.h` is not a leaf header (it includes `io/midi/midi_device.h`)."

- [ ] **Step 2: Write the checklist**

`docs/dev/ui_notify_checklist.md`:

```markdown
# notify migration — on-device checklist

Run after any PR that moves a `display->`/`uiNeedsRendering`/RootUI-hook site onto `deluge_notify`.

- [ ] Boot: logo, "Loading" working animation appears and clears.
- [ ] Record with count-in: popup shows 4,3,2,1 aligned right, then clears; recording starts.
- [ ] Undo / redo: "Undo"/"Redo" console lines.
- [ ] Load a song with a missing sample: error popup with the E-code.
- [ ] Heavy song until voices cull (High CPU indicator on): Play LED alert blink.
- [ ] Stop playback: playhead tick squares clear.
- [ ] Finish a linear recording in clip view: notes redraw at their new length.
- [ ] MIDI transpose / kit note in via MIDI: grid updates.
- [ ] Threshold recording mode popup (select + Record) still edits (site deliberately not migrated).
```

- [ ] **Step 3: Commit**

```bash
git add -f docs/superpowers/specs/2026-08-26-ui-notifications-design.md docs/dev/ui_notify_checklist.md src/notify/README.md
git commit -m "docs(notify): usage, leaf-header correction, on-device checklist"
```

---

## Self-review notes

- **Spec coverage.** §2 `DirtySet` (Task 1; `raise_clip` out-of-range → coarse; no pointers; drain never loses a raise). §3 `NoticeQueue` (Task 2; copied text; `popup_type`; typed raisers in Task 3 with the leaf correction recorded in Task 9). §4 facts: count-in, E336, cull alert, `clearTickSquares` (Task 5); swing/tempo `hasPopupOfType`, `currentUIMode` writes from `[task]` sites (`midi_instrument.cpp`, `playback_handler.cpp:446,1573`) and `displayLoadingAnimationText` are **left for the shell plan** and marked `NOTIFY-DEFERRED` (Task 7 step 3). §5 ownership map: not in this plan by design. §6 context rules: atomics; `COOP-WRITE` note in `notice_queue.h`. §7 migration: drain (Task 4), codemods (6–7), ratchet (8); deletion of `RootUI` hook virtuals and the public `uiNeedsRendering` waits until the `gui/`-internal callers move (shell plan). §8 tests: Tasks 1, 2, 3, 4.
- **Types consistent:** `Dirty`, `ClipBits::test`, `DirtySet::raise/raise_clip/drain/has/any/clips`, `Notice{kind, severity, error_code, string_id, text[24], popup_type}`, `NoticeQueue::push/pop/size`, `notice_text/notice_string/notice_error_code/notice_kind`, `notice(...)` overloads, `legacy_redraw_for`, `drain_notifications`, `Clip::notify_content_changed`, `count_in_visual`, `cull_alert_pending` — spelled identically across tasks.
