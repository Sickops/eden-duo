// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace farming_simulator {
struct ItemSpec {
    std::string label, value;
};
struct ShopCategory {
    std::string id, name, image;
};
struct ShopBrand {
    std::string id, name, image;
};
struct ShopItem {
    std::string id, name, brand_id, category_id, image;
    std::optional<double> price;
    std::vector<ItemSpec> specs;
};
struct VehicleItem {
    std::string id, name, image, config;
    bool leased{};
    std::optional<double> age_days, operating_hours, maintenance_cost, sale_value;
    std::optional<std::string> operating_time_text;
    std::vector<ItemSpec> specs;
    // Garage "Enter"/"Reset" eligibility, the native rules (InGameMenu.lua, decompiled; see
    // reviews/20261017-game-calls-11/GAME-CALLS.md): Enter needs a seat (enterReferenceNode and
    // exitPoint) and a vehicle that is neither broken nor controlled (onClickEnterVehicle); Reset is
    // offered for every owned vehicle the server will reload (isVehicleSaved, ResetVehicleEvent.run).
    // Play-state rules (paused, menu open, AI worker on the combination) are checked again by the
    // game call itself at the moment it runs.
    // root_node keys g_currentMission.nodeToVehicle (and is the vehicle hotspot's objectId);
    // network_id is the vehicle's own id. The call re-checks both before acting.
    uint64_t root_node{}, network_id{};
    bool can_enter{}, can_reset{};
};
struct ViewState;
const ShopItem *SelectedShop(const ViewState &state);
const VehicleItem *SelectedVehicle(const ViewState &state);
std::vector<size_t> ShopItems(const ViewState &state);
} // namespace farming_simulator
