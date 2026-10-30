// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Included in the reader's private namespace after Lua and Bounded.
// Target proof: contracts/PRICE-READER.md; no guest calls or guessed prices.
#pragma once
struct CatalogCache {
  farming_simulator::Snapshot data;
  uint64_t table{};
  uint32_t next = 1;
  bool complete{};
};
std::string CatalogLower(std::string text) {
  for (auto &c : text)
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  return text;
}
std::string CatalogNumber(double value, int digits = 0) {
  std::ostringstream s;
  s << std::fixed << std::setprecision(digits) << value;
  return s.str();
}
std::optional<double> CatalogCosts(Lua &l, uint64_t item, uint64_t selected,
                                   std::string_view field) {
  if (!item)
    return 0.;
  auto raw = l.Field(item, field);
  auto base = raw.Number();
  if (!base && raw.Tag() != 0x1ffff)
    throw Invalid{};
  double sum = base.value_or(0.);
  if (!std::isfinite(sum) || sum < 0 || sum > 1e12)
    throw Invalid{};
  auto groups = l.Field(item, "configurations").Table();
  if (groups && selected) {
    auto entries = l.Entries(selected);
    if (entries.size() > 64)
      throw Invalid{};
    for (const auto &[key, value] : entries) {
      auto name = l.Text(key);
      auto index = Bounded(value, 0, 4095);
      if (name.empty() || !index || std::floor(*index) != *index)
        throw Invalid{};
      auto group = l.Field(groups, name).Table();
      if (!group)
        continue;
      auto option = l.Index(group, uint32_t(*index)).Table();
      if (!option)
        continue;
      auto extra = l.Field(option, field);
      auto amount = extra.Number();
      if (!amount && extra.Tag() != 0x1ffff)
        throw Invalid{};
      if (amount) {
        if (*amount < -1e12 || *amount > 1e12)
          throw Invalid{};
        sum += *amount;
      }
    }
  }
  return std::isfinite(sum) && sum >= 0 && sum <= 1e12
             ? std::optional<double>{sum}
             : std::nullopt;
}
std::optional<double> CatalogNumeric(Lua &l, Value value) {
  auto number = value.Number();
  if (!number && value.Tag() == 0x1fffb) {
    auto text = l.Text(value);
    size_t used{};
    try {
      double parsed = std::stod(text, &used);
      if (used == text.size() && std::isfinite(parsed))
        number = parsed;
    } catch (const std::exception &) {
    }
  }
  return number && *number >= 0 && *number <= 1e12 ? number : std::nullopt;
}
void CatalogSpecs(Lua &l, uint64_t env, uint64_t item,
                  std::vector<farming_simulator::ItemSpec> &out,
                  uint64_t vehicle = 0,
                  std::optional<double> actualMaintenance = {}) {
  auto specs = l.Field(item, "specs").Table();
  auto i18n = l.Field(env, "g_i18n").Table();
  auto texts = l.Field(i18n, "texts").Table();
  auto label = [&](std::string_view key, std::string fallback) {
    auto text = l.Text(l.Field(texts, key));
    return text.empty() ? fallback : text;
  };
  auto unit = label("unit_literShort", "l");
  auto motorValue = [&](std::string_view name) {
    Value value;
    if (vehicle) {
      auto chosen =
          l.Field(l.Field(vehicle, "configurations").Table(), "motor");
      auto index = Bounded(chosen, 0, 4095);
      auto motor =
          l.Field(l.Field(item, "configurations").Table(), "motor").Table();
      if (index && motor && std::floor(*index) == *index)
        value = l.Field(l.Index(motor, uint32_t(*index)).Table(), name);
    }
    return value.Tag() == 0x1ffff ? l.Field(specs, name) : value;
  };
  // Match the shipped spec getter operands and units; no printf template is
  // presented as a label. The native Shop does not display internal lifetime.
  if (auto capacity = l.Field(specs, "capacity").Table()) {
    auto value = l.Field(capacity, "value");
    auto number = value.Number();
    auto text = l.Text(value);
    if (text.empty() && number && *number > 0 && *number <= 1e12) {
      std::ostringstream n;
      n << std::setprecision(15) << *number;
      text = n.str();
    }
    if (!text.empty() && !(number && *number == 0)) {
      auto source = l.Text(l.Field(capacity, "unit"));
      auto capacityUnit = unit;
      if (source.rfind("$l10n_", 0) == 0) {
        auto translated = l.Text(l.Field(texts, source.substr(6)));
        if (!translated.empty())
          capacityUnit = translated;
      }
      out.push_back(
          {label("shop_capacity", "Capacity"), text + " " + capacityUnit});
    }
  }
  if (auto fuel = CatalogNumeric(l, motorValue("fuel")))
    out.push_back({"Fuel", CatalogNumber(std::floor(*fuel)) + " " + unit});
  bool miles = l.Field(i18n, "useMiles").True();
  auto speedUnit =
      label(miles ? "unit_mph" : "unit_kmh", miles ? "mph" : "kph");
  if (auto speed = CatalogNumeric(l, motorValue("maxSpeed")))
    out.push_back({"Maximum speed",
                   CatalogNumber(std::floor(*speed * (miles ? .62137 : 1.))) +
                       " " + speedUnit});
  auto hp = Bounded(l.Field(i18n, "powerFactorHP"), .000001, 1e6),
       kw = Bounded(l.Field(i18n, "powerFactorKW"), .000001, 1e6);
  if (hp && kw) {
    auto addPower = [&](std::string name, std::optional<double> power) {
      if (power)
        out.push_back({std::move(name), CatalogNumber(*power * *kw) + " kW / " +
                                            CatalogNumber(*power * *hp) +
                                            " hp"});
    };
    addPower(label("shop_maxPower", "Power"),
             CatalogNumeric(l, motorValue("power")));
    addPower(label("shop_neededPower", "Required power"),
             CatalogNumeric(l, l.Field(specs, "neededPower")));
  }
  auto upkeep = vehicle ? actualMaintenance
                        : Bounded(l.Field(item, "dailyUpkeep"), 0, 1e12);
  if (upkeep && !vehicle) {
    std::string currency;
    auto money = l.Field(i18n, "moneyUnit").Number();
    if (money) {
      if (money == l.Field(env, "GS_MONEY_EURO").Number())
        currency = "€";
      else if (money == l.Field(env, "GS_MONEY_DOLLAR").Number())
        currency = "$";
      else if (money == l.Field(env, "GS_MONEY_POUND").Number())
        currency = "£";
    }
    out.push_back({label("shop_maintenance", "Maintenance"),
                   CatalogNumber(*upkeep, 2) +
                       (currency.empty() ? "" : " " + currency) + " / day"});
  }
  if (auto width = CatalogNumeric(l, l.Field(specs, "workingWidth")))
    out.push_back({label("shop_workingWidth", "Working width"),
                   CatalogNumber(*width, 1) + " m"});
  if (auto limit = CatalogNumeric(l, l.Field(specs, "speedLimit")))
    out.push_back({"Working speed",
                   CatalogNumber(std::floor(*limit * (miles ? .62137 : 1.))) +
                       " " + speedUnit});
}
void CatalogStep(Lua &l, uint64_t env, CatalogCache &cache) {
  using namespace farming_simulator;
  auto util = l.Field(env, "StoreItemsUtil").Table();
  auto table = l.Field(util, "storeItems").Table();
  if (!table) {
    cache = {};
    return;
  }
  CatalogCache candidate = cache;
  if (candidate.table != table) {
    candidate = {};
    candidate.table = table;
    auto categories = l.Field(util, "storeCategories").Table();
    auto entries = l.Entries(categories);
    if (entries.size() > 64)
      throw Invalid{};
    std::map<std::pair<int, std::string>, ShopCategory> ordered;
    for (const auto &[key, value] : entries) {
      auto category = value.Table();
      if (!category)
        continue;
      ShopCategory c;
      c.id = l.Text(l.Field(category, "name"));
      if (c.id.empty())
        c.id = l.Text(key);
      if (c.id == "sales")
        continue; // Sale offers need their own live pricing contract.
      c.name = l.Text(l.Field(category, "title"));
      c.image = l.Text(l.Field(category, "image"));
      auto order = Bounded(l.Field(category, "orderId"), 0, 65535);
      if (c.id.empty() || c.name.empty() || !order)
        throw Invalid{};
      ordered.emplace(std::make_pair(int(*order), c.id), std::move(c));
    }
    for (auto &entry : ordered)
      candidate.data.shop_categories.push_back(std::move(entry.second));
    auto brands =
        l.Field(l.Field(env, "BrandUtil").Table(), "brandIndexToDesc").Table();
    auto brandEntries = l.Entries(brands);
    if (brandEntries.size() > 128)
      throw Invalid{};
    std::map<uint32_t, ShopBrand> indexed;
    for (const auto &[key, value] : brandEntries) {
      auto brand = value.Table();
      if (!brand)
        continue;
      auto index = Bounded(l.Field(brand, "index"), 1, 128);
      if (!index)
        throw Invalid{};
      ShopBrand b;
      b.id = std::to_string(uint32_t(*index));
      b.name = l.Text(l.Field(brand, "nameI18N"));
      b.image = l.Text(l.Field(brand, "image"));
      if (b.name.empty())
        b.name = l.Text(l.Field(brand, "name"));
      if (b.name.empty())
        throw Invalid{};
      indexed.emplace(uint32_t(*index), std::move(b));
    }
    for (auto &entry : indexed)
      candidate.data.shop_brands.push_back(std::move(entry.second));
  }
  if (candidate.complete) {
    cache = std::move(candidate);
    return;
  }
  for (uint32_t count = 0; count < 16; ++count) {
    auto value = l.Index(table, candidate.next);
    if (value.Tag() == 0x1ffff) {
      candidate.complete = true;
      candidate.data.shop_ready = true;
      break;
    }
    if (candidate.next > 1024)
      throw Invalid{};
    auto item = value.Table();
    if (!item)
      throw Invalid{};
    ShopItem row;
    row.id = std::to_string(candidate.next);
    row.name = l.Text(l.Field(item, "name"));
    row.category_id = l.Text(l.Field(item, "category"));
    row.image = l.Text(l.Field(item, "imageActive"));
    auto brand = Bounded(l.Field(item, "brandIndex"), 0, 128);
    if (brand)
      row.brand_id = std::to_string(uint32_t(*brand));
    row.price = Bounded(l.Field(item, "price"), 0, 1e12);
    CatalogSpecs(l, env, item, row.specs);
    if (row.name.empty())
      throw Invalid{};
    candidate.data.shop_items.push_back(std::move(row));
    ++candidate.next;
  }
  // No partial item/chunk commits if a read or budget failure throws above.
  cache = std::move(candidate);
}
void Vehicles(Lua &l, uint64_t env, uint64_t mission,
              farming_simulator::Snapshot &out) {
  using namespace farming_simulator;
  auto util = l.Field(env, "StoreItemsUtil").Table();
  auto byConfig = l.Field(util, "storeItemsByXMLFilename").Table();
  auto vehicles = l.Field(mission, "vehicles").Table();
  if (!vehicles || !byConfig)
    return;
  auto economy = l.Field(env, "EconomyManager").Table();
  auto ratio =
      Bounded(l.Field(economy, "LIFETIME_OPERATINGTIME_RATIO"), .000001, 1e6);
  auto maximum =
      Bounded(l.Field(economy, "MAX_DAILYUPKEEP_MULTIPLIER"), 0, 1e6);
  auto entries = l.Entries(vehicles);
  if (entries.size() > 1024)
    throw Invalid{};
  std::vector<VehicleItem> rows;
  for (const auto &[key, value] : entries) {
    auto vehicle = value.Table();
    if (!vehicle)
      continue;
    auto property = Bounded(l.Field(vehicle, "propertyState"), 0, 2);
    if (!property || (*property != 1 && *property != 2))
      continue;
    if (l.Field(vehicle, "isDeleted").True())
      continue;
    VehicleItem row;
    row.leased = *property == 2;
    // Native Enter/Reset rules (VehicleItem's comment). A field the game leaves nil reads as
    // "absent": enterReferenceNode/exitPoint are node ids on drivables only.
    auto node = [&](const char *name) {
      return Bounded(l.Field(vehicle, name), 1, 4294967295.).has_value();
    };
    row.can_enter = node("enterReferenceNode") && node("exitPoint") &&
                    !l.Field(vehicle, "isBroken").True() &&
                    !l.Field(vehicle, "isControlled").True();
    row.can_reset = l.Field(vehicle, "isVehicleSaved").True();
    if (auto root = Bounded(l.Field(vehicle, "rootNode"), 1, 4294967295.))
      row.root_node = uint64_t(*root);
    else
      row.can_enter = row.can_reset = false;
    row.config = l.Text(l.Field(vehicle, "configFileName"));
    if (row.config.empty())
      throw Invalid{};
    auto item = l.Field(byConfig, CatalogLower(row.config)).Table();
    if (!item)
      throw Invalid{};
    row.name = l.Text(l.Field(item, "name"));
    row.image = l.Text(l.Field(item, "imageActive"));
    auto index = Bounded(key, 0, 65535);
    auto id = Bounded(l.Field(vehicle, "id"), 0, 1e12);
    if (id) {
      row.id = CatalogNumber(*id);
      row.network_id = uint64_t(*id);
    } else if (index) {
      row.id = CatalogNumber(*index);
      row.can_enter = row.can_reset = false; // no identity the game call could re-check
    } else
      throw Invalid{};
    if (row.name.empty())
      throw Invalid{};
    row.age_days = Bounded(l.Field(vehicle, "age"), 0, 1e9);
    auto millis = Bounded(l.Field(vehicle, "operatingTime"), 0, 1e15);
    if (millis) {
      row.operating_hours = *millis / 3600000.;
      double hours = std::floor(*row.operating_hours);
      double tenth = std::floor((*millis / 60000. - hours * 60.) / 6.);
      row.operating_time_text =
          CatalogNumber(hours) + "." + CatalogNumber(tenth) + " h";
    }
    auto lifetime = Bounded(l.Field(item, "lifetime"), 0, 1e9);
    double saleFactor = .75, upkeepMultiplier = 1.;
    bool factorsKnown = true;
    if (lifetime && *lifetime > 0) {
      if (!row.age_days || !row.operating_hours || !ratio || !maximum)
        factorsKnown = false;
      else {
        double age = std::min(*row.age_days / *lifetime, 1.);
        double operating =
            std::min(*row.operating_hours / (*lifetime * *ratio), 1.);
        saleFactor *= std::exp(-3.5 * (.5 * age + .5 * operating));
        upkeepMultiplier = 1. + *maximum * (.3 * age + .7 * operating);
      }
    } else if (!lifetime && l.Field(item, "lifetime").Tag() != 0x1ffff)
      factorsKnown = false;
    if (factorsKnown) {
      auto price = Bounded(l.Field(vehicle, "price"), 0, 1e12);
      if (price && !row.leased)
        row.sale_value = std::floor(*price * std::max(saleFactor, .05));
      auto base = CatalogCosts(
          l, item, l.Field(vehicle, "configurations").Table(), "dailyUpkeep");
      if (base && std::isfinite(*base * upkeepMultiplier))
        row.maintenance_cost = *base * upkeepMultiplier;
    }
    CatalogSpecs(l, env, item, row.specs, vehicle, row.maintenance_cost);
    rows.push_back(std::move(row));
  }
  out.owned_vehicles = std::move(rows);
  out.vehicles_ready = true;
}
