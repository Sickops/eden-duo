// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_detail_line.h"
#include <cassert>
#include <iostream>
int main(){
  using namespace botw;
  auto p=detail_line(798,434,108,"Attack Up+",30,true);
  assert(p.fits&&p.stat_x==830&&p.modifier_x==830+MeasureTextPx("108",5)+12);
  assert(p.bonus_x==p.modifier_x+MeasureTextPx("Attack Up+",5)+12);
  auto d=detail_line(798,434,78,"Durability Up+",999,true);
  assert(d.fits&&d.end<=1232&&d.modifier_x>=d.stat_x+MeasureTextPx("78",5)+4);
  auto guard=detail_line(798,434,144,"Guard Up+",54,true);
  assert(guard.fits);
  auto none=detail_line(798,434,14,"",0,false);
  assert(none.end==none.stat_x+MeasureTextPx("14",5));
  auto quantity=detail_line(90,560,9999,"",0,false);assert(quantity.fits);
  auto word=detail_line(90,560,30,"Critical Hit",0,false);
  assert(word.fits&&word.end==word.modifier_x+MeasureTextPx("Critical Hit",5));
  auto long_value=detail_line(90,560,78,"Durability Up+",100000,true);assert(long_value.fits);
  auto unsupported=detail_line(798,434,9999,"Extremely long unsupported localized label",100000,true);
  assert(!unsupported.fits); // report overflow; never shrink essential font or invent values.
  assert(detail_modifier_numeric(0x80000001)&&detail_modifier_numeric(256)&&!detail_modifier_numeric(4));
  // Native shows "Durability Up +" / "++" without a number.
  assert(!detail_modifier_numeric(2)&&!detail_modifier_numeric(0x80000002));
  assert(std::string(detail_modifier_label(0x80000001))=="AddPowerPlus_Name");
  assert(std::string(detail_modifier_label(0x80000002))=="AddLifePlus_Name");
  assert(std::string(detail_modifier_label(999)).empty());
  // Native-language text is measured, not a hard-coded English field width.
  auto short_label=detail_line(90,560,108,"Up",30,true);
  auto native_label=detail_line(90,560,108,"Attack Up+",30,true);
  assert(native_label.bonus_x>short_label.bonus_x);
  assert(MeasureTextPx("Attack Up+",5)==190&&MeasureTextPx("Durability Up+",5)==247);
  std::cout<<"15 detail line semantic, width, native-metric and overflow cases passed\n";
}
