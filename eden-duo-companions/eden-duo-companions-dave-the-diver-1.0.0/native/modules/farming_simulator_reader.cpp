// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "farming_simulator_reader.h"
#include "farming_simulator_density.h"
#include "farming_simulator_game_calls.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
namespace farming_simulator_reader {
namespace {
constexpr uint64_t PointerMask = (UINT64_C(1) << 47) - 1;
struct Invalid {};
struct Value {
  uint64_t bits = UINT64_MAX;
  uint64_t Tag() const { return bits >> 47; }
  uint64_t Table() const { return Tag() == 0x1fff4 ? bits & PointerMask : 0; }
  bool True() const { return Tag() == 0x1fffd; }
  std::optional<double> Number() const {
    if (Tag() == 0x1fff2)
      return int32_t(bits);
    double n;
    std::memcpy(&n, &bits, 8);
    if (Tag() >= 0x1fff2 || !std::isfinite(n))
      return {};
    return n;
  }
};
struct Lookup {
  uint64_t nodes{}, key{}, address{};
  uint32_t mask{};
};
class Lua {
public:
  const EdenDsmodHostApi *host{};
  std::unordered_map<uint64_t, std::array<uint8_t, 4096>> pages;
  std::map<std::pair<uint64_t, std::string>, Lookup> lookups;
  size_t byte_limit{4194304};
  size_t lookup_limit{2048};
  size_t bytes{}, calls{};
  void Begin(const EdenDsmodHostApi &h) {
    host = &h;
    pages.clear();
    bytes = calls = 0;
  }
  void Read(uint64_t address, void *output, size_t size) {
    if (!address || address > PointerMask || size > 65536 ||
        size > PointerMask - address)
      throw Invalid{};
    auto *out = static_cast<uint8_t *>(output);
    while (size) {
      uint64_t page = address & ~UINT64_C(4095);
      auto it = pages.find(page);
      if (it == pages.end()) {
        if (bytes >= byte_limit || !host->is_mapped || !host->read_memory ||
            !host->is_mapped(host->userdata, page, 4096))
          throw Invalid{};
        std::array<uint8_t, 4096> data{};
        ++calls;
        bytes += 4096;
        if (!host->read_memory(host->userdata, page, data.data(), 4096))
          throw Invalid{};
        it = pages.emplace(page, std::move(data)).first;
      }
      size_t offset = address - page, take = std::min(size, 4096 - offset);
      std::memcpy(out, it->second.data() + offset, take);
      out += take;
      address += take;
      size -= take;
    }
  }
  template <class T> T Get(uint64_t address) {
    T value{};
    Read(address, &value, sizeof(value));
    return value;
  }
  std::string String(uint64_t pointer, size_t limit = 512) {
    auto h = Get<std::array<uint8_t, 24>>(pointer);
    if (h[9] != 4)
      throw Invalid{};
    uint32_t length{};
    std::memcpy(&length, h.data() + 16, 4);
    if (length > limit)
      throw Invalid{};
    std::string result(length, '\0');
    Read(pointer + 24, result.data(), length);
    return result;
  }
  std::string Text(Value value) {
    return value.Tag() == 0x1fffb ? String(value.bits & PointerMask)
                                  : std::string{};
  }
  uint64_t Keys(uint64_t nodes, uint32_t mask) {
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i <= mask; ++i) {
      auto address = nodes + i * 24;
      // LuaJIT retains dead keys in nil-valued nodes; never dereference those
      // keys.
      hash ^= Value{Get<uint64_t>(address)}.Tag() == 0x1ffff
                  ? 0
                  : Get<uint64_t>(address + 8);
      hash *= 1099511628211ULL;
    }
    return hash;
  }
  Value Field(uint64_t table, std::string_view name) {
    if (!table)
      return {};
    auto h = Get<std::array<uint8_t, 64>>(table);
    if (h[9] != 11)
      throw Invalid{};
    uint64_t nodes{};
    uint32_t mask{};
    std::memcpy(&nodes, h.data() + 40, 8);
    std::memcpy(&mask, h.data() + 52, 4);
    if (mask > 16383 || (mask & (mask + 1)))
      throw Invalid{};
    auto key = std::make_pair(table, std::string{name});
    auto found = lookups.find(key);
    if (found != lookups.end()) {
      auto &hit = found->second;
      if (hit.nodes == nodes && hit.mask == mask) {
        if (!hit.address && hit.key == Keys(nodes, mask))
          return {};
        if (hit.address) {
          auto value = Value{Get<uint64_t>(hit.address)};
          if (value.Tag() == 0x1ffff)
            return {};
          if (Get<uint64_t>(hit.address + 8) == hit.key &&
              String(hit.key & PointerMask) == name)
            return value;
        }
      }
      lookups.erase(found);
    }
    for (uint32_t i = 0; i <= mask; ++i) {
      uint64_t address = nodes + i * 24;
      if (Value{Get<uint64_t>(address)}.Tag() == 0x1ffff)
        continue;
      uint64_t bits = Get<uint64_t>(address + 8);
      if ((bits >> 47) != 0x1fffb)
        continue;
      uint64_t str = bits & PointerMask;
      if (Get<uint8_t>(str + 9) != 4)
        throw Invalid{};
      if (Get<uint32_t>(str + 16) != name.size())
        continue;
      if (String(str) == name) {
        if (lookups.size() >= lookup_limit)
          lookups.clear();
        lookups.emplace(key, Lookup{nodes, bits, address, mask});
        return {Get<uint64_t>(address)};
      }
    }
    if (lookups.size() >= lookup_limit)
      lookups.clear();
    lookups.emplace(std::move(key), Lookup{nodes, Keys(nodes, mask), 0, mask});
    return {};
  }
  Value Index(uint64_t table, uint32_t index) {
    if (!table)
      return {};
    if (Get<uint8_t>(table + 9) != 11)
      throw Invalid{};
    auto count = Get<uint32_t>(table + 48);
    if (count > 65536)
      throw Invalid{};
    if (index < count)
      return {Get<uint64_t>(Get<uint64_t>(table + 16) + index * 8)};
    auto mask = Get<uint32_t>(table + 52);
    if (mask > 16383 || (mask & (mask + 1)))
      throw Invalid{};
    auto nodes = Get<uint64_t>(table + 40);
    for (uint32_t i = 0; i <= mask; ++i) {
      auto address = nodes + i * 24;
      Value value{Get<uint64_t>(address)};
      if (value.Tag() == 0x1ffff)
        continue;
      auto key = Value{Get<uint64_t>(address + 8)}.Number();
      if (key && *key == index)
        return value;
    }
    return {};
  }
  std::vector<std::pair<Value, Value>> Entries(uint64_t table) {
    std::vector<std::pair<Value, Value>> out;
    if (!table)
      return out;
    if (Get<uint8_t>(table + 9) != 11)
      throw Invalid{};
    auto count = Get<uint32_t>(table + 48), mask = Get<uint32_t>(table + 52);
    if (count > 4096 || mask > 4095 || (mask & (mask + 1)))
      throw Invalid{};
    auto array = Get<uint64_t>(table + 16), nodes = Get<uint64_t>(table + 40);
    for (uint32_t i = 0; i < count; ++i) {
      Value v{Get<uint64_t>(array + i * 8)};
      if (v.Tag() != 0x1ffff)
        out.push_back({Value{(UINT64_C(0x1fff2) << 47) | i}, v});
    }
    for (uint32_t i = 0; i <= mask; ++i) {
      auto address = nodes + i * 24;
      Value v{Get<uint64_t>(address)};
      if (v.Tag() != 0x1ffff)
        out.push_back({Value{Get<uint64_t>(address + 8)}, v});
    }
    return out;
  }
};
std::optional<double> Bounded(Value v, double low, double high) {
  auto n = v.Number();
  return n && *n >= low && *n <= high ? n : std::nullopt;
}
// ScriptSystem/global root: shared by Reader::Sample() and TimeControl's own
// Context(), which each resolve the exact same proven storage (never a heap
// address) before doing their own, separate work with it.
struct ScriptRoot {
  uint64_t object{}, state{}, env{};
};
ScriptRoot ResolveScriptRoot(Lua &l, const EdenDsmodHostApi &h) {
  if (h.main_size <= 0x46cb48)
    throw Invalid{};
  ScriptRoot out;
  out.object = l.Get<uint64_t>(h.main_base + 0x46cb40);
  out.state = l.Get<uint64_t>(out.object);
  if (l.Get<uint8_t>(out.state + 9) != 6)
    throw Invalid{};
  out.env = l.Get<uint64_t>(out.state + 72);
  return out;
}
void CheckGlobalTable(Lua &l, uint64_t env) {
  if (l.Field(env, "_G").Table() != env)
    throw Invalid{};
}
#include "farming_simulator_time_control.inc"
#include "farming_simulator_map_reader.inc"
#include "farming_simulator_catalog_reader.inc"
std::vector<uint64_t> FarmSources(Lua &l, uint64_t mission) {
  auto trigger = l.Field(mission, "farmSiloTrigger").Table();
  auto registry = l.Field(trigger, "siloSources").Table();
  std::vector<uint64_t> out;
  if (!registry)
    return out;
  for (const auto &[key, value] : l.Entries(registry)) {
    if (auto source = key.Table())
      out.push_back(source);
  }
  if (out.size() > 64)
    throw Invalid{};
  return out;
}
void Prices(Lua &l, uint64_t env, uint64_t mission,
            farming_simulator::Snapshot &out) {
  using namespace farming_simulator;
  auto util = l.Field(env, "FillUtil").Table(),
       descs = l.Field(util, "fillTypeIndexToDesc").Table();
  auto count = Bounded(l.Field(util, "NUM_FILLTYPES"), 1, 128);
  if (!descs || !count)
    return;
  std::vector<uint32_t> ids;
  for (uint32_t i = 1; i <= uint32_t(*count); ++i) {
    auto desc = l.Index(descs, i).Table();
    if (!desc || !l.Field(desc, "showOnPriceTable").True())
      continue;
    PriceCrop crop;
    crop.id = std::to_string(i);
    crop.name = l.Text(l.Field(desc, "nameI18N"));
    crop.icon = l.Text(l.Field(desc, "hudOverlayFilename"));
    if (crop.name.empty())
      throw Invalid{};
    ids.push_back(i);
    out.price_crops.push_back(std::move(crop));
  }
  if (ids.empty() || ids.size() > 64)
    return;
  std::vector<uint64_t> sources;
  auto silo_registry = l.Field(mission, "siloTriggers").Table();
  if (silo_registry)
    for (const auto &[key, value] : l.Entries(silo_registry)) {
      auto trigger = value.Table() ? value.Table() : key.Table();
      if (!trigger || !l.Field(trigger, "isFarmSilo").True())
        continue;
      for (const auto &[source_key, source_value] :
           l.Entries(l.Field(trigger, "siloSources").Table()))
        if (auto source = source_key.Table())
          sources.push_back(source);
    }
  if (sources.size() > 256)
    throw Invalid{};
  for (size_t column = 0; column < ids.size(); ++column) {
    double sum = 0;
    bool complete = silo_registry != 0;
    for (auto source : sources) {
      auto levels = l.Field(source, "fillLevels").Table();
      auto level = Bounded(l.Index(levels, ids[column]), 0, 1e12);
      // Native silo storage explicitly initializes supported fill IDs,
      // including zero.
      if (level)
        sum += *level;
      else if (!levels || l.Index(levels, ids[column]).Tag() != 0x1ffff)
        complete = false;
    }
    if (complete)
      out.price_crops[column].silo_l = sum;
  }
  auto registry = l.Field(mission, "tipTriggers").Table();
  if (!registry)
    return;
  std::map<std::string, PriceStation> stations;
  auto entries = l.Entries(registry);
  if (entries.size() > 256)
    throw Invalid{};
  for (const auto &[key, value] : entries) {
    auto trigger = value.Table() ? value.Table() : key.Table();
    if (!trigger)
      continue;
    if (!l.Field(trigger, "isSellingPoint").True() ||
        !l.Field(trigger, "appearsOnStats").True())
      continue;
    auto name = l.Text(l.Field(trigger, "stationName"));
    if (name.empty())
      continue;
    auto &station = stations[name];
    station.id = name;
    station.name = name;
    if (station.quotes.empty())
      station.quotes.resize(ids.size());
    auto acceptance = l.Field(trigger, "acceptedFillTypes").Table();
    if (!acceptance)
      throw Invalid{};
    auto bases = l.Field(trigger, "fillTypePrices").Table(),
         deltas = l.Field(trigger, "fillTypePriceRandomDelta").Table(),
         multipliers = l.Field(trigger, "priceMultipliers").Table(),
         infos = l.Field(trigger, "fillTypePriceInfo").Table();
    bool server = l.Field(trigger, "isServer").True();
    for (size_t column = 0; column < ids.size(); ++column) {
      auto id = ids[column];
      if (!l.Index(acceptance, id).True())
        continue;
      PriceQuote quote;
      quote.accepted = true;
      auto base = Bounded(l.Index(bases, id), 0, 1e6);
      if (base) {
        double per_l = *base;
        bool valid = true;
        if (server) {
          auto delta = Bounded(l.Index(deltas, id), -1e6, 1e6),
               multiplier = Bounded(l.Index(multipliers, id), 0, 1e6);
          if (delta && multiplier)
            per_l = (*base + *delta) * *multiplier;
          else
            valid = false;
        }
        if (valid && per_l >= 0 && per_l < 1e9)
          quote.price_per1000 = std::floor(per_l * 1000);
      }
      auto info = Bounded(l.Index(infos, id), 0, 63);
      if (info) {
        int bits = int(*info);
        quote.trend = bits & 4   ? PriceTrend::Rising
                      : bits & 2 ? PriceTrend::Falling
                                 : PriceTrend::Stable;
        quote.highlight = bits & 16  ? PriceHighlight::High
                          : bits & 8 ? PriceHighlight::Low
                                     : PriceHighlight::Neutral;
        quote.great_demand = bits & 32;
      }
      // A single native station may have disjoint grain and woodchip trigger
      // objects.
      if (station.quotes[column].accepted &&
          station.quotes[column].price_per1000 != quote.price_per1000)
        throw Invalid{};
      station.quotes[column] = std::move(quote);
    }
  }
  auto texts = l.Field(l.Field(env, "g_i18n").Table(), "texts").Table();
  for (auto &[key, station] : stations) {
    auto localized = l.Text(l.Field(texts, station.name));
    if (!localized.empty())
      station.name = localized;
    out.price_stations.push_back(std::move(station));
  }
  std::sort(out.price_stations.begin(), out.price_stations.end(),
            [](const auto &a, const auto &b) { return a.name < b.name; });
  out.prices_ready = !out.price_stations.empty();
}
std::optional<double> AnimalFill(Lua &l, uint64_t animal, uint32_t id) {
  auto all = l.Field(animal, "tipTriggersFillLevels").Table();
  auto records = l.Index(all, id).Table();
  if (!all)
    return {};
  if (!records)
    return l.Index(all, id).Tag() == 0x1ffff ? std::optional<double>{0}
                                             : std::nullopt;
  double sum = 0;
  for (const auto &[key, value] : l.Entries(records)) {
    auto n = Bounded(l.Field(value.Table(), "fillLevel"), 0, 1e12);
    if (!n)
      return {};
    sum += *n;
  }
  return sum;
}
void Animals(Lua &l, uint64_t env, uint64_t mission,
             farming_simulator::Snapshot &out) {
  using namespace farming_simulator;
  auto registry = l.Field(mission, "husbandries").Table();
  if (!registry)
    return;
  auto util = l.Field(env, "FillUtil").Table(),
       descs = l.Field(util, "fillTypeIndexToDesc").Table(),
       groups = l.Field(util, "foodGroups").Table();
  auto i18n = l.Field(env, "g_i18n").Table();
  auto texts = l.Field(i18n, "texts").Table();
  auto factor = Bounded(l.Field(i18n, "fluidFactor"), .0001, 10000);
  auto fluid = [&](std::optional<double> n) -> std::optional<double> {
    if (!n || !factor)
      return {};
    return std::floor(*n * *factor);
  };
  for (auto id : {"sheep", "pig", "cow"}) {
    auto animal = l.Field(registry, id).Table();
    if (!animal || !l.Field(animal, "hasStatistics").True())
      continue;
    AnimalGroup group;
    group.id = id;
    auto desc = l.Field(animal, "animalDesc").Table();
    group.name = l.Text(l.Field(desc, "title"));
    group.image = l.Text(l.Field(desc, "imageFilename"));
    group.count = Bounded(l.Field(animal, "totalNumAnimals"), 0, 1e7);
    auto productivity = Bounded(l.Field(animal, "productivity"), 0, 1);
    if (productivity)
      group.productivity = std::floor(*productivity * 100);
    auto clean = Bounded(l.Field(animal, "cleanlinessFactor"), 0, 1);
    if (group.count && *group.count > 0 && clean)
      group.cleanliness = std::floor(*clean * 100);
    group.reproduction_rate_per_day =
        Bounded(l.Field(animal, "reproductionRatePerDay"), 0, 1e7);
    if (group.reproduction_rate_per_day &&
        *group.reproduction_rate_per_day > 0) {
      group.reproduction_interval_minutes =
          1440 / *group.reproduction_rate_per_day;
      auto progress = Bounded(l.Field(animal, "newAnimalPercentage"), 0, 1e7);
      if (progress)
        group.next_animal_minutes =
            std::max(0., 1 - *progress) * *group.reproduction_interval_minutes;
    }
    auto add_output = [&](std::string label, std::optional<double> amount) {
      AnimalNeed row;
      row.label = std::move(label);
      row.unit = "unit_literShort";
      row.level = fluid(amount);
      group.outputs.push_back(std::move(row));
    };
    if (auto slurry = l.Field(animal, "liquidManureTrigger").Table())
      add_output("statistic_liquidManureStorage",
                 Bounded(l.Field(slurry, "fillLevel"), 0, 1e12));
    auto manure = Bounded(l.Field(animal, "manureFillLevel"), 0, 1e12);
    if (manure)
      add_output("statistic_manureStorage", manure);
    auto milk = Bounded(l.Field(desc, "milkPerDay"), 0, 1e9);
    if (milk && *milk > 0) {
      auto level = l.Field(animal, "fillLevelMilk");
      add_output("statistic_milkStorage", level.Tag() == 0x1ffff
                                              ? std::optional<double>{0}
                                              : Bounded(level, 0, 1e12));
    }
    if (l.Field(animal, "palletSpawnerId").Number()) {
      AnimalNeed row;
      auto fill = Bounded(l.Field(animal, "palletFillType"), 1, 128);
      if (fill)
        row.label = l.Text(
            l.Field(l.Index(descs, uint32_t(*fill)).Table(), "nameI18N"));
      row.unit = "unit_literShort";
      if (l.Field(animal, "isServer").True()) {
        auto pallet = l.Field(animal, "currentPallet").Table();
        double amount = 0, capacity = 0;
        bool complete = true;
        if (pallet)
          for (const auto &[k, v] :
               l.Entries(l.Field(pallet, "fillUnits").Table())) {
            auto a = Bounded(l.Field(v.Table(), "fillLevel"), 0, 1e12),
                 c = Bounded(l.Field(v.Table(), "capacity"), 0, 1e12);
            if (a && c) {
              amount += *a;
              capacity += *c;
            } else
              complete = false;
          }
        if (complete) {
          row.level = fluid(amount);
          if (!pallet)
            row.percent = 0;
          else if (capacity > 0)
            row.percent = std::floor(100 * amount / capacity);
        }
      }
      group.outputs.push_back(std::move(row));
    }
    double multiplier = group.count ? 6 * std::max(*group.count, 15.) : 0;
    for (auto water : {true, false}) {
      auto daily =
          Bounded(l.Field(desc, water ? "waterPerDay" : "strawPerDay"), 0, 1e9);
      if (!daily || *daily <= 0)
        continue;
      auto fill = Bounded(
          l.Field(util, water ? "FILLTYPE_WATER" : "FILLTYPE_STRAW"), 1, 128);
      AnimalNeed row;
      row.label = water ? "statistic_water" : "statistic_strawStorage";
      row.unit = "unit_literShort";
      if (fill && !l.Index(l.Field(animal, "tipTriggersFillLevels").Table(),
                           uint32_t(*fill))
                       .Table())
        continue;
      if (fill)
        row.level = fluid(AnimalFill(l, animal, uint32_t(*fill)));
      if (group.count)
        row.capacity = fluid(multiplier * *daily);
      group.needs.push_back(std::move(row));
    }
    auto index = Bounded(l.Field(desc, "index"), 1, 128);
    auto food = index ? l.Index(groups, uint32_t(*index)).Table() : 0;
    group.help_text =
        l.Text(l.Field(texts, "statistic_animalRequirementsMain"));
    for (const auto &[food_key, food_value] : l.Entries(food)) {
      auto entry = food_value.Table();
      if (!entry)
        continue;
      auto types = l.Field(entry, "fillTypes").Table();
      AnimalNeed row;
      row.unit = "unit_literShort";
      double amount = 0;
      bool complete = true;
      for (const auto &[key, value] : l.Entries(types)) {
        auto fill = Bounded(key, 1, 128);
        if (!fill) {
          complete = false;
          continue;
        }
        auto name = l.Text(
            l.Field(l.Index(descs, uint32_t(*fill)).Table(), "nameI18N"));
        if (!row.label.empty())
          row.label += ", ";
        row.label += name;
        auto quantity = AnimalFill(l, animal, uint32_t(*fill));
        if (quantity)
          amount += *quantity;
        else
          complete = false;
      }
      if (complete)
        row.level = std::floor(amount);
      auto weight = Bounded(l.Field(entry, "weight"), 0, 1),
           daily = Bounded(l.Field(desc, "foodPerDay"), 0, 1e9);
      if (group.count && weight && daily)
        row.capacity = multiplier * *daily * *weight;
      auto name = l.Text(l.Field(entry, "nameI18N"));
      if (weight) {
        group.help_text += "\n" +
                           std::to_string(int(std::floor(*weight * 100 + .5))) +
                           "% " + name + " (" + row.label + ")";
      }
      group.needs.push_back(std::move(row));
    }
    out.animals.push_back(std::move(group));
  }
  out.animals_ready = out.animals.size() == 3;
}
void Management(Lua &l, uint64_t env, uint64_t mission,
                farming_simulator::Snapshot &out) {
  using namespace farming_simulator;
  auto stats = l.Field(mission, "missionStats").Table();
  out.loan = Bounded(l.Field(stats, "loan"), 0, 1e12);
  out.loan_max = Bounded(l.Field(stats, "loanMax"), 0, 1e12);
  auto cash = Bounded(l.Field(stats, "money"), -1e12, 1e12);
  if (cash && out.loan)
    out.finance_saldo = *cash - *out.loan;
  auto cls = l.Field(env, "FinanceStats").Table();
  auto names = l.Field(cls, "statNames").Table();
  auto labels = l.Field(cls, "statNamesI18n").Table();
  auto today = l.Field(stats, "financeStats").Table();
  if (names && labels && today) {
    bool complete = true;
    std::map<uint32_t, uint64_t> days;
    for (const auto &[key, value] :
         l.Entries(l.Field(stats, "financeStatsHistory").Table())) {
      auto index = Bounded(key, 1, 4096);
      if (index && value.Table())
        days[uint32_t(*index)] = value.Table();
    }
    uint32_t history_count = days.empty() ? 0 : days.rbegin()->first;
    if (history_count != days.size())
      days.clear();
    std::array<uint64_t, 5> columns{};
    columns[4] = today;
    for (int c = 0; c < 4; ++c) {
      int index = int(history_count) - (3 - c);
      auto found = days.find(uint32_t(index));
      if (index > 0 && found != days.end())
        columns[c] = found->second;
    }
    std::array<double, 5> totals{};
    std::array<bool, 5> valid{};
    for (size_t c = 0; c < 5; ++c)
      valid[c] = columns[c] != 0;
    for (uint32_t i = 1; i <= 27; ++i) {
      auto key = l.Text(l.Index(names, i));
      if (key.empty()) {
        complete = false;
        break;
      }
      FinanceRow row;
      row.label = l.Text(l.Field(labels, key));
      if (row.label.empty()) {
        complete = false;
        break;
      }
      for (size_t c = 0; c < 5; ++c) {
        row.amounts[c] = Bounded(l.Field(columns[c], key), -1e12, 1e12);
        if (row.amounts[c])
          totals[c] += *row.amounts[c];
        else
          valid[c] = false;
      }
      out.finance_rows.push_back(std::move(row));
    }
    out.finances_ready = complete && out.finance_rows.size() == 27;
    if (out.finances_ready)
      for (size_t c = 0; c < 5; ++c)
        if (valid[c])
          out.finance_totals[c] = totals[c];
    auto environment = l.Field(mission, "environment").Table();
    auto day = Bounded(l.Field(environment, "currentDay"), 1, 1e8);
    if (day)
      for (int column = 0; column < 4; ++column) {
        int weekday = (int(*day) - (4 - column) - 1) % 7;
        if (weekday < 0)
          weekday += 7;
        out.finance_day_labels[column] =
            "ui_financesDay" + std::to_string(weekday + 1);
      }
    out.finance_day_labels[4] = "ui_today";
  }
  constexpr std::array<const char *, 19> order = {
      "workedHectares",   "cultivatedHectares",
      "sownHectares",     "fertilizedHectares",
      "threshedHectares", "workedTime",
      "cultivatedTime",   "sownTime",
      "fertilizedTime",   "threshedTime",
      "traveledDistance", "fuelUsage",
      "seedUsage",        "sprayUsage",
      "baleCount",        "plantedTreeCount",
      "cutTreeCount",     "fieldJobMissionCount",
      "playTime"};
  auto values = l.Field(stats, "statistics").Table();
  auto i18n = l.Field(env, "g_i18n").Table();
  auto fluid_factor = Bounded(l.Field(i18n, "fluidFactor"), 0.0001, 10000);
  bool miles = l.Field(i18n, "useMiles").True();
  if (values) {
    for (size_t i = 0; i < order.size(); ++i) {
      StatisticRow row;
      row.label = "statistic_" + std::string{order[i]};
      row.digits = (i < 5 || (i >= 10 && i <= 13)) ? 2 : 0;
      if (i < 5)
        row.unit = "unit_areaShort";
      else if (i >= 11 && i <= 13)
        row.unit = "unit_liter";
      auto metric = l.Field(values, order[i]).Table();
      row.session = Bounded(l.Field(metric, "session"), 0, 1e12);
      row.total = Bounded(l.Field(metric, "total"), 0, 1e12);
      if (i == 10 && miles) {
        if (row.session)
          *row.session *= 0.62137;
        if (row.total)
          *row.total *= 0.62137;
      }
      if (i >= 11 && i <= 13) {
        if (fluid_factor) {
          if (row.session)
            *row.session *= *fluid_factor;
          if (row.total)
            *row.total *= *fluid_factor;
        } else {
          row.session.reset();
          row.total.reset();
        }
      }
      if ((i >= 5 && i <= 9) || i == 18) {
        auto duration = [](double minutes) {
          auto total = int64_t(std::floor(minutes + 0.006));
          std::ostringstream text;
          text << std::setfill('0') << std::setw(2) << total / 60 << ":"
               << std::setw(2) << total % 60;
          return text.str();
        };
        // Target Utils.formatTime receives minutes and formats
        // floor(hours):floor(minutes).
        if (row.session)
          row.session_text = duration(*row.session);
        if (row.total)
          row.total_text = duration(*row.total);
        row.session.reset();
        row.total.reset();
      }
      out.statistic_rows.push_back(std::move(row));
    }
    for (auto key :
         {"workersHired", "eggsCarrying", "eggsAvailable", "storageCapacity"}) {
      StatisticRow row;
      row.label = "statistic_" + std::string{key};
      row.digits = 0;
      row.total_text = std::string{};
      if (std::string_view{key} == "workersHired")
        row.session =
            Bounded(l.Field(l.Field(values, key).Table(), "session"), 0, 1e6);
      if (std::string_view{key} == "eggsCarrying") {
        auto egg = Bounded(
            l.Field(l.Field(env, "FillUtil").Table(), "FILLTYPE_EGG"), 1, 128);
        if (egg) {
          auto pickups =
              l.Field(l.Field(mission, "missionInfo").Table(), "pickupObjects")
                  .Table();
          auto value = l.Index(pickups, uint32_t(*egg));
          if (value.Tag() == 0x1ffff)
            row.session = 0;
          else
            row.session = Bounded(value, 0, 1e9);
        }
      }
      if (std::string_view{key} == "storageCapacity") {
        auto sources = FarmSources(l, mission);
        double capacity = 0;
        bool complete = !sources.empty();
        for (auto source : sources) {
          auto n = Bounded(l.Field(source, "capacity"), 0, 1e12);
          if (n)
            capacity += *n;
          else
            complete = false;
        }
        if (complete)
          row.session = capacity;
      }
      if (std::string_view{key} == "eggsAvailable") {
        auto chicken =
            l.Field(l.Field(mission, "husbandries").Table(), "chicken").Table();
        row.session =
            Bounded(l.Field(chicken, "numActivePickupObjects"), 0, 1e9);
      }
      out.statistic_rows.push_back(std::move(row));
    }
    out.statistics_ready = true;
  }
  try {
    Vehicles(l, env, mission, out);
  } catch (...) {
    out.owned_vehicles.clear();
    out.vehicles_ready = false;
  }
  try {
    Animals(l, env, mission, out);
  } catch (...) {
    out.animals.clear();
    out.animals_ready = false;
  }
  try {
    Prices(l, env, mission, out);
  } catch (...) {
    out.price_crops.clear();
    out.price_stations.clear();
    out.prices_ready = false;
  }
}
} // namespace
struct Reader::Impl {
  Lua lua;
  farming_simulator::game_calls::CallMailbox calls;
  farming_simulator::GameCallState call_state;
  // Hotspot arrays can contain hundreds of native objects and their overlay
  // tables. Keep that optional walk inside its own bounded page cache so it
  // cannot spend the primary HUD reader's 4 MiB validation budget.
  Lua marker_lua;
  // Density metadata, fruit definitions and color maps are another independent
  // read workload. Keep failed/large map sampling from invalidating the HUD.
  Lua layers_lua;
  // Input eligibility has its own bounded page cache. Catalog work cannot
  // exhaust its validation budget or evict its persistent key lookups.
  Lua time_lua;
  TimeControl time;
  farming_simulator::Snapshot snapshot, management;
  CatalogCache catalog;
  uint64_t state{};
  // Bumped every time `management` is actually refreshed below (Snapshot's
  // own management_revision comment has the full reasoning) -- starts at 1
  // so a freshly-constructed Impl (management_revision 0) never matches a
  // reader that has refreshed at least once.
  uint64_t management_revision{};
  int64_t peak_us{}, management_us{};
  size_t peak_bytes{};
  std::chrono::steady_clock::time_point next_management{};
  std::shared_ptr<const farming_simulator::MapLayers> layers;
  std::string layers_error;
  std::string layers_error_key_logged;
  std::string primary_error_key;
  uint64_t layers_world{};
  int64_t layers_us{};
  std::chrono::steady_clock::time_point next_layers{};
  std::chrono::steady_clock::time_point next_layers_log{};
  std::chrono::steady_clock::time_point next_primary_log{};
  std::vector<farming_simulator::MapMarker> markers;
  std::string markers_error;
  std::string markers_error_logged;
  uint64_t markers_world{};
  std::chrono::steady_clock::time_point next_markers{};
  std::chrono::steady_clock::time_point next_markers_log{};
  size_t markers_bytes{},markers_calls{};
  bool diagnostics_enabled{};
  // Loading/unavailable gating (see farming_simulator_view.h's *_loading
  // fields): world_ready_since marks when the current world_identity first
  // became ready, so a category that has never produced data yet can show
  // "Loading" for a bounded warm-up window instead of immediately claiming
  // unavailable. The *_fail_since/*_ever_ready pairs let a category that was
  // already showing good data survive a short run of read misses (bounded
  // by kStaleAfterGoodTimeout) without blanking the page; past that window
  // (or before the category has ever succeeded once this world) it reports
  // unavailable instead of loading forever.
  std::chrono::steady_clock::time_point world_ready_since{};
  uint64_t world_ready_since_identity{};
  std::optional<std::chrono::steady_clock::time_point> world_fail_since;
  std::optional<std::chrono::steady_clock::time_point> finances_fail_since,
      statistics_fail_since, vehicles_fail_since, animals_fail_since,
      prices_fail_since;
  bool finances_ever_ready{}, statistics_ever_ready{}, vehicles_ever_ready{},
      animals_ever_ready{}, prices_ever_ready{};
  // Test-only clock override (see Reader::SetClockForTesting); production
  // Sample() calls never set this, so Now() is plain steady_clock::now().
  std::optional<std::chrono::steady_clock::time_point> fake_now;
  std::chrono::steady_clock::time_point Now() const {
    return fake_now ? *fake_now : std::chrono::steady_clock::now();
  }
};
// How long a category keeps showing its last good data after reads for it
// start failing, before giving up and reporting unavailable (a single missed
// sample, or a short run of them, must not blank a page that was already
// showing good data).
constexpr std::chrono::seconds kStaleAfterGoodTimeout{5};
// How long a category that has never yet produced data this world is shown
// as "Loading" before giving up and reporting unavailable (bounds the
// catalog's bounded 16-item chunks and the 1Hz management snapshot's first
// few refreshes without leaving a "Loading" page stuck forever).
constexpr std::chrono::seconds kWarmupTimeout{20};
Reader::Reader(const EdenDsmodHostApi &, const char *)
    : impl_{std::make_unique<Impl>()} {}
void Reader::SetDiagnostics(bool enabled) {
  impl_->diagnostics_enabled = enabled;
  impl_->time.diagnostics_enabled = enabled;
}
void Reader::SetClockForTesting(std::chrono::steady_clock::time_point now) {
  impl_->fake_now = now;
}
Reader::~Reader() {
  if (!CancelPending() && impl_->time.host.log)
    impl_->time.host.log(impl_->time.host.userdata, 3,
                        "Native time key cleanup was refused; retry before live unload");
}
void Reader::ConfigureWrites(const EdenDsmodHostWriteApi *api) {
  impl_->time.Configure(api);
}
void Reader::ConfigureCalls(const EdenDsmodHostExtensions *ext) {
  namespace gc = farming_simulator::game_calls;
  if (!ext || ext->struct_size < sizeof(EdenDsmodHostExtensions) || !ext->mailbox_address ||
      ext->mailbox_size < gc::mb::Size || !ext->load_u32 || !ext->store_u32 || !ext->load_u64 ||
      !ext->store_u64)
    return;
  void *const user = ext->userdata;
  const auto load32 = ext->load_u32;
  const auto store32 = ext->store_u32;
  const auto load64 = ext->load_u64;
  const auto store64 = ext->store_u64;
  impl_->calls.Configure(
      {[=](uint32_t o, uint32_t *v) { return load32(user, o, v) == EDEN_DSMOD_TRUE; },
       [=](uint32_t o, uint32_t v) { return store32(user, o, v) == EDEN_DSMOD_TRUE; },
       [=](uint32_t o, uint64_t *v) { return load64(user, o, v) == EDEN_DSMOD_TRUE; },
       [=](uint32_t o, uint64_t v) { return store64(user, o, v) == EDEN_DSMOD_TRUE; }});
}
bool Reader::OnAction(const char *action, int64_t value) {
  if (action && !std::strcmp(action, "time_scale"))
    return impl_->snapshot.time_scale_change_allowed &&
           impl_->time.Request(impl_->time_lua, value);
  // Instant game calls (farming_simulator_game_calls.h): the target is resolved here from the
  // snapshot the UI shows, then re-validated by identity inside the game at the moment it runs.
  // One call at a time, only while the load plan's helper is alive.
  namespace gc = farming_simulator::game_calls;
  if (!action)
    return false;
  const std::string_view verb{action};
  const auto &snap = impl_->snapshot;
  const auto now = impl_->Now();
  if (!snap.world_ready || snap.native_menu_open || impl_->calls.Busy() ||
      !impl_->calls.Alive(now))
    return false;
  std::string chunk;
  gc::Kind kind{};
  if (verb == "garage_enter" || verb == "garage_reset") {
    const auto &vehicles = snap.owned_vehicles;
    auto it = std::find_if(vehicles.begin(), vehicles.end(), [&](const auto &v) {
      return v.network_id && int64_t(v.network_id) == value;
    });
    if (it == vehicles.end())
      return false;
    const auto &v = *it;
    const gc::VehicleRef ref{v.root_node, v.network_id};
    if (verb == "garage_enter") {
      if (!v.can_enter)
        return false;
      kind = gc::Kind::Enter;
      chunk = gc::EnterChunk(ref);
    } else {
      if (!v.can_reset)
        return false;
      kind = gc::Kind::Reset;
      chunk = gc::ResetChunk(ref);
    }
  } else
    return false;
  if (!impl_->calls.Submit(kind, chunk, now))
    return false;
  impl_->call_state.busy = true;
  impl_->call_state.kind = gc::KindName(kind);
  return true;
}
bool Reader::RequestPlaceCall(bool tag, const farming_simulator::MapMarker &m) {
  namespace gc = farming_simulator::game_calls;
  const auto &snap = impl_->snapshot;
  const auto now = impl_->Now();
  if (!snap.world_ready || snap.native_menu_open || impl_->calls.Busy() ||
      !impl_->calls.Alive(now) || !m.can_visit)
    return false;
  const gc::PlaceRef ref{m.object_id, m.x, m.z};
  const auto kind = tag ? gc::Kind::Tag : gc::Kind::Visit;
  if (!impl_->calls.Submit(kind, tag ? gc::TagChunk(ref) : gc::VisitChunk(ref), now))
    return false;
  impl_->call_state.busy = true;
  impl_->call_state.kind = gc::KindName(kind);
  return true;
}
bool Reader::CancelPending() { return impl_->time.Cancel(); }
const farming_simulator::Snapshot &Reader::GetSnapshot() const {
  return impl_->snapshot;
}
void Reader::Sample(const EdenDsmodHostApi &host) {
  using namespace farming_simulator;
  const auto sample_start = impl_->Now();
  {
    auto &calls = impl_->calls;
    auto &cs = impl_->call_state;
    calls.Beat(sample_start);
    if (auto finished = calls.Poll(sample_start)) {
      cs.kind = game_calls::KindName(finished->kind);
      cs.status = int(finished->status);
      cs.code = finished->code;
      cs.message = game_calls::OutcomeText(*finished);
      cs.detail = finished->text;
      ++cs.serial;
      if (host.log) {
        auto line = "Farming Simulator game call " + cs.kind + ": status " +
                    std::to_string(cs.status) + " code " + std::to_string(cs.code) + " (" +
                    cs.detail + ")";
        host.log(host.userdata, EDEN_DSMOD_LOG_INFO, line.c_str());
      }
    }
    cs.busy = calls.Busy();
    cs.helper = calls.Alive(sample_start);
  }
  Snapshot next;
  auto &l = impl_->lua;
  l.Begin(host);
  std::string primary_stage="script-root resolution";
  auto report_primary_failure=[&](std::string_view detail) {
    if(!host.log)return;
    auto key=primary_stage+":"+std::string(detail);
    auto message="Farming Simulator primary reader failed during "+primary_stage+
        ": "+std::string(detail)+" (bytes="+std::to_string(l.bytes)+
        ", calls="+std::to_string(l.calls)+")";
    if(key!=impl_->primary_error_key || sample_start>=impl_->next_primary_log) {
      host.log(host.userdata,EDEN_DSMOD_LOG_WARNING,message.c_str());
      impl_->primary_error_key=std::move(key);
      impl_->next_primary_log=sample_start+std::chrono::seconds(30);
    }
  };
  // A read miss anywhere in the primary reader (budget/lookup failure, a
  // lifetime race, ...) must not instantly blank a world that was already
  // showing good data -- that is the flicker this exists to fix. Keep the
  // last committed snapshot verbatim for up to kStaleAfterGoodTimeout of
  // consecutive misses (still gated to the same world_identity it was read
  // for); past that window, or if we were never in a ready world to begin
  // with, give up and report a clean empty/unavailable state instead of
  // showing a previous world's values.
  auto world_retain_or_clear = [&]() -> Snapshot {
    const auto &prev_snap = impl_->snapshot;
    if (prev_snap.world_ready) {
      if (!impl_->world_fail_since)
        impl_->world_fail_since = sample_start;
      if (sample_start - *impl_->world_fail_since < kStaleAfterGoodTimeout)
        return prev_snap;
    }
    return Snapshot{};
  };
  try {
    // Build is pinned by module Create/Supports. Proven ScriptSystem storage,
    // not a heap address.
    auto root = ResolveScriptRoot(l, host);
    primary_stage="global table validation";
    auto object = root.object, state = root.state, env = root.env;
    if (impl_->state != state) {
      l.lookups.clear();
      impl_->marker_lua.lookups.clear();
      impl_->layers_lua.lookups.clear();
      impl_->time_lua.lookups.clear();
      impl_->catalog = {};
      impl_->state = state;
      impl_->markers.clear();
      impl_->markers_error.clear();
      impl_->markers_error_logged.clear();
      impl_->markers_world = 0;
      impl_->next_markers = {};
    }
    CheckGlobalTable(l, env);
    primary_stage="current mission lookup";
    auto mission = l.Field(env, "g_currentMission").Table();
    next.phase = mission ? Phase::Loading : Phase::Title;
    if (mission && l.Field(mission, "isRunning").True() &&
        l.Field(mission, "isLoaded").True() &&
        !l.Field(mission, "isLoadingMap").True()) {
      next.phase = Phase::World;
      next.world_ready = true;
      next.world_identity = mission;
      if (impl_->world_ready_since_identity != mission) {
        // A genuinely new world/save: reset every category's warm-up clock
        // and "ever had good data" memory so none of it leaks from whatever
        // world was loaded before (world_identity gating).
        impl_->world_ready_since_identity = mission;
        impl_->world_ready_since = sample_start;
        impl_->world_fail_since.reset();
        impl_->finances_fail_since.reset();
        impl_->statistics_fail_since.reset();
        impl_->vehicles_fail_since.reset();
        impl_->animals_fail_since.reset();
        impl_->prices_fail_since.reset();
        impl_->finances_ever_ready = impl_->statistics_ever_ready =
            impl_->vehicles_ever_ready = impl_->animals_ever_ready =
                impl_->prices_ever_ready = false;
      }
      primary_stage="world snapshot fields";
      auto info = l.Field(mission, "missionInfo").Table();
      const auto scale = Bounded(l.Field(info, "timeScale"), 1, 120);
      if (scale && (*scale == 1 || *scale == 5 || *scale == 15 || *scale == 30 ||
                    *scale == 60 || *scale == 120))
        next.time_scale = *scale;
      auto native_gui = l.Field(env, "g_gui").Table();
      auto native_gui_name = l.Field(native_gui, "currentGuiName");
      next.native_menu_open = native_gui_name.Tag() == 0x1fffb &&
                              !l.Text(native_gui_name).empty();
      auto mapid = l.Text(l.Field(info, "mapId"));
      next.map_id = mapid == "Map01" ? 1 : mapid == "Map02" ? 2 : 0;
      auto stats = l.Field(mission, "missionStats").Table();
      next.money = Bounded(l.Field(stats, "money"), -1e12, 1e12);
      auto environment = l.Field(mission, "environment").Table();
      auto hour = Bounded(l.Field(environment, "currentHour"), 0, 23),
           minute = Bounded(l.Field(environment, "currentMinute"), 0, 59);
      if (hour && minute)
        next.time_minutes = int(*hour) * 60 + int(*minute);
      auto map = l.Field(mission, "ingameMap").Table();
      if (impl_->markers_world != mission || sample_start >= impl_->next_markers) {
        auto &marker_lua = impl_->marker_lua;
        marker_lua.byte_limit=8*1024*1024;
        marker_lua.lookup_limit=8192;
        marker_lua.Begin(host);
        try {
          impl_->markers = ReadMapMarkers(marker_lua, host, env, map, mission);
          impl_->markers_error.clear();
          impl_->markers_error_logged.clear();
        } catch (const Invalid &) {
          impl_->markers.clear();
          impl_->markers_error = "native hotspot data invalid or over budget";
        } catch (const std::exception &e) {
          impl_->markers.clear();
          impl_->markers_error = e.what();
        } catch (...) {
          impl_->markers.clear();
          impl_->markers_error = "native hotspot read failed";
        }
        impl_->markers_bytes=marker_lua.bytes;
        impl_->markers_calls=marker_lua.calls;
        impl_->markers_world = mission;
        impl_->next_markers = sample_start +
            (impl_->markers_error.empty() ? std::chrono::milliseconds(200)
                                          : std::chrono::seconds(2));
        if (!impl_->markers_error.empty() && host.log &&
            (impl_->markers_error != impl_->markers_error_logged ||
             sample_start >= impl_->next_markers_log)) {
          auto message = "Farming Simulator native map markers unavailable: " +
                         impl_->markers_error;
          host.log(host.userdata, EDEN_DSMOD_LOG_WARNING, message.c_str());
          impl_->markers_error_logged = impl_->markers_error;
          impl_->next_markers_log = sample_start + std::chrono::seconds(30);
        }
      }
      next.map_markers = impl_->markers;
      next.map_markers_error = impl_->markers_error;
      primary_stage="map and player fields";
      auto sx = Bounded(l.Field(map, "worldSizeX"), 1, 65536),
           sz = Bounded(l.Field(map, "worldSizeZ"), 1, 65536);
      auto ox = Bounded(l.Field(map, "worldCenterOffsetX"), 0, 65536),
           oz = Bounded(l.Field(map, "worldCenterOffsetZ"), 0, 65536);
      bool scene_position = false;
      std::optional<double> scene_heading;
      auto vehicle = l.Field(mission, "controlledVehicle").Table();
      auto player = l.Field(mission, "player").Table();
      if (sx && sz && ox && oz) {
        auto root = Bounded(l.Field(vehicle ? vehicle : player, "rootNode"), 1,
                            4294967295.);
        if (root) try {
          auto position = DensityMemory(host).WorldTransform(uint32_t(*root));
          if (position.x >= -*ox && position.x <= *sx-*ox &&
              position.z >= -*oz && position.z <= *sz-*oz) {
            next.player_x = position.x;
            next.player_z = position.z;
            scene_position = true;
            scene_heading = position.heading;
          }
        } catch (...) { /* stale or unsupported scene data: use native map */ }
      }
      if (!scene_position && sx && sz && ox && oz) {
        auto x = Bounded(l.Field(map, "normalizedPlayerPosX"), 0, 1),
             z = Bounded(l.Field(map, "normalizedPlayerPosZ"), 0, 1);
        if (x && z) {
          next.player_x = *x * *sx - *ox;
          next.player_z = *z * *sz - *oz;
        }
      }
      // Heading source depends on mode, and both avoid ingameMap.playerRotation
      // as the primary source: live RE (2026-10-16) found the native map's own
      // Lua update (and so playerRotation) stops running once the player cycles
      // the minimap to its hidden state (ingameMap.state == 2 there, confirmed by
      // writing that field live and watching playerRotation freeze while the
      // player/vehicle kept moving), even though position keeps updating in that
      // state because it is read from the scene node, not the map. On foot, use
      // player.rotY directly -- it is the player's own per-tick field (unrelated
      // to any GUI element's visibility) and a live write to it was observed to
      // immediately propagate into ingameMap.playerRotation whenever the map's
      // update is running, confirming playerRotation is itself derived from
      // rotY. In a vehicle, derive heading from the same scene-node read already
      // used for position (so it updates on the same cadence, hidden or not):
      // earlier live RE found the vehicle rootNode's matrix yaw sits ~180
      // degrees off the vehicle's true facing (atan2 0 vs. native playerRotation
      // pi while parked facing the carport), so add pi and wrap to (-pi, pi].
      // ingameMap.playerRotation is kept only as a fallback/consistency source
      // for when the primary read is unavailable, and only while the map's own
      // update is still fresh (state != 2), since it is known to go stale
      // otherwise.
      if (next.player_x && next.player_z) {
        std::optional<double> heading;
        if (vehicle && scene_heading) {
          double h = *scene_heading + M_PI;
          if (h > M_PI)
            h -= 2 * M_PI;
          else if (h <= -M_PI)
            h += 2 * M_PI;
          heading = h;
        } else if (!vehicle) {
          heading = Bounded(l.Field(player, "rotY"), -100, 100);
        }
        if (!heading) {
          auto map_state = Bounded(l.Field(map, "state"), 0, 100);
          bool map_fresh = !map_state || *map_state != 2;
          if (map_fresh)
            heading = Bounded(l.Field(map, "playerRotation"), -100, 100);
        }
        next.player_heading = heading;
      }
      auto i18n = l.Field(env, "g_i18n").Table();
      auto unit = l.Field(i18n, "moneyUnit").Number();
      if (unit) {
        if (unit == l.Field(env, "GS_MONEY_EURO").Number())
          next.currency_symbol = "€";
        else if (unit == l.Field(env, "GS_MONEY_DOLLAR").Number())
          next.currency_symbol = "$";
        else if (unit == l.Field(env, "GS_MONEY_POUND").Number())
          next.currency_symbol = "£";
      }
      primary_stage="vehicle snapshot fields";
      if (vehicle && l.Field(vehicle, "isEntered").True() &&
          l.Field(vehicle, "isAddedToMission").True()) {
        next.vehicle_ready = true;
        next.vehicle_config = l.Text(l.Field(vehicle, "configFileName"));
        auto config = next.vehicle_config;
        for (char &c : config)
          if (c >= 'A' && c <= 'Z')
            c += 32;
        auto store = l.Field(env, "StoreItemsUtil").Table();
        auto by_xml = l.Field(store, "storeItemsByXMLFilename").Table();
        auto item = l.Field(by_xml, config).Table();
        next.vehicle_name = l.Text(l.Field(item, "name"));
        next.vehicle_image = l.Text(l.Field(item, "imageActive"));
        // Native 2 km/h capture matches lastSpeedReal=.0005089633 * 3600,
        // rounded.
        auto speed = Bounded(l.Field(vehicle, "lastSpeedReal"), 0, 1);
        if (speed)
          next.speed_kmh = *speed * 3600;
        next.fuel_l = Bounded(l.Field(vehicle, "fuelFillLevel"), 0, 1e7);
        next.fuel_capacity_l =
            Bounded(l.Field(vehicle, "fuelCapacity"), 0, 1e7);
        auto units = l.Field(vehicle, "fillUnits").Table();
        auto fill = l.Index(units, 1).Table();
        next.fill_l = Bounded(l.Field(fill, "fillLevel"), 0, 1e9);
        next.fill_capacity_l = Bounded(l.Field(fill, "capacity"), 0, 1e9);
      }
      // Re-read mission identity without the page cache to reject observed
      // lifetime races.
      primary_stage="mission lifetime validation";
      auto unchanged = [&](uint64_t table, const char *name,
                           uint64_t expected) {
        auto hit = l.lookups.find({table, name});
        std::array<uint64_t, 2> final{};
        if ((hit == l.lookups.end() || !hit->second.address) && !expected) {
          l.pages.clear();
          return !l.Field(table, name).Table();
        }
        if (hit != l.lookups.end()) {
          std::array<uint8_t, 64> header{};
          uint64_t nodes{};
          uint32_t mask{};
          if (!host.read_memory(host.userdata, table, header.data(),
                                header.size()))
            return false;
          std::memcpy(&nodes, header.data() + 40, 8);
          std::memcpy(&mask, header.data() + 52, 4);
          if (header[9] != 11 || nodes != hit->second.nodes ||
              mask != hit->second.mask)
            return false;
        }
        return hit != l.lookups.end() &&
               host.read_memory(host.userdata, hit->second.address,
                                final.data(), 16) &&
               final[1] == hit->second.key &&
               Value{final[0]}.Table() == expected;
      };
      if (!unchanged(env, "g_currentMission", mission) ||
          !unchanged(mission, "controlledVehicle", vehicle))
        throw Invalid{};
      uint64_t final_object{}, final_state{};
      if (!host.read_memory(host.userdata, host.main_base + 0x46cb40,
                            &final_object, 8) ||
          final_object != object ||
          !host.read_memory(host.userdata, object, &final_state, 8) ||
          final_state != state)
        throw Invalid{};
      if (impl_->management.world_identity != mission ||
          sample_start >= impl_->next_management) {
        primary_stage="management snapshot";
        const auto management_start = sample_start;
        Snapshot extra;
        extra.world_identity = mission;
        try {
          Management(l, env, mission, extra);
          if (!unchanged(env, "g_currentMission", mission))
            extra = {};
        } catch (...) {
          extra = {};
        }
        // A read miss on a single category (or the whole management
        // snapshot) must not blank data that was already good: keep the
        // previous value for any category this refresh failed to produce,
        // as long as that previous value is for the same world and the
        // category hasn't been failing for longer than
        // kStaleAfterGoodTimeout. Past that window (or if the category has
        // never succeeded this world) the category reports unready/loading
        // via the *_ever_ready/*_fail_since bookkeeping below instead.
        const bool same_world = impl_->management.world_identity == mission;
        const auto &prev = impl_->management;
        auto retain = [&](bool fresh_ready, bool prev_ready,
                          std::optional<std::chrono::steady_clock::time_point>
                              &fail_since, bool &ever_ready,
                          const auto &keep_prev) {
          if (fresh_ready) {
            fail_since.reset();
            ever_ready = true;
            return true;
          }
          if (!fail_since)
            fail_since = sample_start;
          if (same_world && prev_ready &&
              sample_start - *fail_since < kStaleAfterGoodTimeout) {
            keep_prev();
            return true;
          }
          return false;
        };
        extra.finances_ready = retain(
            extra.finances_ready, prev.finances_ready,
            impl_->finances_fail_since, impl_->finances_ever_ready, [&] {
              extra.finance_rows = prev.finance_rows;
              extra.finance_day_labels = prev.finance_day_labels;
              extra.finance_totals = prev.finance_totals;
              extra.finance_saldo = prev.finance_saldo;
              extra.loan = prev.loan;
              extra.loan_max = prev.loan_max;
            });
        extra.statistics_ready = retain(
            extra.statistics_ready, prev.statistics_ready,
            impl_->statistics_fail_since, impl_->statistics_ever_ready,
            [&] { extra.statistic_rows = prev.statistic_rows; });
        extra.vehicles_ready = retain(
            extra.vehicles_ready, prev.vehicles_ready,
            impl_->vehicles_fail_since, impl_->vehicles_ever_ready,
            [&] { extra.owned_vehicles = prev.owned_vehicles; });
        extra.animals_ready =
            retain(extra.animals_ready, prev.animals_ready,
                  impl_->animals_fail_since, impl_->animals_ever_ready,
                  [&] { extra.animals = prev.animals; });
        extra.prices_ready = retain(
            extra.prices_ready, prev.prices_ready, impl_->prices_fail_since,
            impl_->prices_ever_ready, [&] {
              extra.price_crops = prev.price_crops;
              extra.price_stations = prev.price_stations;
            });
        impl_->management = std::move(extra);
        ++impl_->management_revision;
        try {
          CatalogStep(l, env, impl_->catalog);
        } catch (...) { /* retry the uncommitted bounded chunk next tick */
        }
        impl_->management_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                impl_->Now() - management_start)
                .count();
        impl_->next_management = impl_->Now() + std::chrono::seconds{1};
      }
      const auto &extra = impl_->management;
      const bool warming_up =
          sample_start - impl_->world_ready_since < kWarmupTimeout;
      if (extra.world_identity == mission) {
        next.finance_rows = extra.finance_rows;
        next.finance_day_labels = extra.finance_day_labels;
        next.finance_totals = extra.finance_totals;
        next.finance_saldo = extra.finance_saldo;
        next.loan = extra.loan;
        next.loan_max = extra.loan_max;
        next.finances_ready = extra.finances_ready;
        next.statistic_rows = extra.statistic_rows;
        next.statistics_ready = extra.statistics_ready;
        next.vehicles_ready = extra.vehicles_ready;
        next.owned_vehicles = extra.owned_vehicles;
        next.animals_ready = extra.animals_ready;
        next.animals = extra.animals;
        next.prices_ready = extra.prices_ready;
        next.price_crops = extra.price_crops;
        next.price_stations = extra.price_stations;
        next.management_revision = impl_->management_revision;
      }
      next.finances_loading =
          !next.finances_ready && !impl_->finances_ever_ready && warming_up;
      next.statistics_loading = !next.statistics_ready &&
                                !impl_->statistics_ever_ready && warming_up;
      next.vehicles_loading =
          !next.vehicles_ready && !impl_->vehicles_ever_ready && warming_up;
      next.animals_loading =
          !next.animals_ready && !impl_->animals_ever_ready && warming_up;
      next.prices_loading =
          !next.prices_ready && !impl_->prices_ever_ready && warming_up;
      if (impl_->catalog.complete) {
        next.shop_ready = impl_->catalog.data.shop_ready;
        next.shop_categories = impl_->catalog.data.shop_categories;
        next.shop_brands = impl_->catalog.data.shop_brands;
        next.shop_items = impl_->catalog.data.shop_items;
      }
      next.shop_loading = !next.shop_ready && warming_up;
      if (impl_->layers_world != mission || sample_start >= impl_->next_layers) {
        primary_stage="map layer snapshot validation";
        const auto begin = impl_->Now();
        impl_->layers_lua.Begin(host);
        try {
          auto layers = ReadMapLayers(impl_->layers_lua, host, env, mission);
          if (!unchanged(env, "g_currentMission", mission))
            throw Invalid{};
          impl_->layers = std::move(layers);
          impl_->layers_world = mission;
          impl_->layers_error.clear();
          impl_->layers_error_key_logged.clear();
        } catch (const Invalid &) {
          impl_->layers.reset();
          impl_->layers_world = mission;
          impl_->layers_error = "native map metadata invalid";
        } catch (const std::exception &e) {
          impl_->layers.reset();
          impl_->layers_world = mission;
          impl_->layers_error = e.what();
        } catch (...) {
          impl_->layers.reset();
          impl_->layers_world = mission;
          impl_->layers_error = "native map layer read failed";
        }
        impl_->layers_us = std::chrono::duration_cast<std::chrono::microseconds>(
            impl_->Now() - begin).count();
        std::string layer_error_key=impl_->layers_error;
        if(layer_error_key.starts_with("density page budget"))
          layer_error_key="density page budget";
        else if(layer_error_key.starts_with("density page unmapped"))
          layer_error_key="density page unmapped";
        else if(layer_error_key.starts_with("density page read failed"))
          layer_error_key="density page read failed";
        else if(auto details=layer_error_key.find(" (bytes=");details!=std::string::npos)
          layer_error_key.resize(details);
        if (!impl_->layers_error.empty() && host.log &&
            (layer_error_key != impl_->layers_error_key_logged ||
             sample_start >= impl_->next_layers_log)) {
          auto message = "Farming Simulator map layers unavailable: " +
                         impl_->layers_error;
          host.log(host.userdata, EDEN_DSMOD_LOG_WARNING, message.c_str());
          impl_->layers_error_key_logged = layer_error_key;
          impl_->next_layers_log = sample_start + std::chrono::seconds(30);
        }
        impl_->next_layers = impl_->Now() + std::chrono::seconds{2};
      }
      if (impl_->layers_world == mission)
        next.map_layers = impl_->layers;
      next.map_layer_error = impl_->layers_error;
      // Reached the end of a successful in-world sample: the world-level
      // miss streak (if any was running) is over.
      impl_->world_fail_since.reset();
    }
  } catch (const Invalid &) {
    report_primary_failure("invalid native memory or Lua table");
    next = world_retain_or_clear();
  } catch (const std::exception &e) {
    report_primary_failure(e.what());
    next = world_retain_or_clear();
  } catch (...) {
    report_primary_failure("unknown reader failure");
    next = world_retain_or_clear();
  }
  if (!next.world_ready) {
    impl_->management = {};
    impl_->catalog = {};
    impl_->next_management = {};
    impl_->layers.reset();
    impl_->layers_error.clear();
    impl_->layers_error_key_logged.clear();
    impl_->layers_world = 0;
    impl_->next_layers = {};
    impl_->markers.clear();
    impl_->markers_error.clear();
    impl_->markers_error_logged.clear();
    impl_->markers_world = 0;
    impl_->next_markers = {};
    impl_->world_ready_since_identity = 0;
    impl_->world_fail_since.reset();
    impl_->finances_fail_since.reset();
    impl_->statistics_fail_since.reset();
    impl_->vehicles_fail_since.reset();
    impl_->animals_fail_since.reset();
    impl_->prices_fail_since.reset();
    impl_->finances_ever_ready = impl_->statistics_ever_ready =
        impl_->vehicles_ever_ready = impl_->animals_ever_ready =
            impl_->prices_ever_ready = false;
  }
  impl_->time.Tick(impl_->time_lua, host, next);
  impl_->snapshot = std::move(next);
  impl_->snapshot.game_call = impl_->call_state;
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                           impl_->Now() - sample_start)
                           .count();
  impl_->peak_us = std::max(impl_->peak_us, int64_t(elapsed));
  const auto total_bytes = l.bytes + impl_->time_lua.bytes;
  const auto total_calls = l.calls + impl_->time_lua.calls;
  impl_->peak_bytes = std::max(impl_->peak_bytes, total_bytes);
  if (host.publish_i64) {
    // The only one of this group a manifest widget actually gates on (the
    // footer tabs' need_bind); keep it unconditional. The rest below exist
    // only so a test can see reader-internal detail, so a normal run skips
    // them (see Reader::SetDiagnostics).
    host.publish_i64(host.userdata, "fs.world.ready",
                     impl_->snapshot.world_ready);
    const auto &cs = impl_->call_state;
    host.publish_i64(host.userdata, "fs.call.helper", cs.helper);
    host.publish_i64(host.userdata, "fs.call.busy", cs.busy);
    host.publish_i64(host.userdata, "fs.call.serial", cs.serial);
    host.publish_i64(host.userdata, "fs.call.status", cs.status);
    host.publish_i64(host.userdata, "fs.call.code", cs.code);
    if (host.publish_text) {
      host.publish_text(host.userdata, "fs.call.kind", cs.kind.c_str());
      host.publish_text(host.userdata, "fs.call.message", cs.message.c_str());
      host.publish_text(host.userdata, "fs.call.detail", cs.detail.c_str());
    }
    if (impl_->diagnostics_enabled) {
      host.publish_i64(host.userdata, "fs.layers.ready", bool(impl_->snapshot.map_layers));
      host.publish_i64(host.userdata, "fs.layers.us", impl_->layers_us);
      if (host.publish_text)
        host.publish_text(host.userdata, "fs.layers.error",
                          impl_->snapshot.map_layer_error.c_str());
      if (host.publish_text)
        host.publish_text(host.userdata, "fs.markers.error",
                          impl_->snapshot.map_markers_error.c_str());
      host.publish_i64(host.userdata, "fs.markers.bytes",
                       impl_->markers_bytes);
      host.publish_i64(host.userdata, "fs.markers.calls",
                       impl_->markers_calls);
      host.publish_i64(host.userdata, "fs.layers.bytes",
                       impl_->layers_lua.bytes);
      host.publish_i64(host.userdata, "fs.layers.calls",
                       impl_->layers_lua.calls);
      host.publish_i64(host.userdata, "fs.time.scale", impl_->snapshot.time_scale.value_or(0));
      host.publish_i64(host.userdata, "fs.time.allowed", impl_->snapshot.time_scale_change_allowed);
      host.publish_i64(host.userdata, "fs.time.pending", impl_->snapshot.time_scale_pending);
      host.publish_i64(host.userdata, "fs.reader.us", elapsed);
      host.publish_i64(host.userdata, "fs.reader.peak_us", impl_->peak_us);
      host.publish_i64(host.userdata, "fs.reader.management_us",
                       impl_->management_us);
      host.publish_i64(host.userdata, "fs.reader.peak_bytes", impl_->peak_bytes);
      host.publish_i64(host.userdata, "fs.shop.count",
                       impl_->snapshot.shop_items.size());
      host.publish_i64(host.userdata, "fs.vehicles.count",
                       impl_->snapshot.owned_vehicles.size());
      host.publish_i64(host.userdata, "fs.prices.stations",
                       impl_->snapshot.price_stations.size());
      host.publish_i64(host.userdata, "fs.vehicle.ready",
                       impl_->snapshot.vehicle_ready);
      host.publish_i64(host.userdata, "fs.map.id", impl_->snapshot.map_id);
      host.publish_i64(host.userdata, "fs.shop.ready",
                       impl_->snapshot.shop_ready);
      host.publish_i64(host.userdata, "fs.vehicles.ready",
                       impl_->snapshot.vehicles_ready);
      host.publish_i64(host.userdata, "fs.catalog.next", impl_->catalog.next);
      host.publish_i64(host.userdata, "fs.animals.ready",
                       impl_->snapshot.animals_ready);
      host.publish_i64(host.userdata, "fs.finances.ready",
                       impl_->snapshot.finances_ready);
      host.publish_i64(host.userdata, "fs.statistics.ready",
                       impl_->snapshot.statistics_ready);
      host.publish_i64(host.userdata, "fs.prices.ready",
                       impl_->snapshot.prices_ready);
      host.publish_i64(host.userdata, "fs.reader.bytes", total_bytes);
      host.publish_i64(host.userdata, "fs.reader.calls", total_calls);
    }
  }
}
} // namespace farming_simulator_reader
