/// @file
/// @brief The DX7 menu pages' members and chain hooks. See `dx.h`.

#include "scenes/menu/nodes/dx.h"

#include "scenes/browser/browser_scene.h" // DxBrowseItem::on_focus opens this
#include "scenes/l10n_bridge.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/dx_tables.h"
#include "scenes/menu/nodes/sound_osc.h" // modulator0_volume, the one shortcut with a DX7 fallback
#include "scenes/profile.h"

#include "gfx/oled/font.h"
#include "ui/oled/frame.h"
#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include "definitions_cxx.hpp"
#include "foundation/string_format.h"
#include "instruments/deluge/sound_state.h"
#include "processing/dx7/algorithm_diagrams.h"

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
	constexpr size_t kMaxRows = 32;
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

using PatchBytes = std::array<uint8_t, 156>;

/// @brief `show` (`param.cpp:325-339`): @p text on one of the page's two rows at a character column, inverted when
///        it is the param shown.
void show(deluge::ui::oled::OledFrame& p, std::string_view text, int32_t row, int32_t column, bool inverted = false) {
	using namespace deluge::gfx::oled;
	const int32_t y = 7 + (2 + row) * (kTextSizeYUpdated + 2);
	const int32_t x = 5 + column * kTextSpacingX;
	p.glyphs(text, x, y, kTextSpacingX, kTextSizeYUpdated);
	if (inverted) {
		p.invert(Rect{x - 1, y - 1, kTextSpacingX * static_cast<int32_t>(text.size()) + 1, kTextSizeYUpdated + 2});
	}
}

/// @brief `show`'s number form: at least two digits.
void show(deluge::ui::oled::OledFrame& p, int32_t value, int32_t row, int32_t column, bool inverted = false) {
	char digits[12];
	intToString(value, digits, 2);
	show(p, digits, row, column, inverted);
}

/// @brief `renderEnvelope` (`param.cpp:341-350`): rates over levels; `op` 6 is the pitch envelope.
void draw_envelope(deluge::ui::oled::OledFrame& p, const PatchBytes& params, int32_t op, int32_t idx) {
	show(p, "rate", 0, 0);
	show(p, "levl", 1, 0);
	for (int32_t i = 0; i < 4; i++) {
		show(p, params[op * 21 + i], 0, 5 + i * 3, i == idx);
		show(p, params[op * 21 + 4 + i], 1, 5 + i * 3, i + 4 == idx);
	}
}

/// @brief `renderScaling` (`param.cpp:352-396`): the two depths over the two curves, the breakpoint between them.
void draw_scaling(deluge::ui::oled::OledFrame& p, const PatchBytes& params, int32_t op, int32_t idx) {
	using namespace deluge::gfx::oled;
	for (int32_t i = 0; i < 2; i++) {
		show(p, params[op * 21 + 9 + i], 0, 1 + i * 11, 9 + i == idx);
		show(p, dx::curve_name(params[op * 21 + 11 + i]), 1, 1 + i * 11, 11 + i == idx);
	}
	const int32_t y = 7 + 2 * (kTextSizeYUpdated + 2) + ((kTextSizeYUpdated + 1) >> 1);
	const int32_t x = 14 + 6 * kTextSpacingX;
	char digits[12];
	intToString(params[op * 21 + 8], digits, 2);
	p.glyphs(digits, x, y, kTextSpacingX, kTextSizeYUpdated);
	if (idx == 8) {
		p.invert(Rect{x - 1, y - 1, kTextSpacingX * 2 + 1, kTextSizeYUpdated + 2});
	}
}

/// @brief `renderSensParams` (`param.cpp:398-407`).
void draw_sensitivity(deluge::ui::oled::OledFrame& p, const PatchBytes& params, int32_t op, int32_t idx) {
	show(p, "rate scale", 0, 1);
	show(p, params[op * 21 + 13], 0, 12, idx == 13);
	show(p, "ampmod", 1, 0);
	show(p, params[op * 21 + 14], 1, 7, idx == 14);
	show(p, "velo", 1, 11);
	show(p, params[op * 21 + 15], 1, 16, idx == 15);
}

/// @brief `renderTuning` (`param.cpp:409-425`): the mode, coarse, fine and detune (shown from -7), then the level.
void draw_tuning(deluge::ui::oled::OledFrame& p, const PatchBytes& params, int32_t op, int32_t idx) {
	show(p, params[op * 21 + 17] != 0 ? "fixed" : "ratio", 0, 1, idx == 17);
	for (int32_t i = 0; i < 3; i++) {
		int32_t value = params[op * 21 + 18 + i];
		if (i + 18 == 20) {
			value -= 7;
		}
		show(p, value, 0, 7 + i * 3, i + 18 == idx);
	}
	show(p, "level", 1, 1);
	show(p, params[op * 21 + 16], 1, 7, idx == 16);
}

/// @brief `renderLFO` (`param.cpp:427-455`).
void draw_lfo(deluge::ui::oled::OledFrame& p, const PatchBytes& params, int32_t param) {
	for (int32_t i = 0; i < 2; i++) {
		show(p, params[137 + i], 0, 1 + i * 3, 137 + i == param);
	}
	for (int32_t i = 0; i < 2; i++) {
		show(p, i == 0 ? "pitch" : "  amp", 1, 1 + i * 9);
		show(p, params[139 + i], 1, 1 + i * 9 + 6, i + 139 == param);
	}
	show(p, params[141] != 0 ? "sync" : "    ", 0, 7, param == 141);
	show(p, dx::lfo_shape(params[142]), 0, 12, param == 142);
	char digits[12];
	intToString(params[143], digits, 1);
	show(p, digits, 1, 10, param == 143);
}

/// @brief `renderAlgorithm` (`param.cpp:457-473`): a "#NN" badge inverted at the title's right, and the diagram.
void draw_algorithm(deluge::ui::oled::OledFrame& p, int32_t top, const PatchBytes& params) {
	using namespace deluge::gfx::oled;
	const int32_t algorithm = std::min<int32_t>(params[134], deluge::dx7::kNumAlgorithms - 1);
	char badge[12] = "#";
	intToString(algorithm + 1, badge + 1, 2);
	constexpr int32_t kBadgePadding = 2;
	const int32_t width = p.canvas.get_string_width_in_pixels(badge, kTextTitleSizeY);
	const int32_t panel_width = p.bounds().w;
	const int32_t separator_y = 12 + top; // `kScreenTitleSeparatorY`
	const int32_t x = panel_width - width - 2 * kBadgePadding;
	p.glyphs(badge, x + kBadgePadding, top + 1, kTextTitleSpacingX, kTextTitleSizeY);
	p.invert(Rect{x, top, panel_width - x, separator_y - 2 - top + 1});
	const deluge::dx7::AlgorithmDiagram d = deluge::dx7::algorithm_diagram(algorithm);
	p.graphic(d.data, d.x, d.y, d.width, d.height, d.num_bytes_tall);
}

/// @brief `DxParam::drawPixelsForOled` (`param.cpp:475-506`) for @p param of @p patch, on the panel frame @p p.
/// @param top The panel's first visible row, `OLED_MAIN_TOPMOST_PIXEL` on the 48-row panel.
void draw_param(deluge::ui::oled::OledFrame& p, int32_t top, const deluge::instrument::DxPatchState& patch,
                int16_t param) {
	using namespace deluge::gfx::oled;
	const PatchBytes& params = patch.params;
	if (param < 0 || param == 135 || param == 136) {
		// The random detune, the feedback and the sync: one huge number.
		char digits[12];
		intToString(param < 0 ? patch.random_detune : params[param], digits, param < 0 ? 2 : 1);
		p.glyphs(digits, 50, 20, kTextHugeSpacingX, kTextHugeSizeY);
		return;
	}
	const int32_t op = param / dx::kOperatorParams;
	const int32_t idx = param % dx::kOperatorParams;
	if (param < dx::kFirstGlobal + 8 && idx < 8) {
		draw_envelope(p, params, op, idx); // `op` 6 is the pitch envelope
	}
	else if (param < dx::kFirstGlobal && idx < 13) {
		draw_scaling(p, params, op, idx);
	}
	else if (param < dx::kFirstGlobal && idx < 16) {
		draw_sensitivity(p, params, op, idx);
	}
	else if (param < dx::kFirstGlobal) {
		draw_tuning(p, params, op, idx);
	}
	else if (param == 134) {
		draw_algorithm(p, top, params);
	}
	else if (param >= 137 && param <= dx::kLastParam) {
		draw_lfo(p, params, param);
	}
}

bool source_is_dx(const MenuContext& ctx, size_t source) {
	const deluge::instrument::SourceState* state = source_of(ctx, source);
	return state != nullptr && state->osc_type == OscType::DX7;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// `DxBrowseMenu` (`gui/menu_item/dx/browse.cpp`)

void DxBrowseItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `beginSession` (`browse.cpp:27-37`): `shouldGoUpOneLevelOnBegin`, then `openUI(&dxBrowser)`. The menu sets the
	// go-up when it pushes the browser.
	browser::browser_scene().open(ctx);
	state.walk.pending_scene =
	    deluge::ui::shell::SceneId{browser::kBrowserSceneKind, static_cast<uint16_t>(browser::BrowserId::DxSyx)};
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
// `DxCartridge` (`gui/menu_item/dx/cartridge.cpp`)

namespace {

/// @brief Load program @p program of the bank the last read loaded into the menu's source.
void load_program(const MenuContext& ctx, int32_t program) {
	if (ctx.sink != nullptr && ctx.addresses.slot) {
		(void)ctx.sink->load_dx_program(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index), program);
	}
}

} // namespace

void DxCartridgeItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `beginSession`'s `loadPatch()` on the first program: the read put `currentValue` there.
	focus_list(state, 0);
	load_program(ctx, 0);
}

void DxCartridgeItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);
	const std::optional<bind::DxCartridgeNames> bank =
	    ctx.facts != nullptr ? ctx.facts->dx_cartridge() : std::optional<bind::DxCartridgeNames>{};
	if (!bank || bank->count <= 0) {
		return; // `drawPixelsForOled` draws nothing with no bank
	}
	deluge::ui::ListState& list = state.list[depth_of(state)];
	// `selectEncoderAction`: the cursor clamps to the bank, and only a move loads the program.
	list.freeze_cursor = true;
	if (const int32_t t = f.turn(); t != 0) {
		const int32_t next = std::clamp<int32_t>(list.cursor + t, 0, bank->count - 1);
		if (next != list.cursor) {
			list.cursor = static_cast<int16_t>(next);
			load_program(ctx, next);
			f.gate().request_repaint();
		}
	}
	std::array<std::string_view, 32> names{};
	for (int32_t i = 0; i < bank->count; i++) {
		names[i] = std::string_view{bank->names[i].data()};
	}
	const deluge::ui::Response resp =
	    draw_list(f, state, std::span<const std::string_view>{names.data(), static_cast<size_t>(bank->count)});
	if (resp.entered) {
		// `selectButtonPress`: `soundEditor.exitCompletely()`.
		state.walk.exit_completely = true;
	}
}

deluge::ui::InputClaims DxCartridgeItem::owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const {
	return kListInput;
}

deluge::ui::shell::Result DxCartridgeItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
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

void DxParamItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	std::optional<deluge::instrument::DxPatchState> patch = patch_of(ctx);
	int16_t& param = state.dx.param;
	f.claim_turn();
	f.claim_horizontal();
	if (const int32_t t = f.turn(); t != 0 && patch) {
		// `selectEncoderAction` (`param.cpp:180-195`) and `setValue` (`:56-70`): oscillator 1's patch, always.
		if (param >= 0 && param <= dx::kLastParam) {
			const int32_t value = patch->params[param] + t;
			patch->params[param] = static_cast<uint8_t>(std::clamp<int32_t>(value, 0, dx::upper_limit(param)));
		}
		else if (param == dx::kRandomDetune) {
			patch->random_detune = std::clamp<int32_t>(patch->random_detune + t, 0, dx::upper_limit(param));
		}
		write_patch(ctx, *patch);
		f.gate().request_repaint();
	}
	if (const int8_t h = f.horizontal(); h != 0) {
		// `horizontalEncoderAction` (`param.cpp:197-226`), each arm ending in `readValueAgain`, which sets the pad's
		// blink up; the Shift arm returns before it outside the operators and the pitch envelope's first rate.
		if (!f.shift()) {
			param = dx::step_param(param, h);
			state.dx.pad_blink = true;
		}
		else if (patch && param >= 0 && param <= dx::kFirstGlobal) {
			// `patch` is legacy's `!patch` test, which never fires: a DX7 source always has a patch, and so do its
			// facts.
			param = dx::step_operator(param, h, patch->params).value_or(param);
			state.dx.pad_blink = true;
		}
		f.gate().request_repaint();
	}

	std::array<char, 32> title{};
	deluge::ui::oled::title(f, std::string_view{title.data(), dx::title(state.dx.param, title)});
	state.tick.render_resp = {};
	f.claim_select();
	if (f.select_pressed()) {
		state.tick.render_resp.entered = true; // `selectButtonPress` answers nothing: SELECT goes up
	}
	if (const std::optional<deluge::instrument::DxPatchState> patch = patch_of(ctx)) {
		// Legacy draws in the canvas's own coordinates, the rows above the panel's visible top included.
		deluge::ui::oled::OledFrame panel = deluge::ui::oled::panel_frame(f);
		draw_param(panel, f.origin.y, *patch, state.dx.param);
	}
}

deluge::ui::InputClaims DxParamItem::owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const {
	return {.turn = true, .horizontal = true, .select = true};
}

void DxParamItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	state.dx.pad_blink = false;
}

std::optional<ItemBlink> DxParamItem::own_blink(const MenuContext& /*ctx*/, const MenuState& state) const {
	const std::optional<dx::Pad> pad = state.dx.pad_blink ? dx::param_pad(state.dx.param) : std::nullopt;
	return pad ? ItemBlink{.x = pad->x, .y = pad->y, .valid = true} : ItemBlink{};
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

const Node* dx_shortcut_fallback(const Node& refused, const MenuContext& ctx, uint8_t x) {
	// Legacy tests `soundEditor.currentSource`, which the refused `setup()` has not yet set, so it is the source the
	// previous menu left; this tests the pad's own source on this sound (DIFFERENCES D90).
	return &refused == &modulator0_volume && source_is_dx(ctx, x & 1) ? &dx_param : nullptr;
}

} // namespace deluge::scenes::menu::nodes
