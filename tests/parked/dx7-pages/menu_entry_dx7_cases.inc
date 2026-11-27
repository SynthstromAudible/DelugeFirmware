// menu_entry_spec's DX7 cases, parked until the DX7 engine's pages (plan E): each opens a DX7 page on a sound that
// holds a DX7 on an oscillator, which no sound can since the DX7 became its own engine.

	it("site 11 opens DX7 source on the new FM synth, keeping the undo log", _ {
		// `InstrumentClipMinder::createNewInstrument`'s `soundEditor.setup(clip, &dxMenu, 0)`: a named item, so no wipe.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = f.instrument->toModControllable()->effectsOwner()->sound;
		deluge::test::setOscType(*sound, 0, OscType::DX7);
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		closeGuest();

		expect(entry::openMenu(f.clip, 0, false, false, false, false, entry::MenuNode::Dx)).to_be_true();
		expect(opened_on())
		    .to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_menu)});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::dx_menu);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();
	});


	it("site 15 opens the DX7 parameter page on the column's operator, its pad blinking, keeping the undo log", _ {
		// `DXColumn::handlePad`'s Shift arm: `openForOpOrGlobal`'s `setup(getCurrentClip(), &dxParam, 0)`.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = f.instrument->toModControllable()->effectsOwner()->sound;
		deluge::test::setOscType(*sound, 0, OscType::DX7);
		f.song->setCurrentClip(f.clip);
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		closeGuest();

		// Through the column itself, its Shift arm gated: row 5 is operator index 2 (`8 - y - 1`), acted on release.
		{
			HeldShift shift;
			deluge::gui::ui::keyboard::controls::DXColumn column;
			deluge::gui::ui::keyboard::PressedPad release{};
			release.x = 17;
			release.y = 5;
			release.active = false;
			column.handlePad(nullptr, release, nullptr);
		}
		expect(isUIOpen(&shellHostUI)).to_be_true();
		expect(opened_on())
		    .to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_param)});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().dx.param)).to_equal(2 * 21 + 16);
		expect(deluge::ui::scenes::menu_scene().state().dx.pad_blink).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		// With the guest open the press is the menu's: the same operator again toggles to its coarse tuning.
		expect(entry::openShellDxColumn(2)).to_be_true();
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().dx.param)).to_equal(2 * 21 + 18);
		closeGuest();
	});


	it("Shift + (4,0) on a DX7 sound opens the DX7 parameter page alone, on the param it showed last", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		// `potentialShortcutPadAction`'s fallback: modulator 1's volume does not apply to a sound that is not FM, so
		// `setup()` refuses it, and on a DX7 source `setup(clip, &dxParam, 0)` opens the page as `dxParam.param` stands.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = f.instrument->toModControllable()->effectsOwner()->sound;
		deluge::test::setOscType(*sound, 0, OscType::DX7);
		deluge::ui::scenes::menu_scene().land_dx(menu::DxState{.param = 3 * 21 + 2});
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 4, 0)).to_equal(&nodes::modulator0_volume);

		expect(entry::openMenuAtShortcut(f.clip, 4, 0, false, false, false, false)).to_be_true();
		expect(opened_on())
		    .to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_param)});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().depth)).to_equal(0);
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().dx.param)).to_equal(3 * 21 + 2);
		closeGuest();

		// The same pad on a sound that is not DX7 is refused, with setup()'s popup.
		deluge::test::setOscType(*sound, 0, OscType::SAW);
		expect(entry::openMenuAtShortcut(f.clip, 4, 0, false, false, false, false)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
	});


	it("says PARAMETER NOT APPLICABLE for a pad that does not apply, and the DX7 test reads the pad's own source", _ {
		// A DX7 source beside the pad's own: the fallback tests the pad's source, not this one.
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = f.instrument->toModControllable()->effectsOwner()->sound;
		deluge::test::setOscType(*sound, 0, OscType::SAW);
		deluge::test::setOscType(*sound, 1, OscType::DX7);
		setRootUILowLevel(&instrumentClipView);
		{
			HeldShift shift;
			expect(entry::menuShortcutPadAction(4, 0, true)).to_equal(ActionResult::DEALT_WITH);
		}
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(text(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
		deluge::test::setOscType(*sound, 1, OscType::SAW);
		nullifyUIs();
	});

