# The parent tests/CMakeLists.txt already sets the language standard and calls enable_testing().
add_executable(ParamManagerLayoutTests param_manager_layout_tests.cpp)
target_include_directories(ParamManagerLayoutTests PRIVATE mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerLayoutTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerLayoutTests COMMAND ParamManagerLayoutTests)

add_executable(ParamManagerCleanupTests param_manager_cleanup_tests.cpp
	../../src/deluge/modulation/params/param_manager_cleanup.cpp)
target_include_directories(ParamManagerCleanupTests PRIVATE mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerCleanupTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerCleanupTests COMMAND ParamManagerCleanupTests)

add_executable(ParamManagerTransferTests param_manager_transfer_tests.cpp
	../../src/deluge/modulation/params/param_manager_setup.cpp
	../../src/deluge/modulation/params/param_manager_type.cpp
	../../src/deluge/modulation/params/param_manager_transfer.cpp
	../../src/deluge/modulation/params/param_manager_cleanup.cpp)
target_include_directories(ParamManagerTransferTests PRIVATE mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerTransferTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerTransferTests COMMAND ParamManagerTransferTests)

add_executable(ParamManagerAutomationTests param_manager_automation_tests.cpp
	../../src/deluge/modulation/params/param_manager_automation.cpp
	../../src/deluge/modulation/params/param_manager_type.cpp)
target_include_directories(ParamManagerAutomationTests PRIVATE mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerAutomationTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerAutomationTests COMMAND ParamManagerAutomationTests)

add_executable(ParamManagerUndoTests param_manager_undo_tests.cpp
	../../src/deluge/model/consequence/consequence_param_change.cpp)
target_include_directories(ParamManagerUndoTests PRIVATE undo_mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerUndoTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerUndoTests COMMAND ParamManagerUndoTests)

add_executable(SongBackupTests song_backup_tests.cpp
	../../src/deluge/model/song/song_param_manager_backups.cpp
	../../src/deluge/modulation/params/param_manager_setup.cpp
	../../src/deluge/modulation/params/param_manager_transfer.cpp
	../../src/deluge/modulation/params/param_manager_cleanup.cpp
	../../src/deluge/modulation/params/param_manager_type.cpp)
target_include_directories(SongBackupTests PRIVATE song_mocks mocks ../../src ../../src/deluge)
target_compile_features(SongBackupTests PRIVATE cxx_std_23)
add_test(NAME SongBackupTests COMMAND SongBackupTests)

foreach(test_target IN ITEMS ParamManagerCleanupTests ParamManagerTransferTests
		ParamManagerAutomationTests SongBackupTests)
	get_target_property(test_sources ${test_target} SOURCES)
	get_target_property(test_includes ${test_target} INCLUDE_DIRECTORIES)
	set(disabled_target ${test_target}NoDiagnostics)
	add_executable(${disabled_target} ${test_sources} diagnostics_mode_check.cpp)
	target_include_directories(${disabled_target} PRIVATE ${test_includes})
	target_compile_features(${disabled_target} PRIVATE cxx_std_23)
	target_compile_definitions(${disabled_target} PRIVATE
		ALPHA_OR_BETA_VERSION=0 EXPECTED_PARAM_MANAGER_DIAGNOSTICS=0)
	add_test(NAME ${disabled_target} COMMAND ${disabled_target})

	target_sources(${test_target} PRIVATE diagnostics_mode_check.cpp)
	target_compile_definitions(${test_target} PRIVATE
		ALPHA_OR_BETA_VERSION=1 EXPECTED_PARAM_MANAGER_DIAGNOSTICS=1)
endforeach()
