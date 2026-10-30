// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "botw_quick.h"
#include <map>
#include <tuple>
struct Fixture {
  std::map<uint64_t, uint8_t> mem;
  std::map<std::string, int64_t> values;
  std::map<std::string, std::string> texts;
  uint64_t race{};
  unsigned reads{};
  EdenDsmodHostApi h{};
  static constexpr uint64_t M = 0x100000, U = 0x4000000, S = 0x5000000, A = 0x6000000;
  template <class T> void put(uint64_t p, T v) {
    for (size_t i = 0; i < sizeof(v); ++i)
      mem[p + i] = reinterpret_cast<uint8_t *>(&v)[i];
  }
  explicit Fixture(uint64_t flags = 0x1e09000000ULL, bool title = false) {
    h.title_id = 0x01007EF00011E000ULL;
    h.main_base = M;
    h.main_size = 0x3000000;
    h.userdata = this;
    for (unsigned i = 0; i < 32; ++i) {
      auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
      h.build_id[i] = (hex(botw::Build[2*i]) << 4) | hex(botw::Build[2*i+1]);
    }
    h.is_mapped = [](void *p, uint64_t a, uint64_t n) -> EdenDsmodBool {
      auto &f = *static_cast<Fixture *>(p);
      for (uint64_t i = 0; i < n; ++i) if (!f.mem.contains(a+i)) return false;
      return true;
    };
    h.read_memory = [](void *p, uint64_t a, void *b, size_t n) -> EdenDsmodBool {
      auto &f = *static_cast<Fixture *>(p);
      for (size_t i = 0; i < n; ++i) static_cast<uint8_t *>(b)[i] = f.mem.at(a+i);
      if (a == f.race && ++f.reads == 2) static_cast<uint8_t *>(b)[0] ^= 8;
      return true;
    };
    h.publish_i64 = [](void *p, const char *k, int64_t v) { static_cast<Fixture *>(p)->values[k] = v; };
    h.publish_text = [](void *p, const char *k, const char *v) { static_cast<Fixture *>(p)->texts[k] = v; };
    put(M+0x2cbdb80, U); put(M+0x2cc2490, S);
    put(U, M+0x2bb1ab8); put(S, M+0x2bbea98);
    put(U+0x64c30, flags); put<int32_t>(S+0x28, 99); put(S+0x30, A);
    put<uint64_t>(A, 0x7000000); put<uint64_t>(A+8, title ? 0x7001000 : 0);
    put<uint64_t>(A+16, title ? 0 : 0x7002000);
  }
};
