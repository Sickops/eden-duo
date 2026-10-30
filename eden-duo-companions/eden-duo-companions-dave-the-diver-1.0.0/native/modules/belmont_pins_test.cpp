// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_pins.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace belmont_pins;
void Check(bool value, const char *why) {
  if (!value)
    throw std::runtime_error(why);
}
std::size_t Count(const Store &s) {
  std::size_t n = 0;
  for (const auto &p : s.All())
    if (p)
      ++n;
  return n;
}
void Arm(Store &s, int kind) {
  Check(s.Open() && s.Picker() && !s.Armed(), "chooser first");
  Check(s.Choose(kind) && s.Armed() && !s.Picker(),
        "choose arms coordinate capture");
}
int main() {
  try {
    Store s;
    Check(!s.Open() && !s.Begin(0) && !s.Capture(1, 1, 2),
          "inactive cannot enqueue");
    s.Context("A", {0, 0, 100, 100}, true);
    Check(!s.Begin(0) && !s.Capture(1, 1, 2) && !s.TakeRequest(),
          "unarmed tap ignored");
    Check(s.Open() && !s.Begin(0) && !s.Choose(-1) && !s.Choose(9) &&
              s.Picker(),
          "valid icon required");
    Check(s.Choose(3) && !s.Capture(2, 1, 2), "unsolicited coordinate ignored");
    Check(s.Begin(5) && !s.Begin(5) && !s.Capture(5, 1, 2) &&
              !s.Capture(4, 1, 2) && s.Waiting(),
          "fresh sequence required");
    Check(s.Capture(6, 10, 20) && s.Busy() && !s.Armed() && !s.Picker() &&
              Count(s) == 0,
          "add intent without optimistic pin");
    Check(!s.Open() && !s.Begin(7) && !s.Select(0) && !s.Choose(2) &&
              !s.Remove(),
          "pending blocks competing input");
    auto q = s.TakeRequest();
    Check(q && q->operation == Operation::Add && q->pin.area == "A" &&
              q->pin.x == 10 && q->pin.y == 20 && q->pin.kind == 3 &&
              q->pin.native_id == -1,
          "exact add request");
    Check(s.Busy() && !s.TakeRequest() && !s.Open(),
          "dispatch one shot remains busy");
    s.Cancel();
    Check(s.Busy(), "cancel cannot imply dispatched native action undone");
    s.FinishRequest();
    Check(!s.Busy() && Count(s) == 0, "finish does not invent native result");
    std::vector<Pin> native{{"A", 10, 20, 3, 42, 1}};
    Check(s.SetPins(native) && Count(s) == 1 && s.All()[0]->native_id == 42,
          "native top add becomes visible");
    native.push_back({"A", 30, 40, 2, 77, 1});
    Check(s.SetPins(native) && Count(s) == 2, "second top pin");
    Check(s.Select(0) && s.Editing(), "edit existing pin");
    std::swap(native[0], native[1]);
    native[1].kind = 5;
    native[1].x = 15;
    Check(s.SetPins(native) && s.Editing() && s.All()[1]->kind == 5,
          "top edit and reorder preserve native identity");
    Check(s.Choose(8) && s.Busy() && s.All()[1]->kind == 5 &&
              s.All()[1]->x == 15,
          "edit emits intent without optimistic mutation");
    q = s.TakeRequest();
    Check(q && q->operation == Operation::Change && q->pin.native_id == 42 &&
              q->pin.native_navigation == 1 && q->pin.kind == 8 &&
              q->pin.x == 15,
          "change targets stable refreshed identity");
    s.FinishRequest();
    Check(s.All()[1]->kind == 5,
          "failed/unacknowledged change remains source value");
    native[1].kind = 8;
    Check(s.SetPins(native) && s.All()[1]->kind == 8,
          "native acknowledged change visible");
    Check(s.Select(1) && s.Remove() && Count(s) == 2 && s.Busy(),
          "remove is only intent");
    q = s.TakeRequest();
    Check(q && q->operation == Operation::Remove && q->pin.native_id == 42 &&
              q->pin.kind == 8,
          "exact native remove identity");
    native.pop_back();
    Check(s.SetPins(native) && Count(s) == 1 && s.Busy(),
          "native top delete refresh independent of execution lock");
    s.FinishRequest();
    Check(!s.Busy() && Count(s) == 1, "delete completion no extra mutation");
    Check(s.Select(0), "select before top delete");
    native.clear();
    Check(s.SetPins(native) && !s.Editing() && !s.Picker() && !s.Remove(),
          "top deletion cancels missing edit target");
    native = {{"A", 10, 20, 0, 12, 0}, {"A", 30, 40, 1, 12, 1}};
    Check(s.SetPins(native) && s.Select(1) && s.Remove(),
          "same ID across navigation scoped separately");
    q = s.TakeRequest();
    Check(q && q->pin.native_navigation == 1,
          "navigation scopes selected native identity");
    s.FinishRequest();
    Check(s.Select(0) && s.Remove(), "pending remove before top delete");
    native.erase(native.begin());
    Check(s.SetPins(native) && !s.Busy() && !s.TakeRequest(),
          "target gone cancels undispatched remove");
    Arm(s, 2);
    Check(s.Begin(10) && !s.Capture(11, NAN, 2) && !s.Waiting() && s.Armed(),
          "NaN leaves armed for retry");
    Check(s.Begin(11) && !s.Capture(12, 101, 2) && s.Armed(), "bounds reject");
    Check(s.Begin(12) && !s.Capture(13, INFINITY, 2), "infinity reject");
    Check(s.Begin(13) && s.Capture(14, 100, 0) && Count(s) == 1,
          "boundary only enqueues");
    s.Cancel();
    Check(!s.Busy() && !s.TakeRequest() && Count(s) == 1,
          "cancel removes undispatched intent");
    Arm(s, 1);
    s.Context("A", {0, 0, 100, 100}, true);
    Check(s.Armed() && s.Begin(15), "same context preserves capture");
    s.Context("A", {0, 0, 200, 200}, true);
    Check(!s.Armed() && !s.Waiting() && !s.Capture(16, 1, 2),
          "bounds change cancels capture");
    Arm(s, 1);
    s.Context("B", {0, 0, 200, 200}, true);
    Check(!s.Armed() && !s.Select(0), "area change scopes selection");
    s.Context("A", {0, 0, 200, 200}, true);
    Check(s.Select(0), "select before disable");
    s.Context("A", {0, 0, 200, 200}, false);
    Check(!s.Remove() && !s.Open() && !s.Editing() && Count(s) == 1,
          "loading disabled context");
    s.Context("", {0, 0, 200, 200}, true);
    Check(!s.Open(), "empty area blocked");
    s.Context("A", {0, 0, NAN, 200}, true);
    Check(!s.Open(), "invalid bounds blocked");
    s.Context("A", {0, 0, 200, 200}, true);
    native.clear();
    for (unsigned i = 0; i < Capacity; ++i)
      native.push_back({"A", double(i), 2, i % 9, int(i), 0});
    Check(s.SetPins(native) && Count(s) == Capacity && s.Open() &&
              !s.Choose(0) && s.Picker(),
          "native capacity bounds new intents");
    s.Cancel();
    Check(s.Select(0) && s.Choose(8) && Count(s) == Capacity,
          "full native snapshot still permits edit intent");
    s.Cancel();
    native.push_back({"A", 1, 2, 0, 99, 0});
    Check(!s.SetPins(native) && Count(s) == 0 && !s.Busy(),
          "oversized authoritative snapshot fails atomically");
    native = {{"A", 1, 2, 0, 1, 0}, {"A", 3, 4, 2, 1, 0}};
    Check(!s.SetPins(native) && Count(s) == 0, "duplicate identity refused");
    native = {{"A", 1, 2, 0, -1, 0}};
    Check(!s.SetPins(native), "unsaved identity refused");
    native[0].native_id = 1;
    native[0].native_navigation = -1;
    Check(!s.SetPins(native), "unknown navigation refused");
    native[0].native_navigation = 0;
    native[0].kind = 9;
    Check(!s.SetPins(native), "unknown glyph refused");
    native[0].kind = 0;
    native[0].x = NAN;
    Check(!s.SetPins(native), "malformed native coordinate refused");
    Check(s.SetPins({}) && Count(s) == 0, "native empty snapshot valid");
    Arm(s, 1);
    Check(s.Begin(20) && s.Capture(21, 2, 3) && s.TakeRequest() && s.Busy(),
          "executing before clear");
    s.Clear();
    Check(!s.Busy() && !s.Waiting() && !s.Picker() && !s.Armed() &&
              Count(s) == 0,
          "clear hard resets UI shadow");
    Check(NativeImage(0).find("1912900965121370566") != std::string::npos &&
              NativeImage(9).empty() && Image(9).empty(),
          "native glyph bounds");
    std::cout << "native pin shadow tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
