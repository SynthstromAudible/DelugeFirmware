# deluge_notify — the model→UI notification channel (spec C). A leaf: depends on nothing
# but the C++ standard library; [any]-safe raise/push, [task] drain/pop.
add_library(deluge_notify STATIC dirty_set.cpp notice_queue.cpp)
target_include_directories(deluge_notify PUBLIC ${SHARED_INCLUDE})
# See src/gfx/CMakeLists.txt for what DELUGE_COMMON_COMPILE_OPTIONS carries and why it is PRIVATE.
target_compile_options(deluge_notify PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
set_target_properties(deluge_notify PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
