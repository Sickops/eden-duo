# SPDX-FileCopyrightText: Copyright 2026 igawa6
# SPDX-License-Identifier: GPL-3.0-or-later

# Eden Duo additions to the `yuzu-cmd` (eden-cli) target, included with one line from
# src/yuzu_cmd/CMakeLists.txt. include() keeps the including directory's scope: relative paths
# below resolve against src/yuzu_cmd.

target_sources(yuzu-cmd PRIVATE
    emu_window/emu_window_sdl3_aux.cpp
    emu_window/emu_window_sdl3_aux.h
    dsmod_harness.cpp
    dsmod_harness.h
)

if (YUZU_TESTS)
    # These return before the kernel exists; SDL still invokes the quit callback.
    foreach(option IN ITEMS version help)
        add_test(NAME cli_${option} COMMAND ${CMAKE_COMMAND} -E env
            XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/cli-${option}-profile/data
            XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/cli-${option}-profile/config
            XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/cli-${option}-profile/cache
            $<TARGET_FILE:yuzu-cmd> --${option})
    endforeach()
endif()
