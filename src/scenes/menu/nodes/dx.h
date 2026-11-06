#pragma once

/// @brief The DX7 menu pages: "DX7 source" and what it lists.
///
/// The port of `gui/menu_item/dx/`: `dxMenu`, the `Submenu` oscillator 1's type opens when it is DX7
/// (`osc::Type::selectButtonPress`, `gui/menu_item/osc/type.h:103-108`), with three rows:
///
/// - **load DX7 .syx file** (`DxBrowseMenu`): legacy goes up a level and opens the `.syx` browser. The browser has
///   no port, so the row files `MenuAction::OpenDxBrowser`, which the app does nothing with, and goes up as legacy
///   does: the menu stays on "DX7 source".
/// - **DX7 parameters** (`DxGlobalParams`): a list of the globals and the six operators. A global row opens the
///   parameter page on it, an operator row opens "DX7 operator N", the list of that operator's params.
/// - **engine version** (`DxEngineSelect`): auto, modern or vintage, written to the patch.
///
/// The parameter page (`DxParam`) shows one param and its neighbours in one of six layouts and edits it.
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

/// @brief "load DX7 .syx file": goes up as soon as it is entered, filing the browser's action.
class DxBrowseItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	/// @brief `DxBrowseMenu::beginSession` (`gui/menu_item/dx/browse.cpp:27-37`): up a level, and the browser.
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
class DxParamItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

inline const DxGlobalParamsItem kDxGlobalParams{};
inline const DxOperatorParamsItem kDxOperatorParams{};
inline const DxParamItem kDxParam{};

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
/// @brief "engine version" -- `dxEngineSelect`.
inline constexpr Node dx_engine_select =
    custom(sid(deluge::l10n::String::STRING_FOR_DX_ENGINE_SELECT), &kDxEngineSelect);

inline constexpr const Node* kDxMenuChildren[] = {&dx_browse, &dx_global_params, &dx_engine_select};
/// @brief "DX7 source" -- `dxMenu` (`gui/ui/menus.cpp`).
inline constexpr Node dx_menu = submenu(sid(deluge::l10n::String::STRING_FOR_DX_1), kDxMenuChildren);

/// @brief `osc::Type::selectButtonPress` (`gui/menu_item/osc/type.h:103-108`) for oscillator @p source: "DX7
///        source" when its type is DX7, else nothing, so SELECT ascends.
const Node* osc0_type_chain(const MenuContext& ctx, const MenuState& state);
const Node* osc1_type_chain(const MenuContext& ctx, const MenuState& state);
inline constexpr const Node* kOscTypeChainArray[] = {&dx_menu};
inline constexpr std::span<const Node* const> kOscTypeChain{kOscTypeChainArray};

} // namespace deluge::scenes::menu::nodes
