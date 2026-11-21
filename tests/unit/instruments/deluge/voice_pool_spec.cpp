#include "instruments/deluge/voice_pool.h"
#include "support/fake_voice_host.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace deluge::instrument;
using namespace deluge::instrument::test;
using deluge::plugin::VoicePriority;
using deluge::synth::CullKind;
using deluge::synth::NoteFacts;
using Log = std::vector<std::string>;

namespace {

constexpr VoicePriority kPriority = VoicePriority::MEDIUM;
constexpr int32_t kContext = 7;
constexpr std::array<int16_t, 3> kMpe{};

/// A note-on of @p note, before and after the arpeggiator, on channel 3.
FakeEvent event(int32_t note, int32_t before = -1) {
	return {.note_before_arp = before < 0 ? note : before, .note_after_arp = note, .midi_channel = 3, .mpe = kMpe};
}

/// A config in @p mode.
FakeConfig config(PolyphonyMode mode) {
	FakeConfig config;
	config.voicing.polyphony = mode;
	return config;
}

/// Facts with note tails allowed and source 0 active.
NoteFacts tails() {
	return {.note_tails_allowed = true, .source_active_now = {true, false}};
}

/// The last release asked of voice @p id.
std::string lastRelease(Rig& rig, int32_t id) {
	std::string kind;
	rig.pool.for_each([&](FakeHost::Slot& slot) {
		if (slot.part == id) {
			kind = slot.kernel.last_release;
		}
	});
	return kind;
}

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
		const auto next = rig.pool.erase(second);
		expect((*next)->part).to_equal(2);
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

	it("hands the context to unassign on erase, kill_all and fast_release_all", _{
		Rig erased({5, 6});
		const auto first = erased.pool.read([](const FakeHost::List& voices) { return voices.begin(); });
		erased.pool.erase(first, 7);
		expect(erased.host.log.front()).to_equal(std::string{"unassign 0 listed 2 ctx 7"});

		Rig killed({5});
		killed.pool.kill_all(8);
		expect(killed.host.log.front()).to_equal(std::string{"unassign 0 listed 1 ctx 8"});

		Rig released({5});
		released.pool.fast_release_all(256, 9);
		expect(released.host.log.front()).to_equal(std::string{"unassign 0 listed 1 ctx 9"});
	});

	it("unassigns a voice that stays listed", _{
		Rig rig({5, 6});
		rig.pool.for_each([&](FakeHost::Slot& slot) {
			if (slot.part == 1) {
				rig.pool.unassign(slot, 3);
			}
		});
		expect(rig.host.log).to_equal(std::vector<std::string>{"unassign 1 listed 2 ctx 3"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1});
	});

	it("erase_if releases what it names without unassigning again, in order", _{
		Rig rig({5, 6, 7});
		rig.pool.erase_if([](const FakeHost::Slot* slot) { return slot->part != 1; });
		expect(rig.host.log).to_equal(
		    std::vector<std::string>{"edit depth 1", "release 0 listed 3", "release 2 listed 3"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{1});
	});

	it("reserves room so the list does not move as it grows", _{
		Rig rig;
		rig.pool.reserve(4);
		rig.pool.publish(rig.host.acquire());
		const FakeHost::Slot* const* before = rig.pool.list().data();
		for (int32_t i = 0; i < 3; i++) {
			rig.pool.publish(rig.host.acquire());
		}
		expect(rig.pool.list().data()).to_equal(before);
	});

	it("names the first voice of the highest rating, by a strict comparison", _{
		Rig rig({5, 9, 9, 3});
		expect(rig.pool.cull_rating(kPriority, true)).to_equal(std::optional<uint32_t>{9});
		rig.pool.cull(kPriority, CullKind::IMMEDIATE, 0);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 2, 3});
	});

	it("passes over cull-fading voices for terminate and force-release, not for kill", _{
		const std::vector<bool> fading{false, true, false};
		Rig kill({5, 10, 7}, fading);
		kill.pool.cull(kPriority, CullKind::IMMEDIATE, 0);
		expect(kill.ids()).to_equal(std::vector<int32_t>{0, 2});

		Rig terminate({5, 10, 7}, fading);
		expect(terminate.pool.cull_rating(kPriority, true)).to_equal(std::optional<uint32_t>{7});
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

	it("frees the voice a cull stops while it is still listed, its slot released before the list shrinks", _{
		Rig rig({5, 9});
		rig.pool.cull(kPriority, CullKind::IMMEDIATE, 0);
		expect(rig.host.log).to_equal(
		    std::vector<std::string>{"unassign 1 listed 2", "edit depth 2", "release 1 listed 2"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0});
	});

	it("answers none with no voices, and with only cull-fading ones when passing over them", _{
		Rig none;
		expect(none.pool.cull_rating(kPriority, false)).to_equal(std::optional<uint32_t>{});
		none.pool.cull(kPriority, CullKind::IMMEDIATE, 0);
		expect(none.host.log).to_equal(std::vector<std::string>{});

		Rig fading({4, 8}, {true, true});
		expect(fading.pool.cull_rating(kPriority, true)).to_equal(std::optional<uint32_t>{});
		expect(fading.pool.cull_rating(kPriority, false)).to_equal(std::optional<uint32_t>{8});
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

	it("starts a POLY note on a new voice outside any exclusion, then admits and publishes it", _{
		Rig rig;
		expect(rig.pool.note_on(config(PolyphonyMode::POLY), NoteFacts{}, event(60), 0, 0, kContext)).to_equal(true);
		expect(rig.host.log).to_equal(Log{"acquire 0", "randomize", "starting", "timed", "prepare 0 note 60",
		                                  "note_on 60 reset 1 count 0", "started 0", "admit 5", "edit depth 2",
		                                  "changed ctx 7"});
		expect(rig.host.prepared_depth).to_equal(0);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0});
	});

	it("fast-releases the highest voice at the voice limit instead of admitting", _{
		Rig rig({5, 9});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.releases_to_stop = 2; });
		FakeConfig limited = config(PolyphonyMode::POLY);
		limited.voicing.max_voices = 2;
		rig.pool.note_on(limited, NoteFacts{}, event(60), 0, 0, kContext);
		expect(rig.host.log).to_equal(Log{"acquire 2", "randomize", "starting", "timed", "prepare 2 note 60",
		                                  "note_on 60 reset 1 count 2", "started 2", "edit depth 2", "changed ctx 7"});
		expect(lastRelease(rig, 1)).to_equal(std::string{"fast 128"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1, 2});
	});

	it("releases a new voice whose note fails, never listing it", _{
		Rig rig;
		rig.host.starts = false;
		expect(rig.pool.note_on(config(PolyphonyMode::POLY), NoteFacts{}, event(60), 0, 0, kContext)).to_equal(true);
		expect(rig.host.log).to_equal(Log{"acquire 0", "randomize", "starting", "timed", "prepare 0 note 60",
		                                  "note_on 60 reset 1 count 0", "started 0", "release 0 listed 0"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{});
	});

	it("with no memory restarts the sound's highest voice in place, or answers that the note cannot sound", _{
		Rig rig({5, 9});
		rig.host.acquire_fails = true;
		expect(rig.pool.note_on(config(PolyphonyMode::POLY), NoteFacts{}, event(64), 0, 0, kContext)).to_equal(true);
		expect(rig.host.log).to_equal(Log{"acquire none", "unassign 1 listed 2", "renew 1 listed 2", "changed ctx 7",
		                                  "randomize", "starting", "timed", "prepare 1 note 64",
		                                  "note_on 64 reset 1 count 2", "started 1"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1});

		Rig none;
		none.host.acquire_fails = true;
		expect(none.pool.note_on(config(PolyphonyMode::POLY), NoteFacts{}, event(64), 0, 0, kContext)).to_equal(false);
		expect(none.host.log).to_equal(Log{"acquire none"});
	});

	it("reuses a MONO sound's voice a fast release ends, discarding the rest, and resumes its envelopes", _{
		Rig rig({5, 6});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.levels = {3, 4}; });
		rig.pool.note_on(config(PolyphonyMode::MONO), tails(), event(60), 0, 0, kContext);
		expect(rig.host.log).to_equal(Log{"unassign_sources 1 0", "unassign 1 listed 2 ctx 7", "edit depth 2",
		                                  "release 1 listed 1", "starting", "timed", "prepare 0 note 60",
		                                  "note_on 60 reset 0 count 1", "started 0", "resume 3 4"});
		expect(lastRelease(rig, 0)).to_equal(std::string{"fast 128"});
		expect(rig.host.prepared_depth).to_equal(1);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0});
	});

	it("asks no fast release of a MONO voice when an active source is not a sample, or it is fast-releasing", _{
		Rig synth({5});
		FakeConfig saw = config(PolyphonyMode::MONO);
		saw.sources[0].osc_type = OscType::SAW;
		synth.pool.note_on(saw, tails(), event(60), 0, 0, kContext);
		expect(lastRelease(synth, 0)).to_equal(std::string{});
		expect(synth.host.log.front()).to_equal(std::string{"unassign_sources 1 0"});

		Rig fading({5});
		fading.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.in_fast_release = true; });
		fading.pool.note_on(config(PolyphonyMode::MONO), tails(), event(60), 0, 0, kContext);
		expect(lastRelease(fading, 0)).to_equal(std::string{});
		expect(fading.host.log.front()).to_equal(std::string{"acquire 1"});
		expect(fading.ids()).to_equal(std::vector<int32_t>{0, 1});
	});

	it("frees a reused voice whose note fails, while it is still listed", _{
		Rig rig({5});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.starts = false; });
		rig.pool.note_on(config(PolyphonyMode::MONO), tails(), event(60), 0, 0, kContext);
		expect(rig.host.log).to_equal(Log{"unassign_sources 1 0", "starting", "timed", "prepare 0 note 60",
		                                  "note_on 60 reset 0 count 1", "started 0", "unassign 0 listed 1 ctx 7",
		                                  "edit depth 2", "release 0 listed 1"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{});
	});

	it("glides a LEGATO sound's first voice not releasing when note tails are allowed, and nothing else", _{
		Rig rig({5, 6});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.in_release = slot.part == 0; });
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.releases_to_stop = 2; });
		expect(rig.pool.note_on(config(PolyphonyMode::LEGATO), tails(), event(62, 61), 0, 0, kContext))
		    .to_equal(true);
		expect(rig.host.log).to_equal(Log{"prepare_change 1 note 62", "change 62 61 62 ch 3"});
		expect(lastRelease(rig, 0)).to_equal(std::string{"fast 128"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1});
	});
});
// clang-format on

CPPSPEC_SPEC(voice_pool)
