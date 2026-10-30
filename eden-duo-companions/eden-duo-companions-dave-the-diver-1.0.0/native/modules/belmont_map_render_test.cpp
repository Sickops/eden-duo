// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_render.h"
#include <cassert>
#include <cmath>
using namespace belmont_map_render;
int main() {
  belmont_map_fog::Shape box;
  box.kind = belmont_map_fog::ShapeKind::Box;
  box.position = {0, 0};
  box.size = {10, 8};
  assert(ShapeDistance(box, 0, 0) == -4);
  assert(ShapeDistance(box, 5, 0) == 0);
  assert(std::abs(ShapeDistance(box, 8, 8) - 5) < 1e-6);
  belmont_map_fog::Shape triangle;
  triangle.kind = belmont_map_fog::ShapeKind::Quad;
  triangle.vertices = {
      belmont_map_fog::Point{-5, -4}, {5, 4}, {5, -4}, {5, -4}};
  assert(ShapeDistance(triangle, 4, 0) == 0);
  assert(std::abs(ShapeDistance(triangle, 8, -8) - 5) < 1e-6);
  assert(SmoothUnion(0, 0, 3) == -.75f);
  assert(SmoothUnion(0, 4, 3) == 0);
}
