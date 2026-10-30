# Farming Simulator Nintendo Switch Edition presentation

Accepted A, desktop companion,1240×1080. The owner's readability revision02 uses the game's
native SDF font at32px for meaningful body text and38px for headings/primary values. At the
actual816×710 desktop fit these become approximately21px/25px. Native DDS/font/localization
and Help are decoded from the player's merged RomFS; extracted game assets are not distributed.
`gen_manifest.py` emits independent touch regions matching the visible composition.

Operations retains the dominant map and current-machine HUD. Prices shows four crop columns,
with horizontal browsing across every native crop and vertical selling-point scrolling. The
Farm Silo row remains pinned. Native confirmed nonacceptance is blank; unknown quotes are
dashes; verified zero remains zero. Finance shows three readable day columns at a time, with
explicit range controls reaching all five native history days. Currency appears in the table
heading. Native labels, money/loan/balance summaries and all rows remain available. Statistics
retains separate Session/Total columns and native duration strings in larger scrollable rows.

Animals selects one species through large native art/name tabs. Overview preserves count,
productivity, next birth, reproduction interval, outputs and cleanliness. Feeding presents
full-width32px resource labels, values/capacities/gauges and the complete native nutrition text.
The initial six-resource pig view shows complete rows. Full paragraph wrapping determines the
bounded scroll end; species/detail changes reset the local offset and invalidate stale measured
bounds. Missing values never inherit prior-world values.

Shop provides six category/brand filters and four large item cards per browse view. Selecting
an item opens a separate readable detail view with native art/brand, price and scrollable specs;
Back returns to the preserved browse state. Vehicles shows four owned/leased instances and
independent full detail selection. Compact model names may ellipsize; full selected names wrap
at38px. Store art fits its native alpha bounds with two source-pixel padding. Native attribute
icons match source Shop roles. Forty ordinary categories from41 native category records and85
brand records are reader-supported; the special Sales contract remains withheld. The41-category,
80-brand and301-item check lists are synthetic reachability fixtures, not runtime claims.

The chooser uses source-proven Shop/Garage/Help glyphs and native map/crop/animal art, centered
with their labels. Finance/Statistics stay text-only because dedicated native page glyphs have
not been proved. The Internet globe, benchmark chart and Sell basket are not navigation icons.
Footer Shop uses the native building glyph; only the currently displayed Map/Shop/Menu page
is highlighted. Management pages do not falsely select Menu. Native arrows and complete+/-
symbols are centered in their actual touch rectangles; Centre is an explicit map action label.

English Help preserves all8 categories,61 topics,121 text items and73 illustrations in source
order, with six readable topics per list page and bounded article scrolling. Image proportions
follow native helpLineImage584×146 and XML heightScale; all73 DDS ratios already incorporate
that scale, so it must not be applied twice. Title/loading hide farm navigation. Static Help
opened without a farm has one Back footer returning locally to the title. No-world management
navigation refuses and world footer hit regions require fs.world.ready.

All live values come from farming_simulator_reader::Reader. Zero-decimal money floors as the
native formatter does, including negatives; owned maintenance retains two decimals/currency
per day. Operations fuel floors litres without changing precise snapshot data. There are no
purchase, loan, sale or guest-button actions. Instant game calls (Garage Enter/Reset, Map
Visit/Tag place) run the game's own Lua on the game thread through the guest load plan
(`gen_load_plan.py`, `farming_simulator_game_calls.h`; the LuaJIT C API is named and checked by
`lua_symbols.py`); they are reachable only from TEMP console actions until a design pass adds
buttons. Fixtures are compiled only into the standalone check, never the production module.

## Build and private checks

```sh
cmake -S native/modules -B /private/build/farming-simulator -G Ninja \
  -DEDEN_SOURCE_ROOT=/absolute/path/to/eden-duo -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build /private/build/farming-simulator --parallel 3 --target \
  dsmod-farming-simulator farming-simulator-presentation-check
python3 tools/farming-simulator/gen_load_plan.py --verify <exefs/main>
python3 tools/farming-simulator/gen_manifest.py
/private/build/farming-simulator/farming-simulator-presentation-check \
  /private/owned-romfs /private/preview-output
```

The output directory must exist. With owned assets the check validates native font/DDS/XML,
complete Help counts, readiness refusal, visible world-footer text, independent local navigation,
all crop/day/row/spec/catalog/owned-vehicle bounds, measured animal content ends and relevant
image revision keys. It produces explicitly synthetic PPMs for private visual review. Revision02
is inspected at1240×1080 and816×710, including nine-digit negative money and silo amounts,
long model/category names, unavailable/empty states and article/resource bottoms.

The module owns no worker threads. Sampling copies state under a short mutex; asset/font work
runs under a separate render mutex. Routine visible-value image revisions are limited to5Hz;
local actions/readiness changes can update immediately. Eight recent immutable state revisions
are retained. Asset images are bounded by the96MiB module LRU; the host image cache is64MiB.
A1240×1080 RGBA output is5,356,800bytes. Rendering is on demand; no full-page60Hz composition.
Physical-device and Android validation remain pending. Desktop fixture timing does not establish
emulator/device frame cost or live-data correctness.
