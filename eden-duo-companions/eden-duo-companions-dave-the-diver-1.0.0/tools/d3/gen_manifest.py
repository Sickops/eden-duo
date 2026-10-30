#!/usr/bin/env python3
"""Generates packages/DiabloIII/dualscreen/manifest.json (direction A, all four tabs).

Asset-free: every picture is a module:d3:* key the native module composes from the player's own
romfs (UI atlases, baked fonts, scene minimap tiles). Edit this generator, not the JSON."""
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULES = ROOT / 'native/modules'


def cpp_constants(name):
    """Integer / list constants of a module header, so rects never drift from what C++ draws."""
    text = (MODULES / name).read_text()
    out = {}
    for decl in re.findall(r'constexpr int ([^;\[]+);', text):
        for part in decl.split(','):
            k, v = (x.strip() for x in part.split('='))
            try:  # literals, or simple arithmetic over earlier constants (CanvasW / 2)
                out[k] = int(eval(v.replace('/', '//'), {'__builtins__': {}}, dict(out)))
            except (NameError, SyntaxError) as e:
                # A constant the regex matched but couldn't evaluate (a cast, sizeof(...), a
                # ternary, ...) must fail generation loudly, not vanish into a KeyError far away
                # (or, worse, a stale value from an earlier run's import chain).
                raise ValueError(f"{name}: constexpr int {k!r} = {v!r} did not parse ({e}); "
                                 "teach cpp_constants() this shape instead of letting it drop") from e
    for k, body in re.findall(r'constexpr (?:std::array<[^>]+>|int) (\w+)(?:\[\d+\] =)?\s*\{([^}]*)\}', text):
        items = [x.strip() for x in body.split(',') if x.strip()]
        try:
            out[k] = [x.strip('"') if x.startswith('"') else int(x) for x in items]
        except ValueError as e:
            raise ValueError(f"{name}: constexpr array {k!r} = {{{body}}} did not parse ({e}); "
                             "teach cpp_constants() this shape instead of letting it drop") from e
    return out


SK = cpp_constants('d3_skills.h')
OP = cpp_constants('d3_options.h')
GR = cpp_constants('d3_page_gear.h')
VW = cpp_constants('d3_view.h')


def build_ids_from_source():
    """build_hex of every Build in d3_build.cpp, in file order (k277 then k276), zero-padded to the
    64 hex chars (32 bytes) the package format wants -- the one place these ids are hand-typed, so
    the manifest and tools/d3/build_slice.sh's --build-id arguments (read back out of the generated
    manifest.json, same as tools/build_release.sh's build_ids() helper for every other companion)
    can never drift from native/modules/d3_build.cpp's own canonical Build entries."""
    text = (MODULES / 'd3_build.cpp').read_text()
    hexes = re.findall(r'\.build_hex\s*=\s*"([0-9A-Fa-f]+)"', text)
    if not hexes:
        raise ValueError("d3_build.cpp: no '.build_hex = \"...\"' entries found")
    return [h + '0' * (64 - len(h)) for h in hexes]


W, H = 1240, 1080
COL = 262
MAP_CX, MAP_CY = VW['MapImgCX'], VW['MapImgCY']  # map picture centre at zero offset
MAPW, MAPH = VW['MapImgW'], VW['MapImgH']
TABS = ['hero', 'activities', 'map', 'gear']
TAP_USE = 'd3.tap_use'  # persisted flag: tapping a rail slot presses its button (Options page)
# Haptics (strength names: off, light, click, confirm, heavy, reject). Rail inspect = default tap.
HAPTICS = {'enabled': True, 'respect_system': True, 'tap': 'light', 'select': 'light',
           'hold': 'click', 'swipe': 'light', 'refused': 'reject'}
# Quest/bounty objective ring pulse (map_page's qring overlays): tint_bind only indexes a palette
# (mod_ui_widget_state.cpp WidgetTint), so the alpha fade is this many precomputed AARRGGBB steps,
# full white RGB throughout (no colour shift) so the ring keeps its own native yellow art colour
# and only its opacity changes; the module publishes which step is current (d3.qring<i>tint) in
# step with kQRingTintSteps (01001B300B9BE000.cpp).
QRING_TINT_STEPS = 24
QRING_TINT = [f'#{round(255 * (1 - i / QRING_TINT_STEPS)):02X}FFFFFF' for i in range(QRING_TINT_STEPS)]


def img(rect, **kw):
    w = {'type': 'image', 'rect': rect, 'color': '#FFFFFFFF'}
    w.update(kw)
    return w


# Nav order (module d3_view::NavIndex / d3.nav.mask bit index): hero, activities, map, gear,
# options. Map and Options are always present; the other three are excluded (not dimmed) while
# their tab has nothing to show (d3.nav.mask, module-debounced ~1 s against boot flicker).
NAV_IDS = ['hero', 'activities', 'map', 'gear', 'options']
NAV_LABEL_ACTION = {nid: 'tab_' + nid for nid in NAV_IDS}
NAV_ALWAYS = (1 << 2) | (1 << 4)  # map, options
NAV_X0, NAV_X1 = VW['NavCellsX0'], VW['NavCellsX1']
NAV_Y, NAV_H = VW['NavCellY'], VW['NavCellH']
# Tap-only: the hit rect is taller than the drawn cell (NavHitH >= NAV_H, LEGIBILITY.md >= 112 px),
# extended upward over the page body above the bar; the art itself (NAV_Y/NAV_H) is unchanged.
NAV_HIT_H = VW['NavHitH']
NAV_HIT_Y = NAV_Y + NAV_H - NAV_HIT_H


def nav_masks():
    # the 3 free bits (hero, activities, gear); map/options are always set
    return [NAV_ALWAYS | (c & 1) << 0 | ((c >> 1) & 1) << 1 | ((c >> 2) & 1) << 3 for c in range(8)]


def nav_layout(mask):
    """Per-cell (id, x0, width) for `mask`: the present labels share NAV_X0..NAV_X1 with equal space
    around each label (NavLabelW). Same integer arithmetic as module NavBar (d3_view.cpp)."""
    present = [(i, nid) for i, nid in enumerate(NAV_IDS) if mask & (1 << i)]
    labels_w = sum(VW['NavLabelW'][i] for i, _ in present)
    span = NAV_X1 - NAV_X0
    gap = max(0, span - labels_w) // len(present) if present else 0
    x = NAV_X0 + (span - labels_w - gap * len(present)) // 2
    cells = []
    for i, nid in present:
        w = VW['NavLabelW'][i] + gap
        cells.append((nid, x, w))
        x += w
    return cells


def nav_hits(active):
    # one hit set per possible mask, each gated by a derived eq on d3.nav.mask and positioned by
    # nav_layout (module NavBar lays its own cells out the same way); the end caps are decoration
    # only, no hit.
    out = []
    for mask in nav_masks():
        gate = f'd3.nav.mask_{mask}'
        for nid, x0, w in nav_layout(mask):
            if nid == active:
                continue
            out.append(hit([x0, NAV_HIT_Y, w, NAV_HIT_H], NAV_LABEL_ACTION[nid], gate))
    return out


def nav_derived():
    out = [{'name': f'd3.nav.mask_{m}', 'cmp': 'eq', 'a': 'd3.nav.mask', 'b': m} for m in nav_masks()]
    # "this nav bit just dropped out of the (already debounced) mask" - page_binds below redirect
    # off it at exactly the moment its tab leaves the bar, never a tick earlier
    out += [{'name': 'd3.nav.hero_gone', 'expr': '(d3.nav.mask % 2) == 0'},
            {'name': 'd3.nav.act_gone', 'expr': '(floor(d3.nav.mask / 2) % 2) == 0'},
            {'name': 'd3.nav.gear_gone', 'expr': '(floor(d3.nav.mask / 8) % 2) == 0'}]
    return out


def skill_rail():
    """Map rail: one picture (rings + button glyphs), the card above it, and per slot either the
    inspect hit (setting off) or the hit of the button it is bound to right now (setting on; a
    long press still opens the card). d3.rail.s<slot>b<bit> comes from the module."""
    ws = [img([0, 0, W, 995], src_bind='d3.skill.rail', need_bind='d3.skill.on'),
          img([SK['CardX'], SK['CardBottom'] - SK['CardMaxH'], SK['CardW'], SK['CardMaxH']],
              src_bind='d3.skill.card',
              anim={'bind': 'd3.skill.selected', 'group': 'skill_card', 'from': 'fade', 'ms': 160,
                    'easing': 'ease_out'}),
          hit([0, 0, W, 995], 'skill_close', 'd3.skill.selected'),
          {'type': 'image', 'rect': [SK['CardX'], SK['CardBottom'] - SK['CardMaxH'], SK['CardW'], SK['CardMaxH']],
           'color': '#00000000', 'src': 'module:d3:none', 'on_tap': 'skill_pin',
           'on_swipe_up': 'skill_down', 'on_swipe_down': 'skill_up', 'need_bind': 'd3.skill.selected'}]
    for visual, slot in enumerate(SK['RailSlots']):
        rect = [SK['RailX'] + visual * SK['SlotW'], SK['RailY'], SK['SlotW'], SK['RailHitH']]
        ws.append(hit(rect, f'skill_{slot}', 'd3.rail.info'))
        for bit in range(len(SK['ButtonNames'])):
            w = hit(rect, f'press_{bit}', f'd3.use.s{slot}b{bit}')
            w.update(on_hold=f'skill_{slot}', hold_ms=450)
            ws.append(w)
    return ws


def rail_derived():
    out = [{'name': 'd3.tap_use.off', 'cmp': 'eq', 'a': '@flag:' + TAP_USE, 'b': 0},
           {'name': 'd3.rail.info', 'all_nonzero': ['d3.skill.on', 'd3.tap_use.off']}]
    for slot in SK['RailSlots']:
        for bit in range(len(SK['ButtonNames'])):
            out.append({'name': f'd3.use.s{slot}b{bit}',
                        'all_nonzero': ['@flag:' + TAP_USE, f'd3.rail.s{slot}b{bit}']})
    return out


def map_page():
    ws = []
    for i in (0, 1):  # previous below, current on top (no flicker while a new picture decodes);
        # both pan under the fixed hero marker by the module's per-picture offsets
        ws.append(img([MAP_CX - MAPW // 2, MAP_CY - MAPH // 2, MAPW, MAPH], src_bind=f'd3.map.k{i}',
                      x_bind=f'd3.map.k{i}x', y_bind=f'd3.map.k{i}y', need_bind=f'd3.map.k{i}on'))
    # Hidden pre-decode slot (never drawn: fully transparent): warms a freshly minted map key's
    # async decode on the host's asset worker before the module promotes it into k1 above, so k1's
    # key and offset always change together with an already-decoded picture (01001B300B9BE000.cpp
    # map block). Static rect, no x_bind/y_bind -- position is irrelevant at alpha 0.
    ws.append(img([MAP_CX - MAPW // 2, MAP_CY - MAPH // 2, MAPW, MAPH], src_bind='d3.map.k2',
                  color='#00000000', need_bind='d3.map.k2on'))
    # Quest/bounty objective marker pulse: a fixed ring picture per tracked dot (module-decoded
    # once, reused every frame), positioned by the module like the map pictures above and looped
    # by scale_bind/tint_bind off a module-published phase -- no picture re-render per frame.
    for i in range(VW['QRingSlots']):
        ws.append(img([0, 0, VW['QRingSize'], VW['QRingSize']], src='module:d3:qring',
                      need_bind=f'd3.qring{i}on', x_bind=f'd3.qring{i}x', y_bind=f'd3.qring{i}y',
                      scale_bind=f'd3.qring{i}scale', tint_bind=f'd3.qring{i}tint',
                      tint_colors=QRING_TINT))
    ws.append(img([VW['ViewCX'] - 80, VW['ViewCY'] - 80, 160, 160], src='module:d3:marker',
                  rotate_bind='d3.marker.rot', need_bind='d3.ready'))
    ws.append(img([COL, 0, W - 2 * COL, 150], src_bind='d3.title.k1', need_bind='d3.title.on'))
    ws.append(img([COL, 420, W - 2 * COL, 160], src_bind='d3.state.key', need_bind='d3.state.on'))
    for i in (0, 1):
        ws.append(img([0, 0, COL, 995], src_bind=f'd3.mapvit.k{i}', need_bind='d3.assets'))
    ws.append(img([W - COL, 0, COL, 995], src='module:d3:right', need_bind='!d3.rift.on'))
    for i in (0, 1):  # bounty column (outside rifts), previous under current
        ws.append(img([W - COL, 0, COL, 995], src_bind=f'd3.bounty.k{i}', need_bind='d3.bounty.on'))
    for i in (0, 1):  # rift panel (two slots: previous under current)
        ws.append(img([W - COL, 0, COL, 995], src_bind=f'd3.rift.k{i}', need_bind='d3.rift.on'))
    ws += skill_rail()
    ws.append(img([0, 995, W, H - 995], src_bind='d3.nav.key', need_bind='d3.assets'))
    ws.append(pause_overlay())
    return {'id': 'map', 'title': 'Map', 'widgets': ws + nav_hits('map')}


def options_page():
    # module-drawn in the game's options style; the checkbox picture follows the flag
    ws = [img([0, 0, W, 995], src='module:d3:options/1', need_bind='@flag:' + TAP_USE),
          img([0, 0, W, 995], src='module:d3:options/0', need_bind='!@flag:' + TAP_USE),
          hit([OP['OptRowX'], OP['OptRowY'], OP['OptRowW'], OP['OptRowH']], 'toggle_tap_use'),
          img([0, 995, W, H - 995], src_bind='d3.nav.key', need_bind='d3.assets'),
          pause_overlay()]
    # sticky: a background availability edge (e.g. pausing) must never bump the player out of
    # Options while they are changing a setting
    return {'id': 'options', 'title': 'Options', 'no_auto_leave': True, 'widgets': ws + nav_hits('options')}


def pause_overlay():
    # native pause menu open: whole page dimmed + GAME PAUSED (module PauseOverlay); shared by tabs
    return img([0, 0, W, H], src_bind='d3.menu.pause_key',
               anim={'bind': 'd3.menu.pause', 'group': 'pause', 'from': 'fade', 'ms': 140,
                     'easing': 'ease_out'})


def activities_page():
    # design A Activities: vitals column + full-width rift card / bounty board (module d3_activities)
    ws = []
    for i in (0, 1):
        ws.append(img([0, 0, COL, 995], src_bind=f'd3.vit.k{i}', need_bind='d3.assets'))
    for i in (0, 1):
        ws.append(img([COL, 0, W - COL, 995], src_bind=f'd3.act.k{i}', need_bind='d3.act.on'))
    ws.append(state_card())
    ws.append(img([0, 995, W, H - 995], src_bind='d3.nav.key', need_bind='d3.assets'))
    ws.append(pause_overlay())
    return {'id': 'activities', 'title': 'Activities', 'widgets': ws + nav_hits('activities')}


def hit(rect, action, need=None):
    w = {'type': 'image', 'rect': rect, 'color': '#00000000', 'src': 'module:d3:none', 'on_tap': action}
    if need:
        w['need_bind'] = need
    return w


def state_card():
    return img([COL, 420, W - 2 * COL, 160], src_bind='d3.state.key', need_bind='d3.state.on')


def hero_page():
    # one composed picture (d3_page_hero), previous under current; Details list scrolls by whole rows
    ws = [img([0, 0, W, 995], src_bind=f'd3.hero.k{i}', need_bind='d3.hero.on') for i in (0, 1)]
    ws.append(state_card())
    # Discrete tap strips at the top/bottom of each scroll column, grown to the 112 px primary-
    # control floor (LEGIBILITY.md) by extending into the column's own empty middle -- the
    # full-column swipe widget behind them (on_swipe_up/down) covers the same area already and a
    # different gesture family, so this never creates a tap-vs-tap overlap; up/down within one
    # column stay anchored to its top/bottom edge and so never overlap each other either.
    HERO_TAP_H = 112
    ws.append({'type': 'image', 'rect': [336, 56, 452, 920], 'color': '#00000000', 'src': 'module:d3:none',
               'on_swipe_up': 'hero_down', 'on_swipe_down': 'hero_up', 'need_bind': 'd3.hero.on'})
    ws.append(hit([336, 56, 452, HERO_TAP_H], 'hero_up', 'd3.hero.on'))
    ws.append(hit([336, 975 - HERO_TAP_H, 452, HERO_TAP_H], 'hero_down', 'd3.hero.on'))
    ws.append({'type': 'image', 'rect': [788, 56, 452, 920], 'color': '#00000000', 'src': 'module:d3:none',
               'on_swipe_up': 'hero_effects_down', 'on_swipe_down': 'hero_effects_up', 'need_bind': 'd3.hero.on'})
    ws.append(hit([788, 56, 452, HERO_TAP_H], 'hero_effects_up', 'd3.hero.on'))
    ws.append(hit([788, 975 - HERO_TAP_H, 452, HERO_TAP_H], 'hero_effects_down', 'd3.hero.on'))
    ws.append(img([0, 995, W, H - 995], src_bind='d3.nav.key', need_bind='d3.assets'))
    ws.append(pause_overlay())
    return {'id': 'hero', 'title': 'Hero', 'widgets': ws + nav_hits('hero')}


GEAR_ORDER = GR['GearSlotOrder']
GEAR_ROWS = GR['GearAvailRows']
# native inventory drive presses (d3_gear_drive.h DriveButton): Plus, A, B, X, DUp, DDown, then the
# left stick towards ring position k (k * 360/14 - 360/28 degrees clockwise from straight up)
DRIVE_BUTTONS = ['Plus', 'A', 'B', 'X', 'DUp', 'DDown'] + [
    'LStick(%.4f,%.4f)' % (math.sin(math.radians(k * 360 / 14 - 360 / 28)),
                           math.cos(math.radians(k * 360 / 14 - 360 / 28))) for k in range(14)]


def gear_page():
    # every hit is locked while any native menu is open (d3.menu.free); Equip / Unequip / Drop
    # exist only when the module offers them (no locked buttons): d3.gear.can_equip / can_unequip /
    # can_drop / confirm. Equip and Unequip are instant (guest load-plan mailbox, d3_gear_mailbox.h);
    # Drop still drives the native menu (d3_gear_drive.h).
    ws = [img([0, 0, W, 995], src_bind=f'd3.gear.k{i}', need_bind='d3.gear.on') for i in (0, 1)]
    ws.append(state_card())
    rh = GR['GearRowH']
    for i, slot in enumerate(GEAR_ORDER):  # slot list
        ws.append(hit([0, GR['GearRowY'] + i * rh, GR['GearColW'], rh], f'gear_slot_{slot}', 'd3.gear.cards_free'))
    ws.append(hit([0, GR['GearFootY'] - 8, GR['GearColW'], 44], 'gear_bag', 'd3.gear.cards_free'))  # Inventory n/60
    cx, cw, chh = GR['GearCardX'], GR['GearCardW'], GR['GearCardH']
    ws.append(hit([cx, GR['GearEqY'], cw, chh], 'gear_equipped', 'd3.gear.cards_free'))
    # UNEQUIP: bottom of the equipped pane (owner request), instant. Tap-only: the hit grows to
    # fill the whole idle gap between the equipped-card hit above and the AVAILABLE list below
    # (both unaffected), well past the drawn button's own GearUnequipY/H (LEGIBILITY.md primary
    # target floor; this still falls short of 112, the max this layout allows without moving
    # either neighbour).
    unequip_hit_y = GR['GearEqY'] + chh
    unequip_hit_h = GR['GearAvailY'] - unequip_hit_y
    ws.append(hit([cx, unequip_hit_y, cw, unequip_hit_h], 'gear_unequip', 'd3.gear.unequip_ok'))
    for r in range(GEAR_ROWS):  # AVAILABLE cards; swipes scroll the list
        w = hit([cx, GR['GearAvailY'] + r * GR['GearAvailStep'], cw, chh], f'gear_avail_{r}', f'd3.gear.row{r}_free')
        w.update(on_swipe_up='gear_scroll_down', on_swipe_down='gear_scroll_up')
        ws.append(w)
    sw = {'type': 'image', 'rect': [GR['GearMidX'], GR['GearAvailY'], GR['GearMidW'], GEAR_ROWS * GR['GearAvailStep']],
          'color': '#00000000', 'src': 'module:d3:none', 'on_swipe_up': 'gear_scroll_down',
          'on_swipe_down': 'gear_scroll_up', 'need_bind': 'd3.gear.more_free'}
    ws.insert(len(ws) - GEAR_ROWS, sw)
    bw, bh, by, cby = GR['GearBtnW'], GR['GearBtnH'], GR['GearBtnY'], GR['GearConfirmBtnY']
    bx0 = cx + (cw - 2 * bw - GR['GearBtnGap']) // 2
    bx1 = bx0 + bw + GR['GearBtnGap']
    # Equip / Drop (normal row, by) and Cancel / Drop? (confirm row, cby) never share a rect: a tap
    # that lands on a stale rect across a confirm-state flip must physically miss the other action
    # (owner bug 2026-10-17, see d3_page_gear.h GearConfirmBtnY). drop_arm_ok additionally requires
    # !confirm so the old row is not a live, invisible hot zone while the confirm row is shown.
    # Tap-only: each row's hit grows to touch (never overlap) the other row and the nav bar's own
    # hit below, well past the drawn bh=62 (LEGIBILITY.md primary target floor) -- the confirm row
    # cannot grow upward (the AVAILABLE cards end right above it), so 80/101 px is the max this
    # fixed layout allows without moving either button row or the AVAILABLE list.
    cby_h = by - cby       # confirm row: up to (not into) the normal row below
    by_h = NAV_HIT_Y - by  # normal row: down to (not into) the nav bar's taller hit
    ws.append(hit([bx0, by, bw, by_h], 'gear_equip', 'd3.gear.equip_ok'))
    ws.append(hit([bx1, by, bw, by_h], 'gear_drop', 'd3.gear.drop_arm_ok'))
    ws.append(hit([bx0, cby, bw, cby_h], 'gear_cancel', 'd3.gear.confirm'))
    ws.append(hit([bx1, cby, bw, cby_h], 'gear_drop', 'd3.gear.confirm'))
    # secondary backpack grid (read-only): header returns to the slot list
    ws.append(hit([0, 0, 762, 60], 'gear_bag', 'd3.gear.bag_free'))
    for y in range(6):
        for x in range(10):
            ws.append(hit([GR['GearBagX'] + x * GR['GearBagCW'], GR['GearBagY'] + y * GR['GearBagCH'],
                           GR['GearBagCW'], GR['GearBagCH']], f'gear_cell_{y * 10 + x}', 'd3.gear.bag_free'))
    ws.append(img([0, 995, W, H - 995], src_bind='d3.nav.key', need_bind='d3.assets'))
    ws.append(pause_overlay())
    return {'id': 'gear', 'title': 'Gear', 'widgets': ws + nav_hits('gear')}


def gear_derived():
    out = [{'name': 'd3.gear.equip_ok', 'all_nonzero': ['d3.gear.can_equip', 'd3.menu.free']},
           {'name': 'd3.gear.unequip_ok', 'all_nonzero': ['d3.gear.can_unequip', 'd3.menu.free']},
           {'name': 'd3.gear.drop_ok', 'all_nonzero': ['d3.gear.can_drop', 'd3.menu.free']},
           # Drop's "arm" rect (normal row) is live only while not confirmed; the confirm row's own
           # Drop? widget (gated directly on d3.gear.confirm) covers the confirmed tap instead.
           {'name': 'd3.gear.not_confirm', 'cmp': 'eq', 'a': 'd3.gear.confirm', 'b': 0},
           {'name': 'd3.gear.drop_arm_ok', 'all_nonzero': ['d3.gear.drop_ok', 'd3.gear.not_confirm']},
           {'name': 'd3.gear.more_free', 'all_nonzero': ['d3.gear.more', 'd3.gear.cards_free']}]
    out += [{'name': f'd3.gear.row{r}_free', 'all_nonzero': [f'd3.gear.r{r}', 'd3.gear.cards_free']}
            for r in range(GEAR_ROWS)]
    return out


def gear_actions():
    out = {f'gear_slot_{s}': {'kind': 'module', 'action': 'gear_slot', 'argument': s} for s in GEAR_ORDER}
    out.update({f'gear_avail_{r}': {'kind': 'module', 'action': 'gear_avail', 'argument': r} for r in range(GEAR_ROWS)})
    out.update({f'gear_cell_{i}': {'kind': 'module', 'action': 'gear_cell', 'argument': i} for i in range(60)})
    out.update({
        'gear_equipped': {'kind': 'module', 'action': 'gear_equipped', 'argument': 0},
        'gear_scroll_down': {'kind': 'module', 'action': 'gear_scroll', 'argument': 1},
        'gear_scroll_up': {'kind': 'module', 'action': 'gear_scroll', 'argument': -1},
        'gear_equip': {'kind': 'module', 'action': 'gear_equip', 'argument': 0, 'haptic': 'confirm'},
        'gear_unequip': {'kind': 'module', 'action': 'gear_unequip', 'argument': 0, 'haptic': 'confirm'},
        'gear_drop': {'kind': 'module', 'action': 'gear_drop', 'argument': 0, 'haptic': 'confirm'},
        'gear_cancel': {'kind': 'module', 'action': 'gear_cancel', 'argument': 0}})
    # native menu presses requested by the module (one gate per press, 4 ticks ~ 67 ms each)
    out.update({f'd3drv_{i}': {'kind': 'button', 'button': b, 'frames': 4, 'haptic': 'off',
                               'enabled_bind': f'd3.drv.p{i}'} for i, b in enumerate(DRIVE_BUTTONS)})
    return out


manifest = {
    'format': 1,
    'title_id': '01001B300B9BE000',
    'name': 'Diablo III: Eternal Collection dual screen',
    # 1.3.0's release runtime (avoids older betas and the 1.2.1 load-plan/NCE mailbox-hang bug).
    'min_runtime': 22,
    'nav': False,
    'requires_module': True,
    # 2.7.7.92380 (k277, original target) and 2.7.6.90885 (k276, d3hack target); both selected by
    # native/modules/d3_build.h's SelectBuild. The runtime's own gate (mod_module.cpp) accepts the
    # running build id as a prefix of either entry, independent of the native module's own check.
    # Parsed straight from d3_build.cpp (build_ids_from_source), not hand-typed here.
    'module': {'abi': 1, 'build_ids': build_ids_from_source()},
    # generated by tools/d3/gen_load_plan.py: an NCE-safe per-frame guest hook + mailbox for Gear
    # Equip/Unequip (k277 only - k276 has no entry under guest/, so DiscoverModLoadPlan finds
    # nothing for it and the module's own mailbox stays absent there, same as before this existed)
    'load_plan': 'guest/<build>/load-plan.json',
    'canvas_w': W,
    'canvas_h': H,
    'background': '#FF060504',
    '_about': 'Generated by tools/d3/gen_manifest.py - edit the generator, not this file. Asset-free: '
              "art and text = module:d3:* pictures composed from the player's romfs.",
    '_contract': 'Module outputs: d3.ready, d3.assets, d3.state.on/key, d3.map.k{0,1}/on/x/y, '
                 'd3.map.k2on (hidden map pre-decode slot), d3.qring<0..2>.on/x/y/scale/tint '
                 '(objective marker pulse), '
                 'd3.title.k1/on, d3.vit.k{0,1}, d3.mapvit.k{0,1}, d3.marker.rot, d3.rift.on/k{0,1}, '
                 'd3.bounty.on/k{0,1}, d3.perf.* (telemetry); Activities: d3.act.k{0,1}/on; '
                 'menus: d3.menu.native/pause/pause_key; Hero/Gear (d3_hg_tabs): d3.hero.k{0,1}/on, '
                 'd3.gear.k{0,1}/on/cards/bag/r<row>/can_equip/can_unequip/can_drop/confirm/more; '
                 'Equip/Unequip are instant through the guest load-plan mailbox (d3_gear_mailbox.h); '
                 'Drop still drives the native menu (d3_gear_drive.h): press gates d3.drv.p<id> + '
                 'enforce gate d3.enf; Skills (Map rail): d3.skill.on/selected/rail/card, '
                 'd3.rail.s<slot>b<bit> (bound button of each usable slot; bits name ButtonNames); '
                 'Nav bar: d3.nav.mask (bit i = NavIndex i present, debounced ~1 s), d3.nav.key '
                 '(module:d3:nav/<active>/<mask>, src_bind on every page).',
    'haptics': HAPTICS,
    'flags': {TAP_USE: 0},
    'persist_flags': [TAP_USE],
    # Drop's native-menu drive presses: due every second tick (33 ms); d3.enf opens only on a press
    # tick, so idle ticks run nothing (module d3_gear_drive). Equip/Unequip need none of this.
    'enforce': [{'action': f'd3drv_{i}', 'every_ms': 33} for i in range(len(DRIVE_BUTTONS))],
    'enforce_gate': {'point': 'd3.enf', 'max': 1},
    'derived': rail_derived() + nav_derived() + [
        {'name': 'd3.menu.free', 'cmp': 'eq', 'a': 'd3.menu.native', 'b': 0},
        {'name': 'd3.gear.free', 'all_nonzero': ['d3.gear.on', 'd3.menu.free']},
        {'name': 'd3.gear.cards_free', 'all_nonzero': ['d3.gear.cards', 'd3.menu.free']},
        {'name': 'd3.gear.bag_free', 'all_nonzero': ['d3.gear.bag', 'd3.menu.free']}] + gear_derived(),
    # a tab that drops out of the (debounced) nav bar also stops being a legal destination: send
    # the player back to Map the moment it disappears (Options is sticky, see no_auto_leave)
    'page_binds': [{'point': 'd3.nav.hero_gone', 'equals': 1,
                    'when_equal': {'page': 'map', 'transition': 'fade', 'duration_ms': 120}},
                   {'point': 'd3.nav.act_gone', 'equals': 1,
                    'when_equal': {'page': 'map', 'transition': 'fade', 'duration_ms': 120}},
                   {'point': 'd3.nav.gear_gone', 'equals': 1,
                    'when_equal': {'page': 'map', 'transition': 'fade', 'duration_ms': 120}}],
    'actions': {**{('tab_' + t): {'kind': 'page', 'page': t, 'transition': 'fade', 'duration_ms': 120,
                                  'haptic': 'click'} for t in TABS},
                'tab_options': {'kind': 'page', 'page': 'options', 'transition': 'fade', 'duration_ms': 120,
                                'haptic': 'click'},
                'toggle_tap_use': {'kind': 'flag', 'flag': TAP_USE, 'haptic': 'confirm'},
                # tap-to-use: the game's own button, held 6 ticks (~100 ms) like a quick press
                **{f'press_{b}': {'kind': 'button', 'button': name, 'frames': 6, 'haptic': 'confirm',
                                  'enabled_bind': 'd3.skill.on'}
                   for b, name in enumerate(SK['ButtonNames'])},
                'hero_down': {'kind': 'module', 'action': 'hero_scroll', 'argument': 5},
                'hero_up': {'kind': 'module', 'action': 'hero_scroll', 'argument': -5},
                'hero_effects_down': {'kind': 'module', 'action': 'hero_effects_scroll', 'argument': 2},
                'hero_effects_up': {'kind': 'module', 'action': 'hero_effects_scroll', 'argument': -2},
                'gear_bag': {'kind': 'module', 'action': 'gear_bag', 'argument': 0},
                'skill_close': {'kind': 'module', 'action': 'skill_close', 'argument': 0},
                'skill_pin': {'kind': 'module', 'action': 'skill_scroll', 'argument': 0},
                'skill_down': {'kind': 'module', 'action': 'skill_scroll', 'argument': 3},
                'skill_up': {'kind': 'module', 'action': 'skill_scroll', 'argument': -3},
                **{f'skill_{i}': {'kind': 'module', 'action': 'skill_inspect', 'argument': i} for i in range(7)},
                **gear_actions()},
    'pages': [map_page(), hero_page(), activities_page(), gear_page(), options_page()],
}
# the Map tab is the default page (first)
out = ROOT / 'packages/DiabloIII/dualscreen/manifest.json'
out.write_text(json.dumps(manifest, indent=1) + '\n')
print('wrote', out)
