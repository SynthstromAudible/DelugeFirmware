/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The four envelope pages' exported root span. Every leaf is a plain `ParamRef`, so this
///        subtree has no `get`/`set` hooks at all and the `Node` literals in `sound_env.h` are the
///        whole of it.

#include "scenes/menu/nodes/sound_env.h"

#include "scenes/menu/bindings.h"
#include "ui/oled/column_drawings.h"

namespace deluge::scenes::menu::nodes {

namespace {
constexpr const Node* kRoots[] = {&env1, &env2, &env3, &env4};
} // namespace

const std::span<const Node* const> kSoundEnvChildren{kRoots};

namespace {

/// @brief `EnvelopeMenu::renderMenuItems` (`envelope/envelope_menu.h:34-91`) over envelope @p env's four segments.
void draw_envelope(ui::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused, const Node& attack,
                   const Node& decay, const Node& sustain, const Node& release) {
	ui::oled::envelope_graph(panel, bindings::read(attack, ctx), bindings::read(decay, ctx),
	                         bindings::read(sustain, ctx), bindings::read(release, ctx), focused);
}

} // namespace

void env1_page_draw(ui::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused) {
	draw_envelope(panel, ctx, focused, env1_attack, env1_decay, env1_sustain, env1_release);
}
void env2_page_draw(ui::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused) {
	draw_envelope(panel, ctx, focused, env2_attack, env2_decay, env2_sustain, env2_release);
}
void env3_page_draw(ui::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused) {
	draw_envelope(panel, ctx, focused, env3_attack, env3_decay, env3_sustain, env3_release);
}
void env4_page_draw(ui::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused) {
	draw_envelope(panel, ctx, focused, env4_attack, env4_decay, env4_sustain, env4_release);
}

} // namespace deluge::scenes::menu::nodes
