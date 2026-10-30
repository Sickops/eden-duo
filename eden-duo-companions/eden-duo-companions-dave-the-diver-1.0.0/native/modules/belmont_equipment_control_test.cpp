// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_equipment_control.h"
#include <iostream>
#include <stdexcept>
using namespace belmont_equipment_control;
using belmont_inventory::Snapshot;
void Check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
}
Snapshot State() {
  Snapshot s;
  s.supported = s.coherent = s.ready = true;
  s.entity = 0x10000;
  auto &c = s.control;
  c.ready = c.menu_open = c.equipment_tab = c.overview = c.confirm = c.back =
      true;
  c.selected_button = 100;
  for (int i = 0; i < 3; ++i) {
    belmont_inventory::Slot slot;
    slot.button = 100 + unsigned(i) * 100;
    slot.item.type = i + 1;
    slot.item.id = "item" + std::to_string(i);
    slot.enabled = true;
    slot.index = i;
    slot.selected = i == 0;
    slot.navigation[2] = i ? slot.button - 100 : 300;
    slot.navigation[3] = i == 2 ? 100 : slot.button + 100;
    c.slots.push_back(slot);
  }
  return s;
}
void Select(Snapshot &s, std::uint64_t button) {
  s.control.selected_button = button;
  for (auto &slot : s.control.slots)
    slot.selected = slot.button == button;
}
int main() {
  try {
    Controller d;
    auto s = State();
    d.RequestCategory(2);
    Check(d.Update(s, true, 100) == Button::Right,
          "native graph category route");
    Check(d.Update(s, true, 110) == Button::None &&
              d.Update(s, true, 350) == Button::None,
          "no blind repeat on unchanged focus");
    Select(s, 200);
    Check(d.Update(s, true, 351) == Button::A,
          "native category submit after observed focus");
    Check(d.Update(s, true, 400) == Button::None, "one tick category submit");
    s.control.overview = false;
    s.control.picker = true;
    s.control.category = 2;
    Check(d.Update(s, true, 650) == Button::None && !d.busy(),
          "picker acknowledged");
    d.RequestPick(1);
    Check(d.Update(s, true, 1000) == Button::A && !d.busy(),
          "selected explicit item submit");
    Check(d.Update(s, true, 1001) == Button::None, "submit never repeats");
    d.RequestPick(2);
    Check(d.Update(s, true, 1300) == Button::None && !d.busy(),
          "cannot select other category slot");
    s = State();
    d.RequestCategory(3);
    Check(d.Update(s, true, 2000) == Button::Left, "native loop shortest path");
    Select(s, 200);
    Check(d.Update(s, true, 2300) == Button::None && !d.busy(),
          "unexpected user focus aborts");
    s = State();
    d.RequestCategory(3);
    Check(d.Update(s, true, 3000) == Button::Left, "restart request");
    s.entity += 8;
    Check(d.Update(s, true, 3400) == Button::None && !d.busy(),
          "entity replacement aborts");
    s = State();
    d.RequestCategory(2);
    d.Update(s, true, 4000);
    Check(d.Update(s, false, 4300) == Button::None && !d.busy(),
          "loading cinematic gate aborts");
    s = State();
    d.RequestCategory(2);
    d.Update(s, true, 5000);
    Check(d.Update(s, true, 6500) == Button::None && !d.busy(),
          "native no response timeout");
    s = State();
    s.control.menu_open = s.control.equipment_tab = s.control.overview = false;
    s.control.can_open = true;
    d.RequestCategory(2);
    Check(d.Update(s, true, 7000) == Button::Plus,
          "closed native menu opens once");
    Check(d.Update(s, true, 7300) == Button::None,
          "do not toggle menu blindly");
    d.Cancel();
    s = State();
    s.control.menu_open = s.control.equipment_tab = s.control.overview = false;
    s.control.can_open = false;
    d.RequestCategory(2);
    Check(d.Update(s, true, 8000) == Button::None, "covered menu blocks Plus");
    s = State();
    s.control.overview = false;
    s.control.picker = true;
    s.control.category = 3;
    s.control.holding_charm = true;
    s.control.slots[0].index = -1;
    s.control.slots[0].item.type = 3;
    d.RequestPick(-1);
    Check(d.Update(s, true, 9000) == Button::A,
          "native held relic destination submit");
    d.Back();
    Check(d.Update(s, true, 10000) == Button::B, "native picker back");
    Check(d.Update(s, true, 10001) == Button::None, "back single pulse");
    s.control.holding_charm = false;
    Check(d.Update(s, true, 10300) == Button::None && !d.busy(),
          "back acknowledged");
    d.Cancel();
    s = State();
    s.control.current_tab = 0;
    s.control.equipment_tab_index = 1;
    s.control.active_tabs = {0, 1, 2, 3, 4};
    s.control.equipment_tab = false;
    s.control.overview = false;
    s.control.previous_tab = s.control.next_tab = true;
    d.RequestCategory(2);
    Check(d.Update(s, true, 11000) == Button::R,
          "source known Map tab routes R to Equipment");
    Check(d.Update(s, true, 11100) == Button::None, "tab pulse single tick");
    s.control.current_tab = 1;
    s.control.equipment_tab = s.control.overview = true;
    Select(s, 200);
    Check(d.Update(s, true, 11300) == Button::A,
          "native tab change acknowledged before picker submit");
    d.Cancel();
    s = State();
    s.control.current_tab = 2;
    s.control.equipment_tab_index = 1;
    s.control.active_tabs = {0, 1, 2, 3, 4};
    s.control.equipment_tab = false;
    s.control.overview = false;
    s.control.previous_tab = s.control.next_tab = true;
    d.RequestCategory(1);
    Check(d.Update(s, true, 12000) == Button::L,
          "source known Tarot tab routes L");
    d.Cancel();
    d.Cancel();
    s = State();
    s.control.overview = false;
    s.control.picker = true;
    s.control.category = 3;
    s.control.native_pages = 2;
    s.control.native_page = 0;
    s.control.can_next_page = true;
    d.RequestPage(1);
    Check(d.Update(s, true, 13000) == Button::R,
          "native relic next page shoulder binding");
    Check(d.Update(s, true, 13001) == Button::None, "native page one pulse");
    s.control.native_page = 1;
    s.control.can_next_page = false;
    s.control.can_prev_page = true;
    Check(d.Update(s, true, 13300) == Button::None && !d.busy(),
          "native relic page change observed");
    d.RequestPage(-1);
    Check(d.Update(s, true, 14000) == Button::L,
          "native relic previous page shoulder binding");
    s.control.native_page = 0;
    s.control.can_prev_page = false;
    Check(d.Update(s, true, 14300) == Button::None && !d.busy(),
          "native relic previous page observed");
    d.RequestPage(-1);
    Check(d.Update(s, true, 15000) == Button::None && !d.busy(),
          "native page bounds fail closed");
    s.control.holding_charm = true;
    s.control.can_next_page = true;
    d.RequestPage(1);
    Check(d.Update(s, true, 16000) == Button::None && !d.busy(),
          "no native page change during held relic destination choice");
    d.Cancel();
    s = State();
    s.control.overview = false;
    s.control.picker = true;
    s.control.category = 1;
    s.control.slots.clear();
    for (int i = 0; i < 28; ++i) {
      belmont_inventory::Slot slot;
      slot.index = i;
      slot.button = 100 + unsigned(i) * 100;
      slot.item.type = 1;
      slot.item.id = "Weapon" + std::to_string(i);
      slot.enabled = true;
      slot.selected = i == 0;
      slot.navigation[2] = 100 + unsigned((i + 27) % 28) * 100;
      slot.navigation[3] = 100 + unsigned((i + 1) % 28) * 100;
      s.control.slots.push_back(slot);
    }
    d.RequestPick(14);
    std::uint64_t now = 17000;
    for (int i = 0; i < 14; ++i) {
      auto pulse = d.Update(s, true, now);
      Check(pulse == Button::Left || pulse == Button::Right,
            "28weapon carousel 14 observed navigation steps");
      std::uint64_t next = 0;
      for (const auto &slot : s.control.slots)
        if (slot.button == s.control.selected_button)
          next = slot.navigation[pulse == Button::Left ? 2 : 3];
      Select(s, next);
      now += 300;
    }
    Check(d.Update(s, true, now) == Button::A && !d.busy(),
          "farthest carousel target submits after14moves "
          "within40pulse20secondbudget");

    {
      Controller transient;
      auto fresh = State();
      fresh.control.menu_open = false;
      fresh.control.equipment_tab = false;
      fresh.control.overview = false;
      fresh.control.can_open = true;
      transient.RequestCategory(2);
      Check(transient.Update(fresh, true, 100) == Button::Plus,
            "first category opens native menu once");
      fresh = State();
      fresh.control.selected_button = 0;
      fresh.control.confirm = false;
      for (auto &slot : fresh.control.slots)
        slot.selected = false;
      Check(transient.Update(fresh, true, 400) == Button::None &&
                transient.busy() &&
                std::string(transient.reason()) == "selection_pending",
            "menu opened before native focus settles stays pending without "
            "input");
      Check(transient.Update(fresh, true, 800) == Button::None &&
                transient.busy(),
            "transient unfocused overview sends no repeated Plus or A");
      Select(fresh, 200);
      fresh.control.confirm = true;
      Check(transient.Update(fresh, true, 900) == Button::A,
            "fresh native focus authorizes category submit after bounded wait");
      fresh.control.overview = false;
      fresh.control.picker = true;
      fresh.control.category = 2;
      Check(transient.Update(fresh, true, 1200) == Button::None &&
                !transient.busy(),
            "picker opens after delayed focus");
    }
    {
      Controller noFocus;
      auto fresh = State();
      fresh.control.selected_button = 0;
      fresh.control.confirm = false;
      for (auto &slot : fresh.control.slots)
        slot.selected = false;
      noFocus.RequestCategory(1);
      Check(noFocus.Update(fresh, true, 2000) == Button::None && noFocus.busy(),
            "focus wait begins without input");
      Check(noFocus.Update(fresh, true, 3499) == Button::None && noFocus.busy(),
            "focus wait remains bounded before deadline");
      Select(fresh, 100);
      fresh.control.confirm = true;
      Check(noFocus.Update(fresh, true, 3500) == Button::None &&
                !noFocus.busy() &&
                std::string(noFocus.reason()) == "selection_timeout",
            "focus at deadline cannot cause late submit");
      noFocus.RequestCategory(1);
      fresh.control.selected_button = 0;
      Check(noFocus.Update(fresh, true, 4000) == Button::None && noFocus.busy(),
            "second focus wait");
      fresh.control.menu_open = false;
      fresh.control.equipment_tab = false;
      fresh.control.overview = false;
      fresh.control.can_open = true;
      Check(noFocus.Update(fresh, true, 4300) == Button::None &&
                !noFocus.busy(),
            "closing menu during focus wait aborts without reopening Plus");
    }
    {
      Controller route;
      auto page = State();
      auto &c = page.control;
      c.overview = false; c.picker = true; c.category = 3;
      c.native_pages = 2; c.native_page = 0; c.can_next_page = true;
      for (auto &slot : c.slots) { slot.item.type=3; slot.native_page=0; }
      auto &target = c.slots[2]; target.native_page=1; target.enabled=false;
      route.RequestPick(2);
      Check(route.Update(page,true,100) == Button::R, "off-page owned target routes source shoulder first");
      Check(route.Update(page,true,400) == Button::None && route.busy(), "page route never repeats unacknowledged input");
      c.native_page=1; c.can_next_page=false; c.can_prev_page=true;
      Check(route.Update(page,true,401) == Button::None && route.busy() &&
            std::string(route.reason()) == "native_page_button_pending",
            "observed page waits for native group transition without input");
      target.enabled=true; Select(page,target.button);
      Check(route.Update(page,true,500) == Button::A && !route.busy(), "fresh page and selected owned target authorize one submit");
      c.slots[0].enabled=false;
      route.RequestPick(0);
      Check(route.Update(page,true,800) == Button::L, "owned target routes back to page zero");
      c.native_page=0; c.slots[0].enabled=true; Select(page,100);
      Check(route.Update(page,true,1100) == Button::A, "page zero selected target submits");
      route.RequestPick(2);
      Check(route.Update(page,true,1400) == Button::None && !route.busy(), "missing page permission fails closed");
      c.can_next_page=true; target.enabled=false;
      route.RequestPick(2);
      Check(route.Update(page,true,1700) == Button::R, "new off-page request binds native owned identity");
      target.item.id="replaced"; c.native_page=1; target.enabled=true; Select(page,target.button);
      Check(route.Update(page,true,2000) == Button::None && !route.busy(), "target identity changed during page transition aborts");
    }
    {
      Controller transition;
      auto page=State();auto& c=page.control;
      c.overview=false;c.picker=true;c.category=3;c.native_pages=2;c.can_next_page=true;
      for(auto& slot:c.slots){slot.item.type=3;slot.native_page=0;}
      c.slots[2].native_page=1;c.slots[2].enabled=false;
      transition.RequestPick(2);
      Check(transition.Update(page,true,100)==Button::R,"disabled page transition begins");
      c.native_page=1;
      Check(transition.Update(page,true,400)==Button::None&&transition.busy(),"native disabled page acknowledgment bounded wait");
      Check(transition.Update(page,true,1899)==Button::None&&transition.busy(),"native disabled page waits before deadline");
      Check(transition.Update(page,true,1900)==Button::None&&!transition.busy()&&std::string(transition.reason())=="native_page_button_timeout","native disabled page timeout sends no further input");
    }
    d.RequestCategory(99);
    Check(!d.busy(), "invalid categories rejected");
    std::cout << "equipment controller tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
