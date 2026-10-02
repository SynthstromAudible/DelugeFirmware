// tests/unit/audio_stream/stitch_spec.cpp
//
// Own file (not folded into convert_spec.cpp / convert_cluster_spec.cpp): the harness's
// create_test_sourcelist dispatches ctest -> driver by matching a function named after the *file's*
// stem, so a second CPPSPEC_SPEC elsewhere would compile but never actually run under ctest.
#include "storage/audio/stream/stitch.h"

#include "cppspec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

using namespace deluge::audio::stream;

// clang-format off
describe stitch("stitch_boundaries", $ {
	it("no neighbors: self_data unchanged, self boundary flags not set", _ {
		constexpr size_t cluster_size = 32;
		std::array<std::byte, cluster_size + 7> self_data{};
		for (size_t i = 0; i < self_data.size(); ++i) {
			self_data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, cluster_size + 7> original = self_data;
		bool self_start = false;
		bool self_end = false;

		stitch_boundaries(std::span<std::byte>(self_data), /*cluster_index=*/1, RawDataFormat::UNSIGNED_8,
		                   /*audio_data_start_pos_bytes=*/1, cluster_size, self_start, self_end,
		                   /*prev=*/nullptr, /*next=*/nullptr);

		expect(self_data == original).to_equal(true);
		expect(self_start).to_equal(false);
		expect(self_end).to_equal(false);
	});

	it("NATIVE, both neighbors present: no conversion, overhangs refreshed, self flags set, "
	   "neighbor flags untouched", _ {
		constexpr size_t cluster_size = 32;
		std::array<std::byte, cluster_size + 7> self_data{};
		for (size_t i = 0; i < self_data.size(); ++i) {
			self_data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, cluster_size + 7> self_data_before = self_data;

		std::array<std::byte, 11> prev_tail{};
		for (size_t i = 0; i < prev_tail.size(); ++i) {
			prev_tail[i] = static_cast<std::byte>(100 + i);
		}
		bool prev_end_converted = false;
		StitchPrevEdge prev{.tail = std::span<std::byte>(prev_tail), .end_boundary_converted = &prev_end_converted};

		std::array<std::byte, 7> next_head{};
		for (size_t i = 0; i < next_head.size(); ++i) {
			next_head[i] = static_cast<std::byte>(200 + i);
		}
		std::array<std::byte, 3> next_unconverted_head{std::byte{50}, std::byte{51}, std::byte{52}};
		bool next_start_converted = false;
		StitchNextEdge next{.head = std::span<std::byte>(next_head),
		                     .unconverted_head = std::span<const std::byte, 3>(next_unconverted_head),
		                     .start_boundary_converted = &next_start_converted};

		bool self_start = false;
		bool self_end = false;

		stitch_boundaries(std::span<std::byte>(self_data), /*cluster_index=*/1, RawDataFormat::NATIVE,
		                   /*audio_data_start_pos_bytes=*/0, cluster_size, self_start, self_end, &prev, &next);

		// prev.tail[4..11) becomes self_data[0..7) (pre-call values) -- the unconditional overhang refresh.
		for (size_t k = 0; k < 7; ++k) {
			expect(std::to_integer<int>(prev_tail[4 + k])).to_equal(std::to_integer<int>(self_data_before[k]));
		}
		// self_data[size..size+7) becomes next.head[0..7) (pre-call values); NATIVE always takes need_copy7.
		for (size_t k = 0; k < 7; ++k) {
			expect(std::to_integer<int>(self_data[cluster_size + k])).to_equal(200 + static_cast<int>(k));
		}
		// self_data[0..cluster_size) untouched -- NATIVE never writes back into self_data.
		for (size_t i = 0; i < cluster_size; ++i) {
			expect(std::to_integer<int>(self_data[i])).to_equal(std::to_integer<int>(self_data_before[i]));
		}

		expect(self_start).to_equal(true);
		expect(self_end).to_equal(true);
		expect(prev_end_converted).to_equal(false);
		expect(next_start_converted).to_equal(false);
	});

	it("UNSIGNED_8, misaligned, both neighbors present, both neighbor flags false: straddling words "
	   "converted in the neighbor edges and copied back, self+neighbor flags set", _ {
		constexpr size_t cluster_size = 32;
		std::array<std::byte, cluster_size + 7> self_data{};
		for (size_t i = 0; i < self_data.size(); ++i) {
			self_data[i] = static_cast<std::byte>(i);
		}

		std::array<std::byte, 11> prev_tail{};
		for (size_t i = 0; i < prev_tail.size(); ++i) {
			prev_tail[i] = static_cast<std::byte>(100 + i);
		}
		bool prev_end_converted = false;
		StitchPrevEdge prev{.tail = std::span<std::byte>(prev_tail), .end_boundary_converted = &prev_end_converted};

		std::array<std::byte, 7> next_head{};
		for (size_t i = 0; i < next_head.size(); ++i) {
			next_head[i] = static_cast<std::byte>(200 + i);
		}
		std::array<std::byte, 3> next_unconverted_head{std::byte{50}, std::byte{51}, std::byte{52}};
		bool next_start_converted = false;
		StitchNextEdge next{.head = std::span<std::byte>(next_head),
		                     .unconverted_head = std::span<const std::byte, 3>(next_unconverted_head),
		                     .start_boundary_converted = &next_start_converted};

		bool self_start = false;
		bool self_end = false;

		// misalignment = audio_data_start_pos_bytes & 0b11 = 1.
		stitch_boundaries(std::span<std::byte>(self_data), /*cluster_index=*/1, RawDataFormat::UNSIGNED_8,
		                   /*audio_data_start_pos_bytes=*/1, cluster_size, self_start, self_end, &prev, &next);

		// Prev half: unconditional refresh copies self_data[0..7)={0..6} into prev_tail[4..11) first, so the
		// straddling word at prev_tail[1..5) = {101,102,103,0} (0 == pre-call self_data[0]) gets XORed 0x80
		// per byte -> {229,230,231,128}. prev_tail[0] (outside the word) stays untouched.
		expect(std::to_integer<int>(prev_tail[0])).to_equal(100);
		expect(std::to_integer<int>(prev_tail[1])).to_equal(229);
		expect(std::to_integer<int>(prev_tail[2])).to_equal(230);
		expect(std::to_integer<int>(prev_tail[3])).to_equal(231);
		expect(std::to_integer<int>(prev_tail[4])).to_equal(128);

		// 3 bytes copied back: self_data[0..3) = prev_tail[4..7) post-conversion = {128,1,2} (byte 1,2 came
		// unchanged from the unconditional refresh, only byte 0 (index 4) was part of the converted word).
		expect(std::to_integer<int>(self_data[0])).to_equal(128);
		expect(std::to_integer<int>(self_data[1])).to_equal(1);
		expect(std::to_integer<int>(self_data[2])).to_equal(2);
		expect(prev_end_converted).to_equal(true);

		// Next half: stage self_data[size..size+7) = next_head[0..7) BEFORE converting (start_pos =
		// size-4+misalignment = 29 reaches into the overhang at index 32). Straddling word at
		// self_data[29..33) = {29,30,31,200(staged next_head[0])} XORed 0x80 -> {157,158,159,72}.
		expect(std::to_integer<int>(self_data[29])).to_equal(157);
		expect(std::to_integer<int>(self_data[30])).to_equal(158);
		expect(std::to_integer<int>(self_data[31])).to_equal(159);
		expect(std::to_integer<int>(self_data[32])).to_equal(72);

		// 3 bytes written back to next.head; the rest of next.head (from the staging copy) is untouched.
		expect(std::to_integer<int>(next_head[0])).to_equal(72);
		expect(std::to_integer<int>(next_head[1])).to_equal(201);
		expect(std::to_integer<int>(next_head[2])).to_equal(202);
		expect(std::to_integer<int>(next_head[3])).to_equal(203);
		expect(std::to_integer<int>(next_head[4])).to_equal(204);
		expect(std::to_integer<int>(next_head[5])).to_equal(205);
		expect(std::to_integer<int>(next_head[6])).to_equal(206);
		expect(next_start_converted).to_equal(true);

		expect(self_start).to_equal(true);
		expect(self_end).to_equal(true);
	});

	it("idempotency: next-half already-converted branch stages from unconverted_head (not head), then "
	   "falls through to the full need_copy7 overhang copy from head", _ {
		constexpr size_t cluster_size = 32;
		std::array<std::byte, cluster_size + 7> self_data{};
		for (size_t i = 0; i < self_data.size(); ++i) {
			self_data[i] = static_cast<std::byte>(i);
		}

		std::array<std::byte, 7> next_head{};
		for (size_t i = 0; i < next_head.size(); ++i) {
			next_head[i] = static_cast<std::byte>(200 + i);
		}
		std::array<std::byte, 3> next_unconverted_head{std::byte{90}, std::byte{91}, std::byte{92}};
		bool next_start_converted = true; // already converted -- forces the unconverted_head staging branch
		StitchNextEdge next{.head = std::span<std::byte>(next_head),
		                     .unconverted_head = std::span<const std::byte, 3>(next_unconverted_head),
		                     .start_boundary_converted = &next_start_converted};

		bool self_start = false;
		bool self_end = false;

		// ENDIANNESS_WRONG_16 mixes bytes within each 16-bit half (rev16: out = {b1,b0,b3,b2}), so the
		// staged 4th byte (b3) shows up at self_data[start_pos+2] = self_data[31], which the later
		// need_copy7 finalize (touches only [cluster_size, cluster_size+7)) does NOT overwrite -- this is
		// what lets the spec observe which source (unconverted_head vs head) was staged.
		stitch_boundaries(std::span<std::byte>(self_data), /*cluster_index=*/1, RawDataFormat::ENDIANNESS_WRONG_16,
		                   /*audio_data_start_pos_bytes=*/1, cluster_size, self_start, self_end,
		                   /*prev=*/nullptr, &next);

		// Pre-conversion straddling word at self_data[29..33) = {29,30,31,unconverted_head[0]=90}.
		// rev16 byte layout: out0=b1, out1=b0, out2=b3, out3=b2 -> {30,29,90,31}.
		expect(std::to_integer<int>(self_data[29])).to_equal(30);
		expect(std::to_integer<int>(self_data[30])).to_equal(29);
		expect(std::to_integer<int>(self_data[31])).to_equal(90); // == unconverted_head[0], proves the source
		expect(next_start_converted).to_equal(true);              // untouched (was already true)

		// The overhang is finalized wholesale from next.head (need_copy7), overwriting the transiently
		// converted 4th byte and the rest of the overhang -- next.head itself is never written here.
		for (size_t k = 0; k < 7; ++k) {
			expect(std::to_integer<int>(self_data[cluster_size + k])).to_equal(200 + static_cast<int>(k));
			expect(std::to_integer<int>(next_head[k])).to_equal(200 + static_cast<int>(k));
		}

		expect(self_start).to_equal(false); // prev absent
		expect(self_end).to_equal(true);
	});
});

CPPSPEC_SPEC(stitch)
