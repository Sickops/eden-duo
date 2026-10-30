// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_detail_flow.h"
#include <cassert>
#include <iostream>
int main(){
 namespace fl=botw::flow;unsigned n{};
 // Quantised widths: 16 px steps, clamped to the manifest's well family (32..448).
 assert(fl::quantise(1)==32&&fl::quantise(32)==32&&fl::quantise(33)==48&&fl::quantise(448)==448&&fl::quantise(900)==448);++n;
 // Centring: margins differ by at most 1 px for every natural width.
 for(int natural=16;natural<=448;++natural){
  const int w=fl::quantise(natural),off=fl::centre_offset(natural,w);
  const int left=off,right=w-natural-off;assert(left>=0&&right>=0&&right-left<=1);
 }++n;
 // Flow: boxes left to right with an 8 px gap; a box that does not fit wraps to `left`.
 {fl::Flow f(816,1208,860);auto a=f.place(64),b=f.place(208);assert(a.x==860&&b.x==932&&b.row==0);
  auto c=f.place(240);assert(c.row==1&&c.x==816&&f.rows()==2);}++n;
 // The first box on a row never wraps, even when wider than the row.
 {fl::Flow f(816,1208,816);auto a=f.place(448);assert(a.row==0&&a.x==816);}++n;
 // newline() starts an empty row; the next box stays on it.
 {fl::Flow f(816,1208,816);f.place(352);f.newline();auto b=f.place(400);assert(b.row==1&&b.x==816);}++n;
 // Rows never overlap: a row's well ends 8 px before the next row starts.
 assert(fl::kPitch-fl::kHeight==fl::kGap&&fl::kGap==8);++n;
 std::cout<<n<<" native well box-model / flow cases passed\n";
}
