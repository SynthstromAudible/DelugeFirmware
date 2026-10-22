// The cost of a menu write through the app's sink, per target and scope: evidence for the one-frame budget a write on
// every encoder detent must meet on the device (step 2's decision 5; a param write's 400 µs host p95). Prints
// MENU-WRITE-COST lines; the budget is judged from them (a wall-clock assertion would be as flaky as the machine
// running it).

#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "deluge-project/param_address.h"
#include "engines/deluge/params.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "model/edits/change_effect.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <vector>

using namespace deluge::scenes;
using deluge::scenes::bind::Target;
using deluge::scenes::menu::MenuContext;

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
	bind::WriteSink& sink = menu::host_write_sink();
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
Cost time_param_writes(const MenuContext& ctx, deluge::engine::ParamId param, int32_t a, int32_t b) {
	bind::WriteSink& sink = menu::host_write_sink();
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

/// Times @p kWrites gestures on @p owner's chain, as a knob detent makes them (changeEffect), alternating two values.
Cost time_effect_gestures(Song& song, ModControllableAudio& owner, std::string_view path,
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
			const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
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
			MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
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
		const deluge::engine::ParamId lpf = deluge::instrument::param::kLpfFrequency;
		changeRootUI(&sessionView); // each write logs its undo step, as on the device
		{
			spec::SongFixture f = spec::make_synth_song();
			f.song->setCurrentClip(f.clip);
			const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
			report("synth", 0, "lpfFrequency", time_param_writes(ctx, lpf, 0x40000000, -0x40000000));
		}
		{
			spec::SongFixture f = spec::make_kit_song();
			SoundDrum* last = nullptr;
			for (int32_t d = 0; d < kKitDrums; ++d) {
				last = spec::add_drum<SoundDrum>(f);
			}
			f.song->setCurrentClip(f.clip);
			MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
			ctx.affect_entire_held = true;
			ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx);
			expect(ctx.addresses.affect_entire).to_be_true();
			report("kit", kKitDrums, "lpfFrequency", time_param_writes(ctx, lpf, 0x40000000, -0x40000000));
			// The last write (an odd one) reached the kit's last row as well as the one the menu is on.
			const deluge::project::ParamAddress on_last{.owner = deluge::project::OwnerId::drum(*last->id()),
			                                            .space = deluge::project::ParamSpace::ENGINE,
			                                            .id = lpf,
			                                            .clip_id = f.clip->id()};
			expect(menu::host_runtime_facts().param_value(on_last) == std::optional<int32_t>{0x40000000}).to_be_true();
		}
		nullifyUIs();
	});

	it("measures a compressor knob detent on the master's chain and on a full kit's", _ {
		using deluge::serialization::Value;
		spec::SongFixture f = spec::make_kit_song();
		for (int32_t d = 0; d < kKitDrums; ++d) {
			spec::add_drum<SoundDrum>(f);
		}
		expect(deluge::bridge::chainAddressOf(*static_cast<Kit*>(f.instrument), *f.song).has_value()).to_be_true();
		expect(deluge::bridge::chainAddressOf(f.song->live.global_effectable, *f.song).has_value()).to_be_true();
		const Value low = Value::makeNumber(64 << 24);
		const Value high = Value::makeNumber(96 << 24);
		report("master", 0, "audioCompressor/ratio",
		       time_effect_gestures(*f.song, f.song->live.global_effectable, "audioCompressor/ratio", low, high));
		report("kit-chain", kKitDrums, "audioCompressor/ratio",
		       time_effect_gestures(*f.song, *static_cast<Kit*>(f.instrument), "audioCompressor/ratio", low, high));
		expect(f.song->live.global_effectable.effects().compressor.ratio == (96 << 24)
		       || f.song->live.global_effectable.effects().compressor.ratio == (64 << 24))
		    .to_be_true();
	});
});

DELUGE_SPEC(menu_write_cost_spec, menu_write_cost)
