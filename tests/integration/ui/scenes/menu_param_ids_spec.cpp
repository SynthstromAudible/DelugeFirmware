// Every menu item's param, held to the table and id recorded from the menu's kind tables: the menu names each param
// as it did, whatever spelling its tables use.

#include "menu_param_ids_reference.h"
#include "support/main_numbering.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace deluge::ui::scenes::menu;
using reference::MenuParamCell;

namespace {

std::vector<MenuParamCell> walk() {
	const uint16_t count = assign_node_ids(nodes::kAllRoots);
	std::vector<MenuParamCell> out;
	for (uint16_t id = 1; id <= count; id++) {
		const Node* n = node_by_id(id);
		if (n == nullptr || !n->bind.param.valid()) {
			continue;
		}
		const ParamRef p = n->bind.param;
		out.push_back({.node = id,
		               .name = n->name,
		               .table = static_cast<uint8_t>(param_table_of(p)), // 0 a sound's, 1 a chain's, 2 the arp's
		               .id = param_id_of(p).value,
		               .patched = is_patched(p)});
	}
	return out;
}

} // namespace

// clang-format off
describe menu_param_ids_cases("the menu's params by table and id", ${
	it("names every item's param as the kind tables did", _ {
		const std::vector<MenuParamCell> cells = walk();
		if (std::getenv("MENU_PARAM_IDS_RECORD") != nullptr) {
			for (const MenuParamCell& c : cells) {
				std::printf("MENU-PARAM-IDS {%u, %u, %u, %u, %s},\n", c.node, c.name, c.table, c.id, c.patched ? "true" : "false");
			}
		}
		expect(cells.size()).to_equal(std::size(reference::kMenuParamIds));
		for (size_t i = 0; i < cells.size() && i < std::size(reference::kMenuParamIds); i++) {
			MenuParamCell want = reference::kMenuParamIds[i];
			// Recorded before the arp's params left the sound's and the chain's tables: those are the arp's own now, and
			// the ids past them sit lower (support/main_numbering.h).
			namespace numbering = deluge::ui::scenes::spec::main_numbering;
			if (!want.patched && want.id >= numbering::kUnpatchedFirstId) {
				const deluge::project::ParamTarget ours = numbering::target_of_id(want.table == 1, want.id);
				if (ours.space == deluge::project::ParamSpace::ARP) {
					want.table = static_cast<uint8_t>(deluge::ui::scenes::bind::ParamTable::ARP);
				}
				want.id = ours.id.value;
			}
			// A sound's arp rate was its patched param; here it is the arp's, its matrix destination patched alone
			if (want.patched && want.table == 0) {
				if (const std::optional<uint32_t> arp = numbering::arp_id_of_patched(want.id)) {
					want.table = static_cast<uint8_t>(deluge::ui::scenes::bind::ParamTable::ARP);
					want.id = *arp;
					want.patched = false;
				}
			}
			expect(cells[i].node).to_equal(want.node);
			expect(cells[i].name).to_equal(want.name);
			expect(cells[i].table).to_equal(want.table);
			expect(cells[i].id).to_equal(want.id);
			expect(cells[i].patched).to_equal(want.patched);
		}
	});
});
// clang-format on

DELUGE_SPEC(menu_param_ids_spec, menu_param_ids_cases)
