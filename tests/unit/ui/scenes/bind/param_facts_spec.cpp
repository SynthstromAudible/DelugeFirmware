// The facts the UI reads about a param it names by plugin and target, held against the runtime numbering's
// kind-keyed answers for every row of both tables.

#include "arp/params.h"
#include "host/chains/params.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "plugin/types/param_kind.h"
#include "ui/scenes/bind/param_facts.h"

#include <cppspec.hpp>

using namespace deluge;
using plugin::ParamId;
using plugin::ParamKind;
using project::OwnerPlugin;
using project::ParamSpace;
using project::PluginParam;
namespace classic = instruments::classic;
namespace chains = host::chains;
namespace bind = ui::scenes::bind;

namespace {
PluginParam engine(OwnerPlugin plugin, ParamId id) {
	return {.plugin = plugin, .target = {.space = ParamSpace::ENGINE, .id = id}};
}
} // namespace

// clang-format off
describe param_facts("a param's facts by plugin and id", ${
	it("answers every instrument param as its kind-keyed lookup does", _ {
		for (const plugin::ParamInfo& row : classic::kParams) {
			const std::optional<classic::RuntimeSlot> slot = classic::runtime_slot(row.id);
			expect(slot.has_value()).to_be_true();
			const ParamKind kind = slot->set == classic::RuntimeSet::PATCHED ? ParamKind::PATCHED : ParamKind::UNPATCHED_SOUND;
			const PluginParam p = engine(OwnerPlugin::INSTRUMENT, row.id);
			expect(bind::is_patched(p)).to_equal(kind == ParamKind::PATCHED);
			expect(bind::is_cable(p)).to_be_false();
			expect(bind::is_bipolar(p)).to_equal(classic::is_bipolar_at(kind, slot->index));
			expect(bind::is_stutter_rate(p)).to_equal(classic::is_stutter_rate_at(kind, slot->index));
			expect(bind::is_arp_rhythm(p)).to_be_false(); // the rhythm is the arp's own param
			expect(bind::info_of(p)).to_equal(classic::param_row_at(kind, slot->index));
		}
	});
	it("answers every chain param as its kind-keyed lookup does", _ {
		for (const plugin::ParamInfo& row : chains::kParams) {
			const std::optional<uint8_t> index = chains::find_runtime_index(row.id);
			expect(index.has_value()).to_be_true();
			const PluginParam p = engine(OwnerPlugin::GLOBAL_EFFECTABLE, row.id);
			expect(bind::is_patched(p)).to_be_false();
			expect(bind::is_cable(p)).to_be_false();
			expect(bind::is_bipolar(p)).to_equal(classic::is_bipolar_at(ParamKind::UNPATCHED_GLOBAL, *index));
			expect(bind::is_stutter_rate(p)).to_equal(classic::is_stutter_rate_at(ParamKind::UNPATCHED_GLOBAL, *index));
			expect(bind::is_arp_rhythm(p)).to_be_false();
			expect(bind::info_of(p)).to_equal(classic::param_row_at(ParamKind::UNPATCHED_GLOBAL, *index));
		}
	});
	it("answers an arp param on either owner from the arp's table", _ {
		for (const OwnerPlugin owner : {OwnerPlugin::INSTRUMENT, OwnerPlugin::GLOBAL_EFFECTABLE}) {
			for (const deluge::arp::Param& row : deluge::arp::kParams) {
				const PluginParam p{.plugin = owner, .target = {.space = ParamSpace::ARP, .id = row.info.id}};
				expect(bind::info_of(p)).to_equal(&deluge::arp::kParamInfos[row.info.id.value]);
				expect(bind::is_arp_rhythm(p)).to_equal(row.info.id == deluge::arp::param::kRhythm);
				expect(bind::is_patched(p)).to_be_false();
				expect(bind::is_cable(p)).to_be_false();
				expect(bind::is_bipolar(p)).to_be_false();
			}
		}
	});
	it("holds a cable's depth bipolar, unpatched and rowless", _ {
		const ParamId depth = classic::cable_depth_of(synth::PatchSource::LFO_GLOBAL_1,
		                                              classic::cable_target_of(classic::param::kLpfFrequency));
		const PluginParam p = engine(OwnerPlugin::INSTRUMENT, depth);
		expect(bind::is_cable(p)).to_be_true();
		expect(bind::is_bipolar(p)).to_be_true();
		expect(bind::is_patched(p)).to_be_false();
		expect(bind::info_of(p)).to_be_null();
		expect(classic::is_bipolar_at(ParamKind::PATCH_CABLE, classic::cable_word_of(depth))).to_be_true();
	});
	it("names a cable onto a patched param by the param's own index", _ {
		for (uint32_t i = 0; i < classic::kParamBlocks[0].count; i++) {
			expect(classic::cable_target_of(ParamId{i}).word).to_equal(synth::CableTarget::param(static_cast<uint8_t>(i)).word);
		}
	});
	it("holds the expression axes and a CC to the kind-keyed answers", _ {
		for (uint32_t axis = 0; axis < 3; axis++) {
			const PluginParam p{.plugin = OwnerPlugin::INSTRUMENT, .target = {.space = ParamSpace::EXPRESSION, .id = ParamId{axis}}};
			expect(bind::is_bipolar(p)).to_equal(classic::is_bipolar_at(ParamKind::EXPRESSION, axis));
			expect(bind::info_of(p)).to_be_null();
			expect(bind::is_patched(p)).to_be_false();
		}
		const PluginParam cc{.plugin = OwnerPlugin::INSTRUMENT, .target = {.space = ParamSpace::MIDI_CC, .id = ParamId{74}}};
		expect(bind::is_bipolar(cc)).to_be_false();
		expect(bind::info_of(cc)).to_be_null();
		expect(bind::is_cable(cc)).to_be_false();
	});
	it("reads an address in its owner's plugin", _ {
		const project::ParamAddress address = project::ParamAddress::of(project::OwnerId::song(), project::engine_target(chains::param::kStutterRate.value));
		const PluginParam p = bind::plugin_param_of(OwnerPlugin::GLOBAL_EFFECTABLE, address);
		expect(p.plugin).to_equal(OwnerPlugin::GLOBAL_EFFECTABLE);
		expect(p.target.id).to_equal(chains::param::kStutterRate);
		expect(bind::is_stutter_rate(p)).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(param_facts)
