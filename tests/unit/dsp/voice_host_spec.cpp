/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The voice seam's concepts: what a kernel and a host must offer, checked at compile time against a minimal fake and
// against synth::Voice itself.

#include "synth/voice/voice.h"
#include "synth/voice/voice_host.h"

#include <cppspec.hpp>
#include <cstdint>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::synth;

namespace {

/// The least a kernel can be.
struct MinimalKernel {
	[[nodiscard]] uint32_t priority_rating(deluge::plugin::VoicePriority /*priority*/, size_t /*count*/) const {
		return 0;
	}
	[[nodiscard]] bool is_cull_fading() const { return false; }
	bool do_immediate_release(VoiceClock /*clock*/) { return false; }
	bool do_fast_release(uint32_t /*increment*/, VoiceClock /*clock*/) { return false; }
	bool speed_up_release(VoiceClock /*clock*/) { return false; }
};

/// The least a host can be.
struct MinimalHost {
	struct Slot {};
	using List = std::vector<Slot*>;
	struct Exclusion {};
	using Context = int;
	uint32_t next{};
	Slot* acquire() { return nullptr; }
	void unassign(Slot& /*slot*/, Context /*context*/) {}
	void release(Slot& /*slot*/) noexcept {}
	void admit(uint32_t /*cost*/) {}
	void voice_starting() {}
	void voices_changed(Context /*context*/) {}
	void note_edit() {}
	VoiceClock clock() { return VoiceClock{&next}; }
};

/// A kernel without speed_up_release, so no kernel: a host's force-release could not ask it.
struct NoSpeedUp {
	[[nodiscard]] uint32_t priority_rating(deluge::plugin::VoicePriority /*priority*/, size_t /*count*/) const {
		return 0;
	}
	[[nodiscard]] bool is_cull_fading() const { return false; }
	bool do_immediate_release(VoiceClock /*clock*/) { return false; }
	bool do_fast_release(uint32_t /*increment*/, VoiceClock /*clock*/) { return false; }
};

static_assert(voice_kernel<MinimalKernel>);
static_assert(voice_kernel<Voice>, "the synth's own voice is a kernel the pool can rate and release");
static_assert(!voice_kernel<NoSpeedUp>);
static_assert(voice_host<MinimalHost>);

} // namespace

// clang-format off
describe voice_host_spec_body("the voice seam", ${
	it("tells note facts apart field by field", _{
		const NoteFacts a{.note_tails_allowed = true, .source_active_now = {true, false}};
		NoteFacts b = a;
		expect(b).to_equal(a);
		b.source_active_now[1] = true;
		expect(b).not_().to_equal(a);
	});
});
// clang-format on

DELUGE_SPEC(voice_host_spec, voice_host_spec_body)
