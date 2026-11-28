#pragma once

/// @file
/// @brief A legacy view's frames, frozen to a file before the view is deleted, and the side that replays them.
///
/// A file holds one case: its frame count, then each frame that differs from the one before it, by step. Frame 0
/// carries every field; a later frame carries only the fields that changed, and its popups whenever it raised any. A
/// field is its name, its length in bytes, a newline, then the bytes, so the OLED's PBM and the popups' text are stored
/// as they are. The pads are eight lines from the top row down, each eighteen `rrggbb` colours, the sidebar included.
///
/// The files are reference data: they change only to follow upstream `main`, and the commit names the source it
/// follows.

#include "support/differential/differential.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::ui::test::differential {

/// @brief @p f's pads as a frozen file holds them.
[[nodiscard]] inline std::string frozen_pads_text(const Frame& f) {
	std::string out;
	for (size_t row = 0; row < Mask::kRows; ++row) {
		const size_t y = Mask::kRows - 1 - row;
		for (size_t x = 0; x < Mask::kColumns; ++x) {
			const Frame::Rgb& c = f.pads[y][x];
			out += std::format("{}{:02x}{:02x}{:02x}", x == 0 ? "" : " ", c.r, c.g, c.b);
		}
		out += '\n';
	}
	return out;
}

/// @brief The pads @p text holds, into @p f; false when it is not eight lines of eighteen `rrggbb`.
[[nodiscard]] inline bool frozen_pads_parse(std::string_view text, Frame& f) {
	std::istringstream in{std::string{text}};
	std::string cell;
	for (size_t row = 0; row < Mask::kRows; ++row) {
		const size_t y = Mask::kRows - 1 - row;
		for (size_t x = 0; x < Mask::kColumns; ++x) {
			if (!(in >> cell) || cell.size() != 6) {
				return false;
			}
			uint32_t v = 0;
			const auto [end, ec] = std::from_chars(cell.data(), cell.data() + cell.size(), v, 16);
			if (ec != std::errc{} || end != cell.data() + cell.size()) {
				return false;
			}
			f.pads[y][x] = {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
		}
	}
	return true;
}

/// @brief A PBM's bits as hex rows, each row its pixels left to right, four to a digit; a PBM this cannot read is
///        stored as it is.
[[nodiscard]] inline std::string frozen_oled_text(std::string_view pbm) {
	std::istringstream in{std::string{pbm}};
	std::string magic;
	size_t w = 0;
	size_t h = 0;
	if (!(in >> magic >> w >> h) || magic != "P1" || w % 4 != 0) {
		return std::string{pbm};
	}
	std::string out = std::format("P1hex {} {}\n", w, h);
	for (size_t y = 0; y < h; ++y) {
		for (size_t x = 0; x < w; x += 4) {
			uint32_t nibble = 0;
			for (size_t b = 0; b < 4; ++b) {
				int bit = 0;
				if (!(in >> bit)) {
					return std::string{pbm};
				}
				nibble = (nibble << 1) | static_cast<uint32_t>(bit != 0);
			}
			out += "0123456789abcdef"[nibble];
		}
		out += '\n';
	}
	return out;
}

/// @brief The PBM @p text holds, as `page_major_to_pbm` writes one: hex rows back to `P1`, anything else as it is.
[[nodiscard]] inline std::string frozen_oled_parse(std::string_view text) {
	std::istringstream in{std::string{text}};
	std::string magic;
	size_t w = 0;
	size_t h = 0;
	if (!(in >> magic >> w >> h) || magic != "P1hex") {
		return std::string{text};
	}
	std::string out = std::format("P1\n{} {}\n", w, h);
	std::string row;
	for (size_t y = 0; y < h && in >> row; ++y) {
		for (size_t i = 0; i < row.size(); ++i) {
			const uint32_t nibble = static_cast<uint32_t>(row[i] <= '9' ? row[i] - '0' : row[i] - 'a' + 10);
			for (int b = 3; b >= 0; --b) {
				out += ((nibble >> b) & 1u) != 0 ? '1' : '0';
				out += (i + 1 == row.size() && b == 0) ? '\n' : ' ';
			}
		}
	}
	return out;
}

/// @brief @p frames as a frozen file holds them.
[[nodiscard]] inline std::string frozen_encode(const std::vector<Frame>& frames) {
	std::string out = std::format("frames {}\n", frames.size());
	const auto field = [&out](std::string_view name, std::string_view bytes) {
		out += std::format("{} {}\n", name, bytes.size());
		out += bytes;
		out += '\n';
	};
	for (size_t i = 0; i < frames.size(); ++i) {
		const Frame& f = frames[i];
		const Frame* prev = i == 0 ? nullptr : &frames[i - 1];
		const bool oled = prev == nullptr || f.oled != prev->oled;
		const bool pads = prev == nullptr || !(f.pads == prev->pads);
		const bool model = prev == nullptr || f.model != prev->model;
		const bool popups = prev == nullptr || !f.popups.empty();
		if (!oled && !pads && !model && !popups) {
			continue;
		}
		out += std::format("@ {}\n", i);
		if (oled) {
			field("oled", frozen_oled_text(f.oled));
		}
		if (pads) {
			field("pads", frozen_pads_text(f));
		}
		if (model) {
			field("model", f.model);
		}
		if (popups) {
			field("popups", f.popups);
		}
	}
	return out;
}

/// @brief The frames @p text holds, or nothing when it is not a frozen file.
[[nodiscard]] inline std::optional<std::vector<Frame>> frozen_decode(std::string_view text) {
	size_t at = 0;
	const auto line = [&]() -> std::optional<std::string_view> {
		const size_t nl = text.find('\n', at);
		if (nl == std::string_view::npos) {
			return std::nullopt;
		}
		std::string_view l = text.substr(at, nl - at);
		at = nl + 1;
		return l;
	};
	const auto number = [](std::string_view s) -> std::optional<size_t> {
		size_t v = 0;
		const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
		return ec == std::errc{} && end == s.data() + s.size() ? std::optional<size_t>{v} : std::nullopt;
	};
	const std::optional<std::string_view> head = line();
	if (!head || !head->starts_with("frames ")) {
		return std::nullopt;
	}
	const std::optional<size_t> count = number(head->substr(7));
	if (!count || *count == 0) {
		return std::nullopt;
	}
	std::vector<Frame> frames(*count);
	size_t current = 0;
	bool started = false;
	while (at < text.size()) {
		const std::optional<std::string_view> l = line();
		if (!l) {
			return std::nullopt;
		}
		if (l->starts_with("@ ")) {
			const std::optional<size_t> step = number(l->substr(2));
			if (!step || *step >= frames.size() || (started && *step <= current)) {
				return std::nullopt;
			}
			// The fields a step does not name are the step before's; popups are the step's own.
			for (size_t i = started ? current + 1 : 1; i <= *step; ++i) {
				frames[i] = frames[i - 1];
				frames[i].popups.clear();
			}
			current = *step;
			started = true;
			continue;
		}
		const size_t space = l->find(' ');
		const std::optional<size_t> size =
		    space == std::string_view::npos ? std::nullopt : number(l->substr(space + 1));
		if (!started || !size || at + *size + 1 > text.size() || text[at + *size] != '\n') {
			return std::nullopt;
		}
		const std::string_view name = l->substr(0, space);
		const std::string_view bytes = text.substr(at, *size);
		at += *size + 1;
		Frame& f = frames[current];
		if (name == "oled") {
			f.oled = frozen_oled_parse(bytes);
		}
		else if (name == "pads") {
			if (!frozen_pads_parse(bytes, f)) {
				return std::nullopt;
			}
		}
		else if (name == "model") {
			f.model = bytes;
		}
		else if (name == "popups") {
			f.popups = bytes;
		}
		else {
			return std::nullopt;
		}
	}
	for (size_t i = current + 1; i < frames.size(); ++i) {
		frames[i] = frames[i - 1];
		frames[i].popups.clear();
	}
	return frames;
}

/// @brief A legacy side replayed from its frozen frames: each step moves to the next frame.
class FrozenSide final : public Side {
public:
	explicit FrozenSide(std::vector<Frame> frames) : frames_(std::move(frames)) {}
	void enter() override { at_ = 0; }
	void feed(const Step& /*step*/, uint32_t /*at_ms*/) override { ++at_; }
	[[nodiscard]] Frame frame(uint32_t /*at_ms*/) override {
		return at_ < frames_.size() ? frames_[at_] : Frame{.model = "frozen file ran out of frames"};
	}

private:
	std::vector<Frame> frames_;
	size_t at_ = 0;
};

/// @brief Every frame @p side shows for @p c: the one before the first step, then one after each, at the times `run`
///        uses.
[[nodiscard]] inline std::vector<Frame> record(const Case& c, Side& side) {
	std::vector<Frame> frames;
	frames.reserve(c.steps.size() + 1);
	side.enter();
	frames.push_back(side.frame(kFirstFrameMs));
	for (size_t i = 0; i < c.steps.size(); ++i) {
		const uint32_t at = kFirstFrameMs + static_cast<uint32_t>(i + 1);
		side.feed(c.steps[i], at);
		frames.push_back(side.frame(at));
	}
	return frames;
}

/// @brief A case's file name: its name, spaces as underscores.
[[nodiscard]] inline std::string frozen_file_name(std::string_view case_name) {
	std::string out{case_name};
	for (char& ch : out) {
		if (ch == ' ') {
			ch = '_';
		}
	}
	return out + ".txt";
}

/// @brief Read a whole file, or nothing when it cannot be read.
[[nodiscard]] inline std::optional<std::string> read_file(const std::filesystem::path& path) {
	std::ifstream in{path, std::ios::binary};
	if (!in) {
		return std::nullopt;
	}
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

} // namespace deluge::ui::test::differential
