# SPDX-FileCopyrightText: Copyright 2026 Eden Duo Windows fork
# SPDX-License-Identifier: GPL-3.0-or-later

# Eden Duo Windows additions to the Qt frontend, included with one line from
# src/yuzu/CMakeLists.txt so upstream's source list carries no Duo files. include() keeps the
# including directory's scope: relative paths below resolve against src/yuzu.

target_sources(yuzu PRIVATE
    duo/aux_window.cpp
    duo/aux_window.h
    duo/dsmod_installer.cpp
    duo/dsmod_installer.h
)

target_link_libraries(yuzu PRIVATE QuaZip::QuaZip)

# Optional external, boot-free companion preview/verification tool. No game-specific source
# is added to the emulator or Android APK.
set(EDEN_DSMOD_PREVIEW_SOURCE "" CACHE FILEPATH "External DSMod preview tool source")
if(EDEN_DSMOD_PREVIEW_SOURCE)
    add_executable(dsmod_preview EXCLUDE_FROM_ALL "${EDEN_DSMOD_PREVIEW_SOURCE}")
    target_link_libraries(dsmod_preview PRIVATE common core input_common frontend_common video_core
        ${PLATFORM_LIBRARIES} Threads::Threads)
endif()
