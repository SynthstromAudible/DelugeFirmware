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

/// Sets voice i of @p rig to play @p notes[i].
void play(Rig& rig, const std::vector<int32_t>& notes) {
	rig.pool.for_each([&](FakeHost::Slot& slot) { slot.kernel.note = notes[static_cast<size_t>(slot.part)]; });
}

/// The held note 64, on channel 2.
std::optional<VoicePool<FakeHost>::HeldNote> held() {
	return VoicePool<FakeHost>::HeldNote{.note = 64, .midi_channel = 2, .mpe = kMpe};
}

/// Facts with note tails allowed, falling back.
NoteFacts fallingBack() {
	return {.note_tails_allowed = true, .falls_back = true};
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
		expect(rig.host.log).to_equal(Log{"acquire 0", "randomize", "starting", "cycles", "timed", "order 0",
		                                  "prepare 0 note 60", "note_on 60 reset 1 count 0", "started 0", "cycles",
		                                  "admit 5", "edit depth 2", "changed ctx 7"});
		expect(rig.host.prepared_depth).to_equal(0);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0});
	});

	it("fast-releases the highest voice at the voice limit instead of admitting", _{
		Rig rig({5, 9});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.releases_to_stop = 2; });
		FakeConfig limited = config(PolyphonyMode::POLY);
		limited.voicing.max_voices = 2;
		rig.pool.note_on(limited, NoteFacts{}, event(60), 0, 0, kContext);
		expect(rig.host.log).to_equal(Log{"acquire 2", "randomize", "starting", "cycles", "timed", "order 0",
		                                  "prepare 2 note 60", "note_on 60 reset 1 count 2", "started 2", "cycles",
		                                  "edit depth 2", "changed ctx 7"});
		expect(lastRelease(rig, 1)).to_equal(std::string{"fast 128"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1, 2});
	});

	it("releases a new voice whose note fails, never listing it", _{
		Rig rig;
		rig.host.starts = false;
		expect(rig.pool.note_on(config(PolyphonyMode::POLY), NoteFacts{}, event(60), 0, 0, kContext)).to_equal(true);
		expect(rig.host.log).to_equal(Log{"acquire 0", "randomize", "starting", "cycles", "timed", "order 0",
		                                  "prepare 0 note 60", "note_on 60 reset 1 count 0", "started 0", "cycles",
		                                  "release 0 listed 0"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{});
	});

	it("with no memory restarts the sound's highest voice in place, or answers that the note cannot sound", _{
		Rig rig({5, 9});
		rig.host.acquire_fails = true;
		expect(rig.pool.note_on(config(PolyphonyMode::POLY), NoteFacts{}, event(64), 0, 0, kContext)).to_equal(true);
		expect(rig.host.log).to_equal(Log{"acquire none", "unassign 1 listed 2", "renew 1 listed 2", "changed ctx 7",
		                                  "randomize", "starting", "timed", "order 0", "prepare 1 note 64",
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
		                                  "release 1 listed 1", "starting", "timed", "order 0", "prepare 0 note 60",
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
		expect(rig.host.log).to_equal(Log{"unassign_sources 1 0", "starting", "timed", "order 0", "prepare 0 note 60",
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

	it("frees a stolen voice whose note fails, while it is still listed", _{
		Rig rig({5, 9});
		rig.host.acquire_fails = true;
		rig.host.starts = false;
		rig.pool.note_on(config(PolyphonyMode::POLY), NoteFacts{}, event(64), 0, 0, kContext);
		expect(rig.host.log).to_equal(Log{"acquire none", "unassign 1 listed 2", "renew 1 listed 2", "changed ctx 7",
		                                  "randomize", "starting", "timed", "order 0", "prepare 1 note 64",
		                                  "note_on 64 reset 1 count 2", "started 1", "unassign 1 listed 2 ctx 7",
		                                  "edit depth 2", "release 1 listed 2"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{0});
	});

	it("reuses rather than glides a LEGATO sound's voice when note tails are not allowed", _{
		Rig rig({5});
		const NoteFacts noTails{.source_active_now = {true, false}};
		rig.pool.note_on(config(PolyphonyMode::LEGATO), noTails, event(60), 0, 0, kContext);
		expect(rig.host.log.front()).to_equal(std::string{"unassign_sources 1 0"});
		expect(rig.host.log.back()).to_equal(std::string{"resume 0 0"});
	});

	it("starts a MONO note on a new voice under the walk's exclusion when no voice is free to reuse", _{
		Rig rig({5});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.releases_to_stop = 2; });
		rig.pool.note_on(config(PolyphonyMode::MONO), tails(), event(60), 0, 0, kContext);
		expect(rig.host.prepared_depth).to_equal(1);
		expect(rig.ids()).to_equal(std::vector<int32_t>{0, 1});
	});

	it("releases each voice playing the note and not yet releasing, leaving the rest", _{
		Rig rig({1, 2, 3});
		play(rig, {60, 62, 60});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.in_release = slot.part == 2; });
		const auto start = rig.pool.note_off(config(PolyphonyMode::POLY), NoteFacts{}, 60, false, std::nullopt,
		                                     kContext);
		expect(start.has_value()).to_equal(false);
		expect(rig.host.log).to_equal(Log{"prepare_release 0", "note_off tails 0 stage 1", "released 0"});
		expect(rig.host.released_depth).to_equal(1);
	});

	it("matches every voice for all notes", _{
		Rig rig({1, 2});
		play(rig, {60, 62});
		rig.pool.note_off(config(PolyphonyMode::POLY), tails(), 0, true, std::nullopt, kContext);
		expect(rig.host.log).to_equal(Log{"prepare_release 0", "note_off tails 1 stage 1", "released 0",
		                                  "prepare_release 1", "note_off tails 1 stage 1", "released 1"});
	});

	it("glides a LEGATO sound's voice to the held note, going on to the rest", _{
		Rig rig({1, 2});
		play(rig, {60, 60});
		const auto start = rig.pool.note_off(config(PolyphonyMode::LEGATO), fallingBack(), 60, false, held(), kContext);
		expect(start.has_value()).to_equal(false);
		expect(rig.host.log).to_equal(Log{"prepare_change 0 note 64", "change 64 64 64 ch 2", "last_note 64",
		                                  "prepare_change 1 note 64", "change 64 64 64 ch 2", "last_note 64"});
	});

	it("hands a MONO sound's held note back to start as a note-on, releasing nothing", _{
		Rig rig({1});
		play(rig, {60});
		const auto start = rig.pool.note_off(config(PolyphonyMode::MONO), fallingBack(), 60, false, held(), kContext);
		expect(start.has_value()).to_equal(true);
		expect(start->note).to_equal(64);
		expect(rig.host.log).to_equal(Log{});

		Rig past({1, 2, 3});
		play(past, {62, 60, 60});
		past.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.in_release = slot.part == 1; });
		const auto again =
		    past.pool.note_off(config(PolyphonyMode::MONO), fallingBack(), 60, false, held(), kContext);
		expect(again.has_value()).to_equal(true);
		expect(past.host.log).to_equal(Log{});
	});

	it("releases instead of falling back with no held note, or when arpeggiating", _{
		Rig none({1});
		play(none, {60});
		none.pool.note_off(config(PolyphonyMode::MONO), fallingBack(), 60, false, std::nullopt, kContext);
		expect(none.host.log).to_equal(Log{"prepare_release 0", "note_off tails 1 stage 1", "released 0"});

		Rig arp({1});
		play(arp, {60});
		NoteFacts arpeggiating = fallingBack();
		arpeggiating.arpeggiating = true;
		arp.pool.note_off(config(PolyphonyMode::MONO), arpeggiating, 60, false, held(), kContext);
		expect(arp.host.log).to_equal(Log{"prepare_release 0", "note_off tails 1 stage 1", "released 0"});
	});

	it("renders a voice only after its first call, which marks it created, reading the sources once per voice", _{
		Rig rig({5});
		const auto render = [&] {
			return rig.pool.render(FakeControl{}, config(PolyphonyMode::POLY), FakeValues{}, FakeArgs{}, kContext);
		};
		expect(render()).to_equal(uint32_t{0});
		expect(rig.host.log).to_equal(Log{"sources 0", "timed_voice 0", "edit depth 1"});
		rig.host.log.clear();
		expect(render()).to_equal(uint32_t{1});
		expect(rig.host.log).to_equal(
		    Log{"sources 1", "timed_voice 1", "voice_block 0", "render sources 1", "rendered 0", "edit depth 1"});
	});

	it("takes a note-off a created voice ignored once note tails are allowed, before its render", _{
		Rig rig({5, 6});
		rig.pool.for_each([](FakeHost::Slot& slot) {
			slot.just_created = true;
			slot.kernel.ignored_note_off = slot.part == 0;
		});
		rig.pool.render(FakeControl{}, config(PolyphonyMode::POLY), FakeValues{}, FakeArgs{}, kContext);
		expect(rig.host.log).to_equal(Log{"sources 0", "timed_voice 1", "voice_block 0", "render sources 0",
		                                  "rendered 0", "sources 1", "timed_voice 1", "voice_block 1",
		                                  "render sources 1", "rendered 1", "edit depth 1"});
		rig.host.log.clear();
		rig.pool.render(FakeControl{}, config(PolyphonyMode::POLY), FakeValues{.note_tails_allowed = true}, FakeArgs{},
		                kContext);
		expect(rig.host.log).to_equal(Log{"prepare_release 0", "note_off tails 1 stage 1", "released 0", "sources 2",
		                                  "timed_voice 1", "voice_block 0", "render sources 2", "rendered 0",
		                                  "sources 3", "timed_voice 1", "voice_block 1", "render sources 3",
		                                  "rendered 1", "edit depth 1"});
	});

	it("leaves an ignored note-off to a voice's first call, which only marks it created", _{
		Rig rig({5});
		rig.pool.for_each([](FakeHost::Slot& slot) { slot.kernel.ignored_note_off = true; });
		rig.pool.render(FakeControl{}, config(PolyphonyMode::POLY), FakeValues{.note_tails_allowed = true}, FakeArgs{},
		                kContext);
		expect(rig.host.log).to_equal(Log{"sources 0", "timed_voice 0", "edit depth 1"});
	});

	it("unassigns a voice that ends while it is listed, and sweeps it after every voice has rendered", _{
		Rig rig({5, 6});
		rig.pool.for_each([](FakeHost::Slot& slot) {
			slot.just_created = true;
			slot.kernel.keeps = slot.part != 0;
		});
		expect(rig.pool.render(FakeControl{}, config(PolyphonyMode::POLY), FakeValues{}, FakeArgs{}, kContext))
		    .to_equal(uint32_t{2});
		expect(rig.host.log).to_equal(Log{"sources 0", "timed_voice 1", "voice_block 0", "render sources 0",
		                                  "rendered 0", "unassign 0 listed 2 ctx 7", "sources 1", "timed_voice 1",
		                                  "voice_block 1", "render sources 1", "rendered 1", "edit depth 1",
		                                  "release 0 listed 2"});
		expect(rig.ids()).to_equal(std::vector<int32_t>{1});
	});
});
// clang-format on

CPPSPEC_SPEC(voice_pool)
