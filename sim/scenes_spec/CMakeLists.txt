# deluge_scenes_spec: the model-backed CppSpec executable for src/scenes (ruling Q5). Physically
# inside scenes_spec/ so create_specs_driver's generated sources (relative to spec_dir) resolve
# against this file's own CMAKE_CURRENT_SOURCE_DIR -- see the comment at the add_subdirectory call
# in ../CMakeLists.txt.
create_specs_driver(deluge_scenes_spec ${CMAKE_CURRENT_LIST_DIR})

# The host BSP objects deluge_loadcheck uses (../CMakeLists.txt), minus host_main.c --
# create_specs_driver's create_test_sourcelist already generated this driver's own main().
target_sources(deluge_scenes_spec PRIVATE
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_link.c
    ${REPO_ROOT}/src/bsp/host/host_ws.c
    ${REPO_ROOT}/src/bsp/host/host_pcm.c
    ${REPO_ROOT}/src/bsp/host/host_midi.c
    ${REPO_ROOT}/src/bsp/host/host_script.c
    ${REPO_ROOT}/src/bsp/host/host_script_tables.c
    ${REPO_ROOT}/src/bsp/host/host_snapshot.c
    ${REPO_ROOT}/src/bsp/host/host_debug.cpp
    ${REPO_ROOT}/src/bsp/host/host_efatfs_passthrough.cpp
)
# host_debug.cpp reaches into deluge_app's reason_check; src/deluge on the path for
# memory/reason_check.h (mirrors deluge_host/deluge_loadcheck above). ${REPO_ROOT}/tests is for
# host_script.c's <harness/ui_script.h>.
target_include_directories(deluge_scenes_spec PRIVATE ${HOST_INCLUDE_DIRS} ${REPO_ROOT}/src/deluge ${REPO_ROOT}/tests)
target_link_libraries(deluge_scenes_spec PRIVATE
    deluge_scenes deluge_app deluge_scheduler deluge_scenes_spec_support ${SIM_ALLOC_LINK})
if(RT_LIB)
    target_link_libraries(deluge_scenes_spec PRIVATE ${RT_LIB})
endif()

# ctest rows: create_specs_driver already registered one row per *_spec.cpp here (named by its bare
# stem, e.g. "l10n_bridge_spec", "profile_spec"), run without the env below -- these task-1 specs
# touch no SD card.
#
# `scenes_spec` is an ADDITIONAL row, not redundant with those: neither auto-registered name
# contains "scenes" (fix round 1 review), so `ctest -R scenes` -- the plan's own acceptance filter
# -- would match nothing without it. It also runs every linked spec in one row, under the same env
# the level-3 app scripts use (../CMakeLists.txt), for parity with a future scenes spec that needs
# it (script_runner.h's expect_script_goldens). Sim rows carry an explicit TIMEOUT (harness T4 fix
# round 2, see the ui_app_scripts_* rows in ../CMakeLists.txt); give the whole set the same one.
#
# Not create_test_sourcelist's own "run everything" mode (`-A`): its generated main() forwards the
# FULL original argv -- "-A" included -- to every linked spec's own main(), and CppSpec::parse()
# (DELUGE_SPEC, tests/spec/support/spec_main.h) rejects "-A" as an unrecognised flag. So this chains
# one exact-name invocation per spec instead (`<spec_id> --verbose`, exactly how create_specs_driver
# invokes each spec's own row above), built from the same glob so a spec added later needs no CMake
# change here either. Needs `sh` on the COMMAND line (unlike the rest of this file, which calls
# `${CMAKE_COMMAND} -E env` directly) -- fine on this project's Linux/macOS-only host-sim today;
# revisit if a Windows host-sim configuration is ever added.
file(GLOB_RECURSE _scenes_spec_sources RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*_spec.cpp)
set(_scenes_spec_chain "")
foreach(_scenes_spec IN LISTS _scenes_spec_sources)
    cmake_path(REMOVE_EXTENSION _scenes_spec OUTPUT_VARIABLE _scenes_spec_id)
    string(APPEND _scenes_spec_chain "$<TARGET_FILE:deluge_scenes_spec> ${_scenes_spec_id} --verbose && ")
endforeach()
string(APPEND _scenes_spec_chain "true")

add_test(
    NAME scenes_spec
    COMMAND ${CMAKE_COMMAND} -E env DELUGE_HOST_DETERMINISTIC=1 DELUGE_SD_ROOT=${REPO_ROOT}/tests/fixtures/card
            sh -c "${_scenes_spec_chain}")
set_tests_properties(scenes_spec PROPERTIES TIMEOUT 300)
foreach(_scenes_spec IN LISTS _scenes_spec_sources)
    cmake_path(GET _scenes_spec STEM LAST_ONLY _scenes_spec_name)
    set_tests_properties(${_scenes_spec_name} PROPERTIES TIMEOUT 300)
endforeach()
