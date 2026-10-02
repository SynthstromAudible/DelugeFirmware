# Compile through ccache when it is installed, for every CMake tree that builds the app: the device build, the host
# trees (sim/: the simulator, build/host-app and its TSan variant, the loadcheck build) and the unit tests. The
# same sources are compiled in each tree, and harness work rebuilds them often -- a neutering and its restore rebuild
# the same objects twice -- so a hit saves the whole compile. Set DELUGE_NO_CCACHE=ON to build without it.
option(DELUGE_NO_CCACHE "Do not compile through ccache even when it is installed" OFF)
if(NOT DELUGE_NO_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
	find_program(DELUGE_CCACHE_PROGRAM ccache)
	if(DELUGE_CCACHE_PROGRAM)
		set(CMAKE_C_COMPILER_LAUNCHER "${DELUGE_CCACHE_PROGRAM}")
		set(CMAKE_CXX_COMPILER_LAUNCHER "${DELUGE_CCACHE_PROGRAM}")
		message(STATUS "Compiling through ccache: ${DELUGE_CCACHE_PROGRAM}")
	endif()
endif()
