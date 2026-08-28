#pragma once
#include <stdint.h> // not <cstdint>: this header must also compile as plain C17 (see the extern "C" facade below)

#ifdef __cplusplus
#include "fixed_vector.h"

#include <optional>
#include <string_view>

namespace deluge::harness {

/// @brief Which field of `Step` a parsed line filled in.
enum class StepKind : uint8_t { Board, Snapshot, Button, Encoder, Pad, Released, Model, Notice, Quit };

/// @brief One parsed line of a UI test script (spec G §2).
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

#ifdef __cplusplus
}
#endif
