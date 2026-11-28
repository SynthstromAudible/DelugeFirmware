/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief The differential driver: one input sequence played into a legacy root view and into the scene that ports
///        it, a frame recorded after every step on each side, and the first step where the two differ.
///
/// Legacy and the scene act on the same song, so they cannot run in lockstep: a run plays the whole sequence into
/// legacy, puts the song back, then plays it into the scene. The README beside this file says how a view's spec
/// writes its two sides and its cases.

#include "support/popup_capture.h"
#include "ui/shell/input_lowering.h"
#include "ui/widgets/core/input.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::ui::test::differential {

/// @brief A step's popups as a frame holds them: each popup's text after `popup `, each cancel as `cancel`, one a line;
///        nothing else a capture records (a banner, an error, the working animation) is compared.
[[nodiscard]] inline std::string popups_text(const std::vector<deluge::ui::scenes::spec::PopupEvent>& events) {
	using Kind = deluge::ui::scenes::spec::PopupEvent::Kind;
	std::string out;
	for (const deluge::ui::scenes::spec::PopupEvent& e : events) {
		if (e.kind == Kind::POPUP || e.kind == Kind::PERSISTENT) {
			out += "popup " + e.text + '\n';
		}
		else if (e.kind == Kind::CANCEL) {
			out += "cancel\n";
		}
	}
	return out;
}

/// @brief One input event, as a parity script would write it.
struct Step {
	/// @brief What the step presses or turns.
	enum class Kind : uint8_t { Button, Pad, Encoder };
	Kind kind = Kind::Button;
	ui::shell::ButtonId button = ui::shell::ButtonId::None; ///< Kind::Button
	uint8_t x = 0;                                          ///< Kind::Pad: the column, sidebar included
	uint8_t y = 0;                                          ///< Kind::Pad: the row, 0 at the bottom
	bool down = false;                                      ///< Kind::Button and Kind::Pad: press or release
	widgets::core::EncoderRole encoder{};                   ///< Kind::Encoder
	int8_t detents = 0;                                     ///< Kind::Encoder: signed

	/// @brief A button press or release.
	/// @param b The button. @param on True to press.
	/// @return The step.
	[[nodiscard]] static Step press(ui::shell::ButtonId b, bool on) {
		return Step{.kind = Kind::Button, .button = b, .down = on};
	}
	/// @brief A pad strike or release.
	/// @param x The column. @param y The row. @param on True to strike.
	/// @return The step.
	[[nodiscard]] static Step pad(uint8_t x, uint8_t y, bool on) {
		return Step{.kind = Kind::Pad, .x = x, .y = y, .down = on};
	}
	/// @brief An encoder turn.
	/// @param role The encoder. @param delta Signed detents.
	/// @return The step.
	[[nodiscard]] static Step turn(widgets::core::EncoderRole role, int8_t delta) {
		return Step{.kind = Kind::Encoder, .encoder = role, .detents = delta};
	}
	[[nodiscard]] bool operator==(const Step&) const = default;
};

/// @brief A gesture: the steps one deliberate action takes (a tap is a press and a release; a chord holds one button
///        while it does the rest).
using Gesture = std::vector<Step>;

/// @brief What one frame field a case declares as a known difference, each with the D-row that accepts it.
struct Mask {
	static constexpr size_t kColumns = 18;
	static constexpr size_t kRows = 8;
	std::bitset<kColumns * kRows> pads; ///< pad cells left out of the comparison
	uint16_t pads_d_row = 0;            ///< the D-row the masked pads stand for
	uint16_t oled_d_row = 0;            ///< non-zero: the OLED is left out, for this D-row
	uint16_t model_d_row = 0;           ///< non-zero: the model digest is left out, for this D-row

	/// @brief Leave pad (@p x, @p y) out, for @p d_row.
	/// @param x The column. @param y The row. @param d_row The D-row number.
	void mask_pad(size_t x, size_t y, uint16_t d_row) {
		pads.set(y * kColumns + x);
		pads_d_row = d_row;
	}
};

/// @brief A named input sequence, and what it masks.
struct Case {
	std::string name;
	std::vector<Step> steps;
	Mask mask{};
};

/// @brief What the surfaces and the model show after one step.
struct Frame {
	/// @brief One pad's colour, as the device would light it.
	struct Rgb {
		uint8_t r = 0;
		uint8_t g = 0;
		uint8_t b = 0;
		[[nodiscard]] bool operator==(const Rgb&) const = default;
	};
	std::string oled;                                                ///< the main panel, popups left out, as a PBM
	std::array<std::array<Rgb, Mask::kColumns>, Mask::kRows> pads{}; ///< [y][x], sidebar included
	std::string model;                                               ///< the side's model digest, as text
	std::string popups; ///< the popups the step raised or cancelled, one a line
};

/// @brief The side of a run: a legacy view, or the scene that ports it.
class Side {
public:
	virtual ~Side() = default;
	/// @brief Put the song back to the case's start and enter the view; the side may settle until `kFirstFrameMs`.
	virtual void enter() = 0;
	/// @brief Feed one step.
	/// @param step The step. @param at_ms The step's time, the same on both sides.
	virtual void feed(const Step& step, uint32_t at_ms) = 0;
	/// @brief Read what the view shows now.
	/// @param at_ms The time of the step just fed.
	/// @return The frame.
	[[nodiscard]] virtual Frame frame(uint32_t at_ms) = 0;
};

/// @brief Where a run's two sides first disagree.
struct Divergence {
	size_t step = 0;          ///< the index of the step after which they differ
	std::string field;        ///< "oled", "pads", "model" or "popups"
	std::string legacy;       ///< legacy's value of that field
	std::string scene;        ///< the scene's value of that field
	std::string inputs;       ///< the steps up to and including that one
	std::string legacy_model; ///< legacy's model digest at that step, whatever the field
	std::string scene_model;  ///< the scene's model digest at that step, whatever the field
};

/// @brief One step, written as a parity script line would name it.
/// @param s The step.
/// @return Its text.
[[nodiscard]] inline std::string describe(const Step& s) {
	switch (s.kind) {
	case Step::Kind::Button:
		return std::format("button {} {}", static_cast<int>(s.button), s.down ? "down" : "up");
	case Step::Kind::Pad:
		return std::format("pad {} {} {}", s.x, s.y, s.down ? "down" : "up");
	case Step::Kind::Encoder:
		return std::format("encoder {} {:+}", static_cast<int>(s.encoder), s.detents);
	}
	return {};
}

/// @brief A frame's pads as text: one row a line, top row first, each pad `rrggbb`, masked pads `------`.
/// @param f The frame. @param mask Which pads to print as masked.
/// @return The text.
[[nodiscard]] inline std::string describe_pads(const Frame& f, const Mask& mask) {
	std::string out;
	for (size_t row = 0; row < Mask::kRows; ++row) {
		const size_t y = Mask::kRows - 1 - row;
		for (size_t x = 0; x < Mask::kColumns; ++x) {
			if (mask.pads.test(y * Mask::kColumns + x)) {
				out += "------ ";
				continue;
			}
			const Frame::Rgb& c = f.pads[y][x];
			out += std::format("{:02x}{:02x}{:02x} ", c.r, c.g, c.b);
		}
		out += '\n';
	}
	return out;
}

/// @brief The first field in which @p legacy and @p scene differ, under @p mask.
/// @return "oled", "pads", "model" or "popups"; empty when they agree.
[[nodiscard]] inline std::string_view first_difference(const Frame& legacy, const Frame& scene, const Mask& mask) {
	if (mask.oled_d_row == 0 && legacy.oled != scene.oled) {
		return "oled";
	}
	for (size_t y = 0; y < Mask::kRows; ++y) {
		for (size_t x = 0; x < Mask::kColumns; ++x) {
			if (!mask.pads.test(y * Mask::kColumns + x) && !(legacy.pads[y][x] == scene.pads[y][x])) {
				return "pads";
			}
		}
	}
	if (mask.model_d_row == 0 && legacy.model != scene.model) {
		return "model";
	}
	if (legacy.popups != scene.popups) {
		return "popups";
	}
	return {};
}

/// @brief The time of the frame before the first step: a side may use the time before it to settle (a scene's
///        waveform reads), and steps follow it a millisecond apart, too close for any blink to change phase.
inline constexpr uint32_t kFirstFrameMs = 1000;

/// @brief Play @p c into @p legacy, then into @p scene, and find the first step whose frames differ.
///
/// The frame before the first step is read at `kFirstFrameMs`, and step @p i is fed at `kFirstFrameMs + i + 1`, the
/// same on both sides.
/// @param c The case. @param legacy The legacy side. @param scene The scene side.
/// @return The first divergence, or nullopt when every frame agrees.
[[nodiscard]] inline std::optional<Divergence> run(const Case& c, Side& legacy, Side& scene) {
	std::vector<Frame> recorded;
	recorded.reserve(c.steps.size() + 1);
	legacy.enter();
	recorded.push_back(legacy.frame(kFirstFrameMs));
	for (size_t i = 0; i < c.steps.size(); ++i) {
		const uint32_t at = kFirstFrameMs + static_cast<uint32_t>(i + 1);
		legacy.feed(c.steps[i], at);
		recorded.push_back(legacy.frame(at));
	}

	scene.enter();
	std::string inputs;
	for (size_t i = 0; i <= c.steps.size(); ++i) {
		const uint32_t at = kFirstFrameMs + static_cast<uint32_t>(i);
		if (i > 0) {
			scene.feed(c.steps[i - 1], at);
			inputs += describe(c.steps[i - 1]) + '\n';
		}
		const Frame got = scene.frame(at);
		const std::string_view field = first_difference(recorded[i], got, c.mask);
		if (field.empty()) {
			continue;
		}
		Divergence d{.step = i,
		             .field = std::string{field},
		             .inputs = inputs,
		             .legacy_model = recorded[i].model,
		             .scene_model = got.model};
		if (field == "oled") {
			d.legacy = recorded[i].oled;
			d.scene = got.oled;
		}
		else if (field == "pads") {
			d.legacy = describe_pads(recorded[i], c.mask);
			d.scene = describe_pads(got, c.mask);
		}
		else if (field == "model") {
			d.legacy = recorded[i].model;
			d.scene = got.model;
		}
		else {
			d.legacy = recorded[i].popups;
			d.scene = got.popups;
		}
		return d;
	}
	return std::nullopt;
}

/// @brief The report a spec compares with the empty string: empty when the case agrees, otherwise the case, the
///        step, the field, both values and the input that reached them.
/// @param c The case. @param d What `run` returned.
/// @return The report.
[[nodiscard]] inline std::string report(const Case& c, const std::optional<Divergence>& d) {
	if (!d) {
		return {};
	}
	std::string out = std::format("DIFFERENTIAL {} step={} field={}\n--- input\n{}--- legacy\n{}\n--- scene\n{}\n",
	                              c.name, d->step, d->field, d->inputs, d->legacy, d->scene);
	if (d->field != "model") {
		out += std::format("--- models\nlegacy {}\nscene  {}\n", d->legacy_model, d->scene_model);
	}
	return out;
}

/// @brief A seeded walk of @p length gestures drawn from @p alphabet.
///
/// The generator is a fixed 64-bit linear congruential one, so a seed walks the same steps on every platform and
/// standard library.
/// @param name The case's name; the seed is appended. @param seed The seed.
/// @param alphabet The gestures to draw from. @param length How many gestures.
/// @return The case.
[[nodiscard]] inline Case random_walk(std::string_view name, uint64_t seed, std::span<const Gesture> alphabet,
                                      size_t length) {
	Case c{.name = std::format("{} seed {}", name, seed)};
	uint64_t state = seed * 0x9E3779B97F4A7C15ull + 1;
	for (size_t i = 0; i < length && !alphabet.empty(); ++i) {
		state = state * 6364136223846793005ull + 1442695040888963407ull;
		const Gesture& g = alphabet[(state >> 33) % alphabet.size()];
		c.steps.insert(c.steps.end(), g.begin(), g.end());
	}
	return c;
}

} // namespace deluge::ui::test::differential
