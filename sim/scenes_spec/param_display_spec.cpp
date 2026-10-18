/// @file
/// @brief Every param a menu node binds, against how the engine's param table says a menu shows it.
///
/// Each scenes node with a `ParamRef` and no `get`/`set` hook reads and writes through a `Scaling`; the param's table
/// row says the same through `ParamInfo::display`. A node whose param has no row in a table, or nodes that disagree
/// about one param, are named below with their reasons.
///
/// @note The SPEC may include `scenes/menu/`; the library may not (`scripts/check_layer_rules.py`).

#include "engine-api/types/param.h"
#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"

#include "model/project_bridge/param_ids.h"
#include "modulation/params/param.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include "cppspec.hpp"

#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes::menu;
namespace params = deluge::modulation::params;
using deluge::engine::ParamDisplay;
using deluge::engine::ParamId;
using deluge::engine::ParamInfo;

namespace {

/// @brief A param a node binds that the check names rather than compares, with why.
struct Exception {
	params::Kind kind;
	uint8_t index;
	std::string_view reason;
};

/// Nodes whose param no engine table describes: none today. A param of a host space (expression, MIDI CC) or a patch
/// cable has no `ParamInfo` row, so a node binding one is named here.
constexpr std::span<const Exception> kUnresolved{};

/// @brief The scaling a node's reads and writes really use: its own, or Pan where it is one of legacy's two pan items.
Scaling effective_scaling(const Node& node) {
	if (node.bind.scaling != Scaling::Standard) {
		return node.bind.scaling;
	}
	if (node.hint == Hint::Pan || params::isParamPan(node.bind.param.kind, node.bind.param.index)) {
		return Scaling::Pan;
	}
	return Scaling::Standard;
}

ParamDisplay display_of(Scaling s) {
	switch (s) {
	case Scaling::HalfPrecision:
		return ParamDisplay::HALF_PRECISION;
	case Scaling::Pan:
		return ParamDisplay::PAN;
	case Scaling::Standard:
		break;
	}
	return ParamDisplay::STANDARD;
}

/// @brief The table row of the param @p ref names, or nullptr where no engine table has one.
const ParamInfo* row_of(ParamRef ref) {
	const ParamId id = deluge::bridge::paramIdOf(ref.kind, ref.index);
	switch (ref.kind) {
	case params::Kind::PATCHED:
	case params::Kind::NONE:
	case params::Kind::UNPATCHED_SOUND:
		return deluge::engine::find_param(deluge::instrument::kParams, id);
	case params::Kind::UNPATCHED_GLOBAL:
		return deluge::engine::find_param(deluge::global_effectable::kParams, id);
	default:
		return nullptr;
	}
}

void walk(const Node& node, std::set<const Node*>& seen, std::vector<const Node*>& out) {
	if (!seen.insert(&node).second) {
		return;
	}
	if (node.bind.param.valid() && node.bind.get == nullptr && node.bind.set == nullptr) {
		out.push_back(&node);
	}
	for (const Node* child : node.children) {
		walk(*child, seen, out);
	}
	for (const Node* child : node.chain_targets) {
		walk(*child, seen, out);
	}
}

std::vector<const Node*> param_nodes() {
	std::set<const Node*> seen;
	std::vector<const Node*> out;
	for (const Node* root : nodes::kAllRoots) {
		walk(*root, seen, out);
	}
	return out;
}

bool named(ParamRef ref) {
	for (const Exception& e : kUnresolved) {
		if (e.kind == ref.kind && e.index == ref.index) {
			return true;
		}
	}
	return false;
}

} // namespace

// clang-format off
describe param_display_spec_body("The param tables' display against the menu nodes' scaling", $ {
	it("has a row whose display is the scaling of every node that binds the param", _ {
		const auto nodes = param_nodes();
		expect(nodes.empty()).to_be_false();
		for (const Node* node : nodes) {
			const ParamRef ref = node->bind.param;
			const ParamInfo* row = row_of(ref);
			if (row == nullptr) {
				expect(std::string{"unnamed unresolved param "} + std::to_string(ref.index) + (named(ref) ? "" : " unnamed"))
				    .to_equal(std::string{"unnamed unresolved param "} + std::to_string(ref.index));
				continue;
			}
			const bool same = row->display == display_of(effective_scaling(*node));
			expect(std::string{row->file_name} + (same ? " agrees" : " differs"))
			    .to_equal(std::string{row->file_name} + " agrees");
		}
	});
	it("has nodes that agree with each other about each param", _ {
		std::map<uint32_t, Scaling> seen;
		for (const Node* node : param_nodes()) {
			const ParamRef ref = node->bind.param;
			if (row_of(ref) == nullptr) {
				continue;
			}
			const uint32_t key = (static_cast<uint32_t>(ref.kind) << 8) | ref.index;
			const Scaling s = effective_scaling(*node);
			const auto first = seen.emplace(key, s);
			if (!first.second) {
				expect(first.first->second == s).to_be_true();
			}
		}
	});
	it("shows no units on a param", _ {
		for (const ParamInfo& row : deluge::instrument::kParams) {
			expect(row.units == deluge::engine::SettingUnits::NONE).to_be_true();
		}
		for (const ParamInfo& row : deluge::global_effectable::kParams) {
			expect(row.units == deluge::engine::SettingUnits::NONE).to_be_true();
		}
	});
});
// clang-format on

DELUGE_SPEC(param_display_spec, param_display_spec_body)
