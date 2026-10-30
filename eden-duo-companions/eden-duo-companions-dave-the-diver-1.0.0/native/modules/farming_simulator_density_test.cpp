// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "farming_simulator_density.h"
#include <cassert>
#include <iostream>
using farming_simulator_reader::DensityMemory;
struct Memory {
  std::vector<uint8_t> data = std::vector<uint8_t>(34*1024*1024);
  bool mutate{};
  template<class T> void Put(uint64_t p,T value) { std::memcpy(data.data()+p,&value,sizeof(value)); }
  static EdenDsmodBool Mapped(void *p,uint64_t a,size_t n) {
    auto &m=*static_cast<Memory*>(p);return a && a<m.data.size() && n<=m.data.size()-a;
  }
  static EdenDsmodBool Read(void *p,uint64_t a,void *out,size_t n) {
    auto &m=*static_cast<Memory*>(p);if(!Mapped(p,a,n))return false;
    std::memcpy(out,m.data.data()+a,n);
    if(m.mutate && a==0x20000 && n==8) *static_cast<uint64_t*>(out)=0;
    return true;
  }
};
int main() {
  Memory m; EdenDsmodHostApi h{};h.userdata=&m;h.read_memory=Memory::Read;h.is_mapped=Memory::Mapped;h.main_base=0x100000;
  constexpr uint64_t map=0x10000,pointers=0x20000,constant=0x80000,one=0x81000,two=0x82000,raw=0x83000;
  m.Put<uint32_t>(map,7);m.Put<uint32_t>(map+0x1c,4095);m.Put<uint32_t>(map+0x38,2);m.Put<uint64_t>(map+0x28,pointers);
  m.Put<uint32_t>(map+0x40,15);m.Put<uint32_t>(map+0x3c,0);m.Put<uint32_t>(map+0x48,31);m.Put<uint32_t>(map+0x44,4);
  for(unsigned i=0;i<128*128*2;++i)m.Put<uint64_t>(pointers+i*8,constant);
  m.Put<uint64_t>(constant,h.main_base+0x2842ac);m.Put<uint16_t>(constant+0x14,3);
  m.Put<uint64_t>(one,h.main_base+0x2842bc);m.Put<uint16_t>(one+0x14,7);m.Put<uint16_t>(one+0x16,9);
  m.Put<uint64_t>(two,h.main_base+0x2842e8);for(unsigned i=0;i<4;++i)m.Put<uint16_t>(two+0x14+2*i,10+i);
  m.Put<uint64_t>(raw,h.main_base+0x284c40);m.Put<uint8_t>(raw+0x10,5);
  for(unsigned i=0;i<1024;++i) {
    if(i%3==0)m.data[one+0x1c+i/8]|=1u<<(i%8);
    m.data[two+0x1c+i/4]|=(i%3)<<(i%4*2);
    unsigned v=(i*7+11)%32;
    for(unsigned b=0;b<5;++b)if(v&(1u<<b))m.data[raw+0x1c+(i*5+b)/8]|=1u<<((i*5+b)%8);
  }
  m.Put<uint64_t>(pointers,one);m.Put<uint64_t>(pointers+8,two);m.Put<uint64_t>(pointers+24,raw);
  auto grid=DensityMemory(h).Grid(map);
  for(unsigned z=0;z<4;++z)for(unsigned x=0;x<4;++x) {
    unsigned i=(z*8+4)*32+x*8+4;
    assert(grid[z*512+x]==((i%3==0?9:7)|((10+i%3)<<4)));
    assert(grid[z*512+4+x]==(3|(((i*7+11)%32)<<4)));
  }
  assert(grid.back()==(3|(3<<4)));
  auto refused=[&] { try {DensityMemory(h).Grid(map);} catch(const std::runtime_error&) {return true;} return false; };
  m.mutate=true;assert(refused());m.mutate=false;
  m.Put<uint64_t>(one,0xdeadbeef);assert(refused());
  m.Put<uint32_t>(map+0x38,3);assert(refused());
  DensityMemory budget(h);uint8_t byte{};std::string budget_error;
  for(uint64_t page=1;page<=8193 && budget_error.empty();++page)try {
    budget.Read(page*4096,&byte,1);
  } catch(const std::runtime_error &e) {budget_error=e.what();}
  assert(budget_error.find("density page budget pages=8192 address=")==0);
  DensityMemory unmapped(h);std::string unmapped_error;
  try {unmapped.Read(m.data.size()+4096,&byte,1);}
  catch(const std::runtime_error &e) {unmapped_error=e.what();}
  assert(unmapped_error.find("density page unmapped address=")==0);
  std::cout<<"Density checks passed: constant, palette, packed cross-byte samples, planes, mutation and invalid metadata.\n";
}
