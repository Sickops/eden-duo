# SPDX-FileCopyrightText: Copyright 2026 igawa6
# SPDX-License-Identifier: GPL-3.0-or-later

# Eden Duo second-screen compositor shaders, appended to the upstream SHADER_FILES list by one
# include() line in src/video_core/host_shaders/CMakeLists.txt.
list(APPEND SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_dsmod_quad.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_dsmod_map_fade.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_dsmod_quad.vert
)
