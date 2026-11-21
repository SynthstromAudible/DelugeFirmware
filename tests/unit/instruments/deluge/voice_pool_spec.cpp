#include "instruments/deluge/voice_pool.h"
#include "support/fake_voice_host.h"

#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace deluge::instrument;
using namespace deluge::instrument::test;
using deluge::plugin::VoicePriority;
using deluge::synth::CullKind;

namespace {

constexpr VoicePriority kPriority = VoicePriority::MEDIUM;

} // namespace

// clang-format off
describe voice_pool("a sound's voices", ${
	it("publishes voices with the render excluded, noting each edit under the exclusion", _{
		Rig rig;
		rig.pool.publish(rig.host.acquire());
		rig.pool.publish(rig.host.acquire());
		expect(FakeHost::Exclusion::taken).to_equal(2);
		expect(rig.host.log).to_equal(std::vector<std::string>{"acquire 0", "edit depth 1", "acquire 1", "edit depth 1"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1});
	});

	it("frees a voice's resources while it is still listed, then its slot", _{
		Rig rig({5, 6, 7});
		const auto second = rig.pool.read([](const FakeHost::List& voices) { return voices.begin() + 1; });
		(void)rig.pool.erase(second);
		expect(rig.host.log).to_equal(
		    std::vector<std::string>{"unassign 1 listed 3", "edit depth 1", "release 1 listed 2"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 2});
	});

	it("kills every voice: all unassigned while listed, then all released", _{
		Rig rig({5, 6});
		rig.pool.kill_all();
		expect(rig.host.log).to_equal(std::vector<std::string>{"unassign 0 listed 2", "unassign 1 listed 2",
		                                                       "edit depth 1", "release 0 listed 2",
		                                                       "release 1 listed 2"});
		expect(rig.pool.empty()).to_be_true();
	});

	it("fast-releases every voice, keeping those still going and freeing the rest in order", _{
		Rig rig({5, 6, 7});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.releases_to_stop = slot.part == 1 ? 2 : 1; });
		rig.pool.fast_release_all(256);
		expect(rig.ids()).to_equal(std::vector<int32_t>{1});
		expect(rig.host.log).to_equal(std::vector<std::string>{"unassign 0 listed 3", "edit depth 2",
		                                                       "release 0 listed 2", "unassign 2 listed 2",
		                                                       "edit depth 2", "release 2 listed 1"});
	});

	it("names the first voice of the highest rating, by a strict comparison", _{
		Rig rig({5, 9, 9, 3});
		expect(rig.pool.lowest_rating(kPriority, true)).to_equal(std::optional<uint32_t>{9});
		rig.pool.cull(kPriority, CullKind::IMMEDIATE, 0);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 2, 3});
	});

	it("passes over cull-fading voices for terminate and force-release, not for kill", _{
		const std::vector<bool> fading{false, true, false};
		Rig kill({5, 10, 7}, fading);
		kill.pool.cull(kPriority, CullKind::IMMEDIATE, 0);
		expect(kill.ids()).to_equal(std::vector<int32_t>{0, 2});

		Rig terminate({5, 10, 7}, fading);
		expect(terminate.pool.lowest_rating(kPriority, true)).to_equal(std::optional<uint32_t>{7});
		terminate.pool.cull(kPriority, CullKind::FAST_RELEASE, 512);
		expect(terminate.ids()).to_equal(std::vector<int32_t>{0, 1});

		Rig release({5, 10, 7}, fading);
		release.pool.cull(kPriority, CullKind::SPEED_UP, 0);
		expect(release.ids()).to_equal(std::vector<int32_t>{0, 1});
	});

	it("culls the voice it named with the release asked for, freeing it only when that stops it", _{
		Rig rig({5, 9});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.releases_to_stop = 2; });
		rig.pool.cull(kPriority, CullKind::FAST_RELEASE, 512);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1});
		std::string kind;
		rig.pool.for_each([&](FakeHost::Slot& slot) {
			if (slot.part == 1) {
				kind = slot.kernel.last_release;
			}
		});
		expect(kind).to_equal(std::string{"fast 512"});
		rig.pool.cull(kPriority, CullKind::FAST_RELEASE, 512);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0});
	});

	it("answers none with no voices, and with only cull-fading ones when passing over them", _{
		Rig none;
		expect(none.pool.lowest_rating(kPriority, false)).to_equal(std::optional<uint32_t>{});
		none.pool.cull(kPriority, CullKind::IMMEDIATE, 0);
		expect(none.host.log).to_equal(std::vector<std::string>{});

		Rig fading({4, 8}, {true, true});
		expect(fading.pool.lowest_rating(kPriority, true)).to_equal(std::optional<uint32_t>{});
		expect(fading.pool.lowest_rating(kPriority, false)).to_equal(std::optional<uint32_t>{8});
	});

	it("reads and changes voices in place with the render excluded", _{
		Rig rig({5, 6});
		int32_t depth = 0;
		rig.pool.for_each([&](FakeHost::Slot&) { depth = FakeHost::Exclusion::depth; });
		expect(depth).to_equal(1);
		const size_t seen = rig.pool.read([](const FakeHost::List& voices) {
			return FakeHost::Exclusion::depth == 1 ? voices.size() : size_t{0};
		});
		expect(seen).to_equal(size_t{2});
	});
});
// clang-format on

CPPSPEC_SPEC(voice_pool)
