// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/mods/dsmod_module_abi.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace farming_simulator_reader {
// Build-pinned native density accessors: registry +32x32 compressed blocks.
// Every sample is copied, bounded and checked against its live owner pointer.
class DensityMemory {
  static constexpr size_t MaxPages=8192;
  const EdenDsmodHostApi &host;
  std::unordered_map<uint64_t, std::array<uint8_t,4096>> pages;
public:
  explicit DensityMemory(const EdenDsmodHostApi &h):host(h){}
  void Read(uint64_t address, void *out, size_t size) {
    auto *p=static_cast<uint8_t*>(out);
    if (!address || size>262144 || address+size<address) throw std::runtime_error("density range");
    while(size) {
      auto page=address&~uint64_t(4095);
      auto it=pages.find(page);
      if(it==pages.end()) {
        if(pages.size()>=MaxPages)
          throw std::runtime_error("density page budget pages="+
              std::to_string(pages.size())+" address="+std::to_string(page));
        if(!host.is_mapped || !host.read_memory ||
           !host.is_mapped(host.userdata,page,4096))
          throw std::runtime_error("density page unmapped address="+
              std::to_string(page));
        std::array<uint8_t,4096> bytes;
        if(!host.read_memory(host.userdata,page,bytes.data(),bytes.size()))
          throw std::runtime_error("density page read failed address="+
              std::to_string(page));
        it=pages.emplace(page,std::move(bytes)).first;
      }
      size_t n=std::min<size_t>(size,4096-(address-page));
      std::memcpy(p,it->second.data()+address-page,n);p+=n;address+=n;size-=n;
    }
  }
  template<class T>T Get(uint64_t a){T v{};Read(a,&v,sizeof(v));return v;}
  template<class T>bool Same(uint64_t a,const T &expected) const {
    T v{};return host.is_mapped(host.userdata,a,sizeof(T)) &&
      host.read_memory(host.userdata,a,&v,sizeof(T)) && std::memcmp(&v,&expected,sizeof(T))==0;
  }
  uint64_t Entity(uint32_t id, uint64_t required_type = 0x800) {
    uint64_t manager=host.main_base+0x46c918;
    auto buckets=Get<uint64_t>(manager),count=Get<uint64_t>(manager+8);
    if(!id || !count || count>1048576)throw std::runtime_error("density registry");
    auto pred=Get<uint64_t>(buckets+(id%count)*8);
    if(!pred)throw std::runtime_error("density entity absent");
    auto p=Get<uint64_t>(pred);
    for(int i=0;i<256 && p;++i) {
      auto hash=Get<uint64_t>(p+8);
      if(hash%count!=id%count)break;
      if(hash==id && Get<uint32_t>(p+16)==id) {
        auto node=Get<uint64_t>(p+24);
        auto type=Get<uint64_t>(node+16);
        if(!(type&required_type) || !Same(manager,buckets) ||
           !Same(manager+8,count) || !Same(buckets+(id%count)*8,pred) ||
           !Same(pred,p) || !Same(p+24,node) || !Same(node+16,type))break;
        return node;
      }
      p=Get<uint64_t>(p);
    }
    throw std::runtime_error("density entity invalid");
  }
  struct Transform {
    double x{}, y{}, z{}, heading{};
  };
  static std::array<float,16> Multiply(const std::array<float,16> &a,
                                       const std::array<float,16> &b) {
    std::array<float,16> out{};
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)
      for(unsigned k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];
    return out;
  }
  // Read-only counterpart of the scene updater. It composes local matrices
  // from a bounded parent chain and never calls the game or clears dirty bits.
  Transform WorldTransform(uint32_t id) {
    uint64_t node=Entity(id,0x80);
    std::vector<std::array<float,16>> locals;
    std::vector<std::pair<uint64_t,uint64_t>> parents;
    std::set<uint64_t> seen;
    for(unsigned depth=0;node && depth<64;++depth) {
      if(!seen.insert(node).second)throw std::runtime_error("scene parent cycle");
      auto parent=Get<uint64_t>(node+0x20);
      std::array<float,16> local{};Read(node+0x78,local.data(),64);
      if(!Same(node+0x20,parent) || !Same(node+0x78,local))
        throw std::runtime_error("scene transform changed");
      for(auto v:local)if(!std::isfinite(v))throw std::runtime_error("scene transform nonfinite");
      locals.push_back(local);parents.emplace_back(node,parent);node=parent;
    }
    if(node)throw std::runtime_error("scene parent depth");
    if(locals.empty())throw std::runtime_error("scene transform absent");
    auto world=locals.back();
    for(size_t i=locals.size()-1;i>0;--i)world=Multiply(world,locals[i-1]);
    for(auto [child,parent]:parents)if(!Same(child+0x20,parent))
      throw std::runtime_error("scene parent changed");
    for(auto v:world)if(!std::isfinite(v))throw std::runtime_error("scene world nonfinite");
    // Native matrices are column-major; translation is +0xE8..+0xF0 and
    // the forward (+Z) basis is elements 8..10. atan2(x,z) matches the map's
    // positive-clockwise playerRotation convention.
    return {world[12],world[13],world[14],std::atan2(world[8],world[10])};
  }
  uint64_t Map(uint64_t node) {
    if(Get<uint8_t>(node+0x40)&64)throw std::runtime_error("density transform dirty");
    // Current maps use an unrotated, untranslated 2048m density world.
    constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<float,16> transform{};Read(node+0xb8,transform.data(),64);
    if(transform!=identity || Get<float>(node+0x150)!=16 || Get<float>(node+0x6e0)!=32)
      throw std::runtime_error("density transform unsupported");
    auto holder=Get<uint64_t>(node+0x6d8),map=Get<uint64_t>(holder+0x80);
    if(!Same(node+0x6d8,holder) || !Same(holder+0x80,map))throw std::runtime_error("density owner changed");
    return map;
  }
  std::vector<uint16_t> Grid(uint64_t map) {
    auto power=Get<uint32_t>(map),edge=Get<uint32_t>(map+0x1c)+1,
         planes=Get<uint32_t>(map+0x38);
    if(edge!=4096 || power!=7 || planes<1 || planes>2)throw std::runtime_error("density dimensions");
    auto pointer=Get<uint64_t>(map+0x28);
    std::vector<uint64_t> blocks(128*128*planes);Read(pointer,blocks.data(),blocks.size()*8);
    struct Decoded {std::array<uint16_t,16> samples;};
    std::unordered_map<uint64_t,Decoded> decoded;
    std::vector<uint16_t> result(512*512);
    for(unsigned bz=0;bz<128;++bz)for(unsigned bx=0;bx<128;++bx)for(unsigned plane=0;plane<planes;++plane) {
      auto offset=(bz*128+bx)*planes+plane;auto block=blocks[offset];
      auto mask=Get<uint32_t>(map+0x40+plane*8),shift=Get<uint32_t>(map+0x3c+plane*8);
      if(mask>255 || shift>8)throw std::runtime_error("density channels");
      auto hit=decoded.find(block);
      if(hit==decoded.end()) {
        Decoded values{};auto fn=Get<uint64_t>(block);auto relative=fn-host.main_base;
        auto bits=Get<uint8_t>(block+0x10);
        if(relative!=0x2842ac && relative!=0x2842bc && relative!=0x2842e8 && relative!=0x284c40)
          throw std::runtime_error("density accessor");
        if(relative==0x284c40 && (bits<1 || bits>16))throw std::runtime_error("density raw bits");
        for(unsigned z=0;z<4;++z)for(unsigned x=0;x<4;++x) {
          auto index=(z*8+4)*32+x*8+4;uint16_t value{};
          if(relative==0x2842ac)value=Get<uint16_t>(block+0x14);
          else if(relative==0x284c40) {
            // bits was validated 1..16 above, but the raw load is a uint32_t shifted into
            // place -- mask it to the validated bit width before narrowing to uint16_t, or a
            // read that straddles into the next sample's bits (or the load's own upper garbage
            // once bit%8 shifts fewer than 32-bits bits out) corrupts this value.
            auto bit=index*bits;auto mask=(bits>=32?0xffffffffu:(1u<<bits)-1u);
            value=uint16_t((Get<uint32_t>(block+0x1c+bit/8)>>(bit%8))&mask);
          } else {
            auto b=relative==0x2842bc?1u:2u;
            auto palette=(Get<uint8_t>(block+0x1c+index*b/8)>>(index*b%8))&((1<<b)-1);
            value=Get<uint16_t>(block+0x14+palette*2);
          }
          values.samples[z*4+x]=value;
        }
        if(!Same(block,fn))throw std::runtime_error("density block changed");
        hit=decoded.emplace(block,std::move(values)).first;
      }
      if(!Same(pointer+offset*8,block))throw std::runtime_error("density block replaced");
      for(unsigned z=0;z<4;++z)for(unsigned x=0;x<4;++x)
        result[(bz*4+z)*512+bx*4+x]|=(hit->second.samples[z*4+x]&mask)<<shift;
    }
    if(!Same(map+0x28,pointer))throw std::runtime_error("density map replaced");
    return result;
  }
};
} // namespace farming_simulator_reader
