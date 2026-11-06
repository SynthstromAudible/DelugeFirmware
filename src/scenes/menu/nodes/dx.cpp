/// @file
/// @brief The DX7 menu pages' members and chain hooks. See `dx.h`.

#include "scenes/menu/nodes/dx.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/dx_tables.h"
#include "scenes/profile.h"

#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include "definitions_cxx.hpp"
#include "instruments/deluge/sound_state.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title_for(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

/// @brief Source @p source's state on the sound the menu edits, if the facts know it.
const deluge::instrument::SourceState* source_of(const MenuContext& ctx, size_t source) {
	if (ctx.facts == nullptr || !ctx.addresses.slot) {
		return nullptr;
	}
	const deluge::instrument::SoundState* sound = ctx.facts->sound_state(*ctx.addresses.slot);
	return sound != nullptr ? &sound->sources[source] : nullptr;
}

/// @brief Oscillator 1's DX7 patch, the one every DX7 page edits (`DxParam::setValue`: "only OSC1 can be DX7").
std::optional<deluge::instrument::DxPatchState> patch_of(const MenuContext& ctx) {
	const deluge::instrument::SourceState* source = source_of(ctx, 0);
	return source != nullptr ? source->dx_patch : std::nullopt;
}

/// @brief Write oscillator 1's patch.
void write_patch(const MenuContext& ctx, const deluge::instrument::DxPatchState& patch) {
	if (ctx.sink != nullptr && ctx.addresses.slot) {
		(void)ctx.sink->write_dx_patch(*ctx.addresses.slot, 0, patch);
	}
}

/// @brief Draw @p names as a list on the focused level's cursor, as `MenuItem::drawItemsForOled` does for the three
///        DX7 lists, and publish the render's response.
deluge::ui::Response draw_list(deluge::ui::oled::OledFrame& f, MenuState& state,
                               std::span<const std::string_view> names) {
	constexpr size_t kMaxRows = 11;
	std::array<deluge::ui::oled::ListRow, kMaxRows> rows{};
	const size_t n = std::min(names.size(), kMaxRows);
	for (size_t i = 0; i < n; i++) {
		rows[i].label = names[i];
	}
	const size_t d = depth_of(state);
	state.tick.render_resp =
	    deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), state.list[d],
	                           std::span<const deluge::ui::oled::ListRow>{rows.data(), n}, state.scroll[d]);
	return state.tick.render_resp;
}

/// @brief The focused level's list, put on @p cursor.
void focus_list(MenuState& state, int16_t cursor) {
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.list[d].cursor = cursor;
	state.scroll[d] = deluge::ui::ScrollText{};
}

bool source_is_dx(const MenuContext& ctx, size_t source) {
	const deluge::instrument::SourceState* state = source_of(ctx, source);
	return state != nullptr && state->osc_type == OscType::DX7;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// `DxBrowseMenu` (`gui/menu_item/dx/browse.cpp`)

void DxBrowseItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	// `beginSession`: `shouldGoUpOneLevelOnBegin`, then `openUI(&dxBrowser)`. The menu goes up when it drains the
	// request.
	state.walk.pending_action = MenuActionRequest{.action = MenuAction::OpenDxBrowser};
}

void DxBrowseItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	draw_title_for(f, state); // never on screen: the item is left the tick it is entered
}

deluge::ui::shell::Result DxBrowseItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                               const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                               const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `DxEngineSelect` (`gui/menu_item/dx/engine_select.cpp`)

void DxEngineSelectItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	const std::optional<deluge::instrument::DxPatchState> patch = patch_of(ctx);
	focus_list(state, static_cast<int16_t>(patch ? patch->engine_mode : 0));
}

void DxEngineSelectItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);
	deluge::ui::ListState& list = state.list[depth_of(state)];
	// `selectEncoderAction` (`engine_select.cpp:51-65`): a turn that would leave the three modes does nothing at all,
	// rather than stopping at the end.
	constexpr std::array<std::string_view, 3> kModes{"auto", "modern", "vintage"};
	list.freeze_cursor = true;
	if (const int32_t t = f.turn(); t != 0) {
		const int32_t next = list.cursor + t;
		if (next >= 0 && next < std::ssize(kModes)) {
			if (std::optional<deluge::instrument::DxPatchState> patch = patch_of(ctx)) {
				patch->engine_mode = static_cast<uint8_t>(next);
				write_patch(ctx, *patch);
			}
			list.cursor = static_cast<int16_t>(next);
			f.gate().request_repaint();
		}
	}
	(void)draw_list(f, state, kModes); // SELECT ascends: `selectButtonPress` answers nothing
}

deluge::ui::InputClaims DxEngineSelectItem::owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const {
	return kListInput;
}

deluge::ui::shell::Result DxEngineSelectItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                     const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                     const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `DxGlobalParams` (`gui/menu_item/dx/global_params.cpp`)

void DxGlobalParamsItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_list(state, state.dx.global_cursor);
}

void DxGlobalParamsItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	draw_title_for(f, state);
	std::array<std::string_view, dx::kGlobalRows> names{};
	for (int32_t row = 0; row < dx::kGlobalRows; row++) {
		names[row] = dx::global_row(row);
	}
	const deluge::ui::Response resp = draw_list(f, state, names);
	const int16_t cursor = state.list[depth_of(state)].cursor;
	state.dx.global_cursor = cursor;
	if (resp.entered) {
		// `selectButtonPress` (`global_params.cpp:78-90`): the param, or the operator `6 - N` for "operator N".
		if (const int16_t param = dx::global_row_param(cursor); param >= 0) {
			state.dx.param = param;
		}
		else {
			state.dx.op = static_cast<int8_t>(6 + param);
		}
	}
}

deluge::ui::InputClaims DxGlobalParamsItem::owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const {
	return kListInput;
}

deluge::ui::shell::Result DxGlobalParamsItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                     const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                     const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

const Node* dx_global_params_chain(const MenuContext& /*ctx*/, const MenuState& state) {
	return dx::global_row_param(state.dx.global_cursor) >= 0 ? &dx_param : &dx_operator_params;
}

// ---------------------------------------------------------------------------------------------
// `DxOperatorParams` (`gui/menu_item/dx/operator_params.cpp`)

void DxOperatorParamsItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_list(state, state.dx.operator_cursor);
}

void DxOperatorParamsItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// `FormattedTitle::format(N)`: "DX7 operator *" with its asterisk replaced by the operator's number.
	const std::string_view format = deluge::scenes::text_lookup(sid(String::STRING_FOR_DX_OPERATOR_PARAMS));
	std::array<char, 32> title{};
	const size_t n = std::min(format.size(), title.size());
	std::copy_n(format.begin(), n, title.begin());
	if (const size_t star = format.find('*'); star < n) {
		title[star] = static_cast<char>('0' + (6 - state.dx.op));
	}
	deluge::ui::oled::title(f, std::string_view{title.data(), n});

	std::array<std::string_view, dx::kOperatorRows> names{};
	for (int32_t row = 0; row < dx::kOperatorRows; row++) {
		names[row] = dx::operator_row(row);
	}
	const deluge::ui::Response resp = draw_list(f, state, names);
	const int16_t cursor = state.list[depth_of(state)].cursor;
	state.dx.operator_cursor = cursor;
	if (resp.entered) {
		state.dx.param = dx::operator_row_param(state.dx.op, cursor); // `operator_params.cpp:80-84`
	}
}

deluge::ui::InputClaims DxOperatorParamsItem::owned_input(const MenuContext& /*ctx*/,
                                                          const MenuState& /*state*/) const {
	return kListInput;
}

deluge::ui::shell::Result DxOperatorParamsItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                       const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                       const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

const Node* dx_operator_params_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &dx_param;
}

// ---------------------------------------------------------------------------------------------
// `DxParam` (`gui/menu_item/dx/param.cpp`)

void DxParamItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	std::array<char, 32> title{};
	deluge::ui::oled::title(f, std::string_view{title.data(), dx::title(state.dx.param, title)});
	state.tick.render_resp = {};
	f.claim_select();
	if (f.select_pressed()) {
		state.tick.render_resp.entered = true; // `selectButtonPress` answers nothing: SELECT goes up
	}
}

deluge::ui::InputClaims DxParamItem::owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const {
	return {.select = true};
}

deluge::ui::shell::Result DxParamItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                              const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                              const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `osc::Type::selectButtonPress` (`gui/menu_item/osc/type.h:103-108`)

const Node* osc0_type_chain(const MenuContext& ctx, const MenuState& /*state*/) {
	return source_is_dx(ctx, 0) ? &dx_menu : nullptr;
}

const Node* osc1_type_chain(const MenuContext& ctx, const MenuState& /*state*/) {
	return source_is_dx(ctx, 1) ? &dx_menu : nullptr;
}

} // namespace deluge::scenes::menu::nodes
