// Where a kit-wide menu write spends its time: the sink's per-drum steps and SetEngineSetting's phases, timed by
// running them in the order the sink and the Edit run them, on every sound drum of a 16-drum kit. Prints
// MENU-WRITE-PROFILE lines, each phase's total per kit write; the batch Edit's shape is judged from them.

#include "scenes/bind/dispatch.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "engine-api/types/setting.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>

using namespace deluge::scenes;
using deluge::scenes::bind::Target;
using deluge::scenes::menu::MenuContext;
using deluge::serialization::Value;

namespace {

constexpr int32_t kKitDrums = 16;
/// Kit writes timed per target.
constexpr int32_t kWrites = 100;

using Clock = std::chrono::steady_clock;

/// Each phase's accumulated microseconds, in the order the phases first ran.
class Phases {
public:
	template <typename Fn>
	decltype(auto) time(const char* phase, Fn&& fn) {
		const auto start = Clock::now();
		struct Add {
			Phases& phases;
			const char* phase;
			Clock::time_point start;
			~Add() { phases.add(phase, std::chrono::duration<double, std::micro>(Clock::now() - start).count()); }
		} add{*this, phase, start};
		return fn();
	}
	void add(const char* phase, double us) {
		if (!totals_.contains(phase)) {
			order_.push_back(phase);
		}
		totals_[phase] += us;
	}
	void report(std::string_view path, int32_t writes) const {
		for (const std::string& phase : order_) {
			char line[160];
			snprintf(line, sizeof line, "MENU-WRITE-PROFILE target=%.*s phase=%s us_total=%.1f\n",
			         static_cast<int>(path.size()), path.data(), phase.c_str(), totals_.at(phase) / writes);
			std::fputs(line, stdout);
		}
	}

private:
	std::map<std::string, double> totals_;
	std::vector<std::string> order_;
};

/// The file form of stored value @p stored of @p row, as the sink makes it.
std::optional<Value> value_of(const deluge::engine::SettingInfo& row, int32_t stored) {
	if (row.kind == deluge::engine::SettingKind::CHOICE) {
		std::array<char, 1> unused{};
		const auto word = deluge::engine::stored_text(row, stored, unused);
		return word ? std::optional{Value::makeScalar(std::string(*word))} : std::nullopt;
	}
	return Value::makeNumber(stored);
}

/// SetEngineSetting::apply's phases on @p slot, as it runs them.
void commit_phased(Phases& phases, Song& song, deluge::project::SlotAddress address, std::string_view path,
                   const Value& wanted) {
	deluge::project::InstrumentSlot* slot =
	    phases.time("resolve_slot", [&] { return song.project.resolveSlot(address); });
	::Sound* sound = phases.time("sound_at", [&] { return deluge::bridge::soundAt(song, address); });
	const auto value = phases.time("checks", [&] {
		auto canonical = deluge::instrument::canonical_setting(path, wanted);
		if (canonical && deluge::instrument::takes_setting(*sound->boundState(), path)
		    && deluge::instrument::allows_setting(*sound->boundState(), path, *canonical)) {
			return canonical;
		}
		return std::optional<Value>{};
	});
	if (slot == nullptr || sound == nullptr || !value) {
		return;
	}
	const Value element = phases.time("element", [&] {
		if (auto reshaped = deluge::bridge::stateWithSetting(*sound, path, *value)) {
			return deluge::instrument::with_state(slot->state, *reshaped,
			                                      {.input_tick_magnitude = slot->tick_magnitude});
		}
		return deluge::bridge::withLeaf(slot->state, path, *value);
	});
	const deluge::instrument::SoundState before = phases.time("state_copy", [&] { return *sound->boundState(); });
	const auto decoded = phases.time("decode_change", [&] {
		return deluge::instrument::decode_change(element, slot->state, before,
		                                         {.input_tick_magnitude = slot->tick_magnitude});
	});
	if (!decoded) {
		return;
	}
	phases.time("apply_difference", [&] {
		return deluge::bridge::applyDifference(*sound, before, decoded->state, decoded->changed, song);
	});
	phases.time("compare", [&] { return *sound->boundState() == decoded->state; });
	phases.time("sync_slot", [&] {
		deluge::bridge::syncSlotState(*sound, *slot, song);
		return 0;
	});
	phases.time("landed", [&] { return deluge::instrument::setting_value(*sound->boundState(), path); });
}

/// One kit-wide write of menu value @p menu at @p path, phase by phase, as the sink runs it over @p kit's drums.
void write_phased(Phases& phases, Song& song, Kit& kit, std::string_view path, int32_t menu) {
	const auto& row = *deluge::engine::find_setting(deluge::instrument::kSettings, path);
	const auto stored = deluge::engine::from_menu(row, menu);
	const std::string_view partner = deluge::engine::mapping_of(row).partner_path();
	const auto* partner_row =
	    partner.empty() ? nullptr : deluge::engine::find_setting(deluge::instrument::kSettings, partner);
	const Target target{.kind = Target::Kind::SOUND, .path = path};
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type != DrumType::SOUND) {
			continue;
		}
		const auto slot =
		    phases.time("slot_address", [&] { return deluge::bridge::slotAddressOf(*static_cast<SoundDrum*>(drum)); });
		const auto shown = phases.time("view", [&] {
			return bind::view(target, song.project, bind::Addresses{.slot = slot}, menu::host_runtime_facts());
		});
		if (!slot || !shown || !shown->applies) {
			continue;
		}
		phases.time("sink_sound_at", [&] { return deluge::bridge::soundAt(song, *slot); });
		commit_phased(phases, song, *slot, path, *value_of(row, stored->first));
		if (partner_row != nullptr) {
			commit_phased(phases, song, *slot, partner, *value_of(*partner_row, stored->second));
		}
	}
}

void profile(spec::SongFixture& f, std::string_view path, int32_t a, int32_t b) {
	Phases phases;
	auto& kit = *static_cast<Kit*>(f.instrument);
	for (int32_t i = 0; i < kWrites; ++i) {
		const auto start = Clock::now();
		write_phased(phases, *f.song, kit, path, (i % 2) != 0 ? a : b);
		phases.add("whole_write", std::chrono::duration<double, std::micro>(Clock::now() - start).count());
	}
	phases.report(path, kWrites);
}

} // namespace

// clang-format off
describe menu_write_profile("a kit-wide menu write's cost, by phase", ${
	it("profiles voice priority, transpose and the low-pass mode over a 16-drum kit", _ {
		spec::SongFixture f = spec::make_kit_song();
		for (int32_t d = 0; d < kKitDrums; ++d) {
			spec::add_drum<SoundDrum>(f);
		}
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(menu::host_binds(*f.song)).to_be_true();
		expect(ctx.addresses.kit_output.has_value()).to_be_true();
		profile(f, "voicePriority", 0, 2);
		profile(f, "transpose", 100, -100);
		profile(f, "lpfMode", 0, 3);
	});
});

DELUGE_SPEC(menu_write_profile_spec, menu_write_profile)
