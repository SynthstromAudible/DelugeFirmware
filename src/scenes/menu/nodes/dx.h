#pragma once

/// @brief The DX7 menu pages: "DX7 source" and what it lists.
///
/// The port of `gui/menu_item/dx/`: `dxMenu`, the `Submenu` oscillator 1's type opens when it is DX7
/// (`osc::Type::selectButtonPress`, `gui/menu_item/osc/type.h:103-108`), with three rows:
///
/// - **load DX7 .syx file** (`DxBrowseMenu`): opens the `.syx` browser (`browser::BrowserId::DxSyx`) over the menu.
///   BACK from it returns to "DX7 source"; a bank it reads lands on the cartridge page under this row, whose BACK
///   reopens the browser, as legacy's `enterSubmenu(&dxCartridge)` does.
/// - **DX7 parameters** (`DxGlobalParams`): a list of the globals and the six operators. A global row opens the
///   parameter page on it, an operator row opens "DX7 operator N", the list of that operator's params.
/// - **engine version** (`DxEngineSelect`): auto, modern or vintage, written to the patch.
///
/// The parameter page (`DxParam`) shows one param and its neighbours in one of six layouts and edits it. The cartridge
/// page (`DxCartridge`) lists the read bank's programs: a turn loads one into the sound, SELECT closes the menu.
///
/// ## State
///
/// Legacy keeps the param shown and the two lists' cursors on its singletons, so they outlive the editor; the port
/// keeps them in `MenuState::dx`, which `MenuScene::open` carries over. Every write is the whole patch, through
/// `WriteSink::write_dx_patch`.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include <span>

namespace deluge::scenes::menu::nodes {

/// @brief "load DX7 .syx file": opens the `.syx` browser over the menu as soon as it is entered, and again when BACK
///        from the cartridge page returns to it.
class DxBrowseItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	/// @brief `DxBrowseMenu::beginSession` (`gui/menu_item/dx/browse.cpp:27-37`): the browser, going up a level when
	///        it closes.
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	/// @brief `beginSession` runs on a backward navigation too: BACK from the cartridge page reopens the browser.
	void on_focus_backward(const MenuContext& ctx, MenuState& state) const override { on_focus(ctx, state); }
};

/// @brief The DX7 cartridge page: the programs of the bank the `.syx` browser read (`DxCartridge`,
///        `gui/menu_item/dx/cartridge.cpp`), from `RuntimeFacts::dx_cartridge`.
///
/// Entering loads the first program; a turn moves the cursor within the bank and loads the program it lands on, through
/// `WriteSink::load_dx_program`; SELECT closes the menu.
class DxCartridgeItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	/// @brief `beginSession`: the first program, loaded.
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
};

/// @brief "engine version": the patch's engine mode, one of three rows (`DxEngineSelect`,
///        `gui/menu_item/dx/engine_select.cpp`).
class DxEngineSelectItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	/// @brief `readValueAgain` (`engine_select.cpp:38-42`): the cursor on the patch's mode.
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
};

inline const DxBrowseItem kDxBrowse{};
inline const DxEngineSelectItem kDxEngineSelect{};
inline const DxCartridgeItem kDxCartridge{};

/// @brief "DX7 parameters": the globals and the six operators (`DxGlobalParams`,
///        `gui/menu_item/dx/global_params.cpp`). Its cursor outlives the page, as legacy's member does.
class DxGlobalParamsItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	/// @brief `beginSession` (`global_params.cpp:34-36`) keeps the cursor where it was left.
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void on_focus_backward(const MenuContext& ctx, MenuState& state) const override { on_focus(ctx, state); }
};

/// @brief "DX7 operator N": one operator's params (`DxOperatorParams`, `gui/menu_item/dx/operator_params.cpp`). One
///        cursor, shared by the six operators, outlives the page, as legacy's one singleton's does.
class DxOperatorParamsItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	/// @brief `beginSession` (`operator_params.cpp:33-35`) keeps the cursor where it was left.
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void on_focus_backward(const MenuContext& ctx, MenuState& state) const override { on_focus(ctx, state); }
};

/// @brief The DX7 parameter page: `MenuState::dx.param`, drawn and edited as `DxParam` (`gui/menu_item/dx/param.cpp`)
///        does.
///
/// A select turn moves the value within its limits and writes the patch; a horizontal turn steps to the next param,
/// wrapping; Shift + a horizontal turn steps to the same param on the next operator that is switched on. All three are
/// read in `render`, where the turns arrive. Where legacy's operator search never ends (the pitch envelope's first rate
/// with every operator off), the page stays where it is.
class DxParamItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	/// @brief The select encoder's turn and press, and the horizontal encoder (`DxParam::horizontalEncoderAction`).
	deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	/// @brief Opens with no pad blinking (`DxState::pad_blink`).
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	/// @brief The param's own pad, every other cycle, once a horizontal step has set it up; none for the scaling params
	///        (`readValueAgain`'s `setupShortcutBlink(x, y, 1)`).
	std::optional<ItemBlink> own_blink(const MenuContext& ctx, const MenuState& state) const override;
	/// @brief The page's own pad layout while the DX7 engine feature is on (`DxParam::potentialShortcutPadAction`): a
	///        pad picks its param (`dx::pad_param`) and blinks it, and every press is the page's, a pad with no param
	///        included.
	bool shortcut_pad(const MenuContext& ctx, MenuState& state, uint8_t x, uint8_t y) const override;
};

inline const DxGlobalParamsItem kDxGlobalParams{};
inline const DxOperatorParamsItem kDxOperatorParams{};
inline const DxParamItem kDxParam{};

/// @brief Whether the pads under the menu are the DX7 layout: the parameter page is focused and the DX7 engine feature
///        is on (`isDxShortcutLayoutActive`, `gui/ui/sound_editor.cpp:159-162`).
bool dx_pad_layout(const MenuContext& ctx, const MenuState& state);

/// @brief Where SELECT on "DX7 parameters" goes: the parameter page, or "DX7 operator N" (`global_row_param`).
const Node* dx_global_params_chain(const MenuContext& ctx, const MenuState& state);
/// @brief Where SELECT on "DX7 operator N" goes: the parameter page.
const Node* dx_operator_params_chain(const MenuContext& ctx, const MenuState& state);

/// @brief The parameter page -- `dxParam`, title `STRING_FOR_DX_PARAM` (the page draws its own, per param).
inline constexpr Node dx_param = custom(sid(deluge::l10n::String::STRING_FOR_DX_PARAM), &kDxParam);

inline constexpr const Node* kDxOperatorParamsChainArray[] = {&dx_param};
/// @brief "DX7 operator N" -- `dxOperatorParams`, named as "DX7 parameters" is and titled
///        `STRING_FOR_DX_OPERATOR_PARAMS` with the operator's number.
inline constexpr Node dx_operator_params =
    with_select_chain(with_title(custom(sid(deluge::l10n::String::STRING_FOR_DX_GLOBAL_PARAMS), &kDxOperatorParams),
                                 sid(deluge::l10n::String::STRING_FOR_DX_OPERATOR_PARAMS)),
                      &dx_operator_params_chain, kDxOperatorParamsChainArray);

inline constexpr const Node* kDxGlobalParamsChainArray[] = {&dx_param, &dx_operator_params};
/// @brief "DX7 parameters" -- `dxGlobalParams`.
inline constexpr Node dx_global_params =
    with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_DX_GLOBAL_PARAMS), &kDxGlobalParams),
                      &dx_global_params_chain, kDxGlobalParamsChainArray);

/// @brief "load DX7 .syx file" -- `dxBrowseMenu`.
inline constexpr Node dx_browse = custom(sid(deluge::l10n::String::STRING_FOR_DX_BROWSER), &kDxBrowse);
/// @brief The cartridge page -- `dxCartridge`. No list names it: the `.syx` browser lands on it
///        (`MenuScene::land_dx_cartridge`), so it has a place in `kAllRoots`.
inline constexpr Node dx_cartridge = custom(sid(deluge::l10n::String::STRING_FOR_DX_CARTRIDGE), &kDxCartridge);
/// @brief "engine version" -- `dxEngineSelect`.
inline constexpr Node dx_engine_select =
    custom(sid(deluge::l10n::String::STRING_FOR_DX_ENGINE_SELECT), &kDxEngineSelect);

inline constexpr const Node* kDxMenuChildren[] = {&dx_browse, &dx_global_params, &dx_engine_select};
/// @brief "DX7 source" -- `dxMenu` (`gui/ui/menus.cpp`).
inline constexpr Node dx_menu = submenu(sid(deluge::l10n::String::STRING_FOR_DX_1), kDxMenuChildren);

/// @brief `osc::Type::selectButtonPress` (`gui/menu_item/osc/type.h:103-108`) for oscillator 1: "DX7 source" when its
///        type is DX7, else nothing, so SELECT ascends.
const Node* osc0_type_chain(const MenuContext& ctx, const MenuState& state);
/// @brief The same for oscillator 2, which legacy's type list never offers DX7.
const Node* osc1_type_chain(const MenuContext& ctx, const MenuState& state);

/// @brief `potentialShortcutPadAction`'s fallback (`gui/ui/sound_editor.cpp:1662-1665`) for a shortcut whose node does
///        not apply: modulator 1's volume, refused on a sound whose source `x & 1` is DX7, opens the DX7 parameter page
///        instead, on the param it showed last.
/// @param refused The node the pad names, which does not apply.
/// @param ctx     What the menu edits.
/// @param x       The pad's column; its low bit names the source.
/// @return `dx_param`, or nothing where legacy shows "PARAMETER NOT APPLICABLE".
const Node* dx_shortcut_fallback(const Node& refused, const MenuContext& ctx, uint8_t x);
inline constexpr const Node* kOscTypeChainArray[] = {&dx_menu};
inline constexpr std::span<const Node* const> kOscTypeChain{kOscTypeChainArray};

} // namespace deluge::scenes::menu::nodes
