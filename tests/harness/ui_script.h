#pragma once
#include <stdint.h> // not <cstdint>: this header must also compile as plain C17 (see the extern "C" facade below)

/// @brief Bytes per `.state` line, NUL included -- shared by `deluge::harness::kStateLineBytes`
/// (`tests/harness/snapshot.h`) and the C snapshot writer (`src/bsp/host/host_snapshot.h`), so the
/// two widen together rather than as two separately-chosen numbers that could silently drift
/// (final review I-3). Outside the `__cplusplus` guard below so a plain C17 TU sees it too.
#define UI_SCRIPT_STATE_LINE_BYTES 96

#ifdef __cplusplus
#include "fixed_vector.h"

#include <optional>
#include <string_view>

namespace deluge::harness {

/// @brief Which field of `Step` a parsed line filled in.
enum class StepKind : uint8_t { Board, Snapshot, Button, Encoder, Pad, Released, Model, Notice, Quit };

/// @brief One parsed line of a UI test script (spec G §2).
///
/// `name`/`text` are silently truncated (not rejected) if a script's token doesn't fit --
/// scripts are hand-written fixtures the author controls, so a truncation is a copy/paste
/// mistake to notice in a golden diff, not a value worth spending a `ParseError` on. A script
/// relying on more than 31/63 significant characters would collide silently; keep names and
/// notice args shorter than that.
struct Step {
	uint32_t at_ms = 0;
	StepKind kind = StepKind::Board;
	char name[32] = {}; ///< snapshot name / button name / role name / dirty name / notice kind
	int32_t a = 0, b = 0, c = 0;
	bool down = false;
	char text[64] = {}; ///< board spec, notice arg
};

/// @brief A parse failure, with the 1-based source line it occurred on.
struct ParseError {
	uint16_t line = 0;
	char message[64] = {};
};

/// @brief Result of `parse_script`: either every step, or the first error.
struct ScriptResult {
	FixedVector<Step, 256> steps;
	std::optional<ParseError> error;
};

/// @brief Parse a UI test script (spec G §2: `<at_ms> <command> <args...>`, `#` comments, one
/// step per line) into its steps.
///
/// Unknown commands are errors, not warnings. Times must be non-decreasing. Stops at the first
/// error and reports its (1-based) line number.
/// @param text Full script text.
/// @return Steps parsed so far, and the first error if parsing failed.
ScriptResult parse_script(std::string_view text);

} // namespace deluge::harness
#endif // __cplusplus

/// @brief C mirror of `deluge::harness::StepKind`, for the host BSP (spec G §4.2), which is C.
///
/// The `static_assert`s below pin it to the C++ enum, so the two cannot drift apart silently --
/// a drift would otherwise misroute every step in the BSP's dispatch switch with no diagnostic.
typedef enum UiScriptStepKind {
	UI_SCRIPT_BOARD = 0,
	UI_SCRIPT_SNAPSHOT = 1,
	UI_SCRIPT_BUTTON = 2,
	UI_SCRIPT_ENCODER = 3,
	UI_SCRIPT_PAD = 4,
	UI_SCRIPT_RELEASED = 5,
	UI_SCRIPT_MODEL = 6,
	UI_SCRIPT_NOTICE = 7,
	UI_SCRIPT_QUIT = 8,
} UiScriptStepKind;

#ifdef __cplusplus
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Board) == UI_SCRIPT_BOARD);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Snapshot) == UI_SCRIPT_SNAPSHOT);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Button) == UI_SCRIPT_BUTTON);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Encoder) == UI_SCRIPT_ENCODER);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Pad) == UI_SCRIPT_PAD);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Released) == UI_SCRIPT_RELEASED);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Model) == UI_SCRIPT_MODEL);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Notice) == UI_SCRIPT_NOTICE);
static_assert(static_cast<uint8_t>(deluge::harness::StepKind::Quit) == UI_SCRIPT_QUIT);
#endif

/// @brief C mirror of `deluge::harness::Step`, for the host BSP (spec G §4.2), which is C.
typedef struct UiScriptStep {
	uint32_t at_ms;
	uint8_t kind; ///< a `StepKind` value
	char name[32];
	int32_t a, b, c;
	uint8_t down;
	char text[64];
} UiScriptStep;

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Load and parse the script at \p path, replacing any previously loaded script.
/// @return 0 on success; -1 if the file could not be opened; the 1-based line number of a
/// parse error otherwise.
int ui_script_load(const char* path);

/// @brief Pop the next not-yet-consumed step whose `at_ms` is `<= now_ms`, if any.
///
/// Consumes at most one step per call (the caller polls once per frame until this returns 0).
/// @param now_ms Current harness clock, in milliseconds.
/// @param out Filled in when a step is due; untouched otherwise.
/// @return 1 if a step was due and `*out` was filled in; 0 if none was (either the queue is
/// exhausted, or the next step's time has not yet arrived).
int ui_script_next_due(uint32_t now_ms, UiScriptStep* out);

/// @brief Put the cursor back at the first step of the currently loaded script.
///
/// `ui_script_next_due` consumes what it hands back, so a caller that wants to *inspect* the whole
/// script before running it -- the host BSP validates every step at load, and needs the last
/// `at_ms` to set its own deadline -- would otherwise have to re-read the file from disk just to
/// rewind. Does not re-parse: the steps already in memory are kept exactly as they are.
void ui_script_rewind(void);

#ifdef __cplusplus
}
#endif
