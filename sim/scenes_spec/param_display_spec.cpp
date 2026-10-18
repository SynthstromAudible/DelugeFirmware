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
	params::Kind kind;       ///< the param's kind, with `index` its runtime address
	uint8_t index;           ///< the param's index within its kind
	std::string_view reason; ///< why no engine table row describes it
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

std::string_view display_name(ParamDisplay display) {
	switch (display) {
	case ParamDisplay::STANDARD:
		return "STANDARD";
	case ParamDisplay::HALF_PRECISION:
		return "HALF_PRECISION";
	case ParamDisplay::PAN:
		return "PAN";
	}
	return "<unknown>";
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

/// @brief The file names of the rows of @p table whose display is not the standard one, in table order.
std::vector<std::string> non_standard(std::span<const ParamInfo> table) {
	std::vector<std::string> out;
	for (const ParamInfo& row : table) {
		if (row.display != ParamDisplay::STANDARD) {
			out.emplace_back(row.file_name);
		}
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
		expect(nodes.size()).to_be_greater_than(size_t{0});
		for (const Node* node : nodes) {
			const ParamRef ref = node->bind.param;
			const ParamInfo* row = row_of(ref);
			if (row == nullptr) {
				expect(named(ref)).to_be_true("param " + std::to_string(ref.index) + " has no row and is not named");
				continue;
			}
			expect(display_name(row->display))
			    .to_equal(display_name(display_of(effective_scaling(*node))), std::string{row->file_name});
		}
	});
	it("has nodes that agree with each other about each param", _ {
		std::map<uint32_t, std::string_view> seen;
		for (const Node* node : param_nodes()) {
			const ParamRef ref = node->bind.param;
			const ParamInfo* row = row_of(ref);
			if (row == nullptr) {
				continue;
			}
			const uint32_t key = (static_cast<uint32_t>(ref.kind) << 8) | ref.index;
			const std::string_view shown = display_name(display_of(effective_scaling(*node)));
			const auto first = seen.emplace(key, shown);
			if (!first.second) {
				expect(shown).to_equal(first.first->second, std::string{row->file_name});
			}
		}
	});
	it("has the standard display on every param but the ones the tables name", _ {
		// A display a row lacks is the standard one, so a row that changes it must be named here.
		expect(non_standard(deluge::instrument::kParams))
		    .to_equal(std::vector<std::string>{"oscAPhaseWidth", "oscBPhaseWidth", "pan", "compressorThreshold"});
		expect(non_standard(deluge::global_effectable::kParams))
		    .to_equal(std::vector<std::string>{"compressorThreshold", "pan"});
	});
});
// clang-format on

DELUGE_SPEC(param_display_spec, param_display_spec_body)
