# deluge_scenes_fake: the scenes library, linked whole over the ports' fakes, with no app. It linking is the proof that
# no scene needs the app: WHOLE_ARCHIVE keeps every object, so a symbol only the app defines fails the link. The model's
# host hooks (its memory) come from model_hooks_for_tests.cpp; printf's character sink from mock_print.cpp, listed here
# because the spec library links after printf is first needed. NEON: the compressor's menu page and the knob push reach
# argon through effects/.
if(NOT TARGET deluge_ui_scenes_fakes)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/support/ui/scenes ${CMAKE_CURRENT_BINARY_DIR}/scenes_fakes)
endif()
deluge_specs_driver(deluge_scenes_fake TREES host NEON
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/model_hooks_for_tests.cpp
          ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_print.cpp
          ${DELUGE_REPO_ROOT}/tests/unit/ui/scenes/bind/support/model_fixture.cpp
  LIBRARIES "$<LINK_LIBRARY:WHOLE_ARCHIVE,deluge_ui_scenes>" deluge_ui_scenes_fakes deluge_ui_scenes_bind
            deluge_l10n deluge_host_chains deluge_ui_controls deluge_spec
  INCLUDES ${DELUGE_REPO_ROOT}/tests/unit/ui/scenes/bind)
