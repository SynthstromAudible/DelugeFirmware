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

/// The level-3 runner driven by a fake host: a clock the spec advances by hand, an exit that jumps back into the
/// spec, and BSP shadows and input it records. What a real host provides is exactly what this file defines below.

#include "harness/level3/host_script.h"

#include <array>
#include <csetjmp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {
uint32_t g_now = 0;
int g_exit_code = -1;
std::jmp_buf g_exit;

struct Push {
	DelugeInputEventKind kind;
	uint8_t x;
	uint8_t y;
	int16_t value;
};
std::vector<Push> g_pushes;
std::vector<std::pair<uint8_t, int32_t>> g_edges;
std::array<uint8_t, 768> g_oled{};
bool g_blitted = false;
std::array<uint8_t, HOST_SCRIPT_LED_COUNT> g_leds{};
std::array<uint8_t, HOST_SCRIPT_PAD_COLS * HOST_SCRIPT_PAD_ROWS * 3> g_pads{};
} // namespace

extern "C" {
uint32_t host_script_now_ms(void) {
	return g_now;
}
[[noreturn]] void host_script_exit(int code) {
	g_exit_code = code;
	std::longjmp(g_exit, 1);
}
void host_script_push_event(DelugeInputEventKind kind, uint8_t x, uint8_t y, int16_t value) {
	g_pushes.push_back({kind, x, y, value});
}
void host_script_add_encoder_edges(uint8_t index, int32_t edges) {
	g_edges.emplace_back(index, edges);
}
int host_script_set_displays(const char*) {
	return -1;
}
uint8_t host_script_display_count(void) {
	return 1;
}
void host_script_pad_bounds(uint8_t* cols, uint8_t* rows) {
	*cols = 18;
	*rows = 8;
}
const uint8_t* host_script_oled_shadow(uint8_t index, uint16_t* width, uint16_t* height) {
	if (index != 0 || !g_blitted) {
		return nullptr;
	}
	*width = 128;
	*height = 48;
	return g_oled.data();
}
const uint8_t* host_script_led_shadow(void) {
	return g_leds.data();
}
const uint8_t* host_script_pad_shadow(void) {
	return g_pads.data();
}
const uint8_t* host_script_ring_shadow(uint8_t) {
	return nullptr;
}
}

namespace {
/// Writes @p body as the script the runner loads, and points the snapshots at a fresh directory beside it.
/// @return The directory holding the script and its `got/` snapshots.
std::filesystem::path write_script(const std::string& body) {
	const auto dir = std::filesystem::temp_directory_path() / "level3_runner_spec";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const auto path = dir / "script.txt";
	std::ofstream(path) << body;
	setenv("UI_SCRIPT", path.c_str(), 1);
	setenv("UI_SNAPSHOT_DIR", (dir / "got").c_str(), 1);
	return dir;
}

/// Pumps one millisecond at a time until the script quits, the runner exits, or @p until_ms passes.
/// @return The exit code the runner asked for, 0 for a `quit`, or -1 while the script is still running.
int run_until(uint32_t until_ms) {
	if (setjmp(g_exit) != 0) {
		return g_exit_code;
	}
	for (; g_now <= until_ms; ++g_now) {
		host_script_pump();
		if (host_script_finished()) {
			return 0;
		}
	}
	return -1;
}

/// Loads the script after clearing what the fake host recorded; a script the runner refuses exits from here.
/// @return The exit code the runner asked for, or -1 when it loaded.
int start(const std::string& body) {
	g_now = 0;
	g_exit_code = -1;
	g_pushes.clear();
	g_edges.clear();
	write_script(body);
	if (setjmp(g_exit) != 0) {
		return g_exit_code;
	}
	host_script_init();
	return -1;
}
} // namespace

#include "cppspec.hpp"

describe level3_runner(
    "the level-3 runner on a host's clock", $ {
	    it(
	        "applies a step at its millisecond and not before", _ {
		        g_blitted = false;
		        expect(start("100 pad 3 4 down\n200 quit\n")).to_equal(-1);
		        expect(run_until(99)).to_equal(-1);
		        expect(g_pushes.size()).to_equal(size_t(0));
		        expect(run_until(250)).to_equal(0);
		        expect(g_pushes.size()).to_equal(size_t(1));
		        expect(int(g_pushes[0].x)).to_equal(3);
		        expect(int(g_pushes[0].y)).to_equal(4);
	        });

	    it(
	        "turns detents into two edges each", _ {
		        g_blitted = false;
		        expect(start("0 encoder Select +3\n10 quit\n")).to_equal(-1);
		        expect(run_until(20)).to_equal(0);
		        expect(g_edges.size()).to_equal(size_t(1));
		        expect(g_edges[0].second).to_equal(6);
	        });

	    it(
	        "fails a snapshot taken before the first frame", _ {
		        g_blitted = false;
		        expect(start("5 snapshot early\n10 quit\n")).to_equal(-1);
		        expect(run_until(20)).to_equal(2);
	        });

	    it(
	        "fails a run that outlives its last step by five seconds", _ {
		        g_blitted = true;
		        expect(start("10 snapshot s\n20 released\n")).to_equal(-1);
		        expect(run_until(5019)).to_equal(-1);
		        expect(run_until(6000)).to_equal(2);
	        });

	    it(
	        "writes a snapshot due in the same millisecond as a quit", _ {
		        g_blitted = true;
		        expect(start("10 snapshot s\n10 quit\n")).to_equal(-1);
		        expect(run_until(20)).to_equal(0);
		        const auto got = std::filesystem::temp_directory_path() / "level3_runner_spec" / "got";
		        expect(std::filesystem::exists(got / "s.oled0.pbm")).to_be_true();
		        expect(std::filesystem::exists(got / "s.state")).to_be_true();
	        });
    });

CPPSPEC_SPEC(level3_runner)
