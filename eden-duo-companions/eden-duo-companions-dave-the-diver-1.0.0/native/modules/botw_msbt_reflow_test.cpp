// SPDX-License-Identifier: GPL-3.0-or-later
// Iteration 8 item 11 (= F6): host test for the reflow rule + the fetch/decode/cache path.
#include "botw_msbt_reflow.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
static unsigned checks;
#define CHECK(x) do{assert(x);++checks;}while(0)

using namespace botw::text_reflow;

// Minimal LMS "MsgStdBn" (LBL1+TXT2) writer -- just enough to round-trip one label through
// ParseLabels()/ToUtf8(), independent of any real romfs file.
static std::vector<uint8_t> BuildMsbt(const std::string& label, const std::u16string& text) {
  std::vector<uint8_t> lbl1_body;
  auto put32=[&](std::vector<uint8_t>& v,uint32_t x){for(int i=0;i<4;++i)v.push_back(uint8_t(x>>(8*i)));};
  auto put16=[&](std::vector<uint8_t>& v,uint16_t x){for(int i=0;i<2;++i)v.push_back(uint8_t(x>>(8*i)));};
  put32(lbl1_body,1); // 1 bucket
  put32(lbl1_body,1); // count in bucket 0
  put32(lbl1_body,4+8); // offset of the bucket's entries, right after the bucket table (4+8)
  lbl1_body.push_back(uint8_t(label.size()));
  for(char c:label) lbl1_body.push_back(uint8_t(c));
  put32(lbl1_body,0); // text index 0
  while(lbl1_body.size()%16) lbl1_body.push_back(0xAB);

  std::vector<uint8_t> txt2_body;
  put32(txt2_body,1); // 1 text
  put32(txt2_body,4+4); // offset of text 0, right after the offset table
  for(char16_t c:text){ put16(txt2_body,uint16_t(c)); }
  put16(txt2_body,0); // NUL terminator
  while(txt2_body.size()%16) txt2_body.push_back(0);

  std::vector<uint8_t> out(0x20,0);
  std::memcpy(out.data(),"MsgStdBn",8);
  out[8]=0xFF;out[9]=0xFE; // little-endian BOM marker byte pair used by the header check
  out[12]=1; // encoding: UTF-16
  out[14]=2;out[15]=0; // 2 sections
  auto append_section=[&](const char* magic,const std::vector<uint8_t>& body){
    size_t at=out.size();
    out.resize(at+0x10);
    std::memcpy(out.data()+at,magic,4);
    uint32_t size=uint32_t(body.size());
    for(int i=0;i<4;++i) out[at+4+i]=uint8_t(size>>(8*i));
    out.insert(out.end(),body.begin(),body.end());
  };
  append_section("LBL1",lbl1_body);
  append_section("TXT2",txt2_body);
  return out;
}

// Multi-label variant (item 4 fallback-chain tests need a "_Desc" + "_MenuDesc" pair in the
// SAME fake file, the same shape botw_PlayerItem.msbt actually has).
static std::vector<uint8_t> BuildMsbtMulti(const std::vector<std::pair<std::string,std::u16string>>& entries) {
  auto put32=[&](std::vector<uint8_t>& v,uint32_t x){for(int i=0;i<4;++i)v.push_back(uint8_t(x>>(8*i)));};
  auto put16=[&](std::vector<uint8_t>& v,uint16_t x){for(int i=0;i<2;++i)v.push_back(uint8_t(x>>(8*i)));};
  std::vector<uint8_t> lbl1_body;
  put32(lbl1_body,1); // 1 bucket holding every label (fine for a handful of test entries)
  put32(lbl1_body,uint32_t(entries.size()));
  put32(lbl1_body,4+8);
  for(size_t i=0;i<entries.size();++i){
    const auto& label=entries[i].first;
    lbl1_body.push_back(uint8_t(label.size()));
    for(char c:label) lbl1_body.push_back(uint8_t(c));
    put32(lbl1_body,uint32_t(i));
  }
  while(lbl1_body.size()%16) lbl1_body.push_back(0xAB);

  std::vector<uint8_t> txt2_body;
  put32(txt2_body,uint32_t(entries.size()));
  std::vector<uint32_t> offsets(entries.size());
  uint32_t cursor=4+4*uint32_t(entries.size());
  for(size_t i=0;i<entries.size();++i){
    offsets[i]=cursor;
    cursor+=uint32_t(entries[i].second.size()*2+2);
  }
  for(const auto off:offsets) put32(txt2_body,off);
  for(const auto& [label,text]:entries){
    (void)label;
    for(char16_t c:text) put16(txt2_body,uint16_t(c));
    put16(txt2_body,0);
  }
  while(txt2_body.size()%16) txt2_body.push_back(0);

  std::vector<uint8_t> out(0x20,0);
  std::memcpy(out.data(),"MsgStdBn",8);
  out[8]=0xFF;out[9]=0xFE;
  out[12]=1;
  out[14]=2;out[15]=0;
  auto append_section=[&](const char* magic,const std::vector<uint8_t>& body){
    size_t at=out.size();
    out.resize(at+0x10);
    std::memcpy(out.data()+at,magic,4);
    uint32_t size=uint32_t(body.size());
    for(int i=0;i<4;++i) out[at+4+i]=uint8_t(size>>(8*i));
    out.insert(out.end(),body.begin(),body.end());
  };
  append_section("LBL1",lbl1_body);
  append_section("TXT2",txt2_body);
  return out;
}

int main(){
  // ---- Reflow(): representative + ambiguous cases from the sweep ----
  CHECK(Reflow("one line")=="one line");
  CHECK(Reflow("A regional dish made by steaming near-\nripened fruits.")==
        "A regional dish made by steaming near-ripened fruits."); // hyphen break: no space
  CHECK(Reflow("This horn is hard—so hard that\nmost blades cannot scratch it.")==
        "This horn is hard—so hard that most blades cannot scratch it."); // em dash: no space
  CHECK(Reflow("Years of going unpicked have allowed this\nhearty truffle to grow large.")==
        "Years of going unpicked have allowed this hearty truffle to grow large."); // plain join
  CHECK(Reflow("A famous sword.\nIt is said to dance.")=="A famous sword.\nIt is said to dance.");
  CHECK(Reflow("A quote: \"It ends here.\"\nNext paragraph.")==
        "A quote: \"It ends here.\"\nNext paragraph."); // closer after terminator: kept
  CHECK(Reflow("This rock will point toward retrieval.'\"\nFinish.")==
        "This rock will point toward retrieval.'\"\nFinish."); // nested closers after '.': kept
  CHECK(Reflow("Its weight can knock enemies'\nshields out of their hands.")==
        "Its weight can knock enemies' shields out of their hands."); // apostrophe, not a closer-after-terminator: joined
  CHECK(Reflow("One hint:\n-The first clue\n-The second clue")==
        "One hint:\n-The first clue\n-The second clue"); // ':' already keeps; bullet also keeps
  CHECK(Reflow("Paragraph one.\n\nParagraph two.")=="Paragraph one.\n\nParagraph two."); // blank line kept
  CHECK(Reflow("水のある場所に\n氷の柱")==
        "水のある場所に氷の柱"); // CJK: no space
  CHECK(Reflow("First step\n1. Second step")=="First step\n1. Second step"); // next line starts a numbered list: kept
  CHECK(Reflow("Ingredients\n- apple\n- egg")=="Ingredients\n- apple\n- egg"); // next line starts a bullet: kept (both breaks)

  // ---- coordinator refinement 2026-10-18: short-line + banner/bracket rules ----
  // Sample 1 (Animal_Insect_I_Desc, wrap_width 48): both lines before the break are
  // near-full-width (45, 48 chars) even though each ends in a period -- a wrap artifact, not a
  // real paragraph end, so BOTH joins now happen (the original rule wrongly kept the first).
  CHECK(Reflow("This rare dragonfly only appears in the rain.\n"
               "Its wings direct electricity away from its body.\n"
               "Cook it with monster parts for an electricity-\n"
               "resistance elixir.", 48) ==
        "This rare dragonfly only appears in the rain. "
        "Its wings direct electricity away from its body. "
        "Cook it with monster parts for an electricity-resistance elixir.");
  // A SHORT sentence-ending line (well under 0.7*wrap_width) still keeps its break.
  CHECK(Reflow("A short note.\nThe next paragraph starts here.", 48) ==
        "A short note.\nThe next paragraph starts here.");
  // wrap_width <= 0 (unknown alias / no measured lines): falls back to "always keep" (the
  // pre-refinement behaviour), never guesses a width.
  CHECK(Reflow("This rare dragonfly only appears in the rain.\nIts wings direct electricity.", 0) ==
        "This rare dragonfly only appears in the rain.\nIts wings direct electricity.");
  // quest_Find_4Relic#QL_Find_4Relic_1stClear (wrap_width 44): the hyphen-bulleted list has no
  // space after each dash but is still recognised as a bullet, so every "-The Divine Beast..."
  // line stays on its own line; the trailing non-bulleted sentence still joins normally.
  CHECK(Reflow("There are four Divine Beasts:\n"
               "-The Divine Beast Vah Rudania of the Gorons\n"
               "-The Divine Beast Vah Medoh of the Rito\n"
               "\n"
               "You've beaten the phantom of Ganon haunting\n"
               "one of the Divine Beasts.", 44) ==
        "There are four Divine Beasts:\n"
        "-The Divine Beast Vah Rudania of the Gorons\n"
        "-The Divine Beast Vah Medoh of the Rito\n"
        "\n"
        "You've beaten the phantom of Ganon haunting one of the Divine Beasts.");
  // Rule (d): a banner/divider line stays isolated on both sides, even mid-sentence-looking.
  CHECK(Reflow("Koko's Recipe Corner\n/////\nOne bushel of hot buttered apples:", 40) ==
        "Koko's Recipe Corner\n/////\nOne bushel of hot buttered apples:");
  CHECK(Reflow("Essential gear worn by a traveler.\n[Collaboration: Xenoblade Chronicles 2]\nMore text follows.", 40) ==
        "Essential gear worn by a traveler.\n[Collaboration: Xenoblade Chronicles 2]\nMore text follows.");
  // An ellipsis "..." must never be mistaken for a banner/divider run.
  CHECK(!IsDividerLine("Realm wore a helmet much like this one..."));
  CHECK(!IsDividerLine("hurt you, though...probably."));
  CHECK(IsDividerLine("/////Recipe/////"));
  CHECK(IsBracketedLine("[Collaboration: Xenoblade Chronicles 2]"));
  CHECK(!IsBracketedLine("enemies' shields")); // an apostrophe is not a bracket

  // ---- BuildMsbtPath(): the two deterministic alias shapes the generated table uses ----
  CHECK(botw::BuildMsbtPath("botw_ArmorHead")==
        "romfs:/Pack/Bootup_USen.pack#Message/Msg_USen.product.ssarc#ActorType/ArmorHead.msbt");
  CHECK(botw::BuildMsbtPath("quest_100enemy")==
        "romfs:/Pack/Bootup_USen.pack#Message/Msg_USen.product.ssarc#QuestMsg/QL_100enemy.msbt");
  CHECK(botw::BuildMsbtPath("tracker_dungeon").empty()); // outside the two shapes: fails soft

  // ---- ParseLabels()/ToUtf8(): round-trip a hand-built MSBT, tags stripped ----
  {
    std::u16string text=u"Line one.\nLine two\u000Ejoined.\u000F end.";
    // u000E/u000F stand in for an open/close tag pair below (can't embed raw control codes
    // with real tag framing in a plain literal); build it explicitly instead.
    std::u16string tagged=u"Line one.\nLine two ";
    tagged.push_back(0x0E); tagged.push_back(0); tagged.push_back(0); tagged.push_back(0); // open, group 0 type 0 size 0 (ruby-shaped, dropped)
    tagged += u"tagged.";
    auto bytes=BuildMsbt("Test_Label",tagged);
    auto labels=botw::msbt_detail::ParseLabels(bytes);
    CHECK(labels.count("Test_Label")==1);
    const auto utf8=botw::msbt_detail::ToUtf8(labels.at("Test_Label"));
    CHECK(utf8=="Line one.\nLine two tagged.");
    const auto cleaned=Reflow(utf8);
    CHECK(cleaned=="Line one.\nLine two tagged."); // '.' before the \n keeps the break
  }

  // ---- DescriptionCache::Lookup(): end to end through a fake read_romfs ----
  {
    static std::map<std::string,std::vector<uint8_t>> fake_romfs;
    fake_romfs[botw::BuildMsbtPath("botw_ArmorHead")] =
        BuildMsbt("Armor_015_Head_Desc", u"A cap worn long ago\nthat still smells faintly of smoke.");
    static auto* romfs_ptr=&fake_romfs;
    auto read_romfs=[](void*,const char* path,uint64_t,void* output,size_t size)->size_t{
      const auto it=romfs_ptr->find(path);
      if(it==romfs_ptr->end()) return 0;
      if(output==nullptr) return it->second.size();
      const size_t n=std::min(size,it->second.size());
      std::memcpy(output,it->second.data(),n);
      return n;
    };
    // The hash for "Armor_015_Head" is whatever the generated table carries for that actor --
    // look it up by scanning the table for the alias/label pair rather than re-hashing here
    // (keeps this test independent of the hash function itself).
    uint64_t hash=0; bool found=false;
    for(const auto& r: botw::description_table){
      if(r.alias=="botw_ArmorHead" && r.label=="Armor_015_Head_Desc"){hash=r.hash;found=true;break;}
    }
    CHECK(found);
    botw::DescriptionCache cache;
    const auto& cleaned=cache.Lookup(read_romfs,nullptr,hash);
    CHECK(cleaned=="A cap worn long ago that still smells faintly of smoke.");
    // Cached: a second lookup returns the same object without needing the fake romfs again.
    fake_romfs.clear();
    const auto& cleaned2=cache.Lookup(read_romfs,nullptr,hash);
    CHECK(cleaned2==cleaned);
    // Unknown hash: fails soft to "".
    CHECK(cache.Lookup(read_romfs,nullptr,uint64_t(-1)).empty());
  }

  // ---- item 4 (iteration 9): HasCjk()/StripUpgradeSuffix() ----
  CHECK(!botw::text_reflow::HasCjk("Ancient Sheikah technology."));
  CHECK(botw::text_reflow::HasCjk("\xe6\xb0\xb4\xe3\x81\xae")); // "æ°´ã®" (CJK)
  CHECK(botw::text_reflow::StripUpgradeSuffix("Obj_RemoteBombBall2")=="Obj_RemoteBombBall");
  CHECK(botw::text_reflow::StripUpgradeSuffix("Obj_StopTimerLv2")=="Obj_StopTimer");
  CHECK(botw::text_reflow::StripUpgradeSuffix("Obj_Magnetglove").empty()); // no upgrade suffix
  CHECK(botw::text_reflow::StripUpgradeSuffix("Obj_Item_02").empty()); // digit before trailing '2': not an upgrade suffix

  // ---- item 4: DescriptionCache::Lookup() fallback chain, modelled on the REAL PlayerItem.msbt
  // shape (RE'd from the actual 1.6.0 USen file; see NOTES.md "item 4") ----
  {
    static std::map<std::string,std::vector<uint8_t>> fake_romfs;
    fake_romfs[botw::BuildMsbtPath("botw_PlayerItem")] = BuildMsbtMulti({
      // Obj_IceMaker: "_Desc" is Japanese dev text, "_MenuDesc" is the real English line ->
      // falls forward to MenuDesc.
      {"Obj_IceMaker_Desc", u"水のある場所に照射すると"},
      {"Obj_IceMaker_MenuDesc", u"Activate this ancient Sheikah technology to create an ice pillar."},
      // Obj_RemoteBombBall: "_Desc" empty, "_MenuDesc" clean English.
      {"Obj_RemoteBombBall_Desc", u""},
      {"Obj_RemoteBombBall_MenuDesc", u"Ancient Sheikah explosive technology."},
      // Obj_RemoteBombBall2 ("+" upgrade): neither of its OWN two labels is clean -> falls back
      // to the base actor's (Obj_RemoteBombBall, suffix "2" stripped) MenuDesc.
      {"Obj_RemoteBombBall2_Desc", u""},
      {"Obj_RemoteBombBall2_MenuDesc", u"爆発による破壊を目的とした"},
      // Obj_StopTimerLv2 ("+" upgrade, "Lv2" suffix): same shape, base is Obj_StopTimer.
      {"Obj_StopTimerLv2_Desc", u"照射することで"},
      {"Obj_StopTimerLv2_MenuDesc", u"狙って照射することで"},
      {"Obj_StopTimer_Desc", u"狙って照射することで"},
      {"Obj_StopTimer_MenuDesc", u"Ancient Sheikah technology that can stop an object's motion."},
      // Obj_Magnetglove: every candidate under its OWN alias (Desc/MenuDesc; no upgrade suffix
      // so no base-actor fallback either) is the SAME untranslated Japanese sentence -> last
      // resort falls to the rune-select screen's own message file (AppTool_00 "0010"), which
      // DOES have clean English for this rune (real native text, just a different msbt than the
      // per-item Desc fields -- see BuildMsbtPath()/AppToolFallbackLabel() in the header).
      {"Obj_Magnetglove_Desc", u"金属製の物に照射する"},
      {"Obj_Magnetglove_MenuDesc", u"金属製の物に照射する"},
      // Obj_Camera: same shape (own Desc empty, MenuDesc Japanese) -> same AppTool_00 fallback,
      // "0004".
      {"Obj_Camera_Desc", u""},
      {"Obj_Camera_MenuDesc", u"見ている風景や人物を画像として"},
    });
    fake_romfs[botw::BuildMsbtPath("layout_AppTool_00")] = BuildMsbtMulti({
      {"0010", u"Grab on to metallic objects using the magnetic energy that pours forth from the Magnesis rune."},
      {"0004", u"Pictures created are saved in the album."},
    });
    static auto* romfs_ptr=&fake_romfs;
    auto read_romfs=[](void*,const char* path,uint64_t,void* output,size_t size)->size_t{
      const auto it=romfs_ptr->find(path);
      if(it==romfs_ptr->end()) return 0;
      if(output==nullptr) return it->second.size();
      const size_t n=std::min(size,it->second.size());
      std::memcpy(output,it->second.data(),n);
      return n;
    };
    auto hash_for=[&](const char* label)->uint64_t{
      for(const auto& r: botw::description_table) if(r.alias=="botw_PlayerItem" && r.label==label) return r.hash;
      CHECK(false); return 0; // every label used below must already be a real generated-table entry
    };
    botw::DescriptionCache cache;
    CHECK(cache.Lookup(read_romfs,nullptr,hash_for("Obj_IceMaker_Desc"))==
          "Activate this ancient Sheikah technology to create an ice pillar.");
    CHECK(cache.Lookup(read_romfs,nullptr,hash_for("Obj_RemoteBombBall_Desc"))==
          "Ancient Sheikah explosive technology.");
    CHECK(cache.Lookup(read_romfs,nullptr,hash_for("Obj_RemoteBombBall2_Desc"))==
          "Ancient Sheikah explosive technology."); // base actor's MenuDesc, "2" stripped
    CHECK(cache.Lookup(read_romfs,nullptr,hash_for("Obj_StopTimerLv2_Desc"))==
          "Ancient Sheikah technology that can stop an object's motion."); // base, "Lv2" stripped
    CHECK(cache.Lookup(read_romfs,nullptr,hash_for("Obj_Magnetglove_Desc"))==
          "Grab on to metallic objects using the magnetic energy that pours forth from the Magnesis rune."); // AppTool_00 "0010" last-resort fallback
    CHECK(cache.Lookup(read_romfs,nullptr,hash_for("Obj_Camera_Desc"))==
          "Pictures created are saved in the album."); // AppTool_00 "0004" last-resort fallback
  }

  std::cout<<checks<<" checks passed\n";
  return 0;
}
