// The cost of a menu write through the app's sink, per target and scope: evidence for the one-frame budget a write on
// every encoder detent must meet on the device (a param write's 400 µs host p95). Prints
// MENU-WRITE-COST lines; the budget is judged from them (a wall-clock assertion would be as flaky as the machine
// running it).

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"

#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "instruments/classic/params.h"
#include "model/edits/change_effect.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/param_address.h"

#include "support/slot_record.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "instruments/classic/state_access.h"

#include "support/menu_context.h"
#include "support/spec_main.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <vector>

using namespace deluge::ui::scenes;
using deluge::ui::scenes::bind::Target;
using deluge::ui::scenes::menu::MenuContext;

namespace {

/// The kit size the probe fans out over: a full kit's worth of sound rows.
constexpr int32_t kKitDrums = 16;
/// Writes timed per target and scope.
constexpr int32_t kWrites = 200;

struct Cost {
	double median_us;
	double p95_us;
};

/// Times @p kWrites writes of @p path through the app's sink, alternating two menu values.
Cost time_writes(const MenuContext& ctx, std::string_view path, int32_t a, int32_t b) {
	bind::WriteSink& sink = *deluge::gui::shell_host::host_ports().writes;
	// The runner checks every write against the old decode; a probe times the write alone.
	deluge::bridge::TypedWriteAudit& audit = deluge::bridge::typedWriteAudit();
	const bool audited = audit.enabled;
	audit.enabled = false;
	std::vector<double> us;
	us.reserve(kWrites);
	for (int32_t i = 0; i < kWrites; ++i) {
		const bind::WriteRequest request{.target = Target{.kind = Target::Kind::SOUND, .path = path},
		                                 .where = ctx.addresses,
		                                 .menu_value = (i % 2) != 0 ? a : b};
		const auto start = std::chrono::steady_clock::now();
		sink.write(request);
		us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
	}
	audit.enabled = audited;
	std::ranges::sort(us);
	return {.median_us = us[us.size() / 2], .p95_us = us[(us.size() * 95) / 100]};
}

/// Times @p kWrites writes of @p param through the app's sink, alternating two stored words.
Cost time_param_writes(const MenuContext& ctx, deluge::plugin::ParamId param, int32_t a, int32_t b) {
	bind::WriteSink& sink = *deluge::gui::shell_host::host_ports().writes;
	deluge::bridge::TypedWriteAudit& audit = deluge::bridge::typedWriteAudit();
	const bool audited = audit.enabled;
	audit.enabled = false;
	std::vector<double> us;
	us.reserve(kWrites);
	for (int32_t i = 0; i < kWrites; ++i) {
		const bind::WriteRequest request{.target = Target{.kind = Target::Kind::PARAM, .param = param},
		                                 .where = ctx.addresses,
		                                 .param_value = (i % 2) != 0 ? a : b};
		const auto start = std::chrono::steady_clock::now();
		sink.write(request);
		us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
	}
	audit.enabled = audited;
	std::ranges::sort(us);
	return {.median_us = us[us.size() / 2], .p95_us = us[(us.size() * 95) / 100]};
}

/// Times @p kWrites requests built by @p request_at, alternating the two stored words, through the app's sink.
template <typename Write>
Cost time_requests(Write&& write) {
	deluge::bridge::TypedWriteAudit& audit = deluge::bridge::typedWriteAudit();
	const bool audited = audit.enabled;
	audit.enabled = false;
	std::vector<double> us;
	us.reserve(kWrites);
	for (int32_t i = 0; i < kWrites; ++i) {
		const auto start = std::chrono::steady_clock::now();
		write(i);
		us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
	}
	audit.enabled = audited;
	std::ranges::sort(us);
	return {.median_us = us[us.size() / 2], .p95_us = us[(us.size() * 95) / 100]};
}

/// Times @p kWrites gestures on @p owner's chain, as a knob detent makes them (changeEffect), alternating two values.
Cost time_effect_gestures(Song& song, deluge::model::LaneOwner owner, std::string_view path,
                          const deluge::serialization::Value& a, const deluge::serialization::Value& b) {
	deluge::bridge::TypedWriteAudit& audit = deluge::bridge::typedWriteAudit();
	const bool audited = audit.enabled;
	audit.enabled = false;
	std::vector<double> us;
	us.reserve(kWrites);
	for (int32_t i = 0; i < kWrites; ++i) {
		const auto start = std::chrono::steady_clock::now();
		deluge::edits::changeEffect(song, owner, path, (i % 2) != 0 ? a : b);
		us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
	}
	audit.enabled = audited;
	std::ranges::sort(us);
	return {.median_us = us[us.size() / 2], .p95_us = us[(us.size() * 95) / 100]};
}

void report(const char* scope, int32_t drums, std::string_view path, Cost cost) {
	char line[160];
	snprintf(line, sizeof line, "MENU-WRITE-COST target=%.*s scope=%s drums=%d us_median=%.1f us_p95=%.1f\n",
	         static_cast<int>(path.size()), path.data(), scope, static_cast<int>(drums), cost.median_us, cost.p95_us);
	std::fputs(line, stdout);
}

} // namespace

// clang-format off
describe menu_write_cost("a menu write's cost through the app's sink", ${
	it("measures a choice, a combined menu and a reshaping write, on a synth and on a full kit under affect-entire", _ {
		{
			spec::SongFixture f = spec::make_synth_song();
			f.song->setCurrentClip(f.clip);
			const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
			report("synth", 0, "voicePriority", time_writes(ctx, "voicePriority", 0, 2));
			report("synth", 0, "polyphonic", time_writes(ctx, "polyphonic", 1, 2));
			report("synth", 0, "transpose", time_writes(ctx, "transpose", 100, -100));
			report("synth", 0, "osc1/type", time_writes(ctx, "osc1/type", 1, 4));
		}
		{
			spec::SongFixture f = spec::make_kit_song();
			for (int32_t d = 0; d < kKitDrums; ++d) {
				spec::add_drum<SoundDrum>(f);
			}
			MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
			ctx.affect_entire_held = true;
			ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx);
			expect(ctx.addresses.affect_entire).to_be_true();
			report("kit", kKitDrums, "voicePriority", time_writes(ctx, "voicePriority", 0, 2));
			report("kit", kKitDrums, "polyphonic", time_writes(ctx, "polyphonic", 1, 2));
			report("kit", kKitDrums, "transpose", time_writes(ctx, "transpose", 100, -100));
			report("kit", kKitDrums, "osc1/type", time_writes(ctx, "osc1/type", 1, 4));
		}
	});

	it("measures a param detent on a synth and on a full kit under affect-entire", _ {
		const deluge::plugin::ParamId lpf = deluge::instruments::classic::param::kLpfFrequency;
		changeRootUI(&sessionView); // each write logs its undo step, as on the device
		{
			spec::SongFixture f = spec::make_synth_song();
			f.song->setCurrentClip(f.clip);
			const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
			report("synth", 0, "lpfFrequency", time_param_writes(ctx, lpf, 0x40000000, -0x40000000));
		}
		{
			spec::SongFixture f = spec::make_kit_song();
			SoundDrum* last = nullptr;
			for (int32_t d = 0; d < kKitDrums; ++d) {
				last = spec::add_drum<SoundDrum>(f);
			}
			f.song->setCurrentClip(f.clip);
			MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
			ctx.affect_entire_held = true;
			ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx);
			expect(ctx.addresses.affect_entire).to_be_true();
			report("kit", kKitDrums, "lpfFrequency", time_param_writes(ctx, lpf, 0x40000000, -0x40000000));
			// The last write (an odd one) reached the kit's last row as well as the one the menu is on.
			const deluge::project::ParamAddress on_last{.owner = deluge::project::OwnerId::drum(*last->id()),
			                                            .space = deluge::project::ParamSpace::ENGINE,
			                                            .id = lpf,
			                                            .clip_id = f.clip->id()};
			expect(deluge::gui::shell_host::host_ports().controls->param_value(on_last)).to_equal(std::optional<int32_t>{0x40000000});
		}
		nullifyUIs();
	});

	it("measures a cable strength detent and a gold knob's region detent on a synth", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		changeRootUI(&sessionView); // each write logs its undo step, as on the device
		deluge::synth::CableTarget pitch = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_PITCH_ADJUST);
		const Target cable{.kind = Target::Kind::CABLE,
		                   .param = deluge::bridge::cable_depth(PatchSource::LFO_GLOBAL_1, pitch)};
		report("synth", 0, "cable", time_requests([&](int32_t i) {
			       (void)deluge::gui::shell_host::host_ports().writes->write({.target = cable,
			                                            .where = ctx.addresses,
			                                            .param_value = (i % 2) != 0 ? 0x08000000 : 0x10000000,
			                                            .polarity = deluge::instruments::classic::Polarity::BIPOLAR});
		       }));
		const deluge::project::ParamAddress lpf{
		    .owner = deluge::project::OwnerId::instrument(*f.instrument->id()),
		    .space = deluge::project::ParamSpace::ENGINE,
		    .id = deluge::instruments::classic::param::kLpfFrequency,
		    .clip_id = f.clip->id()};
		report("synth-region", 0, "lpfFrequency", time_requests([&](int32_t i) {
			       (void)deluge::gui::shell_host::host_ports().controls_sink->write_param(lpf, (i % 2) != 0 ? 0x40000000 : -0x40000000, 0, 24, deluge::ui::controls::Resolution::KNOB);
		       }));
		nullifyUIs();
	});

	it("measures a marker detent on a synth's sample source", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *f.instrument->id()};
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		deluge::edits::settingFor(sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		(void)sound.getOrCreateFirstRange(0);
		deluge::edits::recordRanges(sound, 0);
		std::vector<deluge::synth::SourceRange> ranges =
		    deluge::ui::scenes::spec::first_ranges(slot);
		ranges[0].zone.end = 100000;
		const bind::RangesCause marker{.kind = bind::RangesCause::Kind::MARKER, .marker = 0};
		report("synth", 0, "marker", time_requests([&](int32_t i) {
			       ranges[0].zone.start = (i % 2) != 0 ? 1000 : 2000;
			       (void)deluge::gui::shell_host::host_ports().sound_writes->write_ranges(slot, 0, ranges, marker);
		       }));
		expect(deluge::ui::scenes::spec::first_ranges(slot)[0].zone.end).to_equal(uint64_t{100000});
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("measures a compressor knob detent on the master's chain and on a full kit's", _ {
		using deluge::serialization::Value;
		spec::SongFixture f = spec::make_kit_song();
		for (int32_t d = 0; d < kKitDrums; ++d) {
			spec::add_drum<SoundDrum>(f);
		}
		expect(deluge::bridge::chainAddressOf(*static_cast<Kit*>(f.instrument)->effectsOwner(), *f.song).has_value()).to_be_true();
		expect(deluge::bridge::chainAddressOf(*f.song->live.master_channel.effectsOwner(), *f.song).has_value()).to_be_true();
		const Value low = Value::makeNumber(64 << 24);
		const Value high = Value::makeNumber(96 << 24);
		report("master", 0, "audioCompressor/ratio",
		       time_effect_gestures(*f.song, &f.song->live.master_channel, "audioCompressor/ratio", low, high));
		report("kit-chain", kKitDrums, "audioCompressor/ratio",
		       time_effect_gestures(*f.song, static_cast<Kit*>(f.instrument), "audioCompressor/ratio", low, high));
		expect(f.song->live.master_channel.binding().effects().compressor.ratio).to_satisfy([](auto ratio) {
			return ratio == (96 << 24) || ratio == (64 << 24);
		});
	});
});

DELUGE_SPEC(menu_write_cost_spec, menu_write_cost)
