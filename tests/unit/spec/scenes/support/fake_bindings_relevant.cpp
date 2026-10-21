/// @file
/// @brief The host suite's model-free stand-in for `bindings::relevant`, which `validate.cpp` asks whether two nodes
///        share a screen. The real one resolves a target node against the model and the app's facts, which this suite
///        has neither of: its trees are built of hook nodes, whose relevance is their own predicate.

#include "scenes/menu/bindings.h"
#include "scenes/menu/node.h"

namespace deluge::scenes::menu::bindings {

bool relevant(const Node& node, const MenuContext& ctx) {
	return node.relevant == nullptr || node.relevant(ctx);
}

} // namespace deluge::scenes::menu::bindings
