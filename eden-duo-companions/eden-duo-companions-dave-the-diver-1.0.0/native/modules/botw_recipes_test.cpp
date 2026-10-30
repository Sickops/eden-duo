// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_recipes.h"
#include <cassert>
#include <iostream>
static unsigned checks;
#define CHECK(x) do{assert(x);++checks;}while(0)
int main(){
 using namespace botw;const auto& c=recipe_catalog();CHECK(c.size()==138);
 Snapshot s; s.valid=true;s.items={{"Item_Fruit_A",7,5,false,1},{"Item_Material_06",7,1,false,2},{"Item_Meat_06",7,1,false,3},{"Item_Ore_H",7,1,false,4}};
 auto stock=recipe_stock(s);CHECK(stock["Item_Fruit_A"]==5);
 auto apple=std::find_if(c.begin(),c.end(),[](const auto& r){return std::string(r.result)=="Item_Cook_I_07";});CHECK(apple!=c.end());CHECK(recipe_witness(*apple,stock).size()==2);
 CHECK(recipe_witness(*apple,{{"Item_Fruit_A",5}}).empty());CHECK(recipe_witness(*apple,{{"Item_Fruit_A",5},{"Item_Material_06",0}}).empty());
 RecipeRule overlap{"test",0,{{"a","b"},{"a"}}};auto witness=recipe_witness(overlap,{{"a",1},{"b",1}});CHECK(witness.size()==2);CHECK(witness[0]=="b"&&witness[1]=="a");CHECK(recipe_witness(overlap,{{"a",99}}).empty());
 RecipeRule empty{"test",0,{{}}};CHECK(recipe_witness(empty,stock).empty());RecipeRule special{"test",2,{{"Item_Fruit_A"}}};CHECK(recipe_witness(special,stock).empty());
 RecipeRule tooMany{"test",0,{{"a"},{"b"},{"c"},{"d"},{"e"},{"f"}}};CHECK(recipe_witness(tooMany,{{"a",1},{"b",1},{"c",1},{"d",1},{"e",1},{"f",1}}).empty());
 RecipesView v;CHECK(v.visible(s).size()==130);CHECK(v.action("recipes_choose",apple-c.begin(),s));CHECK(v.selected_hold_actors(s).size()==2);CHECK(v.selected_hold_actors(Snapshot{}).empty());Snapshot missing=s;missing.items[1].value=0;CHECK(v.selected_hold_actors(missing).empty());missing.items[1].value=-1;CHECK(v.selected_hold_actors(missing).empty());CHECK(!v.action("recipes_choose",-1,s));CHECK(!v.action("recipes_kind",3,s));CHECK(!v.action("recipes_alternative",-1,s));CHECK(!v.action("recipes_alternative",4,s));CHECK(v.action("recipes_available",0,s));CHECK(v.visible(s).size()<130);CHECK(v.action("recipes_kind",2,s));CHECK(v.visible(s).empty());
 Snapshot bad=s;bad.valid=false;CHECK(recipe_stock(bad).empty());CHECK(v.visible(bad).empty());
 v.kind=0;v.available=false;auto meat=std::find_if(c.begin(),c.end(),[](const auto& r){return std::string(r.result)=="Item_Cook_D_04";});CHECK(meat!=c.end());v.select(size_t(meat-c.begin()),stock);CHECK(v.selected_hold_actors(s).size()==2);CHECK(v.selected_hold_actors(s)[0]=="Item_Meat_06");CHECK(v.action("recipes_alternative",0,s));CHECK(v.selected_hold_actors(s).empty());
 for(const auto& rule:c){CHECK(rule.groups.size()<=5);for(const auto& group:rule.groups){std::set<std::string> unique;for(const auto& a:group){CHECK(!a.empty());CHECK(unique.insert(a).second);}}}
 // Fix round 3 Recipes finding ("~250 px empty between ingredients and hints"): the footer
 // (status/hint) y now follows the selected recipe's actual ingredient-group count instead of
 // a fixed position, so a short recipe's footer sits right under its last visible row.
 {
  std::map<std::string,int64_t> ints;std::map<std::string,std::string> texts;
  EdenDsmodHostApi h{};h.userdata=nullptr;
  // Function pointers (not captureless-lambda-to-pointer-decay-only-if-no-capture) can't close
  // over `ints`/`texts` directly -- route through static pointers (single-threaded test).
  static std::map<std::string,int64_t>* sink_i=&ints;static std::map<std::string,std::string>* sink_t=&texts;
  sink_i=&ints;sink_t=&texts;
  h.publish_i64=[](void*,const char* k,int64_t v){(*sink_i)[k]=v;};
  h.publish_text=[](void*,const char* k,const char* v){(*sink_t)[k]=v;};
  RecipesView v2;v2.kind=0;v2.available=false;
  v2.select(size_t(meat-c.begin()),stock); // Item_Cook_D_04, 2 ingredient groups (asserted above)
  v2.publish(h,s,"recipes");
  CHECK(ints.at("botw.recipes.ingredients.count")==2);
  // Layout batch 2 item 1 (2026-10-19): divider_y/header_y/row_base no longer depend on the
  // description at all (it moved below the ingredient rows, see botw_recipes.h) -- they are the
  // title's own worst-case (2-line) position, ALWAYS, not just in this no-read_romfs host test.
  // divider_y=358, header_y=372, row_base=min(404, 492|450)=404 in every case now.
  CHECK(ints.at("botw.recipes.ingredients.divider_y")==398);
  CHECK(ints.at("botw.recipes.ingredients.header_y")==412);
  CHECK(ints.at("botw.recipes.footer.status_y")==444+100+88+8); // row_base 404
  CHECK(ints.at("botw.recipes.footer.hint_y")==444+100+88+8+26);
  CHECK(ints.at("botw.recipes.ingredient.0.y")==444);CHECK(ints.at("botw.recipes.ingredient.1.y")==544);
  CHECK(ints.at("botw.recipes.ingredients.compact")==0);CHECK(ints.at("botw.recipes.footer.status_visible")==1);
  CHECK(ints.at("botw.recipes.footer.hint_visible")==1);
  // Layout batch 2 item 1: description now sits below hint/status/rows, in what used to be the
  // ~330px dead band above the 944px button row. No read_romfs here, so description_clean is
  // always "" and .visible is always 0 -- but .y is still published (content-aware position,
  // right after the hint line + 16px gap: hint_y 626 + hint cap 20 + 16 = 662) so the manifest
  // widget has somewhere correct to sit even with nothing to show this test run.
  CHECK(ints.at("botw.recipes.description.y")==666+20+16);
  CHECK(ints.at("botw.recipes.description.visible")==0);
  // A no-selection state (empty filtered list, via an invalid pouch + available-only filter)
  // must not crash and should report 0 ingredients.
  RecipesView v3;v3.kind=2;v3.available=true;Snapshot invalid{};invalid.valid=false;
  CHECK(v3.visible(invalid).empty());
  v3.publish(h,invalid,"recipes");
  CHECK(ints.at("botw.recipes.ingredients.count")==0);
  CHECK(ints.at("botw.recipes.footer.status_y")==444+8);
  CHECK(ints.at("botw.recipes.footer.hint_visible")==1);
  // Fix round A: the largest recipe (most groups) keeps status above the 944 footer row and
  // hides the hint only when it would not fit.
  size_t most=0;for(size_t i=0;i<c.size();++i)if(c[i].groups.size()>c[most].groups.size())most=i;
  RecipesView v4;v4.kind=0;v4.available=false;v4.select(most,stock);v4.publish(h,s,"recipes");
  const auto groups=int64_t(c[most].groups.size());
  CHECK(ints.at("botw.recipes.ingredients.count")==groups);
  if(groups<=4)CHECK(ints.at("botw.recipes.footer.status_y")+25<=940);
  // every row (88 px) ends above the 944 footer row in both layouts
  CHECK(ints.at("botw.recipes.ingredient."+std::to_string(groups-1)+".y")+88<=944);
  // i8-layout item 10 (content-aware redo): with no description resolvable (host test has no
  // read_romfs), row_base is 404 even for the 5-group "compact" case -- well under the old
  // fixed worst-case 450, so both status and hint now fit (more headroom, not less: the
  // original "hides when groups==5" assumption was sized for the OLD always-450 row_base, not
  // a universal rule).
  if(groups==5){CHECK(ints.at("botw.recipes.ingredients.compact")==1);CHECK(ints.at("botw.recipes.ingredient.0.y")==444);
   CHECK(ints.at("botw.recipes.ingredient.4.y")==444+4*94);CHECK(ints.at("botw.recipes.footer.status_visible")==0);}
  CHECK(ints.at("botw.recipes.footer.hint_visible")==(ints.at("botw.recipes.footer.hint_y")+20<=940));
  if(groups==5)CHECK(ints.at("botw.recipes.footer.hint_visible")==0);
 }
 // Finishing round (owner item: alternatives pop-up grid instead of cycling one by one).
 {
  std::map<std::string,int64_t> ints;std::map<std::string,std::string> texts;
  static std::map<std::string,int64_t>* ai=nullptr;static std::map<std::string,std::string>* at=nullptr;ai=&ints;at=&texts;
  EdenDsmodHostApi h{};h.publish_i64=[](void*,const char* k,int64_t v){(*ai)[k]=v;};h.publish_text=[](void*,const char* k,const char* v){(*at)[k]=v;};
  // Item_Cook_D_04 = {Item_Meat_01|Item_Meat_06} + {Item_Ore_H}; the pouch holds Meat_06 only.
  RecipesView v;v.kind=0;v.available=false;v.select(size_t(meat-c.begin()),stock);
  CHECK(v.alt_open==-1);
  CHECK(!v.action("recipes_alternatives",1,s));            // one-actor group: nothing to choose
  CHECK(!v.action("recipes_alternatives",2,s));            // no third group
  CHECK(!v.action("recipes_alternatives",-1,s));CHECK(!v.action("recipes_alternatives",5,s));
  CHECK(!v.action("recipes_alternative_pick",0,s));         // closed: picks refused
  CHECK(v.action("recipes_alternatives",0,s));CHECK(v.alt_open==0);
  v.publish(h,s,"recipes");
  CHECK(ints.at("botw.recipes.alts.open")==1);CHECK(ints.at("botw.recipes.alts.slot")==0);CHECK(ints.at("botw.recipes.alts.count")==2);
  // owned first: Meat_06 (catalog index 1) before Meat_01 (catalog index 0, not owned)
  CHECK(texts.at("botw.recipes.alts.0.actor")=="Item_Meat_06");CHECK(ints.at("botw.recipes.alts.0.owned")==1);CHECK(ints.at("botw.recipes.alts.0.count")==1);
  CHECK(texts.at("botw.recipes.alts.1.actor")=="Item_Meat_01");CHECK(ints.at("botw.recipes.alts.1.owned")==0);CHECK(ints.at("botw.recipes.alts.1.count")==0);
  CHECK(ints.at("botw.recipes.alts.0.chosen")==1);CHECK(ints.at("botw.recipes.alts.1.chosen")==0);
  CHECK(ints.at("botw.recipes.alts.0.pick")==1);CHECK(ints.at("botw.recipes.alts.1.pick")==0);
  CHECK(ints.at("botw.recipes.alts.0.name")==actor_key("Item_Meat_06"));
  CHECK(ints.at("botw.recipes.ingredient.0.options")==2);CHECK(ints.at("botw.recipes.ingredient.1.options")==1);
  CHECK(ints.at("botw.recipes.selected.ready")==1);
  // stale/foreign payloads: other slot, out-of-range index, negative
  CHECK(!v.action("recipes_alternative_pick",RecipesView::AltPickStride*1+0,s));
  CHECK(!v.action("recipes_alternative_pick",2,s));CHECK(!v.action("recipes_alternative_pick",-1,s));
  CHECK(v.alt_open==0);
  // pick the missing Meat_01: closes, ingredient row and hold plan follow the choice
  CHECK(v.action("recipes_alternative_pick",0,s));CHECK(v.alt_open==-1);
  CHECK(v.selected_hold_actors(s).empty());
  v.publish(h,s,"recipes");
  CHECK(ints.at("botw.recipes.alts.open")==0);CHECK(ints.at("botw.recipes.alts.count")==0);
  CHECK(texts.at("botw.recipes.ingredient.0.actor")=="Item_Meat_01");CHECK(ints.at("botw.recipes.selected.ready")==0);
  CHECK(texts.at("botw.recipes.selected.status")=="Ingredients missing");
  // re-open: the chosen mark follows the pick; pick Meat_06 back -> hold plan uses it
  CHECK(v.action("recipes_alternatives",0,s));v.publish(h,s,"recipes");
  CHECK(ints.at("botw.recipes.alts.0.chosen")==0);CHECK(ints.at("botw.recipes.alts.1.chosen")==1);
  CHECK(v.action("recipes_alternative_pick",ints.at("botw.recipes.alts.0.pick"),s));
  CHECK(v.selected_hold_actors(s).size()==2);CHECK(v.selected_hold_actors(s)[0]=="Item_Meat_06");
  // close action, and every context change closes the pop-up
  CHECK(v.action("recipes_alternatives",0,s));CHECK(v.action("recipes_alternatives_close",0,s));CHECK(v.alt_open==-1);
  CHECK(v.action("recipes_alternatives",0,s));CHECK(v.action("recipes_kind",0,s));CHECK(v.alt_open==-1);
  CHECK(v.action("recipes_alternatives",0,s));CHECK(v.action("recipes_available",0,s));CHECK(v.alt_open==-1);CHECK(v.action("recipes_available",0,s));
  CHECK(v.action("recipes_alternatives",0,s));CHECK(v.action("recipes_choose",apple-c.begin(),s));CHECK(v.alt_open==-1);
  v.select(size_t(meat-c.begin()),stock);CHECK(v.action("recipes_alternatives",0,s));v.publish(h,s,"inventory");CHECK(v.alt_open==-1);
  // bound: the largest native group (59 actors) publishes every alternative, never more than AltCap
  size_t big=0,big_slot=0,big_n=0;for(size_t i=0;i<c.size();++i){if(c[i].kind==2)continue;for(size_t g=0;g<c[i].groups.size();++g)if(c[i].groups[g].size()>big_n){big=i;big_slot=g;big_n=c[i].groups[g].size();}}
  CHECK(big_n>=2&&big_n<=RecipesView::AltCap);
  size_t listed=0;for(const auto& a:c[big].groups[big_slot])listed+=recipe_alternative_listed(a);big_n=listed;
  CHECK(!recipe_alternative_listed("Item_Enemy_Put_57"));CHECK(!recipe_alternative_listed("Item_Mushroom_D"));CHECK(recipe_alternative_listed("Item_Enemy_57"));
  RecipesView w;w.kind=0;w.available=false;w.select(big,stock);CHECK(w.action("recipes_alternatives",int64_t(big_slot),s));
  w.publish(h,s,"recipes");CHECK(ints.at("botw.recipes.alts.count")==int64_t(big_n));
  std::set<int64_t> picks;for(size_t i=0;i<big_n;++i)picks.insert(ints.at("botw.recipes.alts."+std::to_string(i)+".pick"));CHECK(picks.size()==big_n);
  size_t chosen=0;for(size_t i=0;i<big_n;++i)chosen+=size_t(ints.at("botw.recipes.alts."+std::to_string(i)+".chosen"));CHECK(chosen==1);
  // a pick of the last alternative in that group lands and closes
  const int64_t last=ints.at("botw.recipes.alts."+std::to_string(big_n-1)+".pick");CHECK(w.action("recipes_alternative_pick",last,s));
  CHECK(int64_t(w.alternatives[big_slot])==last%RecipesView::AltPickStride);
  // an unlisted actor's catalog index is refused even with the right slot
  {RecipesView y;y.kind=0;y.available=false;y.select(big,stock);CHECK(y.action("recipes_alternatives",int64_t(big_slot),s));
   const auto& g=c[big].groups[big_slot];for(size_t i=0;i<g.size();++i)if(!recipe_alternative_listed(g[i]))CHECK(!y.action("recipes_alternative_pick",int64_t(big_slot)*RecipesView::AltPickStride+int64_t(i),s));}
  // invalid pouch: list still bounded and every entry reads as not owned
  Snapshot none{};none.valid=false;RecipesView x;x.kind=0;x.available=false;x.select(size_t(meat-c.begin()),{});CHECK(x.action("recipes_alternatives",0,none));
  x.publish(h,none,"recipes");CHECK(ints.at("botw.recipes.alts.count")==2);CHECK(ints.at("botw.recipes.alts.0.owned")==0);CHECK(ints.at("botw.recipes.alts.1.owned")==0);
 }

 // i8-layout item 10: botw_text_measure.h's wrap/line-count port, checked against the SAME
 // three strings independently cross-checked against tools/render-pages.py's real Font class
 // (python3 import of the renderer, same Normal_00.bffnt asset) during this pass -- see
 // NOTES.md "Verification". If the generated font-advance table (botw_font_metrics.h) or this
 // port ever drifts from the renderer, this is the case that catches it.
 {
  using namespace botw;
  CHECK(WrappedLineCount("", 4, 560) == 0);
  CHECK(LabelEffectiveLines("", 4, 560, 4, 3) == 0); // Elixir-style: no description at all
  const std::string apple_desc = "The apple's sweetness has been enhanced by smothering it with "
                                  "butter and baking it.";
  CHECK(WrappedLineCount(apple_desc, 4, 560) == 3); // matches the render-verified 3-line wrap
  CHECK(LabelEffectiveLines(apple_desc, 4, 560, 4, 3) == 3);
  const char* title = "Hot Buttered Apple";
  CHECK(FitTextScale(title, 6, 464, 4) == 6); // fits at full scale, no shrink needed
  CHECK(WrappedLineCount(title, 6, 464) == 1);
  const std::string long_desc =
      "A very long elixir description that definitely wraps across many many lines of text to "
      "stress test the five line cap scenario for the recipes page header geometry computation "
      "logic here.";
  CHECK(WrappedLineCount(long_desc, 4, 560) == 5);       // raw wrap, uncapped
  CHECK(LabelEffectiveLines(long_desc, 4, 560, 4, 3) == 3); // capped at the widget's max_lines
  CHECK(TextBlockHeightPx(4, 3) == 84);
  CHECK(TextBlockHeightPx(6, 1) == 30);
  CHECK(TextBlockHeightPx(6, 2) == 78);
 }
 std::cout<<checks<<" recipe assertions passed\n";
}
