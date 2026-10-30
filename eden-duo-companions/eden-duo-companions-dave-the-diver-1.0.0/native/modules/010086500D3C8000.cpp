// SPDX-License-Identifier: GPL-3.0-or-later
// VAllhalla module for VA-11 Hall-A (010086500D3C8000): tells the companion when the game is in
// dialogue/menus and when it is time to mix, so the bottom screen can show the artist's idle
// overlay and lock the bar until mixing starts. Never writes the game's memory directly: it reads
// guest memory, asks the package to press buttons (va_zr_req, va_zl_req, va_jk_down, va_jk_a) and
// calls a few of the game's built-ins (its sounds, the language, the cursor for a dialogue tap)
// through the guest mailbox of tools/va11-hall-a/gen_load_plan.py, the only memory it stores to.
//
// How it knows: the game keeps three numbers (GameMaker doubles) side by side, at X, X+0x150
// and X+0x340, that read 1,0,0 while talking and 0,1,1 while mixing (found with the VAllhalla
// detective on 13 Oct 2026, base game v0). Heap addresses change between boots, so instead of
// a pointer path the module scans the heap for that triple in the background, keeps every
// match, and trusts the match that most recently flipped between the two patterns. In-between
// readings (the game changes the three across a fade) keep the last state.
//
// Outputs (published every sample):
//   va_overlay  1 = show the idle overlay and block the bar, 0 = hide it
//   va_frame    0/1, the overlay's animation frame at 4 fps
//   va_doodle   0..2, the frame of the game's break doodles (break_skin) on the tutorials
//   va_jill_talk 1 while Jill's dialogue line is typing out; va_jill_face her face (0 neutral,
//               1 smile, 2 surprise, 3 uh, 4 sad, 5 think, 6 baka); va_jill_base / _smile /
//               _eyes / _eyesrc / _lips pick the images that draw it (see JillLipsSrc)
//   va_room     the game's room (game.win ROOM index: 10 jill_room, 11 bar, 12 break_time), -1 unknown
//   va_jill_scene 1 = the bar scene with Jill on the overlay (at work, or someone talks on a break);
//               va_doodles 1 = the stand-by screen instead (home, break screen, anywhere else):
//               va_title_f the title screen's Jill frame (0..3), va_standby_glow the "Please Stand
//               by" pulse (0 dark .. 6 lit), va_on_break 1 on a break (walked out of the bar)
//   va_jk_*     the jukebox mirror while the game's song picker is open (see JukeTick): va_jk_open,
//               _page, _full, _prev/_next, _title, _pages, _song1..5 / _lock1..5 / _pv1..5 (previewing), _slot0..11 /
//               _tag0..11 / _sel0..11 (texts and flags); va_jk_down / va_jk_a ask for a d-pad DOWN /
//               A press while the module walks the game's cursor to Done; va_jk_vis / _w / _close
//               the panel's wipe in (bands 0..27 from the top) and squash out (to the middle)
//   va_dither   0, or 2..16: the pixel dissolve over the overlay while it swaps Jill's bar and
//               the doodles (va_jill_scene / va_doodles change at full cover)
//   va_served / va_served_src  the last drink served stands on Jill's counter (its sprite) until
//               the next order is mixed or she leaves the bar; va_bub<i>_x/_y/_a bubbles rise in it;
//               va_served2 / _src / _shadow / _dy and va_bubr<i>_* the second drink of a two-slot
//               order, right of Jill
//   va_pulse / _t / _scale  a pour's flash round the shaker: the ingredient (1..5), its frame
//               (0..9) and size (x1000), as the game's shaker_effect_1..5
//   va_slot1 / va_slot2  each slot button's look (0 front, 1 back, 2 mixed, 3 out), va_slot_cur
//               the slot at the front (the nights with two customers: global.slotamount 2)
//   va_money    the night's earnings as the game's bar shows them ("$1950"; the tutorial's text)
//   va_egg      1 while the artist's note is open (the heart under Age, tapped 3 times)
//   va_neon     0 steady, 1 dip, 2 deep dip: the bar window's neon flicker behind her (see Neon)
//   va_shadow_l / _r  px offsets of the two copies of Jill's shadow on the counter (left <= 0,
//               right >= 0): each edge stretches out now and then and eases back
//   va_mote<i>_x / _y / _a  (i 0..23) a speck of dust in the light: position in px, brightness 0..3
//   va_mode     -1 not sure yet, 0 talking, 1 mixing
//   va_proven   how many matches have been seen flipping
//   va_status   short text for the test readout
//   va_zr_req   1 while the module wants ZR pressed (the package presses it, then acks)
//   va_zl_req   1 while the module wants ZL (Reset) pressed: only to bring the game in line after
//               a Reset in the middle of a mix
//   va_mix_lock 1 while the shaker shakes by itself (Mix button or ZR in the game): everything but
//               Reset and Stop is blocked, like the game
//   va_tut_bob / va_tut_pulse  the manual-shake tutorial's floating hand (px) and breathing
//               ring (scale x1000); the package decides when the tutorial shows
//   va_saved    1 for ~1 s after the game saved (a tutorial finished this session is then kept)
//   va_loaded   1 for ~1 s after a save was loaded (the tutorials start over unless kept)
//   va_started  1 while a shake-started mix is running in the game
//   va_wiggle   0..11 frame of the artist's shaker loop while mixing (violent once blended)
//   va_jx/va_jy jitter offsets in px for the shaker while the shake is violent (blended)
//   va_angle    shaker tilt in degrees (+ = clockwise): leans the way the finger moves and
//               springs back upright (physics), with a rattle once blended; va_sway is 0 now
// Images: "module:gm:<sprite name>[:<frame>[:<scale>[:swaprg][:pad<X>x<Y>]]]" -> that sprite from the player's
// own game data (never shipped), scaled by an integer factor with nearest neighbour; "swaprg"
// swaps its red and green channels (the green Serve button made from the game's Mix button);
// "keyblack" makes its black transparent (the break doodles over the purple overlay);
// "keyedge" only the black joined to the edge; "shadow<S>" its silhouette as a shadow (see below).
//   va_success_frame  0..26 frame of the artist's Success popup while va_done
//   va_fill     0..100: while shaking, time towards the blend point (5 s of non-stop shaking);
//               full once the shake ends (let go, or the player stopped shaking)
//   va_blended  1 when the drink is (or will be) blended; va_done 1 mixed / 2 blended after
//               a shake-mix ended (shows "Success!!") until the drink is served or Reset
// Actions:
//   skip        the player held a finger on the overlay (emergency): hide it until the mode
//               next changes. A plain tap does nothing: the bar stays locked while talking.
//   busy        pinged by the package while the player uses the bar; keeps an emergency
//               opening alive (it closes after ~4 s without use)
//   shake       every tick the shaker is carried: argument = movement step 1..5 (fast left,
//               left, still, right, fast right). Real back-and-forth motion starts the mix (ZR).
//   mix_btn     the Mix button: starts a mix that shakes by itself (ZR), stops it on the next
//               tap (5 s or more = blended), or serves a finished drink (ZR)
//   shake_end   the shaker was let go: stops the mix (ZR again) if a shake started it
//   zr_done     the package pressed ZR for us (zl_done: ZL)
//   reset       the player pressed Reset: empty the bar and the shaker, stopping a mix too
//   pour        one unit poured (argument 0..4: Adelhyde, Bronson, Delta, Flanergide, Karmotrine)
//   ice_toggle / age_toggle   Ice / Age toggled (pressing X / Y)
//   close_err   the player tapped error window <argument> (0..3) away
// A failed drink pops up 4 error windows (va_err0..3 visible, va_errf0..3 frame: 0 clean,
// 1/2 glitch); va_err_any blocks the bar until all are closed.
// va_grab 1 = the shaker can be picked up (something poured); va_shaker_idle 1 = empty shaker.
// The game's shaker: va_game_ok 1 while the contents are read from the game (any input), then
// va_cnt0..4 (Adelhyde, Bronson, Delta, Flanergide, Karmotrine), va_ice_on, va_age_on.
// After a shake-mix: va_result 1 (a recipe: va_ok) or 2 (failed: va_fail), va_drink_src (the
// drink's game sprite, or the broken glass) and va_drink_name, until served or Reset.
//   mixed       the player pressed Mix: while not sure, bring the overlay back (0.12 behaviour)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>


#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include "va11_gm_reader.h"
#include "va11_jill_faces.h"

// stb_image's implementation, once (gm_reader.h only needs its declarations)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

using namespace dsmod_sdk::int_types;

namespace {

// The liquid in a drink sprite (at scale 1, its black box already cut away by keyedge): the
// pixels inside the drawing (all four neighbours drawn, so not the outline) whose colour is
// a saturated, not too dark one that shows up at least 3 times there. The drinks whose liquid
// doesn't show (the Flaming Moai's stone, the coconuts, the bottled A Fedora) have none.
std::vector<u8> LiquidMask(const std::vector<u8>& px, int w, int h, const std::string& name) {
    std::vector<u8> m(size_t(w) * size_t(h), 0);
    for (const char* no : {"moai", "sunshine", "zenstar", "zen_star", "fedora", "tea"}) // none shows
        if (name.find(no) != std::string::npos) return m;
    const auto at = [&](int x, int y) { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; };
    const auto drawn = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && at(x, y)[3] != 0; };
    const auto good = [](const u8* c) {
        const int mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
        return mx > 76 && mx - mn > mx * 3 / 10; // value > 0.3, saturation > 0.3
    };
    std::map<u32, int> count;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (drawn(x, y) && drawn(x + 1, y) && drawn(x - 1, y) && drawn(x, y + 1) && drawn(x, y - 1) && good(at(x, y)))
                ++count[u32(at(x, y)[0]) << 16 | u32(at(x, y)[1]) << 8 | at(x, y)[2]];
    const auto inner = [&](int x, int y) {
        return drawn(x, y) && drawn(x + 1, y) && drawn(x - 1, y) && drawn(x, y + 1) && drawn(x, y - 1);
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (inner(x, y)) {
                const auto it = count.find(u32(at(x, y)[0]) << 16 | u32(at(x, y)[1]) << 8 | at(x, y)[2]);
                if (it != count.end() && it->second >= 3) m[size_t(y) * size_t(w) + size_t(x)] = 1;
            }
    // the light pixels dithered into it (Moonblast's pale pink, Piano Woman's foam) are liquid
    // too, two steps out, so a bubble rises through a dithered layer; dark ones (outline, the
    // empty top) stay out
    for (int step = 0; step < 2; ++step) {
        std::vector<u8> grown = m;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (m[size_t(y) * size_t(w) + size_t(x)] || !inner(x, y) || std::max({at(x, y)[0], at(x, y)[1], at(x, y)[2]}) <= 76)
                    continue;
                for (const auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && m[size_t(ny) * size_t(w) + size_t(nx)]) {
                        grown[size_t(y) * size_t(w) + size_t(x)] = 1;
                        break;
                    }
                }
            }
        m.swap(grown);
    }
    return m;
}
// The liquid's top row (h if it has none): the bubbles' ceiling.
int LiquidTop(const std::vector<u8>& m, int w, int h) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (m[size_t(y) * size_t(w) + size_t(x)]) return y;
    return h;
}

constexpr u64 kTitleId = 0x010086500D3C8000ULL;
constexpr u64 kPage = 0x1000;
constexpr u64 kS2 = 0x150, kS3 = 0x340;  // switch 2 and 3, from switch 1
constexpr u64 kPagesPerTick = 1024;      // 4 MiB of heap per tick while scanning
constexpr size_t kMaxCands = 20000;
constexpr size_t kCheckPerTick = 2000;   // unproven matches are re-checked round-robin
constexpr int kStrikes = 90;             // ~1.5 s of garbage before a match is dropped
constexpr u64 kRescanProven = 3600;      // ~60 s between background scans once sure
constexpr u64 kRescanUnsure = 600;       // ~10 s while not sure yet
constexpr u64 kFrameTicks = 15;          // 4 fps at 60 ticks/s
constexpr u64 kDoodleTicks = 8;          // break_skin's 3 frames, ~130 ms each as in the game
// Where the switches were in every session so far (heap + offset, base game v0). Checked from
// the first tick and trusted straight away when it reads the pattern, so the very first drink
// does not wait for a scan and a flip. If it is ever wrong, the real match wins on its first
// flip (the most recent flip decides, and a seed that never flips has flipped = 0).
constexpr u64 kSeedOff = 0x97340230;
constexpr u64 kEmergencyIdle = 240;      // an emergency opening closes after ~4 s unused
constexpr u64 kBlendTicks = 300;         // the game blends a drink mixed for 5 s
constexpr int kReversalTicks = 2;        // a direction change counts after this many moving ticks
constexpr u64 kShakeWindow = 50;         // two reversals within ~0.8 s = shaking
constexpr u64 kStopTicks = 40;           // no reversal for ~0.67 s = the player stopped shaking
constexpr u64 kZrGap = 14;               // ticks between two of our ZR presses
// The shaker's contents in the game (found with the bar detective, 14 Oct 2026): doubles at fixed
// offsets from the talking/mixing switch (0x30 apart). Order of counts[]: Adelhyde, Bronson,
// Delta, Flanergide, Karmotrine. 0x990 holds the total, used to check the read.
constexpr u64 kGameCountOff[5] = {0x960, 0x930, 0x900, 0x8D0, 0x8A0};
constexpr u64 kGameTotalOff = 0x990, kGameAgedOff = 0x9C0, kGameIceOff = 0x9F0;
// Whether the game is mixing (bar detective mixing finder, 14 Oct): +0x880 reads 1 while mixing
// and +0x800 reads 1 while not; trusted only while they disagree.
constexpr u64 kGameMixingOff = 0x880, kGameIdleOff = 0x800;
// +0x800 is really "a drink is ready" (1 after a mix, failed or not; 0 while mixing and after Reset).
// The bottled A Fedora has no count: it is the text "fed" at +0x620 from the moment it goes into
// the shaker until Reset (found with the module's change watch, 14 Oct 2026). (+0x860, once
// taken for it, turns 1 when any mix starts.)
constexpr u64 kGameBottledOff = 0x620;
constexpr int kFedora = 1000; // drink index of the bottled A Fedora (not in kRecipes)
constexpr u64 kGameMixGrace = 45; // ticks the game may take to follow our own ZR press
constexpr int kErrN = 4;                 // error windows spawned by a failed drink
constexpr u64 kErrStep = 7;              // ~0.12 s between two windows popping up

// VA-11 Hall-A drink recipes (names and amounts only). Order: Adelhyde, Bronson Extract,
// Powdered Delta, Flanergide, Karmotrine (-1 = optional: any amount). Checked against the
// community wiki on 14 Oct 2026; Sunshine Cloud (on the rocks, blended, not aged) confirmed in game.
struct Recipe {
    const char* name;
    int amt[5];
    bool ice, aged, blended;
};
constexpr Recipe kRecipes[] = {
    {"Bad Touch", {0, 2, 2, 2, 4}, true, false, false},
    {"Beer", {1, 2, 1, 2, 4}, false, false, false},
    {"Bleeding Jane", {0, 1, 3, 3, 0}, false, false, true},
    {"Bloom Light", {4, 0, 1, 2, 3}, true, true, false},
    {"Blue Fairy", {4, 0, 0, 1, -1}, false, true, false},
    {"Brandtini", {6, 0, 3, 0, 1}, false, true, false},
    {"Cobalt Velvet", {2, 0, 0, 3, 5}, true, false, false},
    {"Crevice Spike", {0, 0, 2, 4, -1}, false, false, true},
    {"Flaming Moai", {1, 1, 2, 3, 5}, false, false, false},
    {"Fluffy Dream", {3, 0, 3, 0, -1}, false, true, false},
    {"Fringe Weaver", {1, 0, 0, 0, 9}, false, true, false},
    {"Frothy Water", {1, 1, 1, 1, 0}, false, true, false},
    {"Grizzly Temple", {3, 3, 3, 0, 1}, false, false, true},
    {"Gut Punch", {0, 5, 0, 1, -1}, false, true, false},
    {"Marsblast", {0, 6, 1, 4, 2}, false, false, true},
    {"Mercuryblast", {1, 1, 3, 3, 2}, true, false, true},
    {"Moonblast", {6, 0, 1, 1, 2}, true, false, true},
    {"Piano Man", {2, 3, 5, 5, 3}, true, false, false},
    {"Piano Woman", {5, 5, 2, 3, 3}, false, true, false},
    {"Pile Driver", {0, 3, 0, 3, 4}, false, false, false},
    {"Sparkle Star", {2, 0, 1, 0, -1}, false, true, false},
    {"Sugar Rush", {2, 0, 1, 0, -1}, false, false, false},
    {"Sunshine Cloud", {2, 2, 0, 0, -1}, true, false, true},
    {"Suplex", {0, 4, 0, 3, 3}, true, false, false},
    {"Zen Star", {4, 4, 4, 4, 4}, true, false, false},
};
// Each drink's sprite in the game's data (35x53), by its exact name as the probe listed it on the
// player's copy (14 Oct). The game spells Sugar Rush's sprite "Sugar Sush spr", and Frothy Water
// uses the Beer sprite (as in the game).
constexpr const char* kDrinkSprite[] = {
    "Badtouch spr", "Beer spr", "Bleeding Jane spr", "Bloom Light spr", "Blue Fairy spr", "Brandtini spr",
    "Cobalt Velvet spr", "Crevice Spike spr", "Flaming Moai spr", "Fluffy Dream spr", "Fringe Weaver spr",
    "Beer spr", "Grizzly Temple spr", "Gut Punch spr", "Marsblast spr", "Mercuryblast spr", "Moonblast spr",
    "Piano Man spr", "Piano Woman spr", "Piledriver spr", "Sparkle Star spr", "Sugar Sush spr",
    "Sunshine Cloud spr", "Suplex spr", "Zen Star spr",
};
static_assert(sizeof kDrinkSprite / sizeof kDrinkSprite[0] == sizeof kRecipes / sizeof kRecipes[0]);
constexpr int kShakerMax = 20; // units the shaker holds (a Big drink is every amount doubled)

// -1 = failed, else index into kRecipes; *big set for a doubled drink.
int MatchRecipe(const int c[5], bool ice, bool aged, bool blended, bool* big) {
    int total = 0;
    for (int i = 0; i < 5; ++i) total += c[i];
    if (total == 0 || total > kShakerMax) return -1;
    for (int r = 0; r < int(std::size(kRecipes)); ++r) {
        const Recipe& R = kRecipes[r];
        if (R.ice != ice || R.aged != aged || R.blended != blended) continue;
        for (int size = 1; size <= 2; ++size) {
            bool ok = true;
            for (int i = 0; i < 5 && ok; ++i)
                ok = R.amt[i] < 0 || c[i] == R.amt[i] * size;
            if (ok) {
                *big = size == 2;
                return r;
            }
        }
    }
    return -1;
}

struct Cand {
    u64 x;
    u8 seen;     // bit 0: talking pattern seen, bit 1: mixing pattern seen
    u8 strikes;
    s8 state;    // last full pattern: 0 talking, 1 mixing
    u64 flipped; // tick of the last change between the two patterns
};

struct Region {
    u64 begin, end;
};

struct Module {
    EdenDsmodHostApi host{};
    std::vector<Cand> cands;
    std::vector<Region> regions;
    size_t region_i = 0, rr = 0;
    u64 cursor = 0, wait = 0, ticks = 0;
    bool scanning = false;
    int scans = 0;          // finished heap scans (test readout)

    s64 mode = -1;        // decided state
    s64 last_mode = -1;
    bool skip = false;    // overlay dismissed by a tap until the mode changes
    bool manual = true;   // overlay while not sure (starts shown, hold hides, Mix shows)
    bool emergency = false; // the bar was opened by the hold; closes again when left unused
    u64 unused = 0;         // ticks since the player last touched the bar during an emergency

    // manual shake
    bool has_pos = false;   // the shaker is being carried (samples arriving)
    int dir = 0;            // last moving direction: -1, +1 (0 = none yet)
    int run = 0;            // moving ticks in that direction
    int last_step = 3;      // last movement step (1..5)
    u64 rev_ticks[2] = {0, 0}; // ticks of the last two direction reversals
    bool started = false;   // we pressed ZR to start a mix that is still running
    bool by_button = false; // that mix was started with the Mix button (runs until Mix again)
    u64 mix_ticks = 0;      // how long the current / last mix has run
    int done = 0;           // last shake-mix finished: 0 no, 1 mixed, 2 blended (Success!!)
    int counts[5] = {0, 0, 0, 0, 0}; // units in the shaker (the game's own when readable)
    u64 cur_x = 0;          // the trusted switch (0 = none)
    bool game_ok = false;   // the shaker's contents were read from the game this tick
    bool full_after = false;
    int game_mixing = -1;   // the game's own mixing state: -1 unknown, 0 / 1
    int game_ready = -1;    // the game has a drink ready (+0x800): -1 unknown, 0 / 1
    bool fedora = false;    // A Fedora is in the game's shaker (+0x620 reads "fed")
    bool ice = false, aged = false;  // Ice / Age toggled on for this drink
    std::string DrinkSrc() const { // the finished drink's sprite (as the shaker spot shows it)
        if (result == 2) return "module:gm:glitchbottle:0:4";
        if (drink == kFedora) return "module:gm:~fedora:0:4";
        if (drink < 0) return "";
        const std::string spr = kDrinkSprite[drink][0] == '~' ? std::string(kDrinkSprite[drink])
                                                               : vallhalla::GmReader::Key(kDrinkSprite[drink]);
        return "module:gm:" + spr + ":0:4";
    }
    std::string served_src; // the last drink served (its sprite), shown by Jill until the next mix
    // The served drink slides in from the right (pushed across to the customer) once the overlay is
    // up with it, easing out over ~0.45 s; it fades with the overlay like Jill.
    static constexpr u64 kServedSlideTicks = 27;
    static constexpr double kServedSlidePx = 420;
    u64 served_slide_tick = 0; // 0 = not started
    s64 served_dy = 0;         // px up (negative) so the glass's foot meets the counter
    std::string served_shadow; // its shadow on the counter (made from the same sprite)
    // A served drink's look on the counter: its 7x sprite (box cut, black glass), its shadow, its foot
    // lifted to the counter, and its liquid for the bubbles.
    struct DrinkLook {
        std::string src, name, shadow;
        std::vector<u8> liq, light;
        s64 dy = 0;
    };
    DrinkLook MakeLook(const std::string& finished, bool lean_right) {
        DrinkLook d;
        d.src = finished;
        if (d.src.size() > 4 && d.src.compare(d.src.size() - 4, 4, ":0:4") == 0) d.src.replace(d.src.size() - 1, 1, "7");
        d.src += ":keyedge";
        d.name = d.src.substr(10, d.src.find(':', 10) - 10);
        {
            std::scoped_lock lock{gm_mutex};
            int w = 0, ht = 0;
            std::vector<u8> px;
            const bool got = gm.Open(host) && gm.Frame(d.name, 0, 1, w, ht, px);
            if (!got || w != kDrinkW || ht != kDrinkH)
                Log("served drink " + d.name + ": no bubbles (" +
                    (got ? std::to_string(w) + "x" + std::to_string(ht) : std::string("not read")) + ")");
            if (got && w == kDrinkW && ht == kDrinkH) {
                KeyEdge(px, w, ht);
                d.liq = LiquidMask(px, w, ht, d.name);
                d.light.assign(size_t(w) * size_t(ht), 0);
                for (size_t i = 0; i < d.light.size(); ++i) { // light: luma over ~70 %
                    const u8* c = &px[i * 4];
                    d.light[i] = (299 * c[0] + 587 * c[1] + 114 * c[2]) / 1000 > 180;
                }
            }
        }
        d.shadow = finished;
        if (d.shadow.size() > 4 && d.shadow.compare(d.shadow.size() - 4, 4, ":0:4") == 0)
            d.shadow.replace(d.shadow.size() - 1, 1, "1");
        d.shadow += lean_right ? ":keyedge:shadowr7" : ":keyedge:shadow7";
        const int foot = SpriteFoot(d.name);
        d.dy = foot >= 0 ? (44 - foot) * 7 : 0;
        return d;
    }
    DrinkLook served2; // a two-slot order's second drink, right of Jill (served2.src empty: none)
    // Bubbles rise in the served drink's liquid.
    static constexpr int kDrinkW = 35, kDrinkH = 53, kDrinkScale = 7, kBubbles = 5;
    static constexpr int kBubbleRun = 5; // the least liquid (px, at 1x) a bubble starts under
    static constexpr s64 kDrinkX = 198, kDrinkY = 682; // the drink's rect (manifest) before dx / dy
    static constexpr s64 kDrink2X = 978;                // the second drink's, right of Jill
    std::string served_name;
    std::vector<u8> served_liq; // kDrinkW x kDrinkH, 1 = liquid
    std::vector<u8> served_light; // kDrinkW x kDrinkH, 1 = a light pixel (a bubble on it is dark)
    struct Bubble {
        int col = -1;
        double y = 0, speed = 0;
        int level = 0; // brightness 0..3
    } bubbles[kBubbles], bubbles2[kBubbles];
    static void KeyEdge(std::vector<u8>& px, int w, int ht) { // as the keyedge image option
        const auto black = [&](int x, int y) {
            const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            return px[i + 3] && !px[i] && !px[i + 1] && !px[i + 2];
        };
        std::vector<std::pair<int, int>> todo;
        for (int x = 0; x < w; ++x) todo.insert(todo.end(), {{x, 0}, {x, ht - 1}});
        for (int y = 0; y < ht; ++y) todo.insert(todo.end(), {{0, y}, {w - 1, y}});
        while (!todo.empty()) {
            const auto [x, y] = todo.back();
            todo.pop_back();
            if (x < 0 || y < 0 || x >= w || y >= ht || !black(x, y)) continue;
            px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] = 0;
            todo.insert(todo.end(), {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}});
        }
    }
    std::string ServedKey() const { // the drink at 7x, its black box cut away, its black inside glass
        return "module:gm:" + served_name + ":0:1:keyedge:glass:up7";
    }
    void BubbleTick() {
        if (ticks % 3) return;
        Bubbles(served_liq, served_light, bubbles, 0);
        Bubbles(served2.liq, served2.light, bubbles2, 20);
    }
    void Bubbles(const std::vector<u8>& served_liq, const std::vector<u8>& served_light, Bubble (&bubbles)[kBubbles],
                 u64 salt) {
        if (served_liq.empty()) return;
        const int cut = LiquidTop(served_liq, kDrinkW, kDrinkH);
        const auto liquid = [&](int x, int y) {
            return x >= 0 && y >= cut && x < kDrinkW && y < kDrinkH && served_liq[size_t(y) * kDrinkW + size_t(x)];
        };
        for (int i = 0; i < kBubbles; ++i) {
            Bubble& b = bubbles[i];
            if (b.col >= 0) b.y -= b.speed;
            if (b.col < 0 || !liquid(b.col, int(std::floor(b.y)))) { // gone up out of it: a new one below
                b.col = -1;
                if (Rand(40 + salt + u64(i)) % 4) continue; // not every time: they come and go
                // the foot of each column's longest unbroken run of liquid, if it is tall enough to
                // rise in (a glass's bright base under a dark band is liquid-coloured too, but a
                // bubble there would die at once)
                std::vector<std::pair<int, int>> bottoms;
                for (int x = 0; x < kDrinkW; ++x) {
                    int best = 0, best_foot = -1, run = 0;
                    for (int y = kDrinkH - 1; y >= cut; --y) {
                        run = liquid(x, y) ? run + 1 : 0;
                        if (run > best) best = run, best_foot = y + run - 1;
                    }
                    if (best >= kBubbleRun) bottoms.push_back({x, best_foot});
                }
                if (bottoms.empty()) continue;
                const auto pick = bottoms[Rand(50 + salt + u64(i)) % bottoms.size()];
                b.col = pick.first;
                b.y = pick.second + 0.99;
                b.speed = 0.25 + double(Rand(60 + salt + u64(i)) % 4) * 0.08;
                b.level = 1 + int(Rand(70 + salt + u64(i)) % 3);
                if (served_light.size() == served_liq.size() && served_light[size_t(pick.second) * kDrinkW + size_t(pick.first)])
                    b.level += 3; // dark on a light liquid
            }
        }
    }
    s64 ServedDx() const {
        if (!served_slide_tick) return s64(kServedSlidePx);
        const double f = std::min(1.0, double(ticks - served_slide_tick) / double(kServedSlideTicks));
        return s64(std::lround(kServedSlidePx * (1 - f) * (1 - f)));
    }
    std::string finished_src; // the finished drink just cleared (served, or thrown away by Reset)
    std::string finished_prev; // on a two-slot order: the first drink, finished before the second
    u64 finished_tick = 0;
    int result = 0;         // 0 nothing yet, 1 a recipe (success), 2 failed
    int drink = -1;         // kRecipes index when result == 1
    bool big = false;
    u64 err_tick = 0;       // when the error windows started popping up (failed drink)
    u32 err_closed = 0;     // bit i: the player tapped window i away
    u64 done_tick = 0;      // when it finished (Success animation)
    int zr_pending = 0;     // ZR presses still to be made
    u64 last_zr = 0;        // tick of our last ZR press
    bool start_check = false; // we pressed ZR to start a mix: not yet seen mixing in the game
    bool refused = false;   // the game ignored this grab's start (left stick held): no retry
    int zl_pending = 0;     // ZL presses still to be made
    u64 last_zl = 0;        // tick of our last ZL press
    bool reset_sync = false; // Reset mid-mix: bring the game to an empty shaker too
    int sync_tries = 0;
    u64 reset_tick = 0;
    // test readout: what the bottom screen last asked of the module, and whether it was taken
    std::string dbg_act = "-";
    s64 dbg_arg = 0;
    bool dbg_ok = false;
    u32 dbg_busy = 0, dbg_shake = 0, dbg_mix = 0;
    u64 last_shake = 0;     // tick of the last shake sample on the shaker
    // shaker physics: it tilts the way the finger moves and springs back, wobbling a little
    double tilt_in = 0;     // finger's sideways speed this tick (px/tick, from the "tilt" action)
    u64 tilt_tick = 0;      // tick of the last "tilt" sample (none for a while = not carried)
    double tilt_vf = 0;     // smoothed finger speed
    double tilt_a = 0;      // shaker angle in degrees (+ = clockwise: top leans right)
    double tilt_w = 0;      // its angular speed (degrees/tick)

    bool ReadF64(u64 a, double& d) const {
        const u8* p = host.get_read_pointer ? host.get_read_pointer(host.userdata, a, 8) : nullptr;
        if (!p) return false;
        std::memcpy(&d, p, 8);
        return true;
    }

    // The game's text value at a (a GameMaker value: 8 bytes value, 4 flags, 4 kind, kind 1 = text,
    // the value pointing at {char* text, ...}); "" when it is not text or can't be read.
    std::string ReadText(u64 a, int max = 32) const {
        if (!host.get_read_pointer) return "";
        const u8* p = host.get_read_pointer(host.userdata, a, 16);
        if (!p) return "";
        u64 v;
        u32 kind;
        std::memcpy(&v, p, 8);
        std::memcpy(&kind, p + 12, 4);
        if ((kind & 0xFFFFFF) != 1 || !v) return "";
        const u8* r = host.get_read_pointer(host.userdata, v, 8);
        if (!r) return "";
        u64 txt;
        std::memcpy(&txt, r, 8);
        std::string s;
        for (int i = 0; i < max; ++i) {
            const u8* ch = host.get_read_pointer(host.userdata, txt + u64(i), 1);
            if (!ch || !*ch) break;
            s += char(*ch);
        }
        return s;
    }

    // -2 garbage, -1 in between, 0 talking (1,0,0), 1 mixing (0,1,1)
    int Pattern(u64 x) const {
        double a, b, c;
        if (!ReadF64(x, a) || !ReadF64(x + kS2, b) || !ReadF64(x + kS3, c)) return -2;
        auto bit = [](double d) { return d == 0.0 ? 0 : d == 1.0 ? 1 : -1; };
        const int A = bit(a), B = bit(b), C = bit(c);
        if (A < 0 || B < 0 || C < 0) return -2;
        if (A == 1 && B == 0 && C == 0) return 0;
        if (A == 0 && B == 1 && C == 1) return 1;
        return -1;
    }

    void StartScan() {
        regions.clear();
        const u64 hb = host.get_heap_begin ? host.get_heap_begin(host.userdata) : 0;
        const u64 he = host.get_heap_end ? host.get_heap_end(host.userdata) : 0;
        if (he > hb) regions.push_back({hb & ~(kPage - 1), he});
        region_i = 0;
        cursor = regions.empty() ? 0 : regions[0].begin;
        scanning = !regions.empty();
    }

    void Add(u64 x, int st) {
        auto it = std::lower_bound(cands.begin(), cands.end(), x, [](const Cand& c, u64 v) { return c.x < v; });
        if (it != cands.end() && it->x == x) return;
        if (cands.size() >= kMaxCands) return;
        cands.insert(it, {x, u8(1u << st), 0, s8(st), 0});
    }

    void ScanStep() {
        u64 budget = kPagesPerTick;
        while (budget-- && region_i < regions.size()) {
            const Region& r = regions[region_i];
            if (cursor >= r.end) {
                if (++region_i < regions.size()) cursor = regions[region_i].begin;
                continue;
            }
            const u8* p = host.get_read_pointer ? host.get_read_pointer(host.userdata, cursor, kPage) : nullptr;
            if (p) {
                for (u64 off = 0; off < kPage; off += 8) {
                    u64 w;
                    std::memcpy(&w, p + off, 8);
                    if (w != 0x3FF0000000000000ULL) continue; // 1.0
                    const u64 y = cursor + off;
                    if (Pattern(y) == 0) Add(y, 0);                       // y is switch 1
                    if (y >= kS2 && Pattern(y - kS2) == 1) Add(y - kS2, 1); // y is switch 2
                }
            }
            cursor += kPage;
        }
        if (region_i >= regions.size()) {
            scanning = false;
            ++scans;
            wait = Proven() ? kRescanProven : kRescanUnsure;
        }
    }

    bool Check(Cand& c) { // false = drop
        const int st = Pattern(c.x);
        if (st == -2) return ++c.strikes <= kStrikes;
        c.strikes = 0;
        if (st == -1) return true;
        if (c.state != st) c.flipped = ticks;
        c.state = s8(st);
        c.seen |= u8(1u << st);
        return true;
    }

    size_t Proven() const {
        size_t n = 0;
        for (const auto& c : cands) n += c.seen == 3;
        return n;
    }

    void Seed() {
        const u64 hb = host.get_heap_begin ? host.get_heap_begin(host.userdata) : 0;
        const u64 he = host.get_heap_end ? host.get_heap_end(host.userdata) : 0;
        const u64 x = hb + kSeedOff;
        if (hb == 0 || x + kS3 + 8 > he) return;
        auto it = std::lower_bound(cands.begin(), cands.end(), x, [](const Cand& c, u64 v) { return c.x < v; });
        if (it != cands.end() && it->x == x) {
            it->seen = 3; // re-found by a scan before the seed check: trust it too
            return;
        }
        const int st = Pattern(x);
        if (st < 0) return;
        Add(x, st);
        it = std::lower_bound(cands.begin(), cands.end(), x, [](const Cand& c, u64 v) { return c.x < v; });
        if (it != cands.end() && it->x == x) it->seen = 3;
    }

    void Track() {
        Seed();
        std::vector<u8> drop(cands.size(), 0);
        bool any = false;
        for (size_t i = 0; i < cands.size(); ++i)
            if (cands[i].seen == 3 && !Check(cands[i])) drop[i] = any = true;
        for (size_t n = 0; n < std::min(cands.size(), kCheckPerTick); ++n) {
            if (rr >= cands.size()) rr = 0;
            if (cands[rr].seen != 3 && !Check(cands[rr])) drop[rr] = any = true;
            ++rr;
        }
        if (any) {
            size_t w = 0;
            for (size_t i = 0; i < cands.size(); ++i)
                if (!drop[i]) cands[w++] = cands[i];
            cands.resize(w);
        }
        const Cand* best = nullptr;
        for (const auto& c : cands)
            if (c.seen == 3 && c.strikes == 0 && (!best || c.flipped > best->flipped)) best = &c;
        mode = best ? best->state : -1;
        cur_x = best ? best->x : 0;
    }

    int ReadGameMixing() const {
        double m, i;
        if (!cur_x || !ReadF64(cur_x + kGameMixingOff, m) || !ReadF64(cur_x + kGameIdleOff, i)) return -1;
        if (m == 1 && i == 0) return 1;
        if (m == 0 && i == 1) return 0;
        return -1;
    }

    // The game's own shaker contents, whatever poured them (bottom screen, top screen,
    // controller). Trusted only when every value is a whole number in range and the counts add
    // up to the game's total; otherwise the bottom screen's own counting stays in charge.
    bool ReadGame() {
        if (!cur_x) return false;
        if (SlotB()) { // slot 2 at the front: its own shaker, as last read from the game's globals
            if (slotb_fresh) {
                slotb_fresh = false;
                // a pour of ours counts at once, and a read from just before the game took it would
                // pull the cell back for a moment (the meters flickered): lower values wait a second
                for (int i = 0; i < 5; ++i)
                    if (slotb_counts[i] >= counts[i] || ticks - pour_tick[i] > 60) counts[i] = slotb_counts[i];
                ice = slotb_ice;
                aged = slotb_aged;
                fedora = false;
            }
            return true;
        }
        double v[5], total, ice_d, aged_d;
        for (int i = 0; i < 5; ++i)
            if (!ReadF64(cur_x + kGameCountOff[i], v[i])) return false;
        if (!ReadF64(cur_x + kGameTotalOff, total) || !ReadF64(cur_x + kGameIceOff, ice_d) ||
            !ReadF64(cur_x + kGameAgedOff, aged_d))
            return false;
        const auto whole = [](double d, double hi) { return std::isfinite(d) && d >= 0 && d <= hi && d == std::floor(d); };
        double sum = 0;
        for (double d : v) {
            if (!whole(d, 40)) return false;
            sum += d;
        }
        if (!whole(total, 80) || total != sum || !whole(ice_d, 1) || !whole(aged_d, 1)) return false;
        for (int i = 0; i < 5; ++i) counts[i] = int(v[i]);
        fedora = ReadText(cur_x + kGameBottledOff) == "fed";
        ice = ice_d != 0;
        aged = aged_d != 0;
        return true;
    }

    bool Overlay() const {
        if (mode < 0) return manual;
        return mode == 0 && !skip;
    }

    void Reversal() {
        rev_ticks[0] = rev_ticks[1];
        rev_ticks[1] = ticks;
        // every swing of a hand-held shaker with something in it rattles the ice: the game's own
        // "iceadd", louder the faster that swing went (finger speed 0..30 px/tick)
        // Clinks at least kIceGap apart (one waiting at most), each a little higher or lower and
        // a touch louder or softer than the last, so the ice sounds like it moves around.
        if (result == 0 && (HasPour() || started) && !(started && by_button) && ticks - ice_tick >= kIceGap &&
            sound_q.empty()) {
            const double gain = std::clamp(0.15 + swing_peak / 36.0 + (Rand01() - 0.5) * 0.1, 0.12, 0.8);
            PlaySound(kSndIce, gain, 0.93 + Rand01() * 0.14, 60);
            ice_tick = ticks;
        }
        swing_peak = 0;
    }

    // ---- the game's own sounds, played through the guest mailbox (tools/va11-hall-a/gen_load_plan.py) ----
    // The load plan hooks GameMaker's interpreter right after each built-in call; there the stub
    // calls what the module asks for, on the game's thread: audio_play_sound(sound, priority, loop)
    // and then audio_sound_gain(that sound, gain, 0). Sound numbers are the game's own (SOND).
    static constexpr u64 kAudioPlaySound = 0x1D9520, kAudioSoundGain = 0x1D9DD0, // main offsets
                         kAudioSoundPitch = 0x1D9E50;                          // (sound, pitch)
    static constexpr u64 kIceGap = 16; // ticks (~0.27 s) between two clinks of the ice
    static constexpr int kSndIce = 102, kSndClick = 94, kSndError = 90; // iceadd, click_sound, buttonnoclick
    static constexpr u32 kMbSeq = 0x00, kMbHeart = 0x08, kMbDone = 0x0C, kMbFn = 0x10, kMbArgs = 0x18,
                         kMbScratch = 0x80, kMbRet = 0xB0; // scratch: 3 RValue arguments, then the result
    EdenDsmodHostExtensions ext{};
    bool has_mailbox = false;
    struct SoundReq {
        int sound;
        double gain, pitch, fade_ms;
    };
    std::vector<SoundReq> sound_q;
    int snd_stage = 0;       // 0 idle, 1 playing (waiting for audio_play_sound), 2 its follow-up calls
    SoundReq snd_cur{};
    double snd_inst = -1;
    struct FollowUp {
        u64 fn;
        double a, b;
        int argc;
    };
    std::vector<FollowUp> snd_next; // after the sound started: fade in, volume, pitch
    u64 ice_tick = 0;
    u64 rng = 0x9E3779B97F4A7C15ULL;
    double Rand01() { // xorshift: cheap variation for the clinks
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return double(rng >> 11) / double(1ULL << 53);
    }
    u32 mb_seq = 0;
    u32 mb_beat = 0;
    u64 mb_beat_tick = 0, snd_tick = 0;
    double swing_peak = 0;   // the fastest finger speed since the last swing
    u32 sounds_played = 0;

    bool MailboxAlive() const { return has_mailbox && mb_beat_tick && ticks - mb_beat_tick < 60; }
    void PlaySound(int sound, double gain, double pitch = 1.0, double fade_ms = 0) {
        if (!MailboxAlive() || sound_q.size() >= 2) return; // never let sounds pile up behind a swing
        sound_q.push_back({sound, gain, pitch, fade_ms});
    }
    bool MbCall(u64 fn_off, const double (&args)[3], int argc = 3) {
        const u64 base = host.main_base;
        if (!base) return false;
        for (int i = 0; i < 3; ++i) { // RValue: 8-byte value, then flags 0 and kind 0 (a number)
            u64 bits;
            std::memcpy(&bits, &args[i], 8);
            if (!ext.store_u64(ext.userdata, kMbScratch + u32(i) * 16, bits) ||
                !ext.store_u64(ext.userdata, kMbScratch + u32(i) * 16 + 8, 0))
                return false;
        }
        ext.store_u64(ext.userdata, kMbRet, 0);
        ext.store_u64(ext.userdata, kMbRet + 8, 0);
        const u64 regs[5] = {ext.mailbox_address + kMbRet, 0, 0, u64(argc), ext.mailbox_address + kMbScratch};
        for (int i = 0; i < 5; ++i)
            if (!ext.store_u64(ext.userdata, kMbArgs + u32(i) * 8, regs[i])) return false;
        if (!ext.store_u64(ext.userdata, kMbFn, base + fn_off)) return false;
        return ext.store_u32(ext.userdata, kMbSeq, ++mb_seq);
    }
    void SoundTick() {
        if (!has_mailbox) return;
        u32 beat = 0;
        const bool read = ext.load_u32(ext.userdata, kMbHeart, &beat);
        if (ticks % 300 == 0 && !MailboxAlive()) // the hook is not running (another game build?)
            Log(std::string("mailbox heartbeat read ") + (read ? "ok" : "FAILED") + " value " + std::to_string(beat) +
                " main_base " + std::to_string(host.main_base));
        if (read && beat != mb_beat) {
            if (!mb_beat_tick) Log("game sounds ready (the interpreter hook is running)");
            mb_beat = beat;
            mb_beat_tick = ticks;
        }
        if (snd_stage) {
            u32 done = 0;
            if (!ext.load_u32(ext.userdata, kMbDone, &done) || done != mb_seq) {
                if (ticks - snd_tick > 120) { // the game stopped serving (menu, load): give up
                    snd_stage = 0;
                    snd_next.clear();
                }
                return;
            }
            if (snd_stage == 1) { // the sound started: queue its fade-in, volume and pitch
                u64 bits = 0;
                snd_inst = -1;
                if (ext.load_u64(ext.userdata, kMbRet, &bits)) std::memcpy(&snd_inst, &bits, 8);
                ++sounds_played;
                if (sounds_played == 1) Log("first game sound played, instance " + std::to_string(snd_inst));
                snd_next.clear();
                if (snd_inst >= 0) {
                    if (snd_cur.fade_ms > 0) { // start silent, then rise to its volume
                        snd_next.push_back({kAudioSoundGain, 0, 0, 3});
                        snd_next.push_back({kAudioSoundGain, snd_cur.gain, snd_cur.fade_ms, 3});
                    } else if (snd_cur.gain < 0.999) {
                        snd_next.push_back({kAudioSoundGain, snd_cur.gain, 0, 3});
                    }
                    if (std::fabs(snd_cur.pitch - 1.0) > 0.001) snd_next.push_back({kAudioSoundPitch, snd_cur.pitch, 0, 2});
                }
                snd_stage = 2;
            }
            if (!snd_next.empty()) {
                const FollowUp f = snd_next.front();
                snd_next.erase(snd_next.begin());
                const double args[3] = {snd_inst, f.a, f.b};
                if (MbCall(f.fn, args, f.argc)) {
                    snd_tick = ticks;
                    return;
                }
                snd_next.clear();
            }
            snd_stage = 0;
        }
        if (sound_q.empty() || !MailboxAlive() || juke_wait) return; // one request at a time
        const SoundReq r = sound_q.front();
        sound_q.erase(sound_q.begin());
        const double args[3] = {double(r.sound), 10, 0}; // priority 10, no loop
        if (MbCall(kAudioPlaySound, args)) {
            snd_stage = 1;
            snd_cur = r;
            snd_tick = ticks;
        }
    }

    // The jukebox on the bottom screen. When the game's song picker is open (jukebox_bg exists;
    // the dialogue opens it at the start of each shift) the module asks the game itself, through
    // the mailbox, what it shows: the page (global.cur_juke, songs 5*page+1..5), the 12 playlist
    // slots (global.playlist, 0 = empty), the song names (global.shortplayliststrings, the console
    // list), which songs are unlocked (the shop / story flags song_unlock_juketext checks), the
    // game's own labels, the slot picked for a swap (global.change1slot) and the controller cursor
    // (jukebox_navigation_mngr.cur_ind: 0-4 songs, 5-16 slots, 17 Done). The package draws that
    // with the game's sprites and sends taps back; the module then does what the game's own code
    // does: newjuke_addsong (the song into the first empty slot, its name from the game's list,
    // the jukeselect sound and the halo), the X delete (slot 0, name "", the prompt sound), the
    // page arrows (cur_juke, jukepick). Done steers the game's cursor with the d-pad (Done is a
    // group of its own below everything, so DOWN reaches it) and presses A once it is on 17.
    // Built-ins found 16 Oct 2026 by their registrations in main (base v0): (RValue& result,
    // CInstance* self, CInstance* other, int argc, RValue* args). Strings go in as RValues pointing
    // at a RefString {char*, refcount, size} in the mailbox scratch; anything stored in the game's
    // lists is a string the game made (a list entry, or string_copy's result).
    static constexpr u64 kInstanceExists = 0x48C30, kInstanceFind = 0x48B30, kInstanceCreate = 0x49170,
                         kDsListFindValue = 0x38CB0, kDsListSet = 0x38830, kVariableGlobalGet = 0x1CC9B0,
                         kVariableGlobalSet = 0x1CCA70, kVariableInstanceGet = 0x1CCC60, kStringCopy = 0x4F9F0,
                         kAudioStopSound = 0x1D9790, kVariableInstanceSet = 0x1CCD70,
                         kMouseXGet = 0x130FD0, kMouseYGet = 0x131010; // built-in variable getters
    static constexpr int kObjJukeboxBg = 569, kObjMusicHalo = 570, kObjJukeNav = 593; // game.win OBJT
    static constexpr int kSndJukechange = 105, kSndJukepick = 106, kSndJukeselect = 107, kSndPrompt = 115;
    static constexpr int kJukeDone = 17, kJukePageMax = 11, kJukeSlots = 12;
    // The bottom-screen jukebox is off since 1.0.7 (it didn't feel right there): the game's own
    // picker on the top screen does it, and nothing is asked of the game about it. The code stays
    // for another try; the mailbox calls it is built on carry the tap for the next line and the
    // language too.
    static constexpr bool kJukeboxMirror = false;
    static constexpr u32 kMbStr = 0x200; // string arguments, 0x80 each
    struct Arg {
        double num = 0;
        const char* text = nullptr;
        bool last = false; // the previous call's result, as it came back
    };
    struct RV {
        u32 kind = 5; // GameMaker value kinds: 0 real, 1 string, 5 undefined, ...
        double num = 0;
        std::string text;
    };
    struct Call {
        u64 fn;
        std::vector<Arg> args;
        std::function<void(const RV&)> then;
        bool getter = false; // a built-in variable's getter: (CInstance* self, int index, RValue* out)
    };
    std::vector<Call> jq;
    size_t jq_at = 0;
    bool juke_wait = false;
    u64 juke_call_tick = 0, juke_next = 0;
    bool jk_open = false, jk_ready = false; // open; and its lists, labels and flags read
    int jk_page = 0, jk_cur = -1, jk_sel = -1;
    double jk_list = -1, jk_names_list = -1, jk_short = -1, jk_nav = -1, jk_sfx = 100;
    double jk_songs = -1, jk_bgm = 100; // global.fullplaylist (song -> its sound), global.bgmvolume
    int jk_preview = 0;                 // the song previewing since a tap, 0 = none
    int jk_fade = 0;                    // the song fading out after it left the playlist, 0 = none
    u64 jk_fade_at = 0;                 // when to stop it
    u64 jk_last_v = 0, jk_last_fk = 5ULL << 32;
    int jk_slot[kJukeSlots] = {};
    std::map<int, std::string> jk_name;
    std::string jk_please, jk_locked, jk_maxpages, jk_tag[kJukeSlots];
    std::map<std::string, double> jk_flag;
    std::vector<std::pair<int, int>> jk_cmds; // (what, argument): 1 add song 1..5, 2 delete slot, 3 page, 4 done
    int jk_done_steps = -1;                   // the cursor walk to Done, -1 = none
    bool jk_placed = false;                   // the game's cursor is on its grid (exactly_placed)
    int jk_btn = 0;                           // the button the package should press: 1 DDown, 2 A
    u64 jk_btn_tick = 0;
    std::string jk_last;
    // the panel's way in and out, in 27 bands of 40 px: jukebox_bg's wipedown (+0.03 a frame, ~33
    // frames) top to bottom, and its close (high -0.1 a frame) squashing to the middle
    static constexpr int kJukeBands = 27;
    static constexpr u64 kJukeWipeTicks = 33, kJukeCloseTicks = 10;
    u64 jk_show_tick = 0, jk_close_tick = 0;
    void JukeClosed() {
        if (jk_open && jk_ready) jk_close_tick = ticks;
    }
    static constexpr const char* kJukeFlags[] = {"hopesshop", "friendlyshop", "endingshop", "can_truth",
                                                 "gotmeshop", "havenshop", "mikisong", "shootersong",
                                                 "ironheartsong", "frontiersong", "eltonsshop", "barshop",
                                                 "streamingsong", "staffsong"};
    bool MbStore64(u32 off, u64 v) { return ext.store_u64(ext.userdata, off, v); }
    bool MbCallRV(u64 fn_off, const std::vector<Arg>& args, bool getter = false) {
        const u64 base = host.main_base, mb = ext.mailbox_address;
        if (!base || args.size() > 3) return false;
        // "last" = the answer to this queue's previous call, kept when it came back: the mailbox's
        // result slot may hold a sound's answer by now (SoundTick runs between our calls)
        const u64 last_v = jk_last_v, last_fk = jk_last_fk;
        for (u32 i = 0; i < args.size(); ++i) {
            const Arg& a = args[i];
            const u32 slot = kMbScratch + i * 16;
            if (a.last) {
                if (!MbStore64(slot, last_v) || !MbStore64(slot + 8, last_fk)) return false;
            } else if (a.text) {
                const u32 rs = kMbStr + i * 0x80, txt = rs + 0x10;
                const size_t n = std::strlen(a.text);
                if (n > 0x6F) return false;
                for (size_t k = 0; k <= n; k += 8) {
                    u64 w = 0;
                    std::memcpy(&w, a.text + k, std::min<size_t>(8, n + 1 - k));
                    if (!MbStore64(txt + u32(k), w)) return false;
                }
                if (!MbStore64(rs, mb + txt) || !MbStore64(rs + 8, (u64(n) << 32) | 0x10000) ||
                    !MbStore64(slot, mb + rs) || !MbStore64(slot + 8, 1ULL << 32)) // kind 1: string
                    return false;
            } else {
                u64 bits;
                std::memcpy(&bits, &a.num, 8);
                if (!MbStore64(slot, bits) || !MbStore64(slot + 8, 0)) return false;
            }
        }
        MbStore64(kMbRet, 0);
        MbStore64(kMbRet + 8, 5ULL << 32); // undefined until the call writes it
        const u64 regs[5] = {getter ? 0 : mb + kMbRet, 0, getter ? mb + kMbRet : 0, getter ? 0 : u64(args.size()),
                             getter ? 0 : mb + kMbScratch};
        for (int k = 0; k < 5; ++k)
            if (!MbStore64(kMbArgs + u32(k) * 8, regs[k])) return false;
        if (!MbStore64(kMbFn, base + fn_off)) return false;
        return ext.store_u32(ext.userdata, kMbSeq, ++mb_seq);
    }
    std::string TextAt(u64 refstring) const {
        const u8* r = refstring ? host.get_read_pointer(host.userdata, refstring, 8) : nullptr;
        if (!r) return "";
        u64 txt;
        std::memcpy(&txt, r, 8);
        std::string s;
        for (int k = 0; k < 96; ++k) {
            const u8* ch = host.get_read_pointer(host.userdata, txt + u64(k), 1);
            if (!ch || !*ch) break;
            s += char(*ch) == '#' ? ' ' : char(*ch); // '#' is the game's line break
        }
        return s;
    }
    RV MbResult() const {
        RV r;
        u64 v = 0, fk = 0;
        if (!ext.load_u64(ext.userdata, kMbRet, &v) || !ext.load_u64(ext.userdata, kMbRet + 8, &fk)) return r;
        r.kind = u32(fk >> 32) & 0xFFFFFF;
        if (r.kind == 0) std::memcpy(&r.num, &v, 8);
        else if (r.kind == 1) r.text = TextAt(v);
        else if (r.kind == 2) r.num = double(s32(u32(v)));                           // int32
        else if (r.kind == 10 || r.kind == 13 || r.kind == 15) r.num = double(s64(v)); // int64, bool, ref
        return r;
    }
    void JQ(u64 fn, std::vector<Arg> args, std::function<void(const RV&)> then = nullptr) {
        jq.push_back({fn, std::move(args), std::move(then)});
    }
    void JGlobal(const char* name, std::function<void(const RV&)> then) {
        JQ(kVariableGlobalGet, {{0, name}}, std::move(then));
    }
    bool JukeUnlocked(int k) const { // song_unlock_juketext(k) on page jk_page
        const auto f = [this](const char* n) {
            const auto it = jk_flag.find(n);
            return it != jk_flag.end() && it->second == 1;
        };
        const int p = jk_page;
        if (k == 1) {
            if (p == 0) return f("hopesshop");
            if (p == 2) return f("friendlyshop");
            if (p == 7) return f("endingshop");
            if (p == 11) return f("can_truth");
            return true;
        }
        if (k == 2) return p == 2 ? f("gotmeshop") : p == 4 ? f("havenshop") : p == 5 ? f("mikisong") : true;
        if (k == 3) return p == 5 ? f("shootersong") : p == 7 ? f("ironheartsong") : p == 8 ? f("frontiersong") : true;
        if (k == 4) return p == 2 ? f("eltonsshop") : p == 6 ? f("barshop") : true;
        if (k == 5) return p == 2 ? f("streamingsong") : p == 8 ? f("staffsong") : true;
        return false;
    }
    bool JukeFull() const {
        for (int v : jk_slot)
            if (!v) return false;
        return true;
    }
    double JukeGain() const { return std::clamp((jk_sfx - 20) / 100, 0.0, 1.0); }
    void JukeOpenReads() { // once per opening: the lists, the game's labels, the volume, the unlock flags
        JGlobal("playlist", [this](const RV& r) { jk_list = r.num; });
        JGlobal("playlistnames", [this](const RV& r) { jk_names_list = r.num; });
        JGlobal("shortplayliststrings", [this](const RV& r) { jk_short = r.num; });
        JGlobal("pleasesong", [this](const RV& r) { jk_please = r.text; });
        JGlobal("lockedsong", [this](const RV& r) { jk_locked = r.text; });
        JGlobal("maxpagesjuke", [this](const RV& r) { jk_maxpages = r.text; });
        JGlobal("sfxvolume", [this](const RV& r) { if (r.kind == 0) jk_sfx = r.num; });
        JGlobal("bgmvolume", [this](const RV& r) { if (r.kind == 0) jk_bgm = r.num; });
        JGlobal("fullplaylist", [this](const RV& r) { if (r.kind == 0) jk_songs = r.num; });
        static const char* const kTags[] = {"jukeslottag1", "jukeslottag2", "jukeslottag3", "jukeslottag4",
                                            "jukeslottag5", "jukeslottag6", "jukeslottag7", "jukeslottag8",
                                            "jukeslottag9", "jukeslottag10", "jukeslottag11", "jukeslottag12"};
        for (int i = 0; i < kJukeSlots; ++i) JGlobal(kTags[i], [this, i](const RV& r) { jk_tag[i] = r.text; });
        for (const char* n : kJukeFlags) JGlobal(n, [this, n](const RV& r) { jk_flag[n] = r.num; });
        JQ(kInstanceFind, {{double(kObjJukeNav)}, {0}}, [this](const RV& r) {
            jk_nav = r.num;
            jk_ready = jk_list >= 0 && jk_names_list >= 0 && jk_short >= 0;
            jk_show_tick = ticks;
            Log("juke: open (" + jk_please + ", " + jk_locked + ", pages" + jk_maxpages + ")" +
                (jk_ready ? "" : " -- its lists did not come back, not mirrored"));
            JukeCycle(); // only now: its calls carry the list ids just read
        });
    }
    void JukeCycle() { // what changes while it is open
        // A call's arguments are fixed when it is queued, and the game freezes on a bad list id
        // (ds_list_find_value(-1, ..) is a fatal error on the Switch): never queue one.
        if (!jk_ready) return;
        JGlobal("cur_juke", [this](const RV& r) { if (r.kind == 0) jk_page = std::clamp(int(r.num), 0, kJukePageMax); });
        for (int i = 0; i < kJukeSlots; ++i)
            JQ(kDsListFindValue, {{jk_list}, {double(i)}}, [this, i](const RV& r) { jk_slot[i] = r.kind == 0 ? int(r.num) : 0; });
        JGlobal("change1slot", [this](const RV& r) { jk_sel = r.kind == 0 ? int(r.num) : -1; });
        // The game starts its own preview (previewicon 1) when its cursor comes to rest on a song;
        // ours sets it to 0. Then ours stops, so the two never play over each other.
        JGlobal("previewicon", [this](const RV& r) {
            if (r.kind != 0 || r.num < 0.5 || !jk_preview || jk_songs < 0) return;
            VLog("juke: the game previews a song of its own: ours (" + std::to_string(jk_preview) + ") stops");
            JQ(kDsListFindValue, {{jk_songs}, {double(jk_preview)}});
            JQ(kAudioStopSound, {{0, nullptr, true}});
            jk_preview = 0;
        });
        // the cursor only from a real instance (noone = -4 would be another fatal error)
        JQ(jk_nav >= 0 ? kVariableInstanceGet : kVariableGlobalGet,
           jk_nav >= 0 ? std::vector<Arg>{{jk_nav}, {0, "cur_ind"}} : std::vector<Arg>{{0, "cur_juke"}}, [this](const RV& r) {
            jk_cur = jk_nav >= 0 && r.kind == 0 ? int(r.num) : -1;
            std::vector<int> need;
            for (int k = 1; k <= 5; ++k) need.push_back(5 * jk_page + k);
            for (int v : jk_slot)
                if (v) need.push_back(v);
            for (int n : need)
                if (!jk_name.count(n))
                    JQ(kDsListFindValue, {{jk_short}, {double(n)}}, [this, n](const RV& r2) { jk_name[n] = r2.text; });
        });
    }
    // The preview, as the game plays it when the cursor rests on a song (jukebuttons_alarm): the
    // song's own sound looping at the music volume, with global.previewfade set so the jukebox
    // fades its music out meanwhile. Stopping clears previewfade and the game fades the song out
    // itself (jukebuttons_step) while it is on this page; a song of another page is stopped here.
    void JukePreviewStop(bool other_page) {
        if (!jk_preview || jk_songs < 0) return;
        if (other_page) {
            JQ(kDsListFindValue, {{jk_songs}, {double(jk_preview)}});
            JQ(kAudioStopSound, {{0, nullptr, true}});
        }
        JQ(kVariableGlobalSet, {{0, "previewfade"}, {0}});
        jk_preview = 0;
    }
    void JukePreviewFadeOut() { // its slot was emptied: fade it out (~0.7 s), then stop it
        if (!jk_preview || jk_songs < 0) return;
        JQ(kDsListFindValue, {{jk_songs}, {double(jk_preview)}});
        JQ(kAudioSoundGain, {{0, nullptr, true}, {0}, {700}});
        JQ(kVariableGlobalSet, {{0, "previewfade"}, {0}}); // the jukebox music comes back
        jk_fade = jk_preview;
        jk_fade_at = ticks + 45;
        jk_preview = 0;
    }
    void JukePreview(int song) {
        if (jk_songs < 0) return;
        if (jk_fade == song) jk_fade = 0; // it starts again: no late stop
        if (jk_preview && jk_preview != song) {
            JQ(kDsListFindValue, {{jk_songs}, {double(jk_preview)}});
            JQ(kAudioStopSound, {{0, nullptr, true}});
        }
        // the game's own preview (the top screen's cursor resting on a song of this page) too
        for (int k = 1; k <= 5; ++k) {
            const int other = 5 * jk_page + k;
            if (other == song || other == jk_preview) continue;
            JQ(kDsListFindValue, {{jk_songs}, {double(other)}});
            JQ(kAudioStopSound, {{0, nullptr, true}});
        }
        JQ(kVariableGlobalSet, {{0, "previewicon"}, {0}});
        JQ(kVariableGlobalSet, {{0, "previewfade"}, {1}});
        JQ(kDsListFindValue, {{jk_songs}, {double(song)}});
        JQ(kAudioStopSound, {{0, nullptr, true}}); // from the start again on a second tap
        JQ(kDsListFindValue, {{jk_songs}, {double(song)}});
        JQ(kAudioPlaySound, {{0, nullptr, true}, {1}, {1}});
        JQ(kDsListFindValue, {{jk_songs}, {double(song)}});
        JQ(kAudioSoundGain, {{0, nullptr, true}, {std::clamp((jk_bgm - 20) / 100, 0.0, 1.0)}, {0}});
        jk_preview = song;
    }
    void JukeCommand(int what, int arg) {
        if (!jk_ready) return;
        if (what == 1) { // add song arg (1..5) of this page, like newjuke_addsong
            const int song = 5 * jk_page + arg;
            int slot = -1;
            for (int i = 0; i < kJukeSlots && slot < 0; ++i)
                if (!jk_slot[i]) slot = i;
            if (!JukeUnlocked(arg)) return;
            JukePreview(song); // a tap previews the song, like resting the cursor on it
            if (slot < 0) return;
            jk_slot[slot] = song;
            JQ(kDsListSet, {{jk_list}, {double(slot)}, {double(song)}});
            JQ(kDsListFindValue, {{jk_short}, {double(song)}});
            JQ(kDsListSet, {{jk_names_list}, {double(slot)}, {0, nullptr, true}});
            JQ(kInstanceCreate, {{slot < 6 ? 320.0 : 520.0}, {52.0 + 30 * (slot % 6)}, {double(kObjMusicHalo)}});
            PlaySound(kSndJukeselect, JukeGain());
        } else if (what == 2) { // delete slot arg (0..11), like X on it
            if (arg < 0 || arg >= kJukeSlots || !jk_slot[arg]) return;
            const bool previewed = jk_slot[arg] == jk_preview;
            jk_slot[arg] = 0;
            if (previewed) JukePreviewFadeOut();
            JQ(kDsListSet, {{jk_list}, {double(arg)}, {0}});
            JQ(kStringCopy, {{0, "-"}, {1}, {0}}); // "" made by the game
            JQ(kDsListSet, {{jk_names_list}, {double(arg)}, {0, nullptr, true}});
            if (jk_sel == arg) {
                JQ(kVariableGlobalSet, {{0, "change1name"}, {0}});
                JQ(kVariableGlobalSet, {{0, "change1slot"}, {-1}});
            }
            PlaySound(kSndPrompt, JukeGain());
        } else if (what == 3) { // page arg -1 / +1, like LS / RS
            const int np = jk_page + arg;
            if (np < 0 || np > kJukePageMax) {
                PlaySound(kSndJukechange, JukeGain());
                return;
            }
            JukePreviewStop(true); // its button leaves the screen with the page
            jk_page = np;
            JQ(kVariableGlobalSet, {{0, "cur_juke"}, {double(np)}});
            PlaySound(kSndJukepick, JukeGain());
        } else if (what == 4) {
            std::string sl;
            for (int v : jk_slot) sl += " " + std::to_string(v);
            VLog("juke: Ready tapped, slots" + sl + (JukeFull() ? ", full" : ", NOT full") +
                (jk_done_steps >= 0 ? ", walk already running" : ""));
            if (JukeFull() && jk_done_steps < 0) jk_done_steps = 0;
        }
    }
    // Ready: put the game's cursor straight on it, as its own d-pad does (mouse_shift(cur_x -
    // mouse_x, cur_y - mouse_y) with cur_ind 17 at Done's point), then press A a few ticks later,
    // once the hitbox has moved there (cursor_hitbox's Begin Step). A touch resets the shift, as
    // in the game. If the pointer can't be read, walk there with DOWN instead.
    static constexpr double kDoneX = 416 - 29, kDoneY = 220 + 16; // jukebox_done: x - w/2, y + h/2
    double jk_mx = 0;
    u64 jk_a_at = 0;
    // A tap on Jill's scene: the next line. The text box only takes A while the game's cursor is
    // on it (global.dframe_good_position, worked out in cursor_hitbox's Begin Step from
    // place_meeting with dialog_frame, whose mask is x 17..428, y 276..340), and a touch on the
    // top screen can leave the cursor anywhere. So the cursor goes into the box first (its corner,
    // not over the text), then A.
    // its bottom-right corner, where the game's own cursor rests: inside the box, off the text
    static constexpr double kDlgBoxX = 410, kDlgBoxY = 330;
    int dlg_taps = 0;
    // The easter egg: the heart under Age says the game's "itai!" (itai1) each tap and opens the
    // artist's note on the third; closing it starts the count over.
    static constexpr int kSndItai = 103;
    int egg_taps = 0;
    bool egg_open = false;
    void DialogueNextCalls() {
        jq.push_back({kMouseXGet, {}, [this](const RV& r) { jk_mx = r.kind == 0 ? r.num : -1e9; }, true});
        jq.push_back({kMouseYGet, {}, [this](const RV& r) {
                          if (r.kind != 0 || jk_mx < -1e8) { // the pointer can't be read: A as it is
                              jk_a_at = ticks;
                              return;
                          }
                          JQ(kVariableGlobalSet, {{0, "MOUSE_SHIFT_X"}, {kDlgBoxX - jk_mx}});
                          JQ(kVariableGlobalSet, {{0, "MOUSE_SHIFT_Y"}, {kDlgBoxY - r.num}},
                             [this](const RV&) { jk_a_at = ticks + 2; }); // the hitbox moves next frame
                      }, true});
    }
    void JukeDoneStep() {
        if (jk_nav < 0) {
            jk_done_steps = -1;
            return;
        }
        if (jk_done_steps == 0) { // first try: jump
            jq.push_back({kMouseXGet, {}, [this](const RV& r) { jk_mx = r.kind == 0 ? r.num : -1e9; }, true});
            jq.push_back({kMouseYGet, {}, [this](const RV& r) {
                              if (r.kind != 0 || jk_mx < -1e8) {
                                  Log("juke: Ready: the pointer can't be read, walking there");
                                  jk_done_steps = 1;
                                  return;
                              }
                              VLog("juke: Ready: cursor jumps from " + std::to_string(int(jk_mx)) + "," +
                                  std::to_string(int(r.num)));
                              JQ(kVariableGlobalSet, {{0, "MOUSE_SHIFT_X"}, {kDoneX - jk_mx}});
                              JQ(kVariableGlobalSet, {{0, "MOUSE_SHIFT_Y"}, {kDoneY - r.num}});
                              JQ(kVariableInstanceSet, {{jk_nav}, {0, "cur_x"}, {kDoneX}});
                              JQ(kVariableInstanceSet, {{jk_nav}, {0, "cur_y"}, {kDoneY}});
                              JQ(kVariableInstanceSet, {{jk_nav}, {0, "cur_ind"}, {double(kJukeDone)}});
                              JQ(kVariableInstanceSet, {{jk_nav}, {0, "exactly_placed"}, {1}}, [this](const RV&) {
                                  jk_a_at = ticks + 3;
                                  jk_done_steps = -1;
                              });
                          }, true});
            return;
        }
        // fallback: cur_ind only means something while exactly_placed (a touch leaves it where it
        // was); the first d-pad press snaps the cursor to the grid point nearest the pointer
        JQ(kVariableInstanceGet, {{jk_nav}, {0, "exactly_placed"}}, [this](const RV& r) {
            jk_placed = r.kind == 0 && r.num >= 0.5;
        });
        JQ(kVariableInstanceGet, {{jk_nav}, {0, "cur_ind"}}, [this](const RV& r) {
            jk_cur = r.kind == 0 ? int(r.num) : -1;
            const bool on_done = jk_placed && jk_cur == kJukeDone;
            VLog("juke: walk to Ready: cursor " + std::to_string(jk_cur) + (jk_placed ? "" : " (off the grid)") +
                " -> " + (on_done ? "A" : "DOWN"));
            if (on_done) {
                jk_btn = 2;
                jk_done_steps = -1;
            } else if (++jk_done_steps > 12) {
                Log("juke: the cursor never reached Done");
                jk_done_steps = -1;
            } else {
                jk_btn = 1;
            }
        });
    }
    std::string jk_call_desc;
    bool jk_stuck_logged = false;
    std::string JukeDesc(const Call& c) const {
        std::string d = "fn+" + std::to_string(c.fn) + "(";
        for (const Arg& a : c.args)
            d += (a.last ? std::string("<last>") : a.text ? '"' + std::string(a.text) + '"' : std::to_string(a.num)) + ",";
        return d + ")";
    }
    // global.language -> the script folder: "en" is eng, "ch" chn; the others are the folder's name.
    void GameLanguage(const RV& r) {
        if (r.kind != 1 || r.text.empty()) return;
        const std::string want = r.text == "en" ? "eng" : r.text == "ch" ? "chn" : r.text;
        for (int i = 0; i < kLangN; ++i) {
            if (want != kLangs[i]) continue;
            if (!lang_from_game || i != lang) Log("jill: the game plays in \"" + r.text + "\": scripts of \"" + kLangs[i] + "\"");
            lang_from_game = true;
            if (i != lang) {
                lang = i;
                lang_file = 1;
                lang_misses = 0;
                lang_hit = false;
                lines.clear();
                live = anchor = -1;
            }
            return;
        }
    }
    void JukeTick() {
        if (juke_wait && !jk_stuck_logged && ticks - juke_call_tick > 90) { // the game never came back
            jk_stuck_logged = true;
            Log("juke: STUCK in " + jk_call_desc);
        }
        if (!has_mailbox || !MailboxAlive() || snd_stage) return;
        if (juke_wait) {
            u32 done = 0;
            if (!ext.load_u32(ext.userdata, kMbDone, &done) || done != mb_seq) {
                if (ticks - juke_call_tick > 60) {
                    Log("juke: a call got no answer");
                    juke_wait = false;
                    jq.clear();
                    jq_at = 0;
                    juke_next = ticks + 60;
                }
                return;
            }
            juke_wait = false;
            ext.load_u64(ext.userdata, kMbRet, &jk_last_v);
            ext.load_u64(ext.userdata, kMbRet + 8, &jk_last_fk);
            const auto then = jq[jq_at].then; // it may queue more calls
            ++jq_at;
            if (then) then(MbResult());
        }
        if (jk_a_at && ticks >= jk_a_at && !jk_btn) {
            jk_a_at = 0;
            jk_btn = 2;
        }
        if (!sound_q.empty()) return; // a sound goes first (SoundTick), then the next call
        if (jq_at >= jq.size()) {
            jq.clear();
            jq_at = 0;
            if (dlg_taps > 0 && !jk_btn && !jk_a_at) { // a tap on Jill's scene: the next line
                --dlg_taps;
                DialogueNextCalls();
                if (MbCallRV(jq[0].fn, jq[0].args, jq[0].getter)) {
                    juke_wait = true;
                    juke_call_tick = ticks;
                } else {
                    jq.clear();
                    jk_btn = 2;
                }
                return;
            }
            if (slot_tap >= 0 && !jk_btn && !jk_a_at) { // a slot button: the cursor there, then A
                const int t = slot_tap;
                slot_tap = -1;
                CursorToAndA(t == 0 ? 489 : 563, 101);
                if (MbCallRV(jq[0].fn, jq[0].args, jq[0].getter)) {
                    juke_wait = true;
                    juke_call_tick = ticks;
                } else {
                    jq.clear();
                }
                return;
            }
            // which slot is at the front, and its shaker: only while mixing (the buttons are out
            // otherwise), so the talk's taps for the next line never wait behind these reads
            if (room == kRoomBar && mode == 1 && ticks >= slot_next) {
                slot_next = ticks + 12;
                SlotPollCalls();
                if (MbCallRV(jq[0].fn, jq[0].args)) {
                    juke_wait = true;
                    juke_call_tick = ticks;
                } else {
                    jq.clear();
                }
                return;
            }
            if (room == kRoomBar && ticks >= money_next) { // the night's earnings, as the bar shows them
                money_next = ticks + 60;
                JGlobal("cur_day", [this](const RV& r) {
                    const bool free = r.kind == 0 && r.num <= -2 && r.num >= -4; // the tutorial days
                    JGlobal(free ? "freebar" : "barscore", [this, free](const RV& v) {
                        if (free && v.kind == 1) money_text = v.text;
                        else if (!free && v.kind == 0) money_text = "$" + std::to_string(s64(std::llround(v.num)));
                    });
                });
                if (MbCallRV(jq[0].fn, jq[0].args)) {
                    juke_wait = true;
                    juke_call_tick = ticks;
                } else {
                    jq.clear();
                }
                return;
            }
            if (ticks >= lang_next) { // which scripts to read Jill's lines from: the game's own language
                lang_next = ticks + 300;
                JGlobal("language", [this](const RV& r) { GameLanguage(r); });
                if (!jq.empty()) {
                    if (MbCallRV(jq[0].fn, jq[0].args)) {
                        juke_wait = true;
                        juke_call_tick = ticks;
                    } else {
                        jq.clear();
                    }
                    return;
                }
            }
            if (room >= 0 && room != kRoomBar) {
                JukeClosed();
                if (jk_open) Log("juke: closed");
                jk_open = jk_ready = false;
                return;
            }
            if (jk_ready && !jk_cmds.empty()) {
                const auto c = jk_cmds.front();
                jk_cmds.erase(jk_cmds.begin());
                JukeCommand(c.first, c.second);
                if (jq.empty()) return;
            } else if (jk_ready && jk_fade && ticks >= jk_fade_at && jk_songs >= 0) {
                JQ(kDsListFindValue, {{jk_songs}, {double(jk_fade)}});
                JQ(kAudioStopSound, {{0, nullptr, true}});
                jk_fade = 0;
            } else if (jk_ready && jk_done_steps >= 0 && !jk_btn && ticks - jk_btn_tick >= 10) {
                JukeDoneStep();
            } else if (kJukeboxMirror && ticks >= juke_next) {
                juke_next = ticks + (jk_open ? 8 : 30);
                JQ(kInstanceExists, {{double(kObjJukeboxBg)}}, [this](const RV& r) {
                    const bool open = r.num >= 0.5;
                    if (!open) {
                        JukeClosed();
                        if (jk_open) Log("juke: closed");
                        jk_open = jk_ready = false;
                        jk_preview = jk_fade = 0;
                        jk_a_at = 0;
                        jk_cmds.clear();
                        jk_done_steps = -1;
                        return;
                    }
                    if (!jk_open) {
                        jk_open = true;
                        jk_list = jk_names_list = jk_short = jk_nav = -1;
                        JukeOpenReads(); // then the first JukeCycle, once the list ids are in
                    } else {
                        JukeCycle();
                    }
                });
            } else {
                return;
            }
        }
        if (jq_at >= jq.size()) return; // a step that had nothing to ask
        if (MbCallRV(jq[jq_at].fn, jq[jq_at].args, jq[jq_at].getter)) {
            jk_call_desc = JukeDesc(jq[jq_at]);
            VLog("juke: call " + jk_call_desc);
            juke_wait = true;
            juke_call_tick = ticks;
        } else {
            jq.clear();
            jq_at = 0;
            juke_next = ticks + 60;
        }
    }
    std::string JukeSongText(int k) const {
        if (!JukeUnlocked(k)) return jk_locked;
        const auto it = jk_name.find(5 * jk_page + k);
        return it == jk_name.end() ? "" : it->second;
    }
    std::string JukeSlotText(int i) const {
        const auto it = jk_slot[i] ? jk_name.find(jk_slot[i]) : jk_name.end();
        return it == jk_name.end() ? "" : it->second;
    }
    void JukePublish(const EdenDsmodHostApi* h) const {
        // the A (and DOWN) presses asked of the package: the jukebox's Ready, and the tap for the
        // next line on Jill's scene, which goes on without the mirror
        h->publish_i64(h->userdata, "va_jk_down", jk_btn == 1 ? 1 : 0);
        h->publish_i64(h->userdata, "va_jk_a", jk_btn == 2 ? 1 : 0);
        if (!kJukeboxMirror) return;
        const bool show = jk_open && jk_ready;
        const bool closing = !show && jk_close_tick && ticks - jk_close_tick < kJukeCloseTicks;
        const int bands = show ? int(std::min<u64>(kJukeBands, (ticks - jk_show_tick + 1) * kJukeBands / kJukeWipeTicks))
                        : closing ? int(kJukeBands - (ticks - jk_close_tick) * kJukeBands / kJukeCloseTicks)
                                  : 0;
        h->publish_i64(h->userdata, "va_jk_open", show ? 1 : 0);
        h->publish_i64(h->userdata, "va_jk_vis", show || closing ? 1 : 0);
        h->publish_i64(h->userdata, "va_jk_w", bands);
        h->publish_i64(h->userdata, "va_jk_close", closing ? 1 : 0);
        h->publish_i64(h->userdata, "va_jk_page", jk_page);
        h->publish_i64(h->userdata, "va_jk_full", JukeFull() ? 1 : 0);
        h->publish_i64(h->userdata, "va_jk_prev", jk_page > 0 ? 1 : 0);
        h->publish_i64(h->userdata, "va_jk_next", jk_page < kJukePageMax ? 1 : 0);
        h->publish_text(h->userdata, "va_jk_title", jk_please.c_str());
        h->publish_text(h->userdata, "va_jk_pages", (std::to_string(jk_page + 1) + jk_maxpages).c_str());
        static const auto names = [] {
            std::vector<std::string> n;
            for (int k = 1; k <= 5; ++k) n.push_back("va_jk_song" + std::to_string(k));
            for (int k = 1; k <= 5; ++k) n.push_back("va_jk_lock" + std::to_string(k));
            for (int k = 1; k <= 5; ++k) n.push_back("va_jk_pv" + std::to_string(k));
            for (int i = 0; i < kJukeSlots; ++i) n.push_back("va_jk_slot" + std::to_string(i));
            for (int i = 0; i < kJukeSlots; ++i) n.push_back("va_jk_tag" + std::to_string(i));
            for (int i = 0; i < kJukeSlots; ++i) n.push_back("va_jk_sel" + std::to_string(i));
            return n;
        }();
        for (int k = 1; k <= 5; ++k) {
            h->publish_text(h->userdata, names[size_t(k - 1)].c_str(), JukeSongText(k).c_str());
            h->publish_i64(h->userdata, names[size_t(4 + k)].c_str(), JukeUnlocked(k) ? 0 : 1);
            h->publish_i64(h->userdata, names[size_t(9 + k)].c_str(), jk_preview && jk_preview == 5 * jk_page + k ? 1 : 0);
        }
        for (int i = 0; i < kJukeSlots; ++i) {
            h->publish_text(h->userdata, names[size_t(15 + i)].c_str(), JukeSlotText(i).c_str());
            h->publish_text(h->userdata, names[size_t(27 + i)].c_str(), jk_tag[i].c_str());
            h->publish_i64(h->userdata, names[size_t(39 + i)].c_str(), jk_sel == i ? 1 : 0);
        }
    }

    // One finger sample per tick while the shaker is carried. The package cannot hand the module
    // a live position (module arguments are constants), so it sends the finger's movement this
    // tick as a step: 1 fast left/up, 2 left/up, 3 still, 4 right/down, 5 fast right/down
    // (the larger of the two axes; see the "shk_*" derived values in the manifest).
    void Shake(s64 step) {
        if (step < 1 || step > 5) return;
        if (!has_pos || ticks - last_shake > 20) { // a new grab
            dir = 0;
            run = 0;
            rev_ticks[0] = rev_ticks[1] = 0;
            refused = false;
        }
        last_shake = ticks;
        has_pos = true;
        if (started && by_button) by_button = false; // grabbed mid-mix: the finger takes over
        last_step = int(step);
        const int sgn = step < 3 ? -1 : step > 3 ? 1 : 0;
        if (sgn != 0) {
            if (sgn != dir) {
                if (dir != 0 && run >= kReversalTicks) Reversal();
                dir = sgn;
                run = 0;
            }
            ++run;
        }
        const bool shaking = rev_ticks[0] != 0 && ticks - rev_ticks[0] <= kShakeWindow;
        // start the mix (never with an empty shaker, nor again on a drink already finished)
        if (shaking && !started && !refused && result == 0 && HasPour()) BeginMix(false);
    }

    // Start a mix: press ZR in the game, and watch that the game really starts mixing.
    void BeginMix(bool button) {
        started = true;
        by_button = button;
        mix_ticks = 0;
        done = 0;
        ++zr_pending;
        start_check = true;
    }
    // A mix that never was (the game ignored our ZR): back to the poured, unmixed shaker.
    void CancelMix() {
        started = false;
        by_button = false;
        start_check = false;
        mix_ticks = 0;
        done = 0;
    }

    // Error windows after a failed drink: they pop up one by one, each with a short glitch,
    // glitch again now and then, and block the bar until every one is tapped away.
    bool ErrVisible(int i) const {
        return result == 2 && ticks >= err_tick + u64(i) * kErrStep && !(err_closed >> i & 1);
    }
    int ErrFrame(int i) const { // 0 clean, 1 / 2 the artist's glitch frames
        const u64 age = ticks - (err_tick + u64(i) * kErrStep);
        if (age < 8) return 1 + int((age / 2) % 2);
        if ((ticks + u64(i) * 37) % 150 < 3) return 1 + int(i % 2);
        return 0;
    }
    bool ErrAny() const {
        return result == 2 && err_closed != (1u << kErrN) - 1;
    }

    bool HasPour() const {
        for (int c : counts)
            if (c > 0) return true;
        return game_ok && fedora; // A Fedora in the game's shaker (cleared by Reset)
    }
    // Only a bottled item in the shaker (A Fedora): mixed, it is that drink.
    bool OnlyBottled() const {
        for (int c : counts)
            if (c > 0) return false;
        return game_ok && fedora;
    }

    void NewDrink() {
        if ((result == 1 || result == 2) && !DrinkSrc().empty()) { // a serve, unless a Reset says otherwise
            // two customers at once: the first slot's drink waits for the second one
            if (slot_amount == 2 && !finished_src.empty() && finished_src != DrinkSrc()) finished_prev = finished_src;
            VLog("drink finished: " + DrinkSrc() + " (slots " + std::to_string(slot_amount) + ", waiting " +
                (finished_prev.empty() ? std::string("none") : finished_prev) + ")");
            finished_src = DrinkSrc();
            finished_tick = ticks;
        }
        full_after = false;
        fedora = false;
        done = 0;
        result = 0;
        drink = -1;
        big = false;
        err_closed = 0;
        ice = aged = false;
        for (int& c : counts) c = 0;
    }

    void ShakeEnd(bool button = false, bool press_zr = true) {
        has_pos = false;
        if (started && by_button && !button) return; // a Mix-button mix ends with Mix
        if (started) {
            by_button = false; // stop the mix: any mix is a finished drink, 5 s or more is blended
            started = false;
            done = mix_ticks >= kBlendTicks ? 2 : 1;
            drink = MatchRecipe(counts, ice, aged, done == 2, &big);
            if (drink < 0 && OnlyBottled()) {
                drink = kFedora;
                big = false;
            }
            result = drink >= 0 ? 1 : 2;
            if (result == 2) PlaySound(kSndError, 1.0); // the error windows start: the game's buzz
            err_tick = ticks;
            err_closed = 0;
            done_tick = ticks;
            if (press_zr) ++zr_pending;
            if (mode < 0) manual = true; // like Mix: while unsure, the overlay comes back
        }
    }

    // The artist's shaker loop (12 frames, 140 ms each) plays while the drink mixes. Once it is
    // blended the shake turns violent: only the hard-tilt poses (1 3 1 0 7 9 7 0), 35 ms each, plus
    // a jitter of a few pixels (va_jx / va_jy). Frame 0 (upright) when not mixing.
    // The artist's swing, measured from her GIF: tilt (degrees) and sideways sway (GIF px).
    static constexpr int kSwingAngle[12] = {0, 6, 20, 44, 20, 6, 0, -13, -27, -45, -27, -13};
    static constexpr int kSwingSway[12] = {0, 26, 44, 52, 44, 26, 0, -10, -31, -46, -31, -10};

    int SwingStep() const {
        if (!started) return -1;
        const u64 ms = mix_ticks * 1000 / 60;
        if (Violent()) {
            static constexpr int kSeq[8] = {1, 3, 1, 0, 7, 9, 7, 0};
            return kSeq[(ms / 35) % 8];
        }
        return int((ms / 140) % 12);
    }
    // The shaker's tilt follows the finger: its sideways speed sets the lean it springs towards
    // (right = clockwise), so a shake swings it side to side and it wobbles upright when the
    // finger stops or lets go. Once blended a small angle rattle rides on top.
    static constexpr double kTiltPerPx = 1.6;  // degrees of lean per px/tick of finger speed
    static constexpr double kTiltMax = 50;     // degrees
    static constexpr double kSpring = 0.2;     // pull towards the lean, per tick
    static constexpr double kDamp = 0.3;       // keeps a few wobbles, then settles
    void Physics() {
        double v = ticks - tilt_tick <= 2 ? tilt_in : 0.0;
        if (started && by_button && ticks - tilt_tick > 2) v = ButtonSwing(); // shakes by itself
        tilt_vf += (v - tilt_vf) * 0.45;
        const double target = std::clamp(tilt_vf * kTiltPerPx, -kTiltMax, kTiltMax);
        tilt_w += kSpring * (target - tilt_a) - kDamp * tilt_w;
        tilt_a = std::clamp(tilt_a + tilt_w, -70.0, 70.0);
        if (std::fabs(tilt_a) < 0.05 && std::fabs(tilt_w) < 0.05 && v == 0) tilt_a = tilt_w = 0;
    }
    // A Mix-button mix rocks the shaker side to side on its own (the game's look): a sine "finger
    // speed" into the same physics, faster and harder once the drink is blended.
    double ButtonPhase() const {
        const double period = Violent() ? 12.0 : 30.0; // ticks per swing
        return 2 * 3.14159265358979 * double(mix_ticks) / period;
    }
    double ButtonSwing() const {
        return (Violent() ? 22.0 : 12.0) * std::cos(ButtonPhase());
    }
    int Angle() const {
        return int(std::lround(tilt_a)) + Jitter(2, 6);
    }
    int Sway() const { // the finger moves the shaker; a Mix-button mix sways it in place
        if (!(started && by_button)) return 0;
        return int(std::lround((Violent() ? 10.0 : 16.0) * std::sin(ButtonPhase())));
    }

    bool Violent() const {
        return started && mix_ticks >= kBlendTicks;
    }

    int Wiggle() const {
        if (!started) return 0;
        const u64 ms = mix_ticks * 1000 / 60;
        if (Violent()) {
            static constexpr int kSeq[8] = {1, 3, 1, 0, 7, 9, 7, 0};
            return kSeq[(ms / 35) % 8];
        }
        return int((ms / 140) % 12);
    }

    // Small pseudo-random offsets that change every 2 ticks while the shake is violent.
    int Jitter(int axis, int amp) const {
        if (!Violent()) return 0;
        u64 h = (mix_ticks / 2) * 0x9E3779B97F4A7C15ULL + u64(axis) * 0xC2B2AE3D27D4EB4FULL;
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 32;
        return int(h % u64(2 * amp + 1)) - amp;
    }

    // The artist's Success popup (27 frames, 30 ms each; the glitch is frames 10-14), looping
    // from the glitch while a finished drink waits to be served.
    int SuccessFrame() const {
        if (!done) return 0;
        const u64 ms = (ticks - done_tick) * 1000 / 60;
        return int((ms / 30 + 10) % 27);
    }

    // ---- the game saving (found with the bar detective's save watch, 15 Oct 2026) ----
    // Every save file starts with its time, "DD/MM/YYYY HH:MM:SS", and the game keeps those texts
    // in H+0x700000..H+0xA00000 (the load menu's list, and the one just written). A time not seen
    // before that is within a few minutes of the device clock is a save that just happened (old
    // ones show up too whenever the load menu lists them). Checked once a second.
    std::vector<std::string> stamps_seen;
    u64 saved_tick = 0;
    bool saved_once = false;
    void SaveWatch() {
        if (ticks % 60 != 0 || !host.get_heap_begin || !host.get_i64) return;
        const u64 hb = host.get_heap_begin(host.userdata);
        if (!hb) return;
        const s64 now_day = host.get_i64(host.userdata, "clk_date", -1), now_sec = host.get_i64(host.userdata, "clk_tod", -1);
        static constexpr char kStamp[] = "00/00/0000 00:00:00";
        constexpr size_t kStampLen = sizeof kStamp - 1;
        for (u64 a = (hb + 0x700000) & ~(kPage - 1); a < hb + 0xA00000; a += kPage) {
            const u8* p = host.get_read_pointer ? host.get_read_pointer(host.userdata, a, kPage) : nullptr;
            if (!p) continue;
            for (size_t o = 0; o + kStampLen <= kPage; ++o) {
                if (p[o] < '0' || p[o] > '3' || p[o + 2] != '/') continue;
                bool ok = true;
                for (size_t k = 0; k < kStampLen && ok; ++k) {
                    const u8 c = p[o + k];
                    ok = kStamp[k] == '0' ? (c >= '0' && c <= '9') : c == u8(kStamp[k]);
                }
                if (!ok) continue;
                std::string st(reinterpret_cast<const char*>(p + o), kStampLen);
                if (std::find(stamps_seen.begin(), stamps_seen.end(), st) != stamps_seen.end()) continue;
                stamps_seen.push_back(st);
                const auto num = [&](size_t at, size_t n) { return s64(std::strtol(st.substr(at, n).c_str(), nullptr, 10)); };
                const s64 day = num(6, 4) * 10000 + num(3, 2) * 100 + num(0, 2);
                const s64 sec = num(11, 2) * 3600 + num(14, 2) * 60 + num(17, 2);
                const bool fresh = now_day > 0 && day == now_day && std::llabs(now_sec - sec) <= 180;
                Log("save time " + st + (fresh ? ": the game just saved" : ": an older save"));
                if (fresh) {
                    saved_tick = ticks;
                    saved_once = true;
                }
            }
        }
    }

    // ---- the game loading a save (bar detective change finder, 15 Oct 2026) ----
    // Loading slot 2, slot 2 again and slot 1 from a conversation each turned S+0x170 from 3 to -2
    // (and S+0x10 from 1 to 0); both were back by the next conversation. -2 = a save just loaded.
    // As the loaded scene starts it flickers -2, 0, -2, 0, 3 within a few ticks, so a -2 counts only
    // after at least 2 s of anything else.
    static constexpr u64 kLoadOff = 0x170, kLoadOff2 = 0x10, kLoadSettle = 120;
    double load_v = 99, load_v2 = 99;
    u64 load_off_tick = 0; // when S+0x170 last left -2
    u64 loaded_tick = 0;
    bool loaded_once = false;
    void LoadWatch() {
        double v, v2 = 99;
        if (!cur_x || !ReadF64(cur_x + kLoadOff, v)) return;
        ReadF64(cur_x + kLoadOff2, v2);
        if (v != load_v || v2 != load_v2) {
            char b[96];
            std::snprintf(b, sizeof b, "load watch: S+0x170 %g -> %g, S+0x10 %g -> %g", load_v, v, load_v2, v2);
            VLog(b);
            if (load_v == -2 && v != -2) load_off_tick = ticks;
            if (v == -2 && load_v != -2 && load_v != 99 && ticks - load_off_tick >= kLoadSettle) {
                Log("a save was loaded: the tutorials start over");
                loaded_tick = ticks;
                loaded_once = true;
            }
            load_v = v;
            load_v2 = v2;
        }
    }

    // ---- Jill on the stand-by screen (Bar Detective's dialogue watch, 15 Oct 2026) ----
    // S-0x60 is the dialogue line as the game types it out ("J", "Ji", ... "Jill: Here. Happy
    // now?"). The module reads the player's own script files (romfs scripts/<lang>/scriptN.txt;
    // the same line numbers in every language) and finds the line being typed: its text is the
    // script line without its [..] tags, '#' line breaks kept. The line says who talks (Jill's
    // carry [XS:jilltalk,1]) and where we are in the day, which picks her face: the game's own
    // [XS:jillface,..] tags (only the script11 scene has them) plus our table (va11_jill_faces.h).
    // A face holds until the next one is set; each script starts neutral. While Jill's line is
    // still growing her mouth moves, except for her thoughts ("Jill: (...)"); she blinks every
    // few seconds.
    enum Face : u8 { kNeutral, kSmile, kSurprise, kUh, kSad, kThink, kBaka };
    static constexpr u64 kDlgTextOff = 0x60;   // below the switch
    static constexpr u64 kTalkHold = 10;       // ticks without a new letter = she stopped talking
    static constexpr u64 kLipTicks = 5;        // a new mouth shape every ~83 ms
    // Every dialogue file: script1..19 (one per day), then the rest (the same order as
    // tools/va11-hall-a/gen_jill_faces.py: a table's file number is its place here). tutorial_switch comes
    // before tutorial: they share their first 34 lines and the Switch game plays tutorial_switch.
    static constexpr const char* kFiles[] = {
        "script1", "script2", "script3", "script4", "script5", "script6", "script7", "script8",
        "script9", "script10", "script11", "script12", "script13", "script14", "script15", "script16",
        "script17", "script18", "script19", "tutorial_switch", "tutorial", "epilogues", "gameover",
        "1.2/anna", "1.2/pro1", "1.2/pro2", "1.2/pro3", "anna_script.ini"};
    static constexpr int kScripts = int(sizeof kFiles / sizeof kFiles[0]);
    static constexpr int kLangMisses = 6;      // finished lines found nowhere before trying another language
    static constexpr const char* kLangs[] = {"eng", "c_spa", "chn", "cze", "dan", "dutch", "frn", "ger",
                                             "hun", "ita", "jp", "kor", "pol", "por", "rus"};
    static constexpr int kLangN = int(sizeof kLangs / sizeof kLangs[0]);
    struct ScriptLine {
        std::string text; // without tags
        u16 line;         // 1-based line number in its file
        u8 script;        // 1..kScripts (kFiles[script - 1])
        u8 face;          // her face from this line on
        bool jill;        // Jill says it
        bool thought;     // in brackets: "Jill: (...)" is her thinking, the mouth stays shut
        std::string shows, hides; // the customers this line brings in / takes out (CastKeys)
    };
    std::vector<ScriptLine> lines;
    int lang = 0, lang_file = 0, lang_misses = 0; // language tried; next file to load (kScripts+1 = all)
    bool lang_from_game = false; // global.language was read: no more guessing from missed lines
    // The money on the bar, as the game's scorecount_obj draws it: "$" + global.barscore, or the
    // text global.freebar on the tutorial days (global.cur_day -2..-4). Read once a second at the bar.
    std::string money_text;
    u64 money_next = 0;
    // The two slots (the nights with two customers at once: global.slotamount 2). The game keeps
    // each slot's shaker apart -- mod_<ingredient><a|b> (mod_aa .. mod_ea, mod_ab .. mod_eb), its
    // Ice (otr_a / otr_b) and Age (age_a / age_b), mixed_a / mixed_b once mixed -- and
    // global.change_slot says which one is at the front. The bar reads slot 1 from memory as ever;
    // while slot 2 is at the front its contents come from those globals. A tap on a slot button
    // puts the game's cursor on that slot (where the game's own mix_action puts it: 489,101 /
    // 563,101) and presses A, so the game's slot code does the switch.
    int slot_amount = 1, slot_cur = 0;
    bool slot_mixed[2] = {};
    int slotb_counts[5] = {};
    bool slotb_ice = false, slotb_aged = false, slotb_fresh = false;
    u64 slot_next = 0;
    std::string slot_logged;
    u64 pour_tick[5] = {}; // the last pour of each ingredient from the bottom screen
    bool SlotB() const { return slot_amount == 2 && slot_cur == 1; }
    void SlotPollCalls() {
        JGlobal("slotamount", [this](const RV& r) { if (r.kind == 0) slot_amount = int(r.num); });
        JGlobal("change_slot", [this](const RV& r) { if (r.kind == 0) slot_cur = r.num != 0 ? 1 : 0; });
        JGlobal("mixed_a", [this](const RV& r) { slot_mixed[0] = r.kind == 0 && r.num != 0; });
        JGlobal("mixed_b", [this](const RV& r) {
            slot_mixed[1] = r.kind == 0 && r.num != 0;
            {
                const std::string st = "slots " + std::to_string(slot_amount) + ", front " + std::to_string(slot_cur + 1) +
                                       ", mixed " + std::to_string(slot_mixed[0]) + "/" + std::to_string(slot_mixed[1]);
                if (st != slot_logged) VLog("slot state: " + (slot_logged = st));
            }
            if (!SlotB()) return;
            static const char* const kB[] = {"mod_ab", "mod_bb", "mod_cb", "mod_db", "mod_eb"};
            for (int i = 0; i < 5; ++i)
                JGlobal(kB[i], [this, i](const RV& v) { if (v.kind == 0) slotb_counts[i] = int(v.num); });
            JGlobal("otr_b", [this](const RV& v) { slotb_ice = v.kind == 0 && v.num != 0; });
            JGlobal("age_b", [this](const RV& v) {
                slotb_aged = v.kind == 0 && v.num != 0;
                slotb_fresh = true;
            });
        });
    }
    void CursorToAndA(double gx, double gy) { // the game's cursor to (gx, gy), then A
        jq.push_back({kMouseXGet, {}, [this](const RV& r) { jk_mx = r.kind == 0 ? r.num : -1e9; }, true});
        jq.push_back({kMouseYGet, {}, [this, gx, gy](const RV& r) {
                          if (r.kind != 0 || jk_mx < -1e8) return; // can't place it: nothing pressed
                          JQ(kVariableGlobalSet, {{0, "MOUSE_SHIFT_X"}, {gx - jk_mx}});
                          JQ(kVariableGlobalSet, {{0, "MOUSE_SHIFT_Y"}, {gy - r.num}},
                             [this](const RV&) { jk_a_at = ticks + 2; });
                      }, true});
    }
    int slot_tap = -1; // a slot button tapped, waiting for its turn
    u64 lang_next = 0;
    bool lang_hit = false;
    int live = -1;            // the line being typed (index into lines), -1 = unknown
    int anchor = -1;          // the last line that finished typing (continuity for repeated texts)
    bool settled = true;      // the current text stopped growing and was checked
    u8 face = kNeutral;
    std::string dlg_text;
    u64 dlg_grow_tick = 0, lip_tick = 0, blink_tick = 0, blink_next = 200;
    bool jill_line = false;
    int lip = 0;
    u64 Rand(u64 salt) const { // 0..2^32, from the tick: no state, same on every replay
        u64 h = ticks * 0x9E3779B97F4A7C15ULL + salt * 0xC2B2AE3D27D4EB4FULL;
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ULL;
        return (h ^ (h >> 32)) & 0xFFFFFFFFULL;
    }
    static bool JillSpeaks(const std::string& t) { // before a line is known: "Jill: ..."
        static constexpr char kName[] = "Jill:";
        const size_t n = std::min(t.size(), sizeof kName - 1);
        return n > 0 && t.compare(0, n, kName, n) == 0;
    }
    static int FaceNamed(const std::string& n) {
        static constexpr const char* kNames[] = {"", "smile", "surprise", "uh", "sad", "think", "baka"};
        for (int i = 0; i < 7; ++i)
            if (n == kNames[i]) return i;
        return -1;
    }
    // A customer comes in or leaves: [SHOW:x,sprite_<who>], [XS:<who>hide,1]. Gillian and Dana
    // (the staff, in and out between the customers) don't count, nor does [XS:client,..] (it sets
    // up the next one lines before anyone walks in: the tutorial ends on it).
    // Who a line shows ([SHOW:x,sprite_<who>]) and hides ([XS:<who>hide,1]), as 3-letter keys
    // ("sprite_seiwound" and "seihide" are both "sei", "stelhide" is "ste" like "sprite_stella"),
    // Gillian and Dana (the staff, in and out between the customers) left out.
    static std::string CastKeys(const std::string& raw, bool shows) {
        std::string keys;
        const auto add = [&](const std::string& who) {
            if (who.size() < 3 || who.rfind("gil", 0) == 0 || who.rfind("dan", 0) == 0) return;
            keys += who.substr(0, 3);
        };
        if (shows) {
            for (size_t at = raw.find("[SHOW:"); at != std::string::npos; at = raw.find("[SHOW:", at + 1)) {
                const size_t sp = raw.find("sprite_", at), end = raw.find(']', at);
                if (sp != std::string::npos && sp < end) add(raw.substr(sp + 7, end - sp - 7));
            }
        } else {
            for (size_t at = raw.find("hide,1]"); at != std::string::npos; at = raw.find("hide,1]", at + 1)) {
                const size_t open = raw.rfind("[XS:", at);
                if (open != std::string::npos) add(raw.substr(open + 4, at - open - 4));
            }
        }
        return keys;
    }
    // One script file of the language being tried into lines[] (one file per tick).
    void LoadScriptFile() {
        if (lang_file < 1 || lang_file > kScripts || !host.read_romfs) return;
        char path[64];
        const char* file = kFiles[lang_file - 1];
        const bool ini = std::strstr(file, ".ini") != nullptr; // [section] / l_N = "line" / count = N
        std::snprintf(path, sizeof path, ini ? "scripts/%s/%s" : "scripts/%s/%s.txt", kLangs[lang], file);
        const size_t size = host.read_romfs(host.userdata, path, 0, nullptr, 0);
        std::string d(size, '\0');
        if (size) d.resize(host.read_romfs(host.userdata, path, 0, d.data(), size));
        if (d.size() >= 3 && d.compare(0, 3, "\xEF\xBB\xBF") == 0) d.erase(0, 3);
        size_t t = 0; // our table, sorted by script then line
        while (t < std::size(kJillFaces) && kJillFaces[t].script < lang_file) ++t;
        u8 cur = kNeutral;
        int n = 0;
        for (size_t at = 0; at < d.size();) {
            size_t end = d.find('\n', at);
            if (end == std::string::npos) end = d.size();
            std::string raw = d.substr(at, end - at);
            at = end + 1;
            ++n;
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();
            if (ini) { // only the l_N = "..." lines carry dialogue
                const size_t q0 = raw.find('"'), q1 = raw.rfind('"');
                if (raw.compare(0, 2, "l_") != 0 || q0 == std::string::npos || q1 <= q0) continue;
                raw = raw.substr(q0 + 1, q1 - q0 - 1);
            }
            for (; t < std::size(kJillFaces) && kJillFaces[t].script == lang_file && kJillFaces[t].line <= n; ++t)
                if (kJillFaces[t].line == n) cur = kJillFaces[t].face;
            const size_t ft = raw.find("[XS:jillface,");
            if (ft != std::string::npos) {
                const size_t fe = raw.find(']', ft);
                const int f = fe == std::string::npos ? -1 : FaceNamed(raw.substr(ft + 13, fe - ft - 13));
                if (f >= 0) cur = u8(f);
            }
            std::string text;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '[') {
                    const size_t e = raw.find(']', i);
                    if (e != std::string::npos) {
                        i = e;
                        continue;
                    }
                }
                text += raw[i];
            }
            while (!text.empty() && text.back() == ' ') text.pop_back();
            if (text.empty()) continue;
            const size_t colon = text.find(':');
            const bool thought = colon != std::string::npos && text.compare(colon, 3, ": (") == 0;
            lines.push_back({text, u16(n), u8(lang_file), cur, raw.find("[XS:jilltalk,1]") != std::string::npos, thought,
                             CastKeys(raw, true), CastKeys(raw, false)});
        }
        if (++lang_file > kScripts) {
            char b[96];
            std::snprintf(b, sizeof b, "jill: %zu script lines of \"%s\" loaded", lines.size(), kLangs[lang]);
            Log(b);
        }
    }
    void NextLanguage() {
        lang = (lang + 1) % kLangN;
        lang_file = 1;
        lang_misses = 0;
        lines.clear();
        live = anchor = -1;
        Log(std::string("jill: the lines don't match, trying the scripts of \"") + kLangs[lang] + "\"");
    }
    // The script line the typed text t is the start of: the nearest one after the last finished
    // line (texts like "Jill: ..." repeat), else the first one anywhere. *sure: it is one of the
    // next few lines after the last finished one, or every line t could still be gives her the same
    // face -- a few letters into a scene ("Da", "Jil") match lines all over the game, and their
    // faces must not flash by.
    static constexpr int kNearLines = 3;
    int MatchLine(const std::string& t, bool* sure = nullptr) const {
        if (sure) *sure = false;
        if (t.size() < 3 || lang_file <= kScripts) return -1;
        int best = -1;
        long best_score = 0;
        int faces = 0; // bit per face among the candidates
        for (size_t i = 0; i < lines.size(); ++i) {
            const ScriptLine& l = lines[i];
            if (l.text.size() < t.size() || l.text.compare(0, t.size(), t) != 0) continue;
            faces |= 1 << l.face;
            long score = 1000000 + long(i); // anywhere
            if (anchor >= 0 && l.script == lines[size_t(anchor)].script && l.line > lines[size_t(anchor)].line)
                score = 2 * long(l.line - lines[size_t(anchor)].line); // after the last line of this day
            if (l.text.size() == t.size()) score -= 1; // a complete match wins a tie
            if (best < 0 || score < best_score) {
                best = int(i);
                best_score = score;
            }
        }
        // "Follows" means one of the next few lines: the nearest later line that starts the same
        // way can be far ahead (a branch not taken, the next scene) with a face of its own.
        const bool next = best >= 0 && anchor >= 0 && best_score < 1000000 - 1 && best - anchor <= kNearLines;
        if (sure && best >= 0) *sure = next || (faces & (faces - 1)) == 0;
        return best;
    }
    void JillTick() {
        if (lang_file == 0) lang_file = 1;
        LoadScriptFile();
        if (cur_x) {
            std::string t = ReadText(cur_x - kDlgTextOff, 400);
            while (!t.empty() && t.back() == ' ') t.pop_back(); // the index is trimmed the same way
            if (t != dlg_text) {
                if (t.size() > dlg_text.size()) dlg_grow_tick = ticks; // a letter more (or a new line)
                dlg_text = t;
                settled = false;
                bool sure = false;
                const int m = MatchLine(t, &sure);
                if (m >= 0 && sure) {
                    if (m != live) ServedSeesLine(m);
                    live = m;
                }
                else if (m >= 0 || t.size() < 6) live = -1; // not known (yet): her face holds
                jill_line = live >= 0 ? lines[size_t(live)].jill : JillSpeaks(t);
                if (live >= 0) face = lines[size_t(live)].face;
            }
        }
        // the line finished typing: remember where we are, or count a miss (another language?)
        if (!settled && ticks - dlg_grow_tick > kTalkHold) {
            settled = true;
            if (lang_file > kScripts && dlg_text.size() >= 12) {
                const int m = MatchLine(dlg_text);
                if (m >= 0 && lines[size_t(m)].text == dlg_text) {
                    if (m != live) ServedSeesLine(m);
                    live = anchor = m;
                    face = lines[size_t(m)].face;
                    lang_hit = true;
                    char b[96];
                    std::snprintf(b, sizeof b, "jill: %s line %d, %s, face %d", kFiles[lines[size_t(m)].script - 1],
                                  lines[size_t(m)].line, lines[size_t(m)].jill ? "Jill talks" : "someone else", face);
                    VLog(b);
                } else if (!lang_from_game && !lang_hit && ++lang_misses >= kLangMisses) {
                    NextLanguage();
                } else {
                    VLog("jill: no script line for \"" + dlg_text.substr(0, 60) + "\"");
                }
            }
        }
        if (JillTalking()) {
            if (ticks - lip_tick >= kLipTicks) { // another shape, never the same one twice
                lip_tick = ticks;
                const int next = 1 + int(Rand(1) % 8);
                lip = next == lip ? next % 8 + 1 : next;
            }
        } else {
            lip = 0;
        }
        if (ticks >= blink_next) {
            blink_tick = ticks;
            blink_next = ticks + 150 + Rand(2) % 180; // every 2.5..5.5 s
        }
    }
    // Where Jill is: the runner's `room` (an int in main's .bss, base v0 build 32D80C9A...: the
    // getter registered for the built-in "room" reads it). Her bar scene shows only at work; at
    // home, on the break screen and anywhere else the game's break doodles play instead -- except
    // while someone talks on a break (the dialogue text grew since entering it, within 15 s).
    static constexpr u64 kRoomVarOff = 0xB327C8;
    static constexpr int kRoomBar = 11, kRoomBreak = 12, kRooms = 47; // game.win ROOM: jill_room 10
    static constexpr u64 kBreakTalkHold = 15 * 60;
    int room = -1, last_room = -1;
    u64 room_tick = 0;
    // a real break: Jill walked out of the bar into it (loading a save passes through the break
    // room from the title screen too, and that is still the menus)
    bool break_from_bar = false;
    void RoomTick() {
        int r = -1;
        const u8* p = host.main_base && host.get_read_pointer
                          ? host.get_read_pointer(host.userdata, host.main_base + kRoomVarOff, 4)
                          : nullptr;
        if (p) {
            s32 v;
            std::memcpy(&v, p, 4);
            if (v >= 0 && v < kRooms) r = v;
        }
        if (r == room) return;
        if (r >= 0) {
            if (r == kRoomBreak) break_from_bar = last_room == kRoomBar;
            last_room = r;
        }
        room = r;
        room_tick = ticks;
        if (r >= 0 && r != kRoomBar && !served_src.empty()) { // the night is over: the counter is cleared
            served_src.clear();
            served_liq.clear();
            served2 = {};
        }
        Log("room " + std::to_string(r));
    }
    // What the overlay shows lags JillScene() by a pixel dissolve: a Bayer dither grows over the
    // screen (va_dither 1..16, the package's tiles), the scene swaps under full cover, and the
    // dither dissolves again -- between any two of the title Jill, the back street and her bar
    // scene. Only while the overlay is up; otherwise the swap is instant.
    static constexpr u64 kSceneHalf = 18; // ticks each way (0.3 s)
    int scene_shown = -1, scene_phase = 0; // phase 1 covering, 2 uncovering
    u64 scene_tick = 0;
    // What the bottom screen should show: 0 the stand-by with the title screen's Jill (menus,
    // loading, home), 1 her bar scene, 2 the stand-by with the back street (a break).
    int SceneWant() const {
        if (JillScene()) return 1;
        return room == kRoomBreak && break_from_bar ? 2 : 0;
    }
    void SceneTick() {
        const int want = SceneWant();
        if (scene_shown < 0 || !Overlay()) {
            scene_shown = want;
            scene_phase = 0;
            return;
        }
        if (scene_phase == 0 && want != scene_shown) {
            scene_phase = 1;
            scene_tick = ticks;
        } else if (scene_phase == 1 && ticks - scene_tick >= kSceneHalf) {
            scene_shown = want;
            scene_phase = 2;
            scene_tick = ticks;
        } else if (scene_phase == 2 && ticks - scene_tick >= kSceneHalf) {
            scene_phase = 0;
        }
    }
    int Dither() const { // 0 none, 2..16 in steps of 2 (chunky, like the game's fades)
        if (!scene_phase) return 0;
        const int t = int(std::min<u64>(ticks - scene_tick, kSceneHalf));
        const int level = std::clamp(2 * ((t * 8) / int(kSceneHalf) + 1), 2, 16);
        return scene_phase == 1 ? level : std::max(0, 18 - level);
    }
    // A pour (on either screen) flashes that ingredient's outline round the shaker, as the game
    // does with shaker_effect_1..5 (shakerpulse_red/yellow/blue/green/cyan): it grows 8 % and fades
    // 10 % a frame and is gone after 10 frames.
    static constexpr int kPulseFrames = 10;
    int pulse_ing = 0; // 1..5 = Adelhyde .. Karmotrine, 0 = none
    u64 pulse_tick = 0;
    int pulse_seen[5] = {}; // each ingredient's highest count since the shaker was last empty
    void PulseTick() {
        if (pulse_ing && ticks - pulse_tick >= u64(kPulseFrames)) pulse_ing = 0;
        int total = 0;
        for (int c : counts) total += c;
        for (int i = 0; i < 5; ++i) {
            if (!total) pulse_seen[i] = 0; // emptied: count from nothing again
            // one flash per unit: a count past its highest yet (a bottom-screen pour counts at
            // once, then the game's own count can lag a tick and catch up -- that is not a second
            // pour), and never over a flash still playing
            if (counts[i] > pulse_seen[i]) {
                pulse_seen[i] = counts[i];
                if (!pulse_ing) {
                    pulse_ing = i + 1;
                    pulse_tick = ticks;
                }
            }
        }
    }
    bool JillScene() const {
        if (room < 0) return true; // can't tell (another build?): as before
        if (room == kRoomBar) return true;
        return room == kRoomBreak && dlg_grow_tick > room_tick && ticks - dlg_grow_tick < kBreakTalkHold;
    }
    // The bar window's neon behind her: steady, then every 4..10 s a short stutter of 2..4 dips
    // (each 50..120 ms, about one in four deeper) -- never off, so it stays easy on the eyes.
    u64 neon_start = 0, neon_next = 300;
    static u64 Hash(u64 a, u64 salt) {
        u64 h = a * 0x9E3779B97F4A7C15ULL + salt * 0xC2B2AE3D27D4EB4FULL;
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ULL;
        return (h ^ (h >> 32)) & 0xFFFFFFFFULL;
    }
    void NeonTick() {
        if (ticks < neon_next) return;
        neon_start = ticks;
        neon_next = ticks + 240 + Rand(3) % 360;
    }
    int Neon() const { // 0 steady, 1 dip, 2 deep dip
        if (!neon_start) return 0;
        u64 t = ticks - neon_start;
        const int dips = 2 + int(Hash(neon_start, 0) % 3);
        for (int i = 0; i < dips; ++i) {
            const u64 h = Hash(neon_start, u64(i) + 1);
            const u64 dip = 3 + h % 5, gap = 2 + (h >> 8) % 6;
            if (t < dip) return (h >> 16) % 4 == 0 ? 2 : 1;
            t -= dip;
            if (t < gap) return 0;
            t -= gap;
        }
        return 0;
    }
    // Jill's shadow on the counter: each edge on its own now and then stretches out (the left one
    // to the left, the right one to the right) by 4..12 px, eases back over ~1..1.8 s and rests
    // 0.5..3 s. On the art's 4 px grid. The package draws the shadow twice, one copy moved by each
    // edge, so together they make one shadow. The neon's dips leave it be (the package dims the
    // light around it instead).
    struct ShadowEdge {
        u64 start = 0, len = 0, next = 0, amp = 0;
    } shadow_edge[2];
    void ShadowTick() {
        for (u64 side = 0; side < 2; ++side) {
            ShadowEdge& e = shadow_edge[side];
            if (ticks < e.next) continue;
            e.start = ticks;
            e.len = 60 + Rand(10 + side) % 50;
            e.amp = 4 + (Rand(20 + side) % 3) * 4;
            e.next = ticks + e.len + 30 + Rand(30 + side) % 150;
        }
    }
    s64 ShadowOut(int side) const { // px outward, 0 at rest
        const ShadowEdge& e = shadow_edge[side];
        if (!e.len || ticks - e.start >= e.len) return 0;
        const double half_pi = 3.14159265358979 / 2, f = double(ticks - e.start) / double(e.len);
        const double v = f < 0.4 ? std::sin(f / 0.4 * half_pi) : std::cos((f - 0.4) / 0.6 * half_pi);
        return s64(std::lround(v * double(e.amp) / 4)) * 4;
    }
    // Dust in the light: kMotes specks drifting slowly up the lit glass (6..20 px/s) with a small
    // sway, each living 4..9 s and fading in and out, then coming back somewhere else. Brightness
    // 0..3 (the package's tint steps), one step lower while the neon dips. On the window's 4 px
    // grid like the art, stepping at 15 Hz (fewer redraws). From the tick alone, no state.
    static constexpr int kMotes = 24;
    struct Mote {
        s64 x, y;
        int level;
    };
    Mote MoteAt(int i) const {
        const u64 period = 240 + Hash(u64(i), 100) % 300;
        const u64 t0 = (ticks & ~u64(3)) + Hash(u64(i), 101) % period;
        const u64 c = t0 / period, t = t0 % period;
        const u64 h = Hash(u64(i) * 100003 + c, 102), h2 = Hash(u64(i) * 100003 + c, 103);
        const double tau = 2 * 3.14159265358979;
        const double x0 = 268 + double(h % 700);          // the lit glass, x 268..968
        const double y0 = 300 + double((h >> 10) % 560);  // start low, rise at most ~180 px
        const double speed = double(6 + (h >> 20) % 15) / 60.0;
        const double amp = double(4 + h2 % 12);
        const double sway = double(t) / 60.0 * (0.3 + double((h2 >> 4) % 5) * 0.1) * tau +
                            double((h2 >> 8) % 628) / 100.0;
        const s64 x = s64(std::floor((x0 + amp * std::sin(sway)) / 4)) * 4;
        const s64 y = s64(std::floor((y0 - speed * double(t)) / 4)) * 4;
        const double f = double(t) / double(period);
        const double fade = std::min({1.0, f / 0.2, (1 - f) / 0.2});
        const int peak = 1 + int((h2 >> 16) % 3);
        const int level = std::max(0, int(std::lround(fade * peak)) - (Neon() ? 1 : 0));
        return {x, y, level};
    }
    int served_script = 0; // the script the drink was served in (0 = not known)
    // The customers at the bar, as the lines on screen bring them in and take them out. The drinks
    // on the counter stay while they are there -- a customer dozing off or stepping aside is
    // hidden, not gone (Sei asleep on Day 10) -- and go when someone new comes in or the last
    // one has left.
    std::vector<std::string> at_bar; // 3-letter keys
    void ServedSeesLine(int idx) {
        if (idx < 0) return;
        const ScriptLine& l = lines[size_t(idx)];
        const bool had = !at_bar.empty();
        for (size_t i = 0; i + 3 <= l.hides.size(); i += 3)
            at_bar.erase(std::remove(at_bar.begin(), at_bar.end(), l.hides.substr(i, 3)), at_bar.end());
        bool someone_new = false;
        for (size_t i = 0; i + 3 <= l.shows.size(); i += 3) {
            const std::string who = l.shows.substr(i, 3);
            if (std::find(at_bar.begin(), at_bar.end(), who) == at_bar.end()) {
                someone_new = true;
                at_bar.push_back(who);
            }
        }
        const bool all_gone = had && at_bar.empty();
        if (!served_src.empty() && (someone_new || all_gone)) {
            Log(std::string("the served drinks go: ") + (someone_new ? "a new customer" : "the customers left"));
            served_src.clear();
            served_liq.clear();
            served2 = {};
        }
    }
    bool JillThinks() const { // before the line is known: "Jill: (..."
        return live >= 0 ? lines[size_t(live)].thought : dlg_text.compare(0, 7, "Jill: (") == 0;
    }
    bool JillTalking() const {
        return jill_line && !JillThinks() && dlg_grow_tick && ticks - dlg_grow_tick <= kTalkHold;
    }
    int Blink() const { // jill_eyes frame: 0 open, 2 half shut, 1 shut
        const u64 t = ticks - blink_tick;
        return !blink_tick || t >= 12 ? 0 : t < 3 || t >= 8 ? 2 : 1;
    }
    // What the package draws. Base: jill_spr / surprise / uh / sad (+ jill_smile_spr over it for
    // a smile). Eyes: 8 images, the half-shut and shut frame of jill / smile / surprise / uh eyes.
    // Lips: one list, jill_lips 0-8, smile 0-7, surprise 0-7, uh 0-7, sad 0-7 (frame 0 closed).
    int JillBase() const {
        return face == kSurprise ? 1 : face == kUh || face == kBaka ? 2 : face == kSad ? 3 : 0;
    }
    int JillEyesShown() const { return face == kSad ? 0 : face == kThink ? 1 : Blink(); }
    int JillEyesSrc() const {
        const int set = face == kSmile ? 1 : face == kSurprise ? 2 : face == kUh || face == kBaka ? 3 : 0;
        return set * 2 + (JillEyesShown() == 1 ? 1 : 0);
    }
    int JillLipsSrc() const {
        if (face == kNeutral || face == kThink) return lip;
        const int set = face == kSmile || face == kBaka ? 9 : face == kSurprise ? 17 : face == kUh ? 25 : 33;
        return set + (lip ? 1 + (lip - 1) % 7 : 0);
    }

    void Tick() {
        ++ticks;
        if (started) ++mix_ticks;
        Physics();
        SaveWatch();
        SoundTick();
        JukeTick();
        // A drag that ends without a drop (second finger, page change) sends no shake_end: once
        // the shaker's samples stop for ~1/3 s, treat it as let go so the mix never runs on.
        if (has_pos && ticks - last_shake > 20) ShakeEnd();
        // The shaker only mixes while it is being shaken: once the player stops (no direction
        // change for ~0.67 s, even with the finger still down) the mix stops there -- mixed, or
        // blended if the shaking lasted the 5 s. Holding it still never blends a drink.
        if (started && !by_button && has_pos && ticks - rev_ticks[1] > kStopTicks) ShakeEnd();
        const bool before = Overlay();
        Track();
        LoadWatch();
        JillTick();
        RoomTick();
        SceneTick();
        PulseTick();
        BubbleTick();
        if (served_src.empty()) served_slide_tick = 0;
        else if (!served_slide_tick && Overlay() && scene_shown == 1) served_slide_tick = ticks;
        NeonTick();
        ShadowTick();
        game_ok = ReadGame();
        game_mixing = ReadGameMixing();
        {
            double r;
            game_ready = cur_x && ReadF64(cur_x + kGameIdleOff, r) && (r == 0 || r == 1) ? int(r) : -1;
        }
        int game_mix_raw = -1; // +0x880 alone: 1 while the game mixes (also before anything is ready)
        {
            double m;
            if (cur_x && ReadF64(cur_x + kGameMixingOff, m) && (m == 0 || m == 1)) game_mix_raw = int(m);
        }
        const int game_total = counts[0] + counts[1] + counts[2] + counts[3] + counts[4] + (game_ok && fedora ? 1 : 0);
        // We pressed ZR to start a mix, but the game never started it (it ignores ZR while the
        // left stick is held): the bottom shaker stops too, the drink still unmixed, like the top.
        if (started && start_check) {
            if (game_mix_raw == 1) {
                start_check = false;
            } else if (game_mix_raw == 0 && zr_pending == 0 && ticks > last_zr + kGameMixGrace) {
                Log("the game did not start mixing (ZR ignored): mix cancelled");
                if (has_pos) refused = true;
                CancelMix();
            } else if (game_mix_raw < 0) {
                start_check = false; // can't tell: trust the press
            }
        }
        // Reset in the game itself (ZL, top screen) in the middle of a mix: its shaker empties, not
        // mixing and no drink ready. The bottom shaker stops and empties too.
        if (started && !start_check && game_ok && game_mix_raw == 0 && game_ready == 0 && game_total == 0 &&
            zr_pending == 0 && ticks > last_zr + kGameMixGrace) {
            Log("reset in the game mid-mix: mix stopped");
            CancelMix();
            has_pos = false;
            mix_ticks = 0;
            NewDrink();
        }
        // Reset in the middle of a mix: the bottom screen emptied at once; make sure the game did
        // too. If it kept mixing through ZL, stop it (ZR), then reset the drink that leaves (ZL).
        if (reset_sync && zr_pending == 0 && zl_pending == 0 && ticks > reset_tick + kGameMixGrace &&
            ticks > last_zr + kGameMixGrace && ticks > last_zl + kGameMixGrace) {
            if (game_mix_raw < 0 || ++sync_tries > 4) {
                reset_sync = false;
            } else if (game_mix_raw == 1) {
                Log("reset mid-mix: the game is still mixing, stopping it");
                ++zr_pending;
            } else if (game_ready == 1) {
                Log("reset mid-mix: the game holds a drink, resetting it");
                ++zl_pending;
            } else {
                reset_sync = false;
            }
        }
        // Reset in the game: no drink ready any more (also clears A Fedora, which has no contents).
        if (game_ok && result != 0 && !started && game_ready == 0 && game_total == 0 && ticks > done_tick + 60) {
            mix_ticks = 0;
            NewDrink();
        }
        // Mixing started or stopped in the game itself (ZR on the controller or the top screen):
        // the bottom screen follows, the shaker shaking on its own like a Mix-button mix. Our own
        // ZR presses are given a moment to reach the game before its state counts.
        if (game_mixing >= 0 && zr_pending == 0 && ticks > last_zr + kGameMixGrace && !reset_sync) {
            if (game_mixing == 1 && !started && result == 0) {
                started = true;
                by_button = true;
                mix_ticks = 0;
                done = 0;
            } else if (game_mixing == 0 && started) {
                ShakeEnd(true, false); // already stopped in the game: no ZR from us
            }
        }
        // Reset in the game itself (ZL, top screen) empties its shaker: a finished drink on the
        // bottom screen (SUCCESS, or the failed drink and its error windows) goes too. Only once
        // the game was seen still holding that drink, so a game that empties on its own at the
        // end of a mix never wipes the result.
        if (!served_src.empty()) {
            int total = fedora ? 1 : 0;
            for (int c : counts) total += c;
            if (total > 0 || result != 0 || started) { // the next order is on the way
                served_src.clear();
                served_liq.clear();
                served2 = {};
            }
        }
        if (game_ok && result != 0 && !started) {
            int total = fedora ? 1 : 0;
            for (int c : counts) total += c;
            if (total > 0) full_after = true;
            else if (full_after) {
                mix_ticks = 0;
                NewDrink();
            }
        }
        if (mode != last_mode) {
            if (mode < 0) manual = before; // lost track: keep what the player sees
            skip = false;
            emergency = false;
            if (mode == 0 && !started) { // drink served: empty the bar, start a new drink
                // and it stands on the counter by Jill until the next order: the game empties its
                // shaker (and the bar clears the drink) a few ticks before the talk comes back
                mix_ticks = 0;
                NewDrink();
                if (!finished_src.empty() && ticks - finished_tick < 180) {
                    // on the counter: at 7x, without its black box; two drinks on a two-slot order
                    // (the first slot's left of Jill, the second's right of her)
                    const bool two = !finished_prev.empty();
                    DrinkLook d = MakeLook(two ? finished_prev : finished_src, false);
                    served_src = d.src;
                    served_name = d.name;
                    served_shadow = d.shadow;
                    served_liq = std::move(d.liq);
                    served_light = std::move(d.light);
                    served_dy = d.dy;
                    for (auto& b : bubbles) b = {};
                    served2 = two ? MakeLook(finished_src, true) : DrinkLook{};
                    for (auto& b : bubbles2) b = {};
                    served_script = live >= 0 ? lines[size_t(live)].script : anchor >= 0 ? lines[size_t(anchor)].script : 0;
                    Log("served: " + served_src + (two ? " and " + served2.src : ""));
                }
                finished_prev.clear();
                finished_src.clear();
            }
            last_mode = mode;
        }
        if (emergency && ++unused > kEmergencyIdle) { // opened by mistake (or done): lock again
            emergency = false;
            skip = false;
            if (mode < 0) manual = true;
        }
        if (!scanning && mode < 0 && wait > kRescanUnsure) wait = kRescanUnsure; // lost it: look sooner
        if (scanning) {
            ScanStep();
        } else if (wait) {
            --wait;
        } else {
            StartScan();
        }
        LogChanges();
    }

    // ---- game art (asset worker) ----
    std::mutex gm_mutex;
    vallhalla::GmReader gm;
    // The lowest row of a drink's drawing in its 35x53 sprite, its black box left out (as
    // keyedge cuts it): most stand on row 44, stemmed glasses go down to 50. -1 if unreadable.
    int SpriteFoot(const std::string& name) {
        std::scoped_lock lock{gm_mutex};
        int w = 0, ht = 0;
        std::vector<u8> px;
        if (!gm.Open(host) || !gm.Frame(name, 0, 1, w, ht, px) || w <= 0 || ht <= 0) return -1;
        std::vector<u8> bg(size_t(w) * size_t(ht), 0);
        const auto black = [&](int x, int y) {
            const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            return px[i + 3] && !px[i] && !px[i + 1] && !px[i + 2];
        };
        std::vector<std::pair<int, int>> todo;
        for (int x = 0; x < w; ++x) todo.insert(todo.end(), {{x, 0}, {x, ht - 1}});
        for (int y = 0; y < ht; ++y) todo.insert(todo.end(), {{0, y}, {w - 1, y}});
        while (!todo.empty()) {
            const auto [x, y] = todo.back();
            todo.pop_back();
            if (x < 0 || y < 0 || x >= w || y >= ht || bg[size_t(y) * size_t(w) + size_t(x)] || !black(x, y)) continue;
            bg[size_t(y) * size_t(w) + size_t(x)] = 1;
            todo.insert(todo.end(), {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}});
        }
        for (int y = ht - 1; y >= 0; --y)
            for (int x = 0; x < w; ++x)
                if (px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] && !bg[size_t(y) * size_t(w) + size_t(x)]) return y;
        return -1;
    }
    mutable std::mutex picked_mutex;
    std::map<std::string, std::string> picked; // "~query" -> the sprite it found (test readout)

    bool LoadImage(const EdenDsmodHostApi* h, const char* key, void* receiver, EdenDsmodImageSink sink) {
        std::string k = key;
        if (k.rfind("module:", 0) == 0) k = k.substr(7);
        if (k.rfind("gm:", 0) != 0) return false;
        k = k.substr(3);
        // gm:<name>[:<frame>[:<scale>]]
        std::vector<std::string> parts;
        size_t at = 0;
        for (;;) {
            const size_t c = k.find(':', at);
            parts.push_back(k.substr(at, c == std::string::npos ? std::string::npos : c - at));
            if (c == std::string::npos) break;
            at = c + 1;
        }
        const size_t frame = parts.size() > 1 ? size_t(std::strtoul(parts[1].c_str(), nullptr, 10)) : 0;
        const int scale = parts.size() > 2 ? int(std::strtol(parts[2].c_str(), nullptr, 10)) : 1;
        std::scoped_lock lock{gm_mutex};
        if (!gm.Open(h ? *h : host)) return false;
        int w = 0, ht = 0;
        std::vector<u8> px;
        if (!parts[0].empty() && parts[0][0] == '~') {
            const auto* e = gm.Find(parts[0]);
            std::scoped_lock l2{picked_mutex};
            picked[parts[0]] = e ? e->name : std::string("(no match)");
        }
        if (!gm.Frame(parts[0], frame, scale, w, ht, px)) return false;
        // optional extra parts, made on the device (nothing extra ships):
        //   swaprg       red <-> green: the pink Mix button turns green
        //   keyblack     black (and near-black) turns transparent: the break doodles on the overlay
        //   keyedge      only the pure black joined to the edge turns transparent: a drink on Jill's
        //                counter without its black box (its black outlines stay)
        //   glass        (after keyedge) the pure black left is glass: see-through, lightly frosted white
        //   crop<X>x<Y>x<W>x<H> keep only that rectangle (after scaling): one doodle of break_skin
        //   cut<X>x<Y>x<W>x<H>  that rectangle (after scaling) turns transparent: the break
        //                screen's phone and its icons, between the doodles
        //   pad<X>x<Y>   X px of transparent margin left and right, Y px top and bottom (after
        //                scaling): a bigger touch area around a can without stretching it
        for (size_t p = 3; p < parts.size(); ++p) {
            if (parts[p] == "swaprg") {
                for (size_t i = 0; i + 3 < px.size(); i += 4) std::swap(px[i], px[i + 1]);
            } else if (parts[p] == "keyblack") {
                for (size_t i = 0; i + 3 < px.size(); i += 4)
                    if (std::max({px[i], px[i + 1], px[i + 2]}) < 40) px[i + 3] = 0;
            } else if (parts[p].rfind("up", 0) == 0 && parts[p].size() > 2 && std::isdigit(u8(parts[p][2]))) {
                // up<S>: scale up by S (nearest), after the options that work on the 1x pixels
                const int s = std::max(1, int(std::strtol(parts[p].c_str() + 2, nullptr, 10)));
                std::vector<u8> out(size_t(w * s) * size_t(ht * s) * 4);
                for (int y = 0; y < ht * s; ++y)
                    for (int x = 0; x < w * s; ++x)
                        std::memcpy(&out[(size_t(y) * size_t(w * s) + size_t(x)) * 4],
                                    &px[(size_t(y / s) * size_t(w) + size_t(x / s)) * 4], 4);
                px.swap(out);
                w *= s;
                ht *= s;
            } else if (parts[p].rfind("shadow", 0) == 0) {
                const bool right = parts[p].size() > 6 && parts[p][6] == 'r'; // shadowr<S>: leans right
                // shadow<S>: the drawing's own shadow on the counter, made from its silhouette (use
                // at scale 1, after keyedge): flipped down from its foot, squashed to kShadowRows
                // rows, each row a pixel further left (the neon tube is right of the drink), in
                // Jill's shadow purple, then scaled by S
                constexpr int kShadowRows = 5;
                const int s = std::max(1, int(std::strtol(parts[p].c_str() + (right ? 7 : 6), nullptr, 10)));
                int foot = -1;
                for (int y = ht - 1; y >= 0 && foot < 0; --y)
                    for (int x = 0; x < w; ++x)
                        if (px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3]) {
                            foot = y;
                            break;
                        }
                if (foot >= 0) {
                    const int band = std::max(1, (foot + 1) / kShadowRows);
                    const int ow = w + kShadowRows - 1, oh = kShadowRows;
                    std::vector<u8> sh(size_t(ow) * size_t(oh) * 4, 0);
                    for (int r = 0; r < oh; ++r)
                        for (int x = 0; x < w; ++x) {
                            bool any = false;
                            for (int y = foot - r * band; y > foot - (r + 1) * band && y >= 0 && !any; --y)
                                any = px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] != 0;
                            if (!any) continue;
                            const size_t o = (size_t(r) * size_t(ow) + size_t(x + (right ? r : oh - 1 - r))) * 4;
                            sh[o] = 0x2A, sh[o + 1] = 0x08, sh[o + 2] = 0x30, sh[o + 3] = 255;
                        }
                    std::vector<u8> out(size_t(ow * s) * size_t(oh * s) * 4);
                    for (int y = 0; y < oh * s; ++y)
                        for (int x = 0; x < ow * s; ++x)
                            std::memcpy(&out[(size_t(y) * size_t(ow * s) + size_t(x)) * 4],
                                        &sh[(size_t(y / s) * size_t(ow) + size_t(x / s)) * 4], 4);
                    px.swap(out);
                    w = ow * s;
                    ht = oh * s;
                }
            } else if (parts[p] == "glass") {
                // after keyedge: the pure black left inside a drink is its glass (the empty top,
                // the beer handle's loop, the thick base), not its outline (dark navy): see-through
                // with a light frosted white, so the glass still shows
                for (size_t i = 0; i + 3 < px.size(); i += 4)
                    if (px[i + 3] && !px[i] && !px[i + 1] && !px[i + 2]) {
                        px[i] = 210, px[i + 1] = 230, px[i + 2] = 255, px[i + 3] = 64;
                    }
            } else if (parts[p] == "keyedge") {
                // only the pure black joined to the border turns transparent: most drink sprites
                // stand on a black box, and their outlines and shading inside are black too
                const auto black = [&](int x, int y) {
                    const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
                    return px[i + 3] && !px[i] && !px[i + 1] && !px[i + 2];
                };
                std::vector<std::pair<int, int>> todo;
                for (int x = 0; x < w; ++x) todo.insert(todo.end(), {{x, 0}, {x, ht - 1}});
                for (int y = 0; y < ht; ++y) todo.insert(todo.end(), {{0, y}, {w - 1, y}});
                while (!todo.empty()) {
                    const auto [x, y] = todo.back();
                    todo.pop_back();
                    if (x < 0 || y < 0 || x >= w || y >= ht || !black(x, y)) continue;
                    px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] = 0;
                    todo.insert(todo.end(), {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}});
                }
            } else if (parts[p].rfind("crop", 0) == 0) { // keep only that rectangle (after scaling)
                long v[4] = {};
                const char* s = parts[p].c_str() + 4;
                for (long& n : v) {
                    char* end = nullptr;
                    n = std::strtol(s, &end, 10);
                    s = (end && *end == 'x') ? end + 1 : end;
                }
                const int x0 = int(std::clamp(v[0], 0L, long(w))), y0 = int(std::clamp(v[1], 0L, long(ht)));
                const int nw = int(std::clamp(v[2], 0L, long(w - x0))), nh = int(std::clamp(v[3], 0L, long(ht - y0)));
                if (nw > 0 && nh > 0) {
                    std::vector<u8> out(size_t(nw) * size_t(nh) * 4);
                    for (int y = 0; y < nh; ++y)
                        std::memcpy(&out[size_t(y) * size_t(nw) * 4], &px[(size_t(y0 + y) * size_t(w) + size_t(x0)) * 4],
                                    size_t(nw) * 4);
                    px.swap(out);
                    w = nw;
                    ht = nh;
                }
            } else if (parts[p].rfind("cut", 0) == 0) {
                long v[4] = {};
                const char* s = parts[p].c_str() + 3;
                for (long& n : v) {
                    char* end = nullptr;
                    n = std::strtol(s, &end, 10);
                    s = (end && *end == 'x') ? end + 1 : end;
                }
                const int x0 = int(std::clamp(v[0], 0L, long(w))), y0 = int(std::clamp(v[1], 0L, long(ht)));
                const int x1 = int(std::clamp(v[0] + v[2], 0L, long(w))), y1 = int(std::clamp(v[1] + v[3], 0L, long(ht)));
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] = 0;
            } else if (parts[p].rfind("pad", 0) == 0) {
                char* end = nullptr;
                const long px_x = std::strtol(parts[p].c_str() + 3, &end, 10);
                const long px_y = (end && *end == 'x') ? std::strtol(end + 1, nullptr, 10) : px_x;
                const int mx = int(std::clamp(px_x, 0L, 512L)), my = int(std::clamp(px_y, 0L, 512L));
                const int nw = w + 2 * mx, nh = ht + 2 * my;
                std::vector<u8> out(size_t(nw) * size_t(nh) * 4, 0);
                for (int y = 0; y < ht; ++y)
                    std::memcpy(&out[(size_t(y + my) * size_t(nw) + size_t(mx)) * 4],
                                &px[size_t(y) * size_t(w) * 4], size_t(w) * 4);
                px.swap(out);
                w = nw;
                ht = nh;
            }
        }
        sink(receiver, u32(w), u32(ht), px.data(), px.size());
        return true;
    }

    bool OnAction(const std::string& a, s64 arg = 0) {
        const bool ok = DoAction(a, arg);
        char b[96];
        std::snprintf(b, sizeof b, "action %s(%lld) -> %s", a.c_str(), (long long)arg, ok ? "taken" : "REFUSED");
        // the per-tick ones (finger samples while dragging) are logged once per drag / shake
        if (a == "busy" || a == "shake" || a == "tilt") {
            u64& last = a == "shake" ? last_shake_tick : last_busy_tick;
            if (a != "tilt" && (last == 0 || ticks - last > 20)) VLog(std::string(b) + " (first of a run)");
            if (a != "tilt") last = ticks;
        } else if (ok) {
            VLog(b);
        } else {
            Log(b);
        }
        if (a == "busy") ++dbg_busy;
        else if (a == "shake") ++dbg_shake;
        else if (a != "tilt" && a != "zr_done") { // the per-tick ones would hide the rest
            if (a == "mix_btn") ++dbg_mix;
            dbg_act = a;
            dbg_arg = arg;
            dbg_ok = ok;
        }
        return ok;
    }

    bool DoAction(const std::string& a, s64 arg) {
        if (a == "juke_add" || a == "juke_del" || a == "juke_page" || a == "juke_done") {
            if (!jk_open || !jk_ready || jk_cmds.size() >= 8) return false;
            jk_cmds.push_back({a == "juke_add" ? 1 : a == "juke_del" ? 2 : a == "juke_page" ? 3 : 4, int(arg)});
            return true;
        }
        if (a == "slot_tap") { // Slot 1 / Slot 2 on the bottom screen
            const int t = int(arg);
            if (t < 0 || t > 1 || mode != 1 || started || slot_cur == t || (t == 1 && slot_amount != 2)) {
                Log("slot " + std::to_string(t + 1) + " refused: mode " + std::to_string(mode) + ", mixing " +
                    std::to_string(started) + ", slots " + std::to_string(slot_amount) + ", front " + std::to_string(slot_cur + 1));
                return false;
            }
            slot_tap = t;
            return true;
        }
        if (a == "egg_tap") { // the heart under Age: "itai!" every poke, the note on the third
            if (egg_open) return true;
            PlaySound(kSndItai, 1.0);
            if (++egg_taps >= 3) egg_open = true;
            return true;
        }
        if (a == "egg_close") { // the note closed: three pokes again
            egg_open = false;
            egg_taps = 0;
            return true;
        }
        if (a == "dlg_next") {
            if (!Overlay() || scene_shown != 1 || jk_open) {
                Log("next line refused: overlay " + std::to_string(Overlay()) + ", scene " + std::to_string(scene_shown) +
                    ", jukebox " + std::to_string(jk_open));
                return false;
            }
            if (!MailboxAlive()) { // no way to move the cursor: A as it is
                if (!jk_btn) jk_btn = 2;
                return true;
            }
            if (dlg_taps >= 2) {
                Log("next line refused: two taps still waiting (btn " + std::to_string(jk_btn) + ", a_at " +
                    std::to_string(jk_a_at) + ", queue " + std::to_string(jq.size() - jq_at) + ", wait " +
                    std::to_string(juke_wait) + ", sound " + std::to_string(snd_stage) + ")");
                return false;
            }
            ++dlg_taps;
            return true;
        }
        if (a == "juke_btn_done") {
            if (!jk_btn) return false; // a late ack: nothing asked
            jk_btn = 0;
            jk_btn_tick = ticks;
            return true;
        }
        if (a == "skip") {
            if (mode < 0) manual = false;
            else skip = true;
            emergency = !Overlay();
            unused = 0;
            return true;
        }
        if (a == "shake") {
            unused = 0;
            Shake(arg);
            return true;
        }
        if (a == "shake_end") {
            ShakeEnd();
            return true;
        }
        if (a == "reset") { // the player pressed Reset: empty the bar and the shaker
            if (started) { // mid-mix too: the shaking stops and the drink is thrown away
                CancelMix();
                has_pos = false;
                zr_pending = 0; // a start / stop press not made yet is moot now
                reset_sync = true;
                sync_tries = 0;
                reset_tick = ticks;
            }
            mix_ticks = 0;
            NewDrink();
            finished_src.clear(); // thrown away, not served
            finished_prev.clear();
            return true;
        }
        if (a == "pour" && arg >= 0 && arg < 5) { // one unit poured from the bottom screen
            if (done) { // the last drink was finished: this pour starts a new one
                mix_ticks = 0;
                NewDrink();
            }
            ++counts[arg];
            pour_tick[arg] = ticks;
            return true;
        }
        if (a == "close_err" && arg >= 0 && arg < kErrN) { // tapped an error window away
            if (ErrVisible(int(arg))) {
                err_closed |= 1u << arg;
                PlaySound(kSndClick, 1.0);
            }
            return true;
        }
        if (a == "ice_toggle") {
            if (done) NewDrink();
            ice = !ice;
            return true;
        }
        if (a == "age_toggle") {
            if (done) NewDrink();
            aged = !aged;
            return true;
        }
        if (a == "tilt") { // the finger's sideways speed this tick: 0..20 = -30..+30 px/tick
            if (arg < 0 || arg > 20) return false;
            tilt_in = double(arg - 10) * 3.0;
            tilt_tick = ticks;
            swing_peak = std::max(swing_peak, std::fabs(tilt_in));
            return true;
        }
        if (a == "zr_done") {
            if (zr_pending > 0) --zr_pending;
            last_zr = ticks;
            return true;
        }
        if (a == "zl_done") {
            if (zl_pending > 0) --zl_pending;
            last_zl = ticks;
            return true;
        }
        if (a == "busy") { // the player is using the bar (pinged while dragging / pressing)
            unused = 0;
            return true;
        }
        if (a == "mix_btn") { // the bottom screen's Mix / Serve button, like ZR in the game
            unused = 0;
            if (started) { // mixing: stop (mixed, or blended after 5 s)
                ShakeEnd(true);
                return true;
            }
            if (result == 1) { // Serve
                ++zr_pending;
                return true;
            }
            if (result == 0 && HasPour()) { // start mixing; the shaker shakes on its own
                BeginMix(true);
                return true;
            }
            return false;
        }
        if (a == "mixed") {
            if (mode < 0) manual = true;
            return true;
        }
        return false;
    }

    void Sample(const EdenDsmodHostApi* h) const {
        h->publish_i64(h->userdata, "va_overlay", Overlay() ? 1 : 0);
        h->publish_i64(h->userdata, "va_frame", s64((ticks / kFrameTicks) & 1));
        h->publish_i64(h->userdata, "va_doodle", s64((ticks / kDoodleTicks) % 3));
        h->publish_i64(h->userdata, "va_jill_talk", JillTalking() ? 1 : 0);
        h->publish_i64(h->userdata, "va_jill_face", face);
        h->publish_i64(h->userdata, "va_jill_base", JillBase());
        const bool scene = scene_shown == 1;
        h->publish_i64(h->userdata, "va_room", room);
        h->publish_i64(h->userdata, "va_dither", Dither());
        h->publish_i64(h->userdata, "va_jill_scene", scene ? 1 : 0);
        h->publish_i64(h->userdata, "va_doodles", scene ? 0 : 1);
        // the stand-by screen's pictures: the title screen's Jill (jilltitle_spr, 4 frames at 100 ms,
        // as the artist's reference) -- or the back street (break_bg1) on the break screen
        h->publish_i64(h->userdata, "va_title_f", s64((ticks / 6) % 4));
        h->publish_i64(h->userdata, "va_on_break", scene_shown == 2 ? 1 : 0);
        { // "Please Stand by" pulses: fades in, holds lit, fades out, stays dark a moment (2.5 s)
            const u64 t = ticks % 150;
            s64 g = t < 30 ? s64(t / 5) + 1 : t < 100 ? 6 : t < 130 ? s64((129 - t) / 5) + 1 : 0;
            if (t >= 30 && t < 100 && (t / 4) % 9 == 0 && t > 60 && t < 80) g = 4; // a neon-like stutter
            h->publish_i64(h->userdata, "va_standby_glow", g);
        }
        h->publish_i64(h->userdata, "va_jill_smile", scene && face == kSmile ? 1 : 0);
        h->publish_i64(h->userdata, "va_jill_eyes", scene ? JillEyesShown() : 0);
        h->publish_i64(h->userdata, "va_jill_eyesrc", JillEyesSrc());
        h->publish_i64(h->userdata, "va_jill_lips", JillLipsSrc());
        h->publish_i64(h->userdata, "va_neon", Neon());
        h->publish_i64(h->userdata, "va_egg", egg_open ? 1 : 0);
        h->publish_text(h->userdata, "va_money", money_text.c_str());
        { // each slot button as the game draws it: 0 at the front, 1 the other one, 2 mixed, 3 out
            const bool open = mode == 1 && !started;
            const int s1 = !open ? 3 : slot_mixed[0] ? 2 : slot_cur == 0 ? 0 : 1;
            const int s2 = !open || slot_amount != 2 ? 3 : slot_mixed[1] ? 2 : slot_cur == 1 ? 0 : 1;
            h->publish_i64(h->userdata, "va_slot1", s1);
            h->publish_i64(h->userdata, "va_slot2", s2);
            h->publish_i64(h->userdata, "va_slot_cur", slot_cur);
        }
        JukePublish(h);
        h->publish_i64(h->userdata, "va_shadow_l", -ShadowOut(0));
        h->publish_i64(h->userdata, "va_shadow_r", ShadowOut(1));
        for (int i = 0; i < kMotes; ++i) {
            static const auto names = [] {
                std::vector<std::string> n;
                for (int k = 0; k < kMotes; ++k)
                    for (const char* f : {"_x", "_y", "_a"}) n.push_back("va_mote" + std::to_string(k) + f);
                return n;
            }();
            const Mote m = MoteAt(i);
            h->publish_i64(h->userdata, names[size_t(i) * 3].c_str(), m.x);
            h->publish_i64(h->userdata, names[size_t(i) * 3 + 1].c_str(), m.y);
            h->publish_i64(h->userdata, names[size_t(i) * 3 + 2].c_str(), m.level);
        }
        h->publish_i64(h->userdata, "va_mode", mode);
        h->publish_i64(h->userdata, "va_proven", s64(Proven()));
        h->publish_i64(h->userdata, "va_zr_req", zr_pending > 0 && ticks - last_zr >= kZrGap ? 1 : 0);
        h->publish_i64(h->userdata, "va_started", started ? 1 : 0);
        h->publish_i64(h->userdata, "va_zl_req", zl_pending > 0 && ticks - last_zl >= kZrGap ? 1 : 0);
        h->publish_i64(h->userdata, "va_btn_mixing", started && by_button ? 1 : 0);
        h->publish_i64(h->userdata, "va_mix_lock", started && by_button ? 1 : 0);
        h->publish_i64(h->userdata, "va_wiggle", Wiggle());
        h->publish_i64(h->userdata, "va_jx", Jitter(0, 7));
        h->publish_i64(h->userdata, "va_jy", Jitter(1, 4));
        h->publish_i64(h->userdata, "va_angle", Angle());
        h->publish_i64(h->userdata, "va_sway", Sway());
        // While shaking the bar counts towards blending (full at 5 s); once let go the drink is
        // done, so the bar is full: blue for mixed, light for blended.
        h->publish_i64(h->userdata, "va_fill",
                       done ? 100 : s64(std::min<u64>(100, mix_ticks * 100 / kBlendTicks)));
        h->publish_i64(h->userdata, "va_blended", done == 2 || (started && mix_ticks >= kBlendTicks) ? 1 : 0);
        h->publish_i64(h->userdata, "va_done", done);
        h->publish_i64(h->userdata, "va_result", result);
        // the shaker can be grabbed only with something in it (and no finished drink on show)
        h->publish_i64(h->userdata, "va_grab", result == 0 && (HasPour() || started) ? 1 : 0);
        // the shaker's contents as the game has them (va_game_ok 1), for the meters and Ice / Age
        h->publish_i64(h->userdata, "va_game_ok", game_ok ? 1 : 0);
        for (int i = 0; i < 5; ++i) {
            char n[16];
            std::snprintf(n, sizeof n, "va_cnt%d", i);
            h->publish_i64(h->userdata, n, counts[i]);
        }
        {
            const int t = pulse_ing ? int(ticks - pulse_tick) : 0;
            h->publish_i64(h->userdata, "va_pulse", pulse_ing);
            h->publish_i64(h->userdata, "va_pulse_t", t);
            h->publish_i64(h->userdata, "va_pulse_scale", 1000 + 80 * t);
        }
        h->publish_i64(h->userdata, "va_ice_on", ice ? 1 : 0);
        h->publish_i64(h->userdata, "va_age_on", aged ? 1 : 0);
        h->publish_i64(h->userdata, "va_shaker_idle", result == 0 && !HasPour() && !started ? 1 : 0);
        h->publish_i64(h->userdata, "va_ok", result == 1 ? 1 : 0);
        h->publish_i64(h->userdata, "va_fail", result == 2 ? 1 : 0);
        h->publish_i64(h->userdata, "va_err_any", ErrAny() ? 1 : 0);
        // a failed drink locks the bar except Reset (like the game); once the error windows are
        // gone, Reset pulses (scale x1000, a 1 s breathing wave of +-7 %)
        const bool lock = result == 2 && !ErrAny();
        h->publish_i64(h->userdata, "va_fail_lock", lock ? 1 : 0);
        s64 scale = 1000;
        if (lock) {
            const s64 ph = s64((ticks - err_tick) % 60); // 0..59
            const s64 tri = ph < 30 ? ph : 60 - ph;      // 0..30..0
            scale = 1000 + (tri * 70) / 30;
        }
        h->publish_i64(h->userdata, "va_reset_scale", scale);
        for (int i = 0; i < kErrN; ++i) {
            char n[16];
            std::snprintf(n, sizeof n, "va_err%d", i);
            h->publish_i64(h->userdata, n, ErrVisible(i) ? 1 : 0);
            std::snprintf(n, sizeof n, "va_errf%d", i);
            h->publish_i64(h->userdata, n, ErrVisible(i) ? ErrFrame(i) : 0);
        }
        {
            const bool served = !served_src.empty() && (room < 0 || room == kRoomBar);
            h->publish_i64(h->userdata, "va_served_dx", ServedDx());
            h->publish_i64(h->userdata, "va_served_dy", served_dy);
            h->publish_i64(h->userdata, "va_served", served ? 1 : 0);
            h->publish_text(h->userdata, "va_served_src", served ? ServedKey().c_str() : "");
            static const auto keys = [] {
                std::vector<std::string> n;
                for (int i = 0; i < kBubbles; ++i)
                    for (const char* f : {"_x", "_y", "_a"}) n.push_back("va_bub" + std::to_string(i) + f);
                return n;
            }();
            static const auto keys2 = [] {
                std::vector<std::string> n;
                for (int i = 0; i < kBubbles; ++i)
                    for (const char* f : {"_x", "_y", "_a"}) n.push_back("va_bubr" + std::to_string(i) + f);
                return n;
            }();
            for (int i = 0; i < kBubbles; ++i) { // the second drink's, right of Jill
                const Bubble& b = bubbles2[i];
                const bool on = served && !served2.src.empty() && b.col >= 0;
                h->publish_i64(h->userdata, keys2[size_t(i * 3)].c_str(), kDrink2X + ServedDx() + b.col * kDrinkScale);
                h->publish_i64(h->userdata, keys2[size_t(1 + i * 3)].c_str(),
                               kDrinkY + served2.dy + s64(std::floor(b.y)) * kDrinkScale);
                h->publish_i64(h->userdata, keys2[size_t(2 + i * 3)].c_str(), on ? b.level : 0);
            }
            for (int i = 0; i < kBubbles; ++i) {
                const Bubble& b = bubbles[i];
                const bool on = served && b.col >= 0;
                h->publish_i64(h->userdata, keys[size_t(i * 3)].c_str(), kDrinkX + ServedDx() + b.col * kDrinkScale);
                h->publish_i64(h->userdata, keys[size_t(1 + i * 3)].c_str(),
                               kDrinkY + served_dy + s64(std::floor(b.y)) * kDrinkScale);
                h->publish_i64(h->userdata, keys[size_t(2 + i * 3)].c_str(), on ? b.level : 0);
            }
            h->publish_text(h->userdata, "va_served_shadow", served ? served_shadow.c_str() : "");
            const bool two = served && !served2.src.empty();
            h->publish_i64(h->userdata, "va_served2", two ? 1 : 0);
            h->publish_text(h->userdata, "va_served2_src", two ? ("module:gm:" + served2.name + ":0:1:keyedge:glass:up7").c_str() : "");
            h->publish_text(h->userdata, "va_served2_shadow", two ? served2.shadow.c_str() : "");
            h->publish_i64(h->userdata, "va_served2_dy", served2.dy);
        }
        if (result == 1 && drink == kFedora) {
            h->publish_text(h->userdata, "va_drink_src", "module:gm:~fedora:0:4");
            h->publish_text(h->userdata, "va_drink_name", "A Fedora");
            std::scoped_lock l2{picked_mutex};
            const auto it = picked.find("~fedora");
            h->publish_text(h->userdata, "va_drink_sprite",
                            ("sprite: " + (it == picked.end() ? std::string("loading...") : it->second)).c_str());
        } else if (result == 1) {
            // exact names go in as their lookup key ("Sugar Sush spr" -> "sugarsushspr": no spaces in the src)
            const std::string spr = kDrinkSprite[drink][0] == '~' ? std::string(kDrinkSprite[drink])
                                                                   : vallhalla::GmReader::Key(kDrinkSprite[drink]);
            std::string key = "module:gm:" + spr + ":0:4";
            h->publish_text(h->userdata, "va_drink_src", key.c_str());
            h->publish_text(h->userdata, "va_drink_name",
                            (std::string(kRecipes[drink].name) + (big ? " (Big)" : "")).c_str());
            const std::string q = kDrinkSprite[drink];
            std::scoped_lock l2{picked_mutex};
            const auto it = picked.find(q);
            h->publish_text(h->userdata, "va_drink_sprite",
                            ("sprite: " + (q[0] != '~' ? q : it == picked.end() ? std::string("loading...") : it->second)).c_str());
        } else if (result == 2) {
            h->publish_text(h->userdata, "va_drink_src", "module:gm:glitchbottle:0:4");
            h->publish_text(h->userdata, "va_drink_name", "Failed drink");
        }
        h->publish_i64(h->userdata, "va_success_frame", SuccessFrame());
        // the manual-shake tutorial: the hand floats up and down (1.2 s), the ring breathes (1 s)
        const double tau = 2 * 3.14159265358979;
        h->publish_i64(h->userdata, "va_tut_bob", std::lround(12.0 * std::sin(tau * double(ticks % 72) / 72.0)));
        h->publish_i64(h->userdata, "va_loaded", loaded_once && ticks - loaded_tick <= 60 ? 1 : 0);
        h->publish_i64(h->userdata, "va_saved", saved_once && ticks - saved_tick <= 60 ? 1 : 0);
        h->publish_i64(h->userdata, "va_tut_pulse", 1000 + std::lround(60.0 * std::sin(tau * double(ticks % 60) / 60.0)));
        const char* st = mode == 1 ? "auto: mixing"
                         : mode == 0 ? (skip ? "auto: talking (opened by hold)" : "auto: talking")
                         : cands.empty() ? "auto: searching"
                                         : "auto: waiting for first mix";
        // test readout: state, matches (proven), finished scans, heap size scanned
        const u64 hb = host.get_heap_begin ? host.get_heap_begin(host.userdata) : 0;
        const u64 he = host.get_heap_end ? host.get_heap_end(host.userdata) : 0;
        char line[160];
        std::snprintf(line, sizeof line, "%s  %zu/%zu s%d %lluM%s", st, Proven(), cands.size(), scans,
                      (unsigned long long)((he > hb ? he - hb : 0) >> 20), scanning ? " *" : "");
        h->publish_text(h->userdata, "va_status", line);
        // test readouts: what the bar reads from the game, and the module's own state
        h->publish_text(h->userdata, "va_bar_dbg", GameLine().c_str());
        char dbg[120];
        std::snprintf(dbg, sizeof dbg, " | last %s(%lld)=%d busy%u shk%u mix%u", dbg_act.c_str(),
                      (long long)dbg_arg, dbg_ok ? 1 : 0, dbg_busy, dbg_shake, dbg_mix);
        h->publish_text(h->userdata, "va_bar_dbg2", (StateLine() + dbg).c_str());
    }

    // ---- test log (Eden Duo's log, logcat tag YuzuNative; every line starts "VAH ") ----
    std::string GameLine() const { // the raw values next to the switch
        auto raw = [&](u64 off) {
            double d = 0;
            char b[24];
            if (!cur_x || !ReadF64(cur_x + off, d)) return std::string("?");
            std::snprintf(b, sizeof b, "%g", d);
            return std::string(b);
        };
        char b[220];
        std::snprintf(b, sizeof b, "bar %s A%d B%d D%d F%d K%d tot%s fed%s rdy%s mix%s ice%s age%s pour%d",
                      game_ok ? "OK" : "--", counts[0], counts[1], counts[2], counts[3], counts[4], raw(kGameTotalOff).c_str(),
                      fedora ? "1" : "0", raw(kGameIdleOff).c_str(), raw(kGameMixingOff).c_str(),
                      raw(kGameIceOff).c_str(), raw(kGameAgedOff).c_str(), HasPour() ? 1 : 0);
        return b;
    }
    std::string StateLine() const {
        char b[160];
        std::snprintf(b, sizeof b, "res%d drink%d st%d btn%d done%d grab%d idle%d ovl%d mode%lld lock%d err%d zr%d zl%d",
                      result, drink, started ? 1 : 0, by_button ? 1 : 0, done,
                      result == 0 && (HasPour() || started) ? 1 : 0, result == 0 && !HasPour() && !started ? 1 : 0,
                      Overlay() ? 1 : 0, (long long)mode, result == 2 && !ErrAny() ? 1 : 0, ErrAny() ? 1 : 0, zr_pending, zl_pending);
        return b;
    }
    // Release builds log what happened once (rooms, saves, the language, a stuck call, a refused
    // action, the served drink...); kVerboseLog adds the step by step (every call to the game,
    // every action taken, every dialogue line found, every change of the game's or the bar's
    // state) for a debugging session.
    static constexpr bool kVerboseLog = false;
    void VLog(const std::string& s) const {
        if (kVerboseLog) Log(s);
    }
    void Log(const std::string& s) const {
        if (!host.log) return;
        char b[32];
        std::snprintf(b, sizeof b, "VAH t%llu ", (unsigned long long)ticks);
        host.log(host.userdata, EDEN_DSMOD_LOG_INFO, (b + s).c_str());
    }
    std::string logged_game, logged_state;
    u64 last_busy_tick = 0, last_shake_tick = 0;
    void LogChanges() { // once per tick: only what changed
        std::string g = GameLine(), s = StateLine();
        if (g != logged_game) VLog("game  " + g);
        if (s != logged_state) VLog("state " + s);
        logged_game = std::move(g);
        logged_state = std::move(s);
    }
};

EdenDsmodBool SupportsBuild(const char*) {
    return EDEN_DSMOD_TRUE; // reads only and finds its data by pattern; the package pins the build
}

void* Create(const EdenDsmodHostApi* host, const char*) {
    try {
        if (!dsmod_sdk::HostAbiMatches(host) || host->title_id != kTitleId) return nullptr;
        auto* m = new Module();
        m->host = *host;
        return m;
    } catch (...) {
        return nullptr;
    }
}

void Destroy(void* inst) {
    delete static_cast<Module*>(inst);
}

void Sample(void* inst, const EdenDsmodHostApi* host) {
    try {
        static_cast<Module*>(inst)->Sample(host);
    } catch (...) {
    }
}

void TickFn(void* inst, const EdenDsmodHostApi* host) {
    auto* m = static_cast<Module*>(inst);
    try {
        m->host = *host;
        m->Tick();
        // Eden Duo hit-tests touches right after tick() (sample() only feeds the drawing), so the
        // overlay gate must be in the snapshot here too or the input_block lets touches through.
        m->Sample(host);
    } catch (...) {
        m->cands.clear(); // out of memory: start over, the overlay falls back to manual
        m->scanning = false;
    }
}

void Configure(void* inst, const EdenDsmodHostExtensions* ext) {
    auto* m = static_cast<Module*>(inst);
    if (!m || !ext || !ext->mailbox_address || !ext->load_u32 || !ext->store_u32 || !ext->load_u64 || !ext->store_u64)
        return; // no load plan applied (another game build): no game sounds, everything else works
    m->ext = *ext;
    m->has_mailbox = true;
    u32 seq = 0;
    if (ext->load_u32(ext->userdata, 0, &seq)) m->mb_seq = seq; // carry on from a reloaded package
    m->Log("guest mailbox at " + std::to_string(ext->mailbox_address));
}

EdenDsmodBool LoadImageFn(void* inst, const EdenDsmodHostApi* host, const char* key, void* receiver,
                          EdenDsmodImageSink sink) {
    try {
        return static_cast<Module*>(inst)->LoadImage(host, key ? key : "", receiver, sink) ? EDEN_DSMOD_TRUE
                                                                                         : EDEN_DSMOD_FALSE;
    } catch (...) {
        return EDEN_DSMOD_FALSE;
    }
}

EdenDsmodBool OnAction(void* inst, const char* action, int64_t argument) {
    try {
        return static_cast<Module*>(inst)->OnAction(action ? action : "", argument) ? EDEN_DSMOD_TRUE
                                                                                       : EDEN_DSMOD_FALSE;
    } catch (...) {
        return EDEN_DSMOD_FALSE;
    }
}

// The package font (PixelMplus12, M+ FONT LICENSE; see LICENSE-PixelMplus.txt in the package):
// "VAFN" u16 line_height, u16 first_codepoint, u16 count, then count glyphs of
// u16 x, y, w, h, s16 bearing_x, bearing_y, u16 advance (little-endian), made by the package
// build from the TTF. The atlas is the package's pixelmplus12.png.
EdenDsmodBool DecodeFont(void*, const uint8_t* bytes, size_t size, void* receiver, EdenDsmodFontSink sink) {
    if (!bytes || size < 10 || std::memcmp(bytes, "VAFN", 4) != 0) return EDEN_DSMOD_FALSE;
    u16 line, first, count;
    std::memcpy(&line, bytes + 4, 2);
    std::memcpy(&first, bytes + 6, 2);
    std::memcpy(&count, bytes + 8, 2);
    if (size < 10 + size_t(count) * 14 || line == 0) return EDEN_DSMOD_FALSE;
    std::vector<EdenDsmodFontGlyph> glyphs(count);
    for (u16 i = 0; i < count; ++i) {
        const u8* g = bytes + 10 + size_t(i) * 14;
        std::memcpy(&glyphs[i].x, g, 2);
        std::memcpy(&glyphs[i].y, g + 2, 2);
        std::memcpy(&glyphs[i].w, g + 4, 2);
        std::memcpy(&glyphs[i].h, g + 6, 2);
        std::memcpy(&glyphs[i].bearing_x, g + 8, 2);
        std::memcpy(&glyphs[i].bearing_y, g + 10, 2);
        std::memcpy(&glyphs[i].advance, g + 12, 2);
    }
    sink(receiver, line, first, glyphs.data(), count);
    return EDEN_DSMOD_TRUE;
}

const EdenDsmodFontExtensions kFontExtensions = {
    EDEN_DSMOD_FONT_EXT_VERSION, sizeof(EdenDsmodFontExtensions), EDEN_DSMOD_FONT_EXT_HASH, DecodeFont,
};

const EdenDsmodModuleApi kModule = {
    EDEN_DSMOD_MODULE_ABI_VERSION,
    sizeof(EdenDsmodModuleApi),
    0,
    EDEN_DSMOD_MODULE_ABI_HASH,
    kTitleId,
    "VAllhalla",
    EDEN_DSMOD_CAP_EXTENSIONS | EDEN_DSMOD_CAP_ROMFS_READ,
    SupportsBuild,
    Create,
    Destroy,
    Sample,
    TickFn,
};

const EdenDsmodModuleExtensions kExtensions = {
    EDEN_DSMOD_EXT_VERSION, sizeof(EdenDsmodModuleExtensions), EDEN_DSMOD_EXT_HASH, Configure, OnAction, LoadImageFn,
};

} // namespace

DSMOD_SDK_EXPORT_MODULE(kModule)
DSMOD_SDK_EXPORT_EXTENSIONS(kExtensions)
DSMOD_SDK_EXPORT_FONT_EXTENSIONS(kFontExtensions)
