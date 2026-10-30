// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <tuple>
void quick(Fixture& f, bool open) {
  f.put<uint64_t>(Fixture::A+45*8,0x7005000);
  f.put<uint64_t>(0x7005000,Fixture::M+0x2BA1D40);
  f.put<uint64_t>(0x7005150,Fixture::M+(open ? 0x2BA1C10 : 0x2BA1B50));
  f.put<int32_t>(0x7008720,0);
  f.put<int32_t>(0x7008724,0);
  f.put<uint32_t>(Fixture::U+0x64C24,open ? 3 : 0);
  f.put<uint32_t>(Fixture::U+0x64C58,open ? 0x80000 : 0);
  f.put<uint64_t>(Fixture::U+0x64C30,open ? 0x1E09000004ULL : 0x1E09000000ULL);
}
int main() {
  using botw::QuickKind;
  { Fixture f; quick(f,false); auto q=botw::native_quick(f.h); assert(q.valid && q.kind==QuickKind::None); }
  { Fixture f; quick(f,true); auto q=botw::native_quick(f.h); assert(q.valid && q.kind==QuickKind::Weapon); }
  { Fixture f; quick(f,true); f.put<uint32_t>(Fixture::U+0x64C24,4); assert(!botw::native_quick(f.h).valid); }
  { Fixture f; quick(f,true); f.put<uint32_t>(Fixture::U+0x64C58,0); assert(!botw::native_quick(f.h).valid); }
  { Fixture f; quick(f,true); f.put<uint64_t>(0x7005000,0); assert(!botw::native_quick(f.h).valid); }
  { Fixture f; quick(f,true); f.race=Fixture::U+0x64C24; assert(!botw::native_quick(f.h).valid); }
  { Fixture f; quick(f,true); f.mem.erase(0x7005150); assert(!botw::native_quick(f.h).valid); }
  { Fixture f; quick(f,true); f.h.build_id[0]^=1; assert(!botw::native_quick(f.h).valid); }
  for (auto [selector, direction, expected] : std::array{
    std::tuple{1,0x40000,QuickKind::Shield}, std::tuple{2,0x80000,QuickKind::Bow},
    std::tuple{3,0x40000,QuickKind::Arrow}, std::tuple{4,0x10000,QuickKind::Rune}}) {
    Fixture f; quick(f,true); f.put<int32_t>(0x7008720,selector); f.put<int32_t>(0x7008724,selector);
    f.put<uint32_t>(Fixture::U+0x64C58,direction); auto q=botw::native_quick(f.h);
    assert(q.valid && q.kind==expected);
    f.put<uint32_t>(Fixture::U+0x64C58,direction|0x20); q=botw::native_quick(f.h);
    assert(q.valid && q.kind==expected);
  }
  { Fixture f; quick(f,true); f.put<int32_t>(0x7008724,1); auto q=botw::native_quick(f.h); assert(q.valid && q.kind==QuickKind::Pending); }
  { Fixture f; quick(f,true); f.put<uint32_t>(Fixture::U+0x64C58,0x40000); assert(!botw::native_quick(f.h).valid); }
  { Fixture f; quick(f,true); f.put<uint32_t>(Fixture::U+0x64C58,0xC0000); assert(!botw::native_quick(f.h).valid); }
  for (uint32_t directions:{0x50000u,0x90000u,0xC0000u,0xD0000u}) {
    // Multi-direction input must not preserve an armed hold during a cleared
    // or mismatched category pair, or during World-to-shortcut entry.
    Fixture f;quick(f,true);f.put<uint32_t>(Fixture::U+0x64C58,directions);
    for (int selector_copy:{-1,1}) {
      f.put<int32_t>(0x7008724,selector_copy);
      assert(!botw::native_quick(f.h).valid);
    }
    quick(f,false);f.put<uint32_t>(Fixture::U+0x64C58,directions);
    assert(!botw::native_quick(f.h).valid);
  }
  { Fixture f; quick(f,true); f.race=0x7008720; assert(!botw::native_quick(f.h).valid); }
  { Fixture f; quick(f,true); f.put<uint64_t>(Fixture::U+0x64C30,0x1E09000000ULL); auto q=botw::native_quick(f.h); assert(q.valid && q.kind==QuickKind::Pending); }
  const botw::NativeQuick neutral{true,QuickKind::None}, held{true,QuickKind::Weapon};
  { botw::QuickOverlay s; s.update(true,neutral); s.update(true,held); assert(s.shown==QuickKind::Weapon); s.update(true,neutral); assert(s.shown==QuickKind::None); }
  { botw::QuickOverlay s; s.update(true,held); assert(s.shown==QuickKind::None); }
  { botw::QuickOverlay s; s.update(true,neutral); s.update(false,held); s.update(true,held); assert(s.shown==QuickKind::None); s.update(true,neutral); s.update(true,held); assert(s.shown==QuickKind::Weapon); }
  { botw::QuickOverlay s; s.update(true,neutral); s.update(true,held); s.update(true,{}); assert(s.shown==QuickKind::None); s.update(true,held); assert(s.shown==QuickKind::None); }
  { Fixture f; quick(f,false); f.put<uint32_t>(Fixture::U+0x64C58,0x80000); auto q=botw::native_quick(f.h); assert(q.valid && q.kind==QuickKind::Pending); }
  { Fixture f; quick(f,true); f.put<uint64_t>(0x7005150,Fixture::M+0x2BA1B50); auto q=botw::native_quick(f.h); assert(q.valid && q.kind==QuickKind::Pending); }
  { botw::QuickOverlay s; const botw::NativeQuick pending{true,QuickKind::Pending}; s.update(true,pending); s.update(true,held); assert(s.shown==QuickKind::None); s.update(true,neutral); s.update(true,pending); assert(s.shown==QuickKind::None && s.armed); s.update(true,held); assert(s.shown==QuickKind::Weapon); }
  {
    botw::QuickOverlay s;const botw::NativeQuick pending{true,QuickKind::Pending};
    s.update(true,neutral);s.update(true,held);
    // A tap changes the native cursor; it does not consume the physical hold.
    // Repeated samples and a pending category transition retain that hold.
    for(unsigned i=0;i<4;++i){s.update(true,held);assert(s.armed && s.shown==QuickKind::Weapon);}
    s.update(true,pending);assert(s.armed && s.shown==QuickKind::None);
    s.update(true,{true,QuickKind::Shield});assert(s.armed && s.shown==QuickKind::Shield);
    s.update(true,pending);assert(s.armed && s.shown==QuickKind::None);
    s.update(true,{true,QuickKind::Rune});assert(s.armed && s.shown==QuickKind::Rune);
    s.update(true,neutral);assert(s.armed && s.shown==QuickKind::None);
    s.update(true,{true,QuickKind::Bow});assert(s.shown==QuickKind::Bow);
  }
  {
    botw::QuickOverlay s;const botw::NativeQuick pending{true,QuickKind::Pending};
    // Neither pre-held input nor a Pending transition arms an unseen hold.
    s.update(true,pending);s.update(true,held);assert(!s.armed && s.shown==QuickKind::None);
    s.update(true,neutral);s.update(true,held);assert(s.shown==QuickKind::Weapon);
    s.update(false,pending);assert(!s.armed && s.shown==QuickKind::None);
    s.update(true,pending);s.update(true,held);assert(!s.armed && s.shown==QuickKind::None);
    s.update(true,neutral);s.update(true,held);assert(s.shown==QuickKind::Weapon);
    s.update(true,{});s.update(true,pending);s.update(true,held);
    assert(!s.armed && s.shown==QuickKind::None);
  }
  {
    Fixture f;quick(f,true);f.put<int32_t>(0x7008720,-1);f.put<int32_t>(0x7008724,-1);
    const auto q=botw::native_quick(f.h);assert(q.valid && q.kind==QuickKind::Pending);
    f.put<uint32_t>(Fixture::U+0x64C58,0);assert(!botw::native_quick(f.h).valid);
    quick(f,false);const auto released=botw::native_quick(f.h);
    assert(released.valid && released.kind==QuickKind::None);
  }
  { Fixture f; State s; s.quick.armed=true; tick(&s,&f.h); assert(!s.quick.armed); }
  { Fixture f; State s; s.quick.armed=true; f.h.get_i64=[](void*,const char*,int64_t)->int64_t{return 1;}; tick(&s,&f.h); assert(s.quick.armed); }
  std::cout << "native quick-selector bounds/race/scope/release/category-transition/pre-held/hidden cases passed\n";
}
