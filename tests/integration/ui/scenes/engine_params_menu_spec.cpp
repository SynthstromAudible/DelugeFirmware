// A sound's menu offers only the params its playing engine owns and the sound's own: walking the sound menu as its
// relevance lets a user reach it, in each synth mode, finds no param of an engine the sound is not playing.

#include "instruments/classic/engine_params.h"
#include "support/descriptor_cases.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/nodes.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <set>
#include <string>
#include <vector>

using namespace deluge::ui::scenes::menu;

namespace {

/// The params of the sound's own table the nodes under @p node reach, descending only into what is relevant in @p ctx.
void reached(const Node& node, const MenuContext& ctx, std::set<const Node*>& seen,
             std::vector<deluge::plugin::ParamId>& out) {
	if (!seen.insert(&node).second || !bindings::relevant(node, ctx)) {
		return;
	}
	if (node.bind.param.valid() && param_table_of(node.bind.param) == deluge::ui::scenes::bind::ParamTable::SOUND) {
		out.push_back(param_id_of(node.bind.param));
	}
	for (const Node* child : node.children) {
		if (child != nullptr) {
			reached(*child, ctx, seen, out);
		}
	}
}

/// The parked params the sound menu reaches in the sound @p where names, by file name; empty when there are none.
std::string parked_reached(descriptor_cases::Where where, deluge::instruments::classic::SynthMode mode) {
	std::string out;
	for (const int8_t source : {int8_t{0}, int8_t{1}}) {
		const MenuContext& ctx = descriptor_cases::enter(where, source);
		std::set<const Node*> seen;
		std::vector<deluge::plugin::ParamId> params;
		reached(nodes::sound_root, ctx, seen, params);
		for (const deluge::plugin::ParamId id : params) {
			if (!deluge::instruments::classic::param_is_live(mode, id)) {
				const auto* row = deluge::plugin::find_param(deluge::instruments::classic::kParams, id);
				out += std::string{row != nullptr ? row->file_name : "?"} + " ";
			}
		}
	}
	return out;
}

} // namespace

// clang-format off
describe engine_params_menu("a sound's menu, by the params its engine owns", $ {
	it("reaches no oscillator engine param in DX7 mode", _ {
		expect(parked_reached(descriptor_cases::Where::SYNTH_DX7, deluge::instruments::classic::SynthMode::DX7))
		    .to_equal(std::string{});
	});
	it("reaches none of the DX7's params in the oscillator engines' modes", _ {
		expect(parked_reached(descriptor_cases::Where::SYNTH, deluge::instruments::classic::SynthMode::SUBTRACTIVE))
		    .to_equal(std::string{});
		expect(parked_reached(descriptor_cases::Where::SYNTH_FM, deluge::instruments::classic::SynthMode::FM))
		    .to_equal(std::string{});
		expect(parked_reached(descriptor_cases::Where::SYNTH_RINGMOD, deluge::instruments::classic::SynthMode::RINGMOD))
		    .to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(engine_params_menu_spec, engine_params_menu)
