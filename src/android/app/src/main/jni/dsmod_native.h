// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "core/perf_stats.h"

namespace Common {
class DynamicLibrary;
}

// Eden Duo: the dual-screen (DSMod) side of EmulationSession. native.cpp calls these hooks at
// fixed points of the session lifecycle; the state they manage (the aux surface, the second-screen
// options, the No Companion override) and the Duo JNI entry points live in dsmod_native.cpp.
//
// Lock order: EmulationSession::m_mutex, then the aux mutex inside dsmod_native.cpp.
namespace DSModSession {

/// Hands the Second Screen settings to the renderer and the mod runtime.
void ApplySecondScreenOptions();

/// Developer builds: reads <user dir>/dsmod_env.txt once, after the app directory is set.
void OnAppDirectorySet();

/// Optional periodic performance line for developer builds (the Android FPS overlay's stats).
void OnPerfStats(const Core::PerfStatsResults& stats);

/// System::Load succeeded: the mod runtime (if any) exists.
void OnEmulationLoaded();

/// The game's renderer and GPU exist: attaches a waiting second-screen surface.
void OnRendererReady(std::shared_ptr<Common::DynamicLibrary> vulkan_library);

/// ShutdownEmulation, before a pending program change is applied. Without one the game is gone,
/// and its No Companion value is dropped.
void OnShutdownEmulation(bool program_change_pending);

/// Before ShutdownMainProcess: lets go of the renderer. The aux surface stays cached.
void OnBeforeShutdownMainProcess();

/// The render window is being torn down.
void OnRenderWindowTeardown();

} // namespace DSModSession
