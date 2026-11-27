// An arp's param set in its owner's ParamManager: every owner with an arp holds one, as its last main collection and
// before the expression set, starting at that owner's values; the arp-only manager of a MIDI or gate kit row counts no
// main collection; cloning and stealing carry the set; the bridge names it by ParamSpace::ARP on any owner.

#include "arp/params.h"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/arp_param_set.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "project/param_address.h"

#include <cstdint>
#include <limits>
#include <optional>

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace arp = deluge::arp;
namespace params = deluge::modulation::params;
using deluge::bridge::RuntimeParam;
using deluge::project::OwnerPlugin;
using deluge::project::ParamSpace;
using deluge::project::ParamTarget;

namespace {

/// Whether @p manager's arp set holds @p owner's starting word at every id.
bool holds_defaults(ParamManager& manager, arp::Owner owner) {
	const ArpParamSet* set = manager.getArpParamSet();
	if (set == nullptr || set->owner() != owner) {
		return false;
	}
	for (std::size_t i = 0; i < arp::kParamCount; ++i) {
		if (set->word(arp::id_at(i)) != arp::default_value(owner, arp::id_at(i))) {
			return false;
		}
	}
	return true;
}

} // namespace

// clang-format off
describe arp_param_set("an arp's param set", ${
	it("follows a sound's cables and comes before its expression set", _ {
		ParamManager manager;
		expect(manager.setupWithPatching() == Error::NONE).to_equal(true);
		expect(static_cast<int32_t>(manager.arpParamSetOffset)).to_equal(3);
		expect(static_cast<int32_t>(manager.getExpressionParamSetOffset())).to_equal(4);
		expect(manager.containsAnyMainParamCollections()).to_equal(true);
		expect(holds_defaults(manager, arp::Owner::SOUND)).to_equal(true);
		expect(manager.ensureExpressionParamSetExists()).to_equal(true);
		expect(manager.getExpressionParamSetSummary() == &manager.summaries[4]).to_equal(true);
	});

	it("follows a MIDI clip's CC collection", _ {
		ParamManager manager;
		expect(manager.setupMIDI() == Error::NONE).to_equal(true);
		expect(static_cast<int32_t>(manager.arpParamSetOffset)).to_equal(1);
		expect(static_cast<int32_t>(manager.getExpressionParamSetOffset())).to_equal(2);
		expect(holds_defaults(manager, arp::Owner::CLIP)).to_equal(true);
	});

	it("follows a kit's unpatched set, its gate fully open, and an arp-less owner holds none", _ {
		ParamManager kit;
		expect(kit.setupUnpatched(arp::Owner::KIT) == Error::NONE).to_equal(true);
		expect(holds_defaults(kit, arp::Owner::KIT)).to_equal(true);
		expect(kit.getArpParamSet()->word(arp::param::kGate)).to_equal(std::numeric_limits<int32_t>::max());

		ParamManager song;
		expect(song.setupUnpatched() == Error::NONE).to_equal(true);
		expect(song.getArpParamSet() == nullptr).to_equal(true);
		expect(static_cast<int32_t>(song.getExpressionParamSetOffset())).to_equal(1);
	});

	it("stands alone on a MIDI or gate kit row, which counts no main collection", _ {
		ParamManager row;
		expect(row.setupArpOnly(arp::Owner::ROW) == Error::NONE).to_equal(true);
		expect(row.containsAnyMainParamCollections()).to_equal(false);
		expect(row.containsAnyParamCollectionsIncludingExpression()).to_equal(true);
		expect(holds_defaults(row, arp::Owner::ROW)).to_equal(true);
		expect(row.ensureExpressionParamSetExists(true)).to_equal(true);
		expect(row.getExpressionParamSetSummary() == &row.summaries[1]).to_equal(true);
		row.destructAndForgetMainParamCollections();
		expect(row.getArpParamSet() == nullptr).to_equal(true);
		expect(row.getExpressionParamSet() != nullptr).to_equal(true);
	});

	it("travels with a clone and a steal", _ {
		ParamManager source;
		expect(source.setupMIDI() == Error::NONE).to_equal(true);
		source.getArpParamSet()->set_word(arp::param::kRhythm, 12345);

		ParamManager clone;
		expect(clone.cloneParamCollectionsFrom(&source, true) == Error::NONE).to_equal(true);
		expect(clone.getArpParamSet() != nullptr).to_equal(true);
		expect(clone.getArpParamSet() != source.getArpParamSet()).to_equal(true);
		expect(clone.getArpParamSet()->word(arp::param::kRhythm)).to_equal(12345);

		ParamManager thief;
		thief.stealParamCollectionsFrom(&source);
		expect(thief.getArpParamSet() != nullptr).to_equal(true);
		expect(thief.getArpParamSet()->word(arp::param::kRhythm)).to_equal(12345);
		expect(source.getArpParamSet() == nullptr).to_equal(true);
	});

	it("is named ParamSpace::ARP on any owner", _ {
		for (OwnerPlugin owner : {OwnerPlugin::INSTRUMENT, OwnerPlugin::GLOBAL_EFFECTABLE}) {
			for (std::size_t i = 0; i < arp::kParamCount; ++i) {
				const ParamTarget target{.space = ParamSpace::ARP, .id = arp::id_at(i)};
				const std::optional<RuntimeParam> runtime = deluge::bridge::runtimeParamOf(owner, target);
				expect(runtime.has_value()).to_equal(true);
				if (runtime) {
					expect(*runtime == RuntimeParam{params::Kind::ARP, static_cast<int32_t>(i)}).to_equal(true);
				}
				const std::optional<ParamTarget> back =
				    deluge::bridge::targetOf(owner, params::Kind::ARP, static_cast<int32_t>(i));
				expect(back.has_value() && *back == target).to_equal(true);
			}
			const ParamTarget past{.space = ParamSpace::ARP, .id = arp::id_at(arp::kParamCount)};
			expect(deluge::bridge::runtimeParamOf(owner, past).has_value()).to_equal(false);
		}
	});
});
// clang-format on

DELUGE_SPEC(arp_param_set_spec, arp_param_set)
