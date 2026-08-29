#pragma once
#include "fake_surfaces.h"
#include "harness/snapshot.h"
#include "harness/ui_script.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef UI_SCRIPTS_DIR
#error "UI_SCRIPTS_DIR must be defined by CMake"
#endif
#ifndef UI_GOLDEN_DIR
#error "UI_GOLDEN_DIR must be defined by CMake"
#endif
#ifndef UI_GOT_DIR
#error "UI_GOT_DIR must be defined by CMake"
#endif

/// Level 2 of the UI test harness (spec G §4): drive the real `Shell` over `FakeSurfaceSet` from a
/// UI script, write `.pbm`/`.state` snapshots, and diff them against the recorded goldens.
namespace uitest {

using deluge::harness::ParseError;
using deluge::harness::StateLines;
using deluge::harness::Step;
using deluge::harness::StepKind;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::Services;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

/// @brief What one `run_script` call is parameterised by.
///
/// The clock is entirely the script's: `tick_ms` is the step between `Shell::tick` calls and the
/// only unit of time the run knows (spec G §5 -- no wall clock in the UI). `ShellConfig::seed` is
/// left at its 0 default, which is what the harness seeds every random element from.
struct RunOptions {
	/// Milliseconds between successive `Shell::tick` calls.
	uint32_t tick_ms = 10;
	/// The board the `FakeSurfaceSet` is sized from; a `board` step in the script overrides it.
	const DelugeBoard* board = &deluge::ui::shell::kDelugeBoard;
	/// The profile whose button matrix, encoder order and LED table the script's names resolve
	/// through, and which the shell is built with.
	const BoardProfile* profile = &deluge::ui::shell::deluge_profile();
	/// What to put on the stack before the first tick: `Shell::init(*initial_scene)` when set,
	/// otherwise `Shell::boot("test")` -- the default boot path, ending on the `Boot` splash.
	std::optional<SceneId> initial_scene;
};

/// @brief What one `run_script` call produced.
///
/// `error` carries both parse failures (an unknown *command*) and run failures (an unknown button,
/// encoder role, notice kind or `Dirty` name -- names the parser cannot check, because it has no
/// board profile). Both are `ParseError`s, deliberately: the two are indistinguishable to the
/// person reading the failure -- a script line naming something that does not exist -- and a
/// second error type would only make every caller test two fields to ask one question. The `line`
/// is the script's own 1-based source line in both cases; line 0 means the failure belongs to no
/// script line at all (a bad `tick_ms`, or a snapshot that could not be written).
struct RunResult {
	/// How many `snapshot` steps were written.
	uint32_t snapshots = 0;
	/// The first parse or run failure, if any; the run stops there and writes nothing further.
	std::optional<ParseError> error;
	/// One line per applied step, for a failing spec to print.
	std::string log;
};

namespace detail {

/// @brief The 1-based source line of the step at \p index.
///
/// `Step` does not carry its own line number, and the parser needs none -- it reports the line of
/// the *first* failure and stops. A run failure happens later, so the line is recovered the way
/// the parser assigned it: one step per significant line, a line being significant when anything
/// survives stripping its `#` comment and surrounding whitespace.
/// @note O(script length) per call, so reporting a failure is O(n^2) overall. Deliberate, and not
/// worth "optimising": the parser caps a script at 256 steps, and this runs at most once per step.
/// @param text  The script text the steps were parsed from.
/// @param index The step's index in the parsed vector.
/// @return The 1-based line number, or 0 if the script has fewer significant lines than that.
inline uint16_t line_of_step(std::string_view text, size_t index) {
	size_t significant = 0;
	uint16_t line_no = 0;
	for (size_t pos = 0; pos <= text.size();) {
		const size_t nl = text.find('\n', pos);
		std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
		line_no++;
		if (const size_t hash = line.find('#'); hash != std::string_view::npos) {
			line = line.substr(0, hash);
		}
		const size_t first = line.find_first_not_of(" \t\r");
		if (first != std::string_view::npos && significant++ == index) {
			return line_no;
		}
		if (nl == std::string_view::npos) {
			break;
		}
		pos = nl + 1;
	}
	return 0;
}

/// @brief Build a `ParseError` for \p message on \p line.
/// @param line    1-based source line.
/// @param message The failure text; truncated to fit `ParseError::message`.
/// @return The error.
inline ParseError make_error(uint16_t line, std::string_view message) {
	ParseError e;
	e.line = line;
	const size_t n = std::min(message.size(), sizeof(e.message) - 1);
	std::memcpy(e.message, message.data(), n);
	e.message[n] = '\0';
	return e;
}

/// @brief The name of \p kind, for `RunResult::log`.
/// @param kind The step kind.
/// @return A static, NUL-terminated name; the script command that produces the step.
inline const char* step_kind_name(StepKind kind) {
	switch (kind) {
	case StepKind::Board:
		return "board";
	case StepKind::Snapshot:
		return "snapshot";
	case StepKind::Button:
		return "button";
	case StepKind::Encoder:
		return "encoder";
	case StepKind::Pad:
		return "pad";
	case StepKind::Released:
		return "released";
	case StepKind::Model:
		return "model";
	case StepKind::Notice:
		return "notice";
	case StepKind::Quit:
		return "quit";
	}
	return "?";
}

/// @brief The `ButtonId` whose `button_name()` is \p name.
/// @param name The script's button name.
/// @return The id, or `std::nullopt` when no button is named that.
inline std::optional<ButtonId> button_by_name(std::string_view name) {
	for (uint8_t i = 1; i < static_cast<uint8_t>(ButtonId::Count); i++) {
		const ButtonId b = static_cast<ButtonId>(i);
		if (const char* n = deluge::ui::shell::button_name(b); n != nullptr && name == n) {
			return b;
		}
	}
	return std::nullopt;
}

/// @brief Where \p b sits in \p profile's button matrix.
/// @param profile The board profile.
/// @param b       The button to locate.
/// @param x       Set to the matrix column on success.
/// @param y       Set to the matrix row on success.
/// @return False when the profile's matrix does not carry \p b at all.
inline bool matrix_of(const BoardProfile& profile, ButtonId b, uint8_t& x, uint8_t& y) {
	for (size_t i = 0; i < profile.button_by_matrix.size(); i++) {
		if (profile.button_by_matrix[i] == b) {
			x = static_cast<uint8_t>(i % profile.button_cols);
			y = static_cast<uint8_t>(i / profile.button_cols);
			return true;
		}
	}
	return false;
}

/// @brief The `EncoderRole` named \p name.
///
/// `EncoderRole` has no name table in `src/ui` (nothing in the firmware prints one), so the
/// harness carries its own, in the enum's declaration order.
/// @param name The script's role name.
/// @return The role, or `std::nullopt` when no role is named that.
inline std::optional<deluge::ui::EncoderRole> encoder_role_by_name(std::string_view name) {
	static constexpr const char* kNames[] = {"Select", "Horizontal", "Vertical", "GoldUpper", "GoldLower", "Tempo"};
	static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(deluge::ui::EncoderRole::Count));
	for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
		if (name == kNames[i]) {
			return static_cast<deluge::ui::EncoderRole>(i);
		}
	}
	return std::nullopt;
}

/// @brief Which board encoder index \p role is wired to in \p profile.
/// @param profile The board profile.
/// @param role    The role to locate.
/// @return The index, or `std::nullopt` when the profile does not wire that role.
inline std::optional<uint8_t> encoder_index_of(const BoardProfile& profile, deluge::ui::EncoderRole role) {
	for (size_t i = 0; i < profile.encoder_by_index.size(); i++) {
		if (profile.encoder_by_index[i] == role) {
			return static_cast<uint8_t>(i);
		}
	}
	return std::nullopt;
}

/// @brief The `Dirty` fact named \p name.
///
/// The name table lives here rather than in `src/notify`: it exists only so a *test script* can
/// spell a fact, and the firmware never prints one.
/// @param name The script's `Dirty` name.
/// @return The fact, or `std::nullopt` when no fact is named that.
inline std::optional<deluge::notify::Dirty> dirty_by_name(std::string_view name) {
	using deluge::notify::Dirty;
	static constexpr const char* kNames[] = {
	    "GridMain",    "GridSidebar",   "Oled",        "ClipContent",   "NoteRows",
	    "SampleReady", "PlaybackState", "SongSwapped", "OutputChanged", "ParamAutomation",
	    "ModTarget",   "SelectedDrum",  "MidiLearn",
	};
	static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(Dirty::Count));
	for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
		if (name == kNames[i]) {
			return static_cast<Dirty>(i);
		}
	}
	return std::nullopt;
}

/// @brief The `Notice::Kind` named \p name, matching `shell::notice_kind_name`'s spelling.
/// @param name The script's kind name.
/// @return The kind, or `std::nullopt` when no kind is named that.
inline std::optional<deluge::notify::Notice::Kind> notice_kind_by_name(std::string_view name) {
	using Kind = deluge::notify::Notice::Kind;
	for (uint8_t i = 0; i <= static_cast<uint8_t>(Kind::CancelPopup); i++) {
		if (name == deluge::ui::shell::notice_kind_name(static_cast<Kind>(i))) {
			return static_cast<Kind>(i);
		}
	}
	return std::nullopt;
}

/// @brief The name of the LED a `LedFrame` entry declares.
///
/// `LedId` values equal `ButtonId` values for every LED under a button (`shell/leds.h`'s ruling,
/// which `Shell::derive_leds` relies on to index the board's LED arrays by the id itself), so the
/// declaration keys straight into `button_name()` -- no board-profile table to reverse.
/// @param id The declared LED.
/// @return The button's name, or the id's digits when it names no button.
inline std::string led_name(deluge::ui::LedId id) {
	if (const char* n = deluge::ui::shell::button_name(static_cast<ButtonId>(id)); n != nullptr) {
		return n;
	}
	return std::to_string(static_cast<unsigned>(static_cast<uint8_t>(id)));
}

/// @brief The `.state` text for the current moment: the shell's own lines plus the fake's pads,
///        LEDs and knob indicators, all through the harness's builders so spec G §3's category
///        ordering applies to the whole file rather than to each half separately.
/// @param shell The shell to ask for scene/lock/notice lines.
/// @param fake  The surfaces to read pads, LEDs and indicators from.
/// @return The sorted `.state` text.
inline std::string state_text(const Shell& shell, FakeSurfaceSet& fake) {
	StateLines lines;
	const std::string shell_lines = shell.state_lines();
	for (size_t pos = 0; pos < shell_lines.size();) {
		const size_t nl = shell_lines.find('\n', pos);
		// The `npos` branch is not dead code guarding an impossible input: `state_lines()`
		// newline-terminates every line today, but nothing asserts that, and without this the
		// `pos = nl + 1` below would wrap to 0 and re-add the whole string until `StateLines`
		// filled -- a future edit to `state_lines()` would surface as a hanging test.
		if (nl == std::string::npos) {
			lines.add(std::string_view{shell_lines}.substr(pos));
			break;
		}
		lines.add(std::string_view{shell_lines}.substr(pos, nl - pos));
		pos = nl + 1;
	}
	if (fake.grid().grid_storage) {
		const deluge::gfx::grid::PadGrid& g = fake.grid().grid();
		for (uint8_t y = 0; y < g.dims().rows; y++) {
			for (uint8_t x = 0; x < g.dims().cols(); x++) {
				const RGB& c = g.at(x, y);
				deluge::harness::add_pad_line(lines, x, y, c.r, c.g, c.b);
			}
		}
	}
	// Read from `LedSurface::frame` -- this tick's *declarations*, as `Shell::derive_leds` composed
	// them -- rather than from the fake's `set_led` shadow. The shadow only ever holds the boolean
	// `flush` resolved the declaration to against this tick's clock, which cannot tell a lit LED
	// from a blinking one that happens to be in its on phase; spec G §3 asks for
	// `led <Name> blink <period>`, and the period exists only here. Same `LedSurface` object the
	// shell writes (`fake` *is* `shell.surfaces()`), reached through the fake because
	// `SurfaceSet::leds()` is non-const.
	for (const auto& [id, state] : fake.leds().frame.entries()) {
		switch (state.kind) {
		case deluge::ui::LedState::Kind::Off:
			break; // An LED declared off is not shown at all -- only lit/blinking ones are.
		case deluge::ui::LedState::Kind::On:
			deluge::harness::add_led_line(lines, led_name(id), false, 0);
			break;
		case deluge::ui::LedState::Kind::Blink:
			deluge::harness::add_led_line(lines, led_name(id), true, state.period_ms);
			break;
		}
	}
	// `i` is an `IndicatorId` value, and that enum is `{Lower = 0, Upper = 1}` (see the ordering
	// note on it) -- so index 1, not 0, is the upper knob. The `.state` name has to be the physical
	// knob, or a level-2 snapshot would disagree with the level-3 one for the same screen.
	for (uint8_t i = 0; i < 2; i++) {
		if (const deluge::ui::KnobIndicator* k = fake.indicator(i); k != nullptr) {
			deluge::harness::add_indicator_line(lines, i == 1, k->levels);
		}
	}
	return lines.sorted_text();
}

/// @brief Somewhere for a `board` step's parsed panel list to live.
///
/// Caller-owned rather than a function-local `static`: two runs in one process would otherwise
/// share one descriptor array, which is exactly the kind of process-global mutable state
/// `run_script` goes out of its way to drain at the top of every run.
struct BoardStorage {
	DelugeDisplayDesc descs[deluge::ui::shell::kMaxOled]{};
	DelugeBoard board{};
};

/// @brief Parse a `board` step's spec (`128x48+5,128x64+0`) into a `DelugeBoard` over caller-owned
///        storage, so a script can size the surfaces for a board whose BSP does not exist.
///
/// Every field but the display list is inherited from \p base: the spec's own words are only ever
/// "which panels", exactly as `DELUGE_HOST_DISPLAYS` is at level 3 (spec G §4.2).
/// @param base The board to start from.
/// @param spec The comma-separated `WxH+top` list.
/// @param out  Filled in on success; untouched otherwise. Caller-owned, and it must outlive the
///             `SurfaceSet::build()` call that reads it (nothing keeps the pointer past that).
/// @return False when the spec is malformed or names no panels.
inline bool board_from_spec(const DelugeBoard& base, std::string_view spec, BoardStorage& out) {
	DelugeDisplayDesc* const descs = out.descs;
	size_t n = 0;
	for (size_t pos = 0; pos <= spec.size() && n < deluge::ui::shell::kMaxOled;) {
		const size_t comma = spec.find(',', pos);
		const std::string one{spec.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos)};
		unsigned w = 0, h = 0, top = 0;
		if (std::sscanf(one.c_str(), "%ux%u+%u", &w, &h, &top) != 3 || w == 0 || h == 0) {
			return false;
		}
		descs[n++] = DelugeDisplayDesc{static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint8_t>(top)};
		if (comma == std::string_view::npos) {
			break;
		}
		pos = comma + 1;
	}
	if (n == 0) {
		return false;
	}
	out.board = base;
	out.board.displays = descs;
	out.board.display_count = static_cast<uint8_t>(n);
	out.board.display_width = descs[0].width;
	out.board.display_height = descs[0].height;
	return true;
}

} // namespace detail

/// @brief Run \p script_text against a fresh `Shell` over a fresh `FakeSurfaceSet`, writing every
///        `snapshot` step's files into \p out_dir.
///
/// The clock is the script's: `t` starts at 0 and advances by `opts.tick_ms`; every step whose
/// `at_ms` has been reached is applied *before* `tick(t)`, and a `snapshot` step's files are
/// written *after* it, so a snapshot always shows the frame the steps at or before its own time
/// produced. The run ends after the last step's time, or at a `quit` step.
///
/// The two process-global model channels (`notify::dirty()`, `notify::notices()`) are drained
/// before the first tick: they outlive any one `Shell`, and a fact another spec left behind would
/// otherwise land in this run's first frame.
/// @param script_text The script (spec G §2).
/// @param registry    The application's scene table.
/// @param services    The shell's services.
/// @param out_dir     Directory to write snapshots into; created if missing.
/// @param opts        Clock, board, profile and initial scene.
/// @return What was written, or the first parse/run failure (in which case nothing was written).
inline RunResult run_script(std::string_view script_text, const SceneRegistry& registry, Services services,
                            std::string_view out_dir, RunOptions opts = {}) {
	RunResult result;
	if (opts.tick_ms == 0) {
		// `t += 0` never advances and the `t >= last_ms` break only fires for an empty script, so
		// without this the run hangs rather than saying what is wrong. Line 0: not a script fault.
		result.error = detail::make_error(0, "tick_ms must be greater than zero");
		return result;
	}
	deluge::harness::ScriptResult parsed = deluge::harness::parse_script(script_text);
	if (parsed.error) {
		result.error = *parsed.error;
		return result;
	}

	// A `board` step names the panel set the surfaces are *built* from, so it is read before the
	// `FakeSurfaceSet` exists rather than applied inside the loop -- there is no way to re-size a
	// live surface pool, and spec G §2 marks the step "at 0 ms" for exactly that reason.
	// Declared here, not inside the scan: `FakeSurfaceSet`'s constructor reads through it, so it
	// must outlive that call.
	detail::BoardStorage board_storage;
	const DelugeBoard* board = opts.board;
	for (size_t i = 0; i < parsed.steps.size(); i++) {
		const Step& s = parsed.steps[i];
		if (s.kind != StepKind::Board) {
			continue;
		}
		if (s.at_ms != 0) {
			result.error = detail::make_error(detail::line_of_step(script_text, i), "board is only allowed at 0 ms");
			return result;
		}
		if (!detail::board_from_spec(*opts.board, s.text, board_storage)) {
			result.error = detail::make_error(detail::line_of_step(script_text, i), "unparseable board spec");
			return result;
		}
		board = &board_storage.board;
	}

	deluge::notify::dirty().drain();
	for (deluge::notify::Notice drop; deluge::notify::notices().pop(drop);) {}

	FakeSurfaceSet fake{*board, *opts.profile};
	ShellConfig config;
	config.seed = 0;
	Shell shell{fake, *opts.profile, registry, services, config};
	if (opts.initial_scene) {
		shell.init(*opts.initial_scene);
	}
	else {
		shell.boot("test");
	}

	uint32_t last_ms = 0;
	for (const Step& s : parsed.steps) {
		last_ms = std::max(last_ms, s.at_ms);
	}

	size_t next = 0;
	bool quit = false;
	for (uint32_t t = 0;; t += opts.tick_ms) {
		std::vector<std::string> due_snapshots;
		for (; next < parsed.steps.size() && parsed.steps[next].at_ms <= t; next++) {
			const Step& s = parsed.steps[next];
			const uint16_t line = detail::line_of_step(script_text, next);
			result.log += std::to_string(t) + " " + detail::step_kind_name(s.kind) + " " + s.name + "\n";
			switch (s.kind) {
			case StepKind::Board:
				break; // already applied, above
			case StepKind::Snapshot:
				due_snapshots.emplace_back(s.name);
				break;
			case StepKind::Button: {
				const std::optional<ButtonId> b = detail::button_by_name(s.name);
				if (!b) {
					result.error = detail::make_error(line, "unknown button");
					return result;
				}
				uint8_t x = 0, y = 0;
				if (!detail::matrix_of(*opts.profile, *b, x, y)) {
					result.error = detail::make_error(line, "button not in this board's matrix");
					return result;
				}
				shell.on_board_event(
				    DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(s.down ? 255 : 0)}, t);
				break;
			}
			case StepKind::Encoder: {
				const std::optional<deluge::ui::EncoderRole> role = detail::encoder_role_by_name(s.name);
				if (!role) {
					result.error = detail::make_error(line, "unknown encoder role");
					return result;
				}
				const std::optional<uint8_t> index = detail::encoder_index_of(*opts.profile, *role);
				if (!index) {
					result.error = detail::make_error(line, "encoder not on this board");
					return result;
				}
				// Computed wide and range-checked, never narrowed on faith: `on_encoder_edges`
				// takes an `int8_t`, so at the default 2 edges/detent a `+64` would wrap to -128
				// and turn a script asking for 64 detents clockwise into 64 anticlockwise. A
				// script that asks for more than the API can carry is a script to fix, not a
				// direction to silently reverse.
				const int32_t edges = s.a * static_cast<int32_t>(config.lowering.edges_per_detent);
				if (edges < -128 || edges > 127) {
					result.error = detail::make_error(line, "too many detents for one on_encoder_edges call");
					return result;
				}
				shell.on_encoder_edges(*index, static_cast<int8_t>(edges), t);
				break;
			}
			case StepKind::Pad:
				// `Step::down` is deliberately unread: the parser already folds `up` into velocity
				// 0 (`ui_script.cpp:192-195`), which is exactly what `DELUGE_EVENT_PAD`'s `value`
				// means ("0 = release", `control_surface.h:50`), so the two would say the same
				// thing twice.
				shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(s.a),
				                                      static_cast<uint8_t>(s.b), static_cast<int16_t>(s.c)},
				                     t);
				break;
			case StepKind::Released:
				shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_NO_PRESSES, 0, 0, 0}, t);
				break;
			case StepKind::Model: {
				const std::optional<deluge::notify::Dirty> d = detail::dirty_by_name(s.name);
				if (!d) {
					result.error = detail::make_error(line, "unknown Dirty name");
					return result;
				}
				// `raise_clip` is the per-clip form; every other fact has no clip index to carry,
				// and `DirtySet::raise_clip` would just raise the coarse fact anyway.
				if (*d == deluge::notify::Dirty::ClipContent || *d == deluge::notify::Dirty::NoteRows) {
					deluge::notify::dirty().raise_clip(*d, static_cast<uint16_t>(s.a));
				}
				else {
					deluge::notify::dirty().raise(*d);
				}
				break;
			}
			case StepKind::Notice: {
				const std::optional<deluge::notify::Notice::Kind> k = detail::notice_kind_by_name(s.name);
				if (!k) {
					result.error = detail::make_error(line, "unknown notice kind");
					return result;
				}
				deluge::notify::Notice n;
				n.kind = *k;
				if (*k == deluge::notify::Notice::Kind::Error) {
					n.error_code = static_cast<int16_t>(s.a);
				}
				else {
					std::memcpy(n.text, s.text, std::min(sizeof(n.text) - 1, sizeof(s.text) - 1));
				}
				(void)deluge::notify::notices().push(n);
				break;
			}
			case StepKind::Quit:
				quit = true;
				break;
			}
			if (quit) {
				next++;
				break;
			}
		}
		// `quit` stops the run, but not before the tick it landed in: steps are applied in file
		// order, so a `snapshot` written at the same `at_ms` as a later `quit` was asked for first
		// and must still be produced. Breaking out here instead would give its author no file and
		// no error. When nothing is due (`popup.txt`'s lone `3200 quit`) this is one extra tick
		// that writes nothing.

		shell.tick(t);

		for (const std::string& name : due_snapshots) {
			for (uint8_t i = 0; i < fake.oled().size(); i++) {
				if (!deluge::harness::write_file(out_dir, name + ".oled" + std::to_string(i) + ".pbm", fake.pbm(i))) {
					// Line 0: an I/O failure belongs to no script line -- see `RunResult::error`.
					result.error = detail::make_error(0, "could not write a snapshot pbm");
					return result;
				}
			}
			if (!deluge::harness::write_file(out_dir, name + ".state", detail::state_text(shell, fake))) {
				result.error = detail::make_error(0, "could not write a snapshot state");
				return result;
			}
			result.snapshots++;
		}

		if (quit || t >= last_ms) {
			break;
		}
	}
	return result;
}

/// @brief Run `tests/ui-scripts/shell/<script_name>.txt` and diff its snapshots against
///        `tests/golden/ui/shell/<script_name>/`.
///
/// The diff is done here, in C++, rather than by shelling out to `scripts/ui_snapshot_diff.py`:
/// every file in the golden directory must exist in the run's output and be byte-identical, and
/// the run must not have produced a `.pbm`/`.state` the golden directory does not have. Setting
/// `GFX_GOLDEN_UPDATE=1` records instead of comparing -- the same convention, and the same
/// deliberate exact-"1" test, as `tests/spec/gfx/support/golden.h`'s `expect_golden`.
/// @param self        The enclosing `it(...)` block's lambda parameter (see `expect_golden`).
/// @param script_name The script's basename, without `.txt`.
/// @param registry    The application's scene table.
/// @param services    The shell's services.
/// @param opts        Clock, board, profile and initial scene.
/// @return What the run produced, so a caller can assert on `snapshots` too.
template <typename Self>
inline RunResult expect_script_goldens(Self& self, std::string_view script_name, const SceneRegistry& registry,
                                       Services services, RunOptions opts = {}) {
	namespace fs = std::filesystem;
	const std::string name{script_name};
	const fs::path script = fs::path(UI_SCRIPTS_DIR) / (name + ".txt");
	const fs::path got = fs::path(UI_GOT_DIR) / "shell" / name;
	const fs::path golden = fs::path(UI_GOLDEN_DIR) / name;

	// Every assertion below compares *labelled strings* rather than passing cppspec's optional
	// failure message: `Matchers::Equal` overrides `failure_message()` and never consults the
	// custom one, so a message given that way is silently dropped and the values print bare. Baking
	// the label into both sides is what makes a failure say which file, and which script, it is
	// about while still printing the two texts side by side.
	std::ifstream in(script);
	std::stringstream text;
	text << in.rdbuf();
	expect(std::string(in.good() ? "readable" : "MISSING") + ": " + script.string())
	    .to_equal("readable: " + script.string());

	// A stale file from an earlier run of a since-renamed snapshot would otherwise look like an
	// extra the golden directory is missing, forever.
	std::error_code ec;
	fs::remove_all(got, ec);

	RunResult result = run_script(text.str(), registry, services, got.string(), opts);
	expect(name + ": "
	       + (result.error ? "line " + std::to_string(result.error->line) + ": " + result.error->message
	                       : std::string("ran to completion")))
	    .to_equal(name + ": ran to completion");
	if (result.error) {
		return result;
	}

	std::vector<std::string> got_names;
	if (fs::is_directory(got)) {
		for (const fs::directory_entry& e : fs::directory_iterator(got)) {
			got_names.push_back(e.path().filename().string());
		}
		std::sort(got_names.begin(), got_names.end());
	}

	const char* update = std::getenv("GFX_GOLDEN_UPDATE");
	if (update != nullptr && std::string(update) == "1") {
		// A script that produced nothing would otherwise record an empty directory and then pass
		// forever -- both file lists empty, nothing to compare. Recording is the one moment that
		// mistake is still visible.
		expect(name + ": " + (got_names.empty() ? "produced nothing to record" : "produced files to record"))
		    .to_equal(name + ": produced files to record");
		if (got_names.empty()) {
			return result;
		}
		fs::remove_all(golden, ec);
		fs::create_directories(golden, ec);
		for (const std::string& file : got_names) {
			fs::copy_file(got / file, golden / file, fs::copy_options::overwrite_existing, ec);
		}
		return result;
	}

	const bool have_golden = fs::is_directory(golden);
	expect(std::string(have_golden ? "recorded" : "MISSING (record it with GFX_GOLDEN_UPDATE=1)") + ": "
	       + golden.string())
	    .to_equal("recorded: " + golden.string());
	if (!have_golden) {
		return result;
	}

	std::vector<std::string> want_names;
	for (const fs::directory_entry& e : fs::directory_iterator(golden)) {
		want_names.push_back(e.path().filename().string());
	}
	std::sort(want_names.begin(), want_names.end());
	for (const std::string& file : want_names) {
		std::ifstream wi(golden / file);
		std::stringstream want;
		want << wi.rdbuf();
		std::ifstream gi(got / file);
		std::stringstream have;
		have << gi.rdbuf();
		// The whole file goes into the comparison, under a header naming it, so a `.state` mismatch
		// prints both texts and a `.pbm` mismatch prints both images -- with the file's name on
		// both sides of the diff either way.
		const std::string label = "--- " + name + "/" + file + " ---\n";
		expect(label + (gi.good() ? have.str() : std::string("<not produced by the run>\n")))
		    .to_equal(label + want.str());
	}

	// Joined into one string rather than compared as vectors: an extra or missing file then shows
	// as a readable two-column diff of file names, whatever printer cppspec has for a container.
	const auto join = [](const std::vector<std::string>& v) {
		std::string s;
		for (const std::string& e : v) {
			s += e;
			s += '\n';
		}
		return s;
	};
	const std::string files_label = "--- files in " + name + " ---\n";
	expect(files_label + join(got_names)).to_equal(files_label + join(want_names));
	return result;
}

} // namespace uitest
