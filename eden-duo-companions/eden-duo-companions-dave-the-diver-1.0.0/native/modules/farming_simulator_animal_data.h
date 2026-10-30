// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace farming_simulator {
struct AnimalNeed {
    std::string label, unit;
    // Native display quantities. Capacity absent means quantity only, no gauge.
    std::optional<double> level, capacity;
    // Native pallet output percentage when its own quantity is shown.
    std::optional<double> percent;
};
struct AnimalGroup {
    std::string id, name, image;
    // Percentages are display values 0..100, timing is verified minutes.
    std::optional<double> count, productivity, cleanliness, next_animal_minutes;
    // Native reproduction frequency in animals per day; not a percentage.
    std::optional<double> reproduction_rate_per_day;
    // Set only after the native display conversion has been verified.
    std::optional<double> reproduction_interval_minutes;
    std::vector<AnimalNeed> outputs;
    std::vector<AnimalNeed> needs;
    // Native animal nutrition text and optional native help illustration.
    std::string help_text, help_image;
};
struct Snapshot;
// AnimalMaxOffset()/View::AnimalsMaxOffset() removed: the needs list (plus
// its trailing help text/illustration) is the runtime's own native
// "animal-needs" ScrollRegion now (RenderAnimalNeeds, content_h_bind) --
// the module no longer tracks or clamps a scroll position for it at all.
} // namespace farming_simulator
