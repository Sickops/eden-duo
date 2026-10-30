#include "belmont_map_control.h"
#include <cassert>
#include <iostream>
using namespace belmont_map_control;
Request RequestFor(Operation op = Operation::Add) {
  Request r;
  r.operation = op;
  r.source_proven = true;
  r.level_guid = std::string(32, 'a');
  r.type_guid = std::string(32, 'b');
  r.navigation_index = 0;
  r.x = 2;
  r.y = 3;
  r.bounds = {0, 0, 10, 10};
  r.native_id = 7;
  return r;
}
Control Live() {
  Control c;
  c.ready = c.coherent = c.menu_open = c.map_tab = c.controls_enabled =
      c.selection_empty = true;
  c.entity = 0x10000;
  c.markers = 0x20000;
  c.level_guid = std::string(32, 'a');
  c.navigation_index = 0;
  c.current_tab = c.map_tab_index = 0;
  return c;
}
Control Palette() {
  auto c = Live();
  c.palette = c.place_palette = c.top_layer = true;
  c.controls_enabled = false;
  c.palette_object = 0x30000;
  c.position_address = c.markers + 0x48;
  c.items = {{0x40000, std::string(32, 'c'), true},
             {0x50000, std::string(32, 'b'), true}};
  c.selected_index = 0;
  return c;
}
int main() {
  {
    Controller p;
    auto r = RequestFor();
    r.source_proven = false;
    assert(!p.Start(r, 0));
    r = RequestFor();
    r.x = 20;
    assert(!p.Start(r, 0));
    r = RequestFor();
    r.bounds[0] = NAN;
    assert(!p.Start(r, 0));
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.selection_empty = false;
    c.selected_marker_id = 9;
    c.cursor_ready = true;
    c.cursor_position = {0, 0};
    auto step = p.Update(c, 1);
    assert(step.button == Button::None && step.move_cursor && p.Busy());
    assert(step.stick_x > 0 && step.stick_y > 0);
    assert(!p.Update(c, 100).move_cursor);
    c.selection_empty = true;
    c.selected_marker_id = -1;
    step = p.Update(c, 300);
    assert(step.button == Button::Y && !step.move_cursor);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.selection_empty = false;
    c.selected_marker_id = 9;
    c.cursor_ready = true;
    c.cursor_position = {2, 3};
    const auto step = p.Update(c, 1);
    assert(step.button == Button::None && step.move_cursor);
    assert(step.stick_x == 0.35f && step.stick_y == 0);
    c.cursor_position = {2.1f, 3};
    const auto next = p.Update(c, 300);
    assert(next.button == Button::None && next.move_cursor && next.stick_x > 0);
    assert(p.Update(c, 21000).done && !p.Busy());
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    assert(p.Update(c, 1).button == Button::Y);
    assert(p.Update(c, 300).button == Button::None);
    assert(p.Update(c, 1600).done);
    assert(!p.Busy());
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    assert(p.Update(c, 1).button == Button::Y);
    c = Palette();
    auto step = p.Update(c, 300);
    assert(step.write_position && step.address == 0x20048 &&
           step.position[0] == 2);
    assert(p.CanWrite(c, step));
    auto stale = c;
    stale.navigation_index = 1;
    assert(!p.CanWrite(stale, step));
    stale = c;
    stale.placement[0] = 9;
    assert(!p.CanWrite(stale, step));
    p.PositionWriteResult(true);
    c.placement = {2, 3};
    assert(p.Update(c, 600).button == Button::Right);
    assert(p.Update(c, 900).button == Button::None);
    c.selected_index = 1;
    step = p.Update(c, 1200);
    assert(step.button == Button::A && step.submitted);
    assert(p.Busy());
    assert(p.Update(c, 1500, true).done);
    assert(!p.Busy());
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Palette();
    assert(p.Update(c, 300).write_position);
    p.PositionWriteResult(true);
    c.placement = {1, 3};
    assert(p.Update(c, 600).done);
    assert(!p.Busy());
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Palette();
    c.place_palette = false;
    c.change_palette = true;
    assert(p.Update(c, 1).done);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Palette();
    assert(p.Update(c, 1).write_position);
    p.PositionWriteResult(false);
    assert(!p.Busy());
  }
  {
    Controller p;
    assert(p.Start(RequestFor(Operation::Remove), 0));
    auto c = Live();
    c.selection_empty = false;
    c.selected_marker_id = 8;
    c.cursor_ready = true;
    c.cursor_position = {0, 0};
    auto s = p.Update(c, 1);
    assert(s.move_cursor && s.button == Button::None &&
           std::hypot(s.stick_x, s.stick_y) <= 1.001);
    c.selected_marker_id = 7;
    s = p.Update(c, 300);
    assert(s.button == Button::Y && s.submitted);
    assert(p.Update(c, 600, true).done);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(Operation::Change), 0));
    auto c = Live();
    c.selected_marker_id = 7;
    c.selection_empty = false;
    assert(p.Update(c, 1).button == Button::A);
    c = Palette();
    c.place_palette = false;
    c.change_palette = true;
    c.change_marker_id = 8;
    assert(p.Update(c, 300).done);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(Operation::Change), 0));
    auto c = Palette();
    c.place_palette = false;
    c.change_palette = true;
    c.change_marker_id = 7;
    c.selected_index = 1;
    auto s = p.Update(c, 1);
    assert(s.button == Button::A && s.submitted);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.level_guid = std::string(32, 'c');
    assert(p.Update(c, 1).done);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.navigation_index = 1;
    assert(p.Update(c, 1).done);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.map_tab = false;
    c.current_tab = 2;
    c.previous_tab = true;
    assert(p.Update(c, 1).button == Button::L);
    assert(p.Update(c, 300).button == Button::None);
    assert(p.Update(c, 1600).done);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.menu_open = false;
    c.can_open = true;
    assert(p.Update(c, 1).button == Button::Plus);
    c = Live();
    assert(p.Update(c, 300).button == Button::Y);
  }
  {
    auto read = [](U64, void *, std::size_t) { return false; };
    Decoder d(read);
    belmont::Snapshot s;
    belmont::Identity id{};
    auto c = d.Sample(id, s);
    assert(!c.ready && !c.coherent && c.reason == "game_unavailable");
    s.supported = s.coherent = s.entity_ready = true;
    s.application_loading = s.room_loading = 0;
    c = d.Sample(id, s);
    assert(!c.ready && c.reason == "game_changed");
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.ready = false;
    c.reason = "map_context_unavailable";
    assert(!p.Update(c, 1).done && p.Busy());
    assert(!p.Update(c, 1000).done);
    c = Live();
    assert(p.Update(c, 1100).button == Button::Y);
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Live();
    c.ready = false;
    c.reason = "map_context_unavailable";
    p.Update(c, 1);
    assert(p.Update(c, 1600).done && !p.Busy());
  }
  {
    Controller p;
    assert(p.Start(RequestFor(), 0));
    auto c = Palette();
    c.items[1].enabled = false;
    auto step = p.Update(c, 1);
    assert(step.write_position);
    p.PositionWriteResult(true);
    c.placement = {2, 3};
    assert(p.Update(c, 300).done && !p.Busy());
  }
  std::cout << "native map controller: bounds, observed navigation, guarded "
               "placement, exact-ID edits, failure and mutation checks PASS\n";
}
