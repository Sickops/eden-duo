// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// dsm:u — fork-only service that lets guest code (exlaunch modules, homebrew) talk to the
// emulator's second-screen support. Command ids (dsm.cpp) and payload types
// (video_core/dsmod/aux_routing.h) define the guest-side contract.

#pragma once

namespace Core {
class System;
}

namespace Service::DSM {

void LoopProcess(Core::System& system);

} // namespace Service::DSM
