#!/usr/bin/env python3
"""Authored hit regions for accepted A. Art/text are decoded from the player's RomFS."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'packages/FarmingSimulatorNintendoSwitchEdition/dualscreen/manifest.json'
BUILD='7D0651174A65DD848CB60BE3304FD62900000000000000000000000000000000'
actions={}
# The map's own pixels (base texture, current layer, markers, player arrow)
# live in their own "fs.ui.map_frame" image (RenderMapBase in
# farming_simulator_view.cpp), shown here through the runtime's native
# pan_zoom Image widgets instead of being baked into the big "fs.ui.frame"
# page image. A drag/pinch on these widgets is then a pure runtime transform
# -- no module action, no new revision, no re-encoding the whole page --
# which is what fixes the reported lag: map_pan/map_pinch used to mint a
# revision per gesture tick and wait for the asset worker to decode it
# before the next one could even mint.
#
# The module renders the map into ONE padded canvas (MapCanvasW x
# MapCanvasH world units, see farming_simulator_view.h) wide/tall enough
# that the expanded viewport's own width/height fraction of it is exactly
# 1.0 -- i.e. expanded's static src_rect is the whole canvas. Compact's
# src_rect is then the centred crop of that canvas whose width/height
# fraction matches 888/MapCanvasW and 888/MapCanvasH respectively, so both
# viewports show the map at the *same* world-units-per-pixel scale (no
# stretching at any zoom, "expanded just shows more ground"). The numbers
# below must track farming_simulator_view.h's CompactMapPx/ExpandedMapW/
# ExpandedMapH/MapWorldExtent constants.
_COMPACT_PX,_EXPANDED_W,_EXPANDED_H,_WORLD=888.,1240.,1000.,2048.
_CANVAS_W=_WORLD*_EXPANDED_W/_COMPACT_PX
_CANVAS_H=_WORLD*_EXPANDED_H/_COMPACT_PX
_COMPACT_FRAC_W=_WORLD/_CANVAS_W
_COMPACT_FRAC_H=_WORLD/_CANVAS_H
map_widgets=[]
for width,height,gate,suffix in [(888,888,'fs.ui.map_view_compact','compact'),
                                 (1240,1000,'fs.ui.map_view_expanded','expanded')]:
    wid=f'map-{suffix}'
    if suffix=='compact':
        u0,v0=(1-_COMPACT_FRAC_W)/2,(1-_COMPACT_FRAC_H)/2
        src_rect=[u0,v0,u0+_COMPACT_FRAC_W,v0+_COMPACT_FRAC_H]
    else:
        src_rect=[0,0,1,1]
    map_widgets.append({'id':wid,'type':'image','rect':[0,80,width,height],
                        'src_bind':'fs.ui.map_frame','need_bind':gate,
                        'pan_zoom':True,'min_zoom':1,'max_zoom':4,'src_rect':src_rect,
                        'view_zoom_bind':'fs.ui.map_home_zoom',
                        'view_cx_bind':f'fs.ui.map_home_cx_{suffix}',
                        'view_cy_bind':f'fs.ui.map_home_cy_{suffix}',
                        'view_reset_bind':'fs.ui.map_identity'})
    if suffix=='expanded':
        # The padded canvas extends past the world; keep the expanded view
        # window over the world itself (centred when the window is wider
        # than the world, i.e. at the widest zoom). Optional runtime key:
        # older runtimes ignore it and clamp to the whole canvas.
        bu,bv=(1-_COMPACT_FRAC_W)/2,(1-_COMPACT_FRAC_H)/2
        map_widgets[-1]['pan_bounds']=[round(bu,5),round(bv,5),round(1-bu,5),round(1-bv,5)]
# Player arrow: a separate, unthrottled overlay on top of each map-{suffix}
# pan_zoom widget (map_widgets above), instead of being pre-rotated and
# baked into the shared fs.ui.map_frame raster (which only rebuilds at the
# module's own ~5Hz MapSnapshotKey cadence -- the reported "5fps" map
# motion). fs.ui.map_arrow is a constant sprite key (module caches it
# forever, see 0100D04004176000.cpp); rotate_bind (fs.ui.map_arrow_heading_deg)
# is republished every module Sample() tick, so the runtime turns it live.
# Fixed at each viewport's own rect centre: while the view is at its native
# default ("following" -- the common case while walking/driving), the
# runtime always centres home_cx/cy (the player's own position) there, so
# this is exactly where the player is on screen.
#
# x_bind/y_bind (fs.ui.map_arrow_dx/dy_{suffix}, ArrowPanOffset in
# farming_simulator_view.h / 0100D04004176000.cpp's Sample()) move the arrow
# off that centre to track the player's true screen position once the user
# has panned or zoomed the viewport away from home -- but only on a runtime
# that answers the pan_zoom widget's own live view back to the module
# ("@view_zoom/@view_pan_x/@view_pan_y:map-{suffix}", requested: see this
# cycle's runtime-change request for a pan-anchored map marker on the
# Farming Simulator companion). An older runtime (e.g. beta.17)
# ignores the unknown key, ArrowPanOffset answers nullopt, the module
# publishes 0/0 and fs.ui.map_arrow_pan_tracked_{suffix}=0, and the visibility
# expr below falls back to exactly the prior behaviour: pinned to the centre,
# hidden once "view_custom:map-{suffix}" says the view moved away from it
# (the native/runtime's own state, same as the Reset button's visibility).
for width,height,gate,suffix in [(888,888,'fs.ui.map_compact','compact'),
                                 (1240,1000,'fs.ui.map_expanded','expanded')]:
    # G3 critique fix (2026-10-17): RenderMapArrow's canvas grew 60x60->84x84
    # so the restored native glyph can render at 44x44 (readable as a
    # pointer) with the lime ring still strictly outside it -- see that
    # function's own doc comment and farming_simulator_view.h's matching
    # static_assert. The rect stays centred on the same viewport-centre
    # point as before, just 12px further out on each edge.
    cx,cy=width/2,80+height/2
    map_widgets.append({'id':f'map-arrow-{suffix}','type':'image','rect':[cx-42,cy-42,84,84],
                        'src_bind':'fs.ui.map_arrow','need_bind':f'fs.view.map_arrow_{suffix}',
                        'rotate_bind':'fs.ui.map_arrow_heading_deg',
                        'x_bind':f'fs.ui.map_arrow_dx_{suffix}',
                        'y_bind':f'fs.ui.map_arrow_dy_{suffix}'})
scrolls=[]
widgets=map_widgets+[{'type':'image','rect':[0,0,1240,1080],'src_bind':'fs.ui.frame','need_bind':'module_ready',
          # Cross-fades the full-canvas frame over 140ms, but only when
          # fs.ui.transition (a counter the module bumps on a meaningful view
          # change -- footer page, time-scale picker open/close, map layer,
          # shop/vehicle detail open/close, animal species/feeding tab, help
          # topic) differs from its value at the previous swap; a routine
          # data-only update (clock, money, player movement, prices) swaps
          # instantly as before. Older runtimes ignore both unknown keys.
          'src_fade_ms':140,'src_fade_bind':'fs.ui.transition'},
         {'type':'label','rect':[36,80,1100,80],'text':'Companion unavailable','text_scale':4,'hide_bind':'module_ready','keep_max':0},
         {'type':'label','rect':[36,180,1100,100],'bind_text':'module_error_message','text_scale':2,'hide_bind':'module_ready','keep_max':0},
         {'type':'label','rect':[36,80,1100,80],'text':'Loading companion assets...','text_scale':3,'need_bind':'module_ready','hide_bind':'fs.ui.art_ready','keep_max':0}]
def action(name,verb,argument,haptic=None,enabled_bind=None):
    d={'kind':'module','action':verb,'argument':argument}
    if haptic:d['haptic']=haptic
    if enabled_bind:d['enabled_bind']=enabled_bind
    actions[name]=d
    return name
def view_reset_action(name,view,haptic=None):
    # Map Reset: a native runtime action (ActionKind::ViewReset), never a
    # module action -- it glides the named pan_zoom widget's own view back to
    # its bound default (2x following the player), the same mechanism that
    # handles the drag/pinch gestures themselves. See map_widgets below.
    d={'kind':'view_reset','view':view}
    if haptic:d['haptic']=haptic
    actions[name]=d
    return name
def hit(rect,name,gate=None,**kw):
    w={'type':'rect','rect':rect,'bg':'#00000000','color':'#00000000','frame':0,'on_tap':name,**kw}
    if gate:w['need_bind']=gate
    widgets.append(w)
def swipe(rect,gate,vertical=None,horizontal=None,**kw):
    w={'type':'rect','rect':rect,'bg':'#00000000','color':'#00000000','frame':0,'need_bind':gate,**kw}
    areas={'shop_specs':'shop_specs'}
    prefix=vertical[1] if vertical else horizontal[1]
    w['on_pan']=action(prefix+'_drag','scroll:'+areas[prefix],'$payload')
    w['id']='scroll-'+prefix
    if vertical:
        verb,prefix=vertical
        w['on_swipe_up']=action(prefix+'_next',verb,1)
        w['on_swipe_down']=action(prefix+'_previous',verb,-1)
    if horizontal:
        verb,prefix=horizontal
        w['on_swipe_left']=action(prefix+'_next',verb,1)
        w['on_swipe_right']=action(prefix+'_previous',verb,-1)
    widgets.append(w)
# Guide topics list: the runtime's own native "help-topics" ScrollRegion
# (scrolls list below), the same shape as the Shop catalog grid's own
# conversion (a single column here instead of 2 cols) -- drag/fling/clip are
# the runtime's, with no module round trip per scroll step. Each row is its
# own module image ("fs.ui.help.topic_row_src_{i}", see 0100D04004176000.cpp's
# help_topic_row_src); the hit template's payload "{i}" is the absolute
# topic index directly (OnViewAction's "help_topic" action), not a 0..6
# visible-window slot. No wheel step wired (ScrollRegion has no wheel path,
# Thor has no wheel).
help_topics_rep={'repeat':64,'repeat_bind':'fs.ui.help.topic_count','repeat_dy':144,'scroll':'help-topics'}
widgets.append({'type':'image','rect':[0,88,352,144],'src_bind':'fs.ui.help.topic_row_src_{i}',
                'need_bind':'fs.ui.page.8',**help_topics_rep})
widgets.append({'type':'rect','rect':[0,88,352,144],'bg':'#00000000','color':'#00000000','frame':0,
                'need_bind':'fs.ui.page.8',
                'on_tap':action('help_topic_pick','help_topic','$payload',haptic='click'),
                'payload':'{i}',**help_topics_rep})
scrolls.append({'id':'help-topics','rect':[0,88,352,864],'count_bind':'fs.ui.help.topic_count',
                'row_h':144,'show_bind':'fs.ui.page.8',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Guide article body: the runtime's own native "help-article"/
# "help-article-static" ScrollRegions (scrolls list below) instead of a
# module-tracked offset -- content_h_bind is the article's real (measured,
# cap-clamped) content height (fs.ui.help.article_h, HelpArticleCap in
# farming_simulator_view.h bounds the image's own allocated size, never the
# scrollable range itself). Both regions share the one image
# (fs.ui.help.article_src) and the one reset_bind (fs.ui.help_topic, so
# picking a different topic snaps either viewport back to the top the same
# way the module used to zero help_offset on a topic pick) but have
# different rects/show_binds: in-world Help gets the shorter 708-tall
# viewport (the farm/time header above it), static (title/no-farm) Help
# gets the taller 836. fs.ui.page.8 alone cannot tell the two apart (true
# for both), hence the dedicated fs.ui.help_dynamic/fs.ui.static_help
# show_binds.
widgets.append({'type':'image','rect':[372,244,832,4000],'src_bind':'fs.ui.help.article_src',
                'need_bind':'fs.ui.help_dynamic','repeat':1,'scroll':'help-article'})
widgets.append({'type':'image','rect':[372,244,832,4000],'src_bind':'fs.ui.help.article_src',
                'need_bind':'fs.ui.static_help','repeat':1,'scroll':'help-article-static'})
scrolls.append({'id':'help-article','rect':[372,244,832,708],'content_h_bind':'fs.ui.help.article_h',
                'show_bind':'fs.ui.help_dynamic','reset_bind':'fs.ui.help_topic',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
scrolls.append({'id':'help-article-static','rect':[372,244,832,836],'content_h_bind':'fs.ui.help.article_h',
                'show_bind':'fs.ui.static_help','reset_bind':'fs.ui.help_topic',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Shop filter list: the runtime's own native "shop-filters" ScrollRegion
# (scrolls list below) instead of a module-tracked offset, same shape as
# the Guide topics list's own conversion -- drag/fling/clip are the
# runtime's, each row is its own module image
# ("fs.ui.shop_filters.row_src_{i}", see 0100D04004176000.cpp's
# shop_filter_row_src). The hit template's payload "{i}" is the absolute
# category/brand index directly (OnViewAction's "shop_filter_pick" action),
# not a 0..6 visible-window slot. reset_bind'd to
# fs.ui.shop_filters_identity (a hash of shop_filter_mode/world_identity)
# so switching between Category/Brand snaps the list back to the top. No
# wheel step wired (ScrollRegion has no wheel path, Thor has no wheel).
shop_filters_rep={'repeat':256,'repeat_bind':'fs.ui.shop_filters.count','repeat_dy':108,'scroll':'shop-filters'}
widgets.append({'type':'image','rect':[20,192,344,108],'src_bind':'fs.ui.shop_filters.row_src_{i}',
                'need_bind':'fs.ui.shop_browse',**shop_filters_rep})
widgets.append({'type':'rect','rect':[20,192,344,108],'bg':'#00000000','color':'#00000000','frame':0,
                'need_bind':'fs.ui.shop_browse',
                'on_tap':action('shop_filter_pick','shop_filter_pick','$payload',haptic='click'),
                'payload':'{i}',**shop_filters_rep})
scrolls.append({'id':'shop-filters','rect':[20,192,344,640],'count_bind':'fs.ui.shop_filters.count',
                'row_h':108,'show_bind':'fs.ui.shop_browse','reset_bind':'fs.ui.shop_filters_identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Shop catalog grid: the runtime's own native "shop-catalog" ScrollRegion
# (scrolls list below) instead of a module-tracked offset -- drag/fling/clip
# are all the runtime's, with no module round trip per scroll step. Each
# card is its own module image, published per filtered index as
# "fs.ui.shop_grid.src_{i}" (see 0100D04004176000.cpp's shop_grid_src); the
# runtime only requests the ones actually on screen. The hit template's
# payload "{i}" is the absolute filtered-item index (shop_item's argument is
# now that index directly, not a 0..5 visible-window slot). No wheel step is
# wired here: ScrollRegion has no wheel path of its own (only on_swipe_*
# widgets do), and Thor has no wheel to serve anyway.
shop_catalog_rep={'repeat':512,'repeat_bind':'fs.ui.shop_grid.count','repeat_cols':2,
                  'repeat_dx':424,'repeat_row_dy':384,'scroll':'shop-catalog'}
widgets.append({'type':'image','rect':[384,96,412,372],'src_bind':'fs.ui.shop_grid.src_{i}',
                'need_bind':'fs.ui.shop_browse',**shop_catalog_rep})
widgets.append({'type':'rect','rect':[384,96,412,372],'bg':'#00000000','color':'#00000000','frame':0,
                'need_bind':'fs.ui.shop_browse',
                'on_tap':action('shop_item','shop_item','$payload',haptic='click'),
                'payload':'{i}',**shop_catalog_rep})
scrolls.append({'id':'shop-catalog','rect':[384,96,836,768],'count_bind':'fs.ui.shop_grid.count',
                'row_h':384,'cols':2,'show_bind':'fs.ui.shop_browse',
                'reset_bind':'fs.ui.shop_grid.identity','bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
hit([20,96,192,88],action('shop_back','shop_back',0,haptic='light'),'fs.ui.shop_details')
swipe([480,224,740,728],'fs.ui.shop_details',vertical=('shop_specs','shop_specs'))
# Garage list: the runtime's own native "garage-list" ScrollRegion (scrolls
# list below) instead of a module-tracked offset, same shape as the Shop
# catalog grid's own conversion (a single column here instead of a grid) --
# drag/fling/clip are the runtime's, each row is its own module image
# ("fs.ui.garage.row_src_{i}", see 0100D04004176000.cpp's garage_row_src).
# The hit template's payload "{i}" is the absolute vehicle index directly
# (OnViewAction's "vehicle_pick" action), not a 0..4 visible-window slot. No
# wheel step wired (ScrollRegion has no wheel path, Thor has no wheel).
garage_list_rep={'repeat':512,'repeat_bind':'fs.ui.garage.count','repeat_dy':207,'scroll':'garage-list'}
widgets.append({'type':'image','rect':[20,104,440,207],'src_bind':'fs.ui.garage.row_src_{i}',
                'need_bind':'fs.ui.page.4',**garage_list_rep})
widgets.append({'type':'rect','rect':[20,104,440,207],'bg':'#00000000','color':'#00000000','frame':0,
                'need_bind':'fs.ui.page.4',
                'on_tap':action('vehicle_pick','vehicle_pick','$payload',haptic='click'),
                'payload':'{i}',**garage_list_rep})
scrolls.append({'id':'garage-list','rect':[20,104,440,828],'count_bind':'fs.ui.garage.count',
                'row_h':207,'show_bind':'fs.ui.page.4','reset_bind':'fs.ui.garage.identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Garage specs: the runtime's own native "garage-specs" ScrollRegion, same
# shape as the list above -- each row is the CURRENTLY selected vehicle's
# own spec ("fs.ui.garage.spec_row_src_{i}"), reset_bind'd to
# fs.ui.garage_spec_identity (a hash of vehicle_selected/world_identity) so
# picking a different vehicle snaps its specs back to the top.
garage_specs_rep={'repeat':64,'repeat_bind':'fs.ui.garage.spec_count','repeat_dy':80,'scroll':'garage-specs'}
widgets.append({'type':'image','rect':[484,720,736,80],'src_bind':'fs.ui.garage.spec_row_src_{i}',
                'need_bind':'fs.ui.page.4',**garage_specs_rep})
# Shrunk from 240 (3 rows) to 132 (1.65 rows): UX-SPEC B.2's action row (below) sits at
# 484,860-1240,956, which the old 720..960 viewport would have scrolled straight under.
# The viewport stays this height even for a vehicle with neither action available (the row's
# own 0-height collapse is pixels-only -- see RenderGarageActionRow/action_kind 0 -- a static
# manifest ScrollRegion rect cannot reclaim that space per-vehicle; a documented, deliberate
# simplification, not the "no empty gap either way" behaviour UX-SPEC B.2 describes).
scrolls.append({'id':'garage-specs','rect':[484,720,736,80],'count_bind':'fs.ui.garage.spec_count',
                'row_h':80,'show_bind':'fs.ui.page.4','reset_bind':'fs.ui.garage_spec_identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Garage action row (UX-SPEC B.2/B.3, grown by the G3 critique fix, 2026-10-17): a fixed row
# pinned below the (now shorter) specs ScrollRegion, never part of it. 736x128 (112px buttons,
# the LEGIBILITY.md primary-control floor, plus a 16px top margin) replaces the old 756x96/80px
# row -- RenderGarageActionRow's own doc comment has the native-grammar/sizing rationale. Moved
# up from y=860 to y=840 (968-128) so the taller row still ends flush with the footer's own
# top edge instead of overlapping it; the specs ScrollRegion above shrank from 132 to 120 to
# match (82..840 content window instead of 82..852 -- UX-SPEC B.2 allows shrinking the specs
# region to make room). The served image (RenderGarageActionRow) is one of 3 constant pictures
# selected purely by fs.ui.garage.action_kind (1 both / 2 Enter only / 3 Reset only); action_kind
# 0 (neither available) shows nothing and has no hit region, which is what "the row collapses to
# 0 height" means here in practice (the pixels above it do not move, a declared deviation -- see
# the ScrollRegion comment above).
widgets.append({'type':'image','rect':[484,840,736,128],'src_bind':'fs.ui.garage.action_src',
                'need_bind':'fs.ui.garage.action_visible'})
garage_enter=action('garage_enter','garage_enter',0,haptic='confirm')
garage_reset=action('garage_reset','garage_reset',0,haptic='click')
hit([484,856,360,112],garage_enter,'fs.ui.garage.action_both')
hit([860,856,360,112],garage_reset,'fs.ui.garage.action_both')
hit([484,856,736,112],garage_enter,'fs.ui.garage.action_enter_only')
hit([484,856,736,112],garage_reset,'fs.ui.garage.action_reset_only')
# Map marker tap-select + selection card (UX-SPEC C): tapping a vehicle or place marker on the
# map ("map-compact"/"map-expanded", above) sets the module's map_marker_selected and swaps the
# right pane (compact only -- see map_marker_pick's own OnViewAction comment) to a selection
# card, same pane-replace slot as the time-scale picker/legend. The card's header (name/
# category/art) is baked directly into fs.ui.frame (View::Operations); only its action row below
# is this separate, smaller served image + hit regions, same convention as the Garage action row
# above. Tap-vs-pan on the pan_zoom map widgets: the runtime's own gesture slop already decides
# this upstream of on_tap (UpdateGestures in mod_input.cpp) -- a short tap under the slop reaches
# this widget's on_tap, a drag past it drives the pan_zoom widget's pan/pinch instead, so the hit
# widgets below need no special-casing to coexist with panning.
#
# Per-marker hit regions are plain transparent rects positioned by x_bind/y_bind
# (fs.ui.map_marker_dx_{suffix}_{i}/dy_{suffix}_{i}, the marker's screen offset from the
# viewport centre) rather than a native WidgetType::Map (that mechanism -- on_marker_tap,
# map_areas/dynamic_markers -- belongs to a different, runtime-tracked marker system this
# module's own baked-raster map does not use). Pan-tracked the same way as the player arrow
# (map-arrow-{compact,expanded} above, ArrowPanOffset/farming_simulator_view.h): on a runtime
# that answers "@view_zoom/@view_pan_x/@view_pan_y:map-{suffix}", the module recomputes each
# marker's offset from that viewport's LIVE pan/zoom (clamped inside the rect -- "stays clipped
# to the map rect") every tick, so the hit region tracks a panned/zoomed view instead of only
# the default one. On an older runtime that never answers the query, the per-suffix key falls
# back to the exact same home-view-only value the two suffixes used to share (0100D04004176000.cpp),
# and fs.ui.map_marker_show_{compact,expanded}_{i}'s own "pan_tracked || !view_custom" gate below
# (the same gate fs.view.map_arrow_{suffix} already uses) hides the region once panned, same as
# before this fix.
MARKER_CAP=48
marker_pick=action('map_marker_pick','map_marker_pick','$payload',haptic='light')
for i in range(MARKER_CAP):
    for suffix,cx,cy in (('compact',444,524),('expanded',620,580)):
        widgets.append({'type':'rect','rect':[cx-32,cy-32,64,64],'bg':'#00000000','color':'#00000000',
                        'frame':0,'need_bind':f'fs.ui.map_marker_show_{suffix}_{i}',
                        'x_bind':f'fs.ui.map_marker_dx_{suffix}_{i}',
                        'y_bind':f'fs.ui.map_marker_dy_{suffix}_{i}',
                        'on_tap':marker_pick,'payload':i})
# Selection card action row (G3 critique fix, 2026-10-17): grown from a 148x80 side-by-side
# split (below the 112px primary-control floor either way) to two stacked full-width 112px rows
# -- RenderMapSelectionButtons' own doc comment has the sizing rationale; a single constant
# 304x236 canvas for every kind, 'place'/kind baked into the served picture, not the rect.
# Pane: 916,732-1220,968 (304x236, flush with the pane's own 968 bottom edge, same margin
# convention the Garage action row above uses). Card (expanded, UX-SPEC C review's
# floating-card pattern): centred in the 492-wide floating card body at 732,96
# (View::Operations' MapSelectionCard branch, CardH grown 500->646 to fit the taller rows
# below the header/art -- see that function's own comment), same 304-wide stack.
map_enter=action('map_sel_enter','garage_enter',0,haptic='confirm')
# No map-card Reset: Reset is Garage-only (owner, 2026-10-18 -- one stray tap on the map
# card sent the selected vehicle to the shop lot). Vehicles get Enter only here.
map_visit=action('map_sel_visit','map_visit',0,haptic='confirm')
map_tag=action('map_sel_tag','map_tag',0,haptic='click')
for suffix,bx,by in (('pane',916,732),('card',732+(492-304)//2,96+390)):
    widgets.append({'type':'image','rect':[bx,by,304,236],'src_bind':'fs.ui.map_selection.buttons_src',
                    'need_bind':f'fs.ui.map_sel.{suffix}_active'})
    hit([bx,by,304,112],map_enter,f'fs.ui.map_sel.{suffix}_enter_only')
    hit([bx,by,304,112],map_visit,f'fs.ui.map_sel.{suffix}_both_place')
    hit([bx,by+124,304,112],map_tag,f'fs.ui.map_sel.{suffix}_both_place')
    hit([bx,by,304,112],map_visit,f'fs.ui.map_sel.{suffix}_visit_only')
    hit([bx,by,304,112],map_tag,f'fs.ui.map_sel.{suffix}_tag_only')
for i in range(3):hit([20+i*404,16,392,104],action(f'animal_species_{i}','animal_species',i),'fs.ui.page.6')
for i in range(2):hit([20+i*606,136,594,88],action(f'animal_detail_{i}','animal_detail',i),'fs.ui.page.6')
# Animal needs pane: the runtime's own native "animal-needs" ScrollRegion
# (scrolls list below) instead of a module-tracked offset -- the same mixed
# row-list-plus-free-form-content shape as the Guide article's own
# conversion (needs rows, then free-form help text and an optional
# illustration). Previously ONE 1200x16000 module image (the full
# AnimalNeedsCap, farming_simulator_view.h) -- always over the runtime's
# 4096px-per-axis module image limit AND its 16MB MaxImageBytes
# (1200*16000*4 ~= 76.8MB), silently refused on every request. Now
# AnimalNeedsStripCap fixed AnimalNeedsStripHeight(1600)-tall strips
# (1200*1600*4 ~= 7.3MB each), tiled vertically by this same ScrollRegion
# exactly like any other y-only row list (RenderAnimalNeedsStrip,
# 0100D04004176000.cpp's fs.ui.animal.needs_strip_src_{i}) -- only the
# strip(s) actually on screen are requested. content_h_bind
# (fs.ui.animal.needs_h) is still the real measured TOTAL content height,
# independent of the strip split. reset_bind'd to fs.ui.animal_identity (a
# hash of selected species/feeding tab/world identity --
# 0100D04004176000.cpp) so picking a different species or tab snaps it back
# to the top. No wheel step wired (ScrollRegion has no wheel path, Thor has
# no wheel).
animal_needs_rep={'repeat':10,'repeat_dy':1600,'scroll':'animal-needs'}
widgets.append({'type':'image','rect':[20,248,1200,1600],'src_bind':'fs.ui.animal.needs_strip_src_{i}',
                'need_bind':'fs.ui.animal_feeding',**animal_needs_rep})
scrolls.append({'id':'animal-needs','rect':[20,248,1200,704],'content_h_bind':'fs.ui.animal.needs_h',
                'show_bind':'fs.ui.animal_feeding','reset_bind':'fs.ui.animal_identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Prices matrix: the runtime's own native two-axis "fs-prices" ScrollRegion
# (scrolls list below) instead of module-tracked crop/station offsets --
# drag/fling/clip on both axes are the runtime's, with no module round trip
# per scroll step. The body used to be a repeat template of per-station row
# images, each the FULL PriceColumnCap(32)*212 = 6784px wide -- always over
# the runtime's 4096px-per-axis module image limit, silently refused on
# every request (the live Prices page showed only the frozen station column,
# the one piece under the limit, and nothing else). Each row -- and the
# header row below -- is now PriceColumnChunks column chunks of
# PriceColumnChunk(16)*212 = 3392px each (farming_simulator_view.h), laid
# out as a repeat_cols grid (col = chunk, row = station --
# 0100D04004176000.cpp's price_row_src is in that same row-major order) so a
# fixed-size Image widget never stretches any one chunk; content_w_bind
# still clamps the actually scrollable width to the live (capped) crop
# count, unchanged. The crop header row (scroll_follow "x", frozen
# vertically) is its own PriceColumnChunks-chunk repeat template (1 row, no
# repeat_cols needed); the station-name column (scroll_follow "y", frozen
# horizontally, 352px -- already comfortably under the limit, untouched) is
# still the one tall image it always was. No wheel step is wired here,
# matching the Shop catalog grid's own conversion: ScrollRegion has no wheel
# path, and Thor has no wheel to serve.
_PRICE_CHUNK_COLS,_PRICE_CHUNK_W=16,212*16
_PRICE_CHUNKS=-(-32//_PRICE_CHUNK_COLS) # ceil(PriceColumnCap/PriceColumnChunk) = 2
prices_rep={'repeat':40*_PRICE_CHUNKS,'repeat_bind':'fs.ui.prices.row_cells',
            'repeat_cols':_PRICE_CHUNKS,'repeat_dx':_PRICE_CHUNK_W,'repeat_row_dy':84,
            'scroll':'fs-prices'}
widgets.append({'type':'image','rect':[372,340,_PRICE_CHUNK_W,84],'src_bind':'fs.ui.prices.row_src_{i}',
                'need_bind':'fs.ui.page.3',**prices_rep})
prices_header_rep={'repeat':_PRICE_CHUNKS,'repeat_dx':_PRICE_CHUNK_W,'scroll':'fs-prices',
                   'scroll_follow':'x'}
widgets.append({'type':'image','id':'prices-header','rect':[372,104,_PRICE_CHUNK_W,240],
                'src_bind':'fs.ui.prices.header_src_{i}','need_bind':'fs.ui.page.3',
                **prices_header_rep})
widgets.append({'type':'image','id':'prices-stations','rect':[20,340,352,84*40],
                'src_bind':'fs.ui.prices.stations_src','need_bind':'fs.ui.page.3',
                'repeat':1,'scroll':'fs-prices','scroll_follow':'y'})
scrolls.append({'id':'fs-prices','rect':[372,340,848,588],'axes':'xy','lock_axis':True,
                'content_w_bind':'fs.ui.prices.w','count_bind':'fs.ui.prices.count','row_h':84,
                'show_bind':'fs.ui.page.3','reset_bind':'fs.ui.prices.identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Finance table: the runtime's own native two-axis "fs-finance"
# ScrollRegion, the same shape as Prices' "fs-prices" above (frozen day
# header row + frozen row-label column + a repeat template of per-row day
# images), except the day axis has a fixed, always-exactly-5-wide content
# (FinanceRow::amounts is a std::array<5> -- farming_simulator_view.h), so
# content_w is a plain constant here (1180 = 5*236), not a bind. The totals
# row is a fourth, independent image: also frozen on x (scroll_follow "x")
# but outside the main vertical row scroll entirely, always pinned at the
# bottom (fs.ui.finance.totals_src).
finance_rep={'repeat':64,'repeat_bind':'fs.ui.finance.count','repeat_dy':76,'scroll':'fs-finance'}
widgets.append({'type':'image','rect':[512,292,1180,76],'src_bind':'fs.ui.finance.row_src_{i}',
                'need_bind':'fs.ui.page.5',**finance_rep})
widgets.append({'type':'image','id':'finance-header','rect':[512,216,1180,76],
                'src_bind':'fs.ui.finance.header_src','need_bind':'fs.ui.page.5',
                'repeat':1,'scroll':'fs-finance','scroll_follow':'x'})
# The frozen row-label column used to be ONE 492x(76*64)=4864px-tall image --
# over the runtime's 4096px-per-axis module image limit, silently refused on
# every request (the live Finance page's label column rendered BLANK). Now
# chunked into _FINANCE_LABEL_CHUNKS fixed _FINANCE_LABEL_CHUNK(32)-row
# chunks (32*76 = 2432px tall), the same repeat_dy/scroll_follow pattern as
# Prices' own header chunking above (farming_simulator_view.h's
# FinanceLabelChunks comment).
_FINANCE_LABEL_CHUNK,_FINANCE_LABEL_ROW_H=32,76
_FINANCE_LABEL_CHUNK_H=_FINANCE_LABEL_CHUNK*_FINANCE_LABEL_ROW_H
_FINANCE_LABEL_CHUNKS=-(-64//_FINANCE_LABEL_CHUNK) # ceil(FinanceRowCap/FinanceLabelChunk) = 2
widgets.append({'type':'image','id':'finance-labels','rect':[20,292,492,_FINANCE_LABEL_CHUNK_H],
                'src_bind':'fs.ui.finance.labels_src_{i}','need_bind':'fs.ui.page.5',
                'repeat':_FINANCE_LABEL_CHUNKS,'repeat_dy':_FINANCE_LABEL_CHUNK_H,
                'scroll':'fs-finance','scroll_follow':'y'})
widgets.append({'type':'image','id':'finance-totals','rect':[512,908,1180,44],
                'src_bind':'fs.ui.finance.totals_src','need_bind':'fs.ui.page.5',
                'repeat':1,'scroll':'fs-finance','scroll_follow':'x'})
scrolls.append({'id':'fs-finance','rect':[512,292,708,608],'axes':'xy','lock_axis':True,
                'content_w':1180,'count_bind':'fs.ui.finance.count','row_h':76,
                'show_bind':'fs.ui.page.5','reset_bind':'fs.ui.finance.identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Statistics list: the runtime's own native "statistic-rows" ScrollRegion
# (scrolls list below) instead of a module-tracked offset, same shape as
# the Finance table's row axis -- drag/fling/clip are the runtime's, each
# row is its own module image ("fs.ui.stats.row_src_{i}", see
# 0100D04004176000.cpp's stat_row_src). No wheel step wired (ScrollRegion
# has no wheel path, Thor has no wheel).
stats_rep={'repeat':256,'repeat_bind':'fs.ui.stats.count','repeat_dy':76,'scroll':'statistic-rows'}
widgets.append({'type':'image','rect':[20,170,1200,76],'src_bind':'fs.ui.stats.row_src_{i}',
                'need_bind':'fs.ui.page.7',**stats_rep})
scrolls.append({'id':'statistic-rows','rect':[20,170,1200,764],'count_bind':'fs.ui.stats.count',
                'row_h':76,'show_bind':'fs.ui.page.7','reset_bind':'fs.ui.stats.identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Map drag/pinch are the pan_zoom map-compact/map-expanded widgets' own
# native gesture handling (see map_widgets above) -- no hit-testing rect, no
# module action, needed here at all.
next_layer=action('map_layer_next','map_cycle',1,haptic='click')
previous_layer=action('map_layer_previous','map_cycle',-1,haptic='click')
for width,gate,suffix in [(888,'fs.ui.map_compact','compact'),(1240,'fs.ui.map_expanded','expanded')]:
    left=(width-440)//2
    # Expanded hides the footer tabs (fs.ui.footer_visible below), so the
    # selector/Reset sit flush with the bottom edge there instead of the
    # compact pane's fixed y=864 (which leaves room for the footer below).
    selector_y=1080-24-88 if width==1240 else 864
    widgets.append({'type':'rect','rect':[left,selector_y,440,88],'bg':'#00000000','color':'#00000000',
                    'frame':0,'need_bind':gate,
                    'on_swipe_left':next_layer,'on_swipe_right':previous_layer,'swipe_px':40})
    hit([left,selector_y,88,88],previous_layer,gate)
    hit([left+352,selector_y,88,88],next_layer,gate)
    # Reset: a module-rendered picture (game font/button look, see
    # farming_simulator_view.cpp's View::Button and ImageFor's constant
    # "module:farming_simulator:reset_button" key), not the runtime's own
    # pixel-font button widget. Visible only once that viewport's own
    # pan_zoom view has actually moved away from home (the runtime's
    # "view_custom:<id>", ANDed with the viewport's own need_bind gate via
    # the derived expr below) -- the module has no way to know that any
    # more, now that pan/zoom/follow are entirely native. The tap target is
    # a separate transparent hit rect at the same spot, same style as the
    # hit() helper used everywhere else.
    reset_gate=f'fs.view.map_reset_{suffix}'
    widgets.append({'type':'image','rect':[width-200,selector_y,180,88],
                    'src_bind':'fs.ui.reset_button','need_bind':reset_gate})
    hit([width-200,selector_y,180,88],
        view_reset_action(f'map_reset_{suffix}',f'map-{suffix}',haptic='click'),
        reset_gate,input_block=True)
for i in range(2):
    hit([908+i*164,888,148,64],action(f'map_legend_{i}','map_legend',i),'fs.ui.layer_active',input_block=True)
# Map legend: the runtime's own native "map-legend" ScrollRegion (scrolls
# list below) instead of a module-tracked offset, same shape as the
# Statistics list -- drag/fling/clip are the runtime's, each row is its own
# module image ("fs.ui.map_legend.row_src_{i}", see 0100D04004176000.cpp's
# legend_row_src). reset_bind'd to fs.ui.map_legend_identity (a hash of
# map_layer/world_identity) so switching the active layer snaps it back to
# the top. No wheel step wired (ScrollRegion has no wheel path, Thor has no
# wheel).
map_legend_rep={'repeat':64,'repeat_bind':'fs.ui.map_legend.count','repeat_dy':100,'scroll':'map-legend'}
widgets.append({'type':'image','rect':[908,168,312,100],'src_bind':'fs.ui.map_legend.row_src_{i}',
                'need_bind':'fs.ui.map_legend',**map_legend_rep})
scrolls.append({'id':'map-legend','rect':[908,168,312,692],'count_bind':'fs.ui.map_legend.count',
                'row_h':100,'show_bind':'fs.ui.map_legend','reset_bind':'fs.ui.map_legend_identity',
                'bar':'#FF7FC031','bar_track':'#FF000000','bar_w':8})
# Bound shifted hit regions to the same visible content area as their pixels.
for rect,gate in [([0,0,1240,88],'fs.ui.page.8'),([0,952,1240,16],'fs.ui.page.8'),
                  ([0,0,1240,96],'fs.ui.shop_browse'),([0,864,1240,104],'fs.ui.shop_browse'),
                  ([0,0,384,192],'fs.ui.shop_browse'),([0,832,384,136],'fs.ui.shop_browse'),
                  ([0,0,460,104],'fs.ui.page.4'),([0,932,460,36],'fs.ui.page.4')]:
    widgets.append({'type':'rect','rect':rect,'bg':'#00000000','color':'#00000000',
                    'frame':0,'need_bind':gate,'input_block':True})
# Fixed Shop selectors stay above the list-clip blockers.
for mode,x in [(0,20),(1,196)]:hit([x,96,168,88],action(f'shop_mode_{mode}','shop_mode',mode),'fs.ui.shop_browse')
hit([20,880,344,80],action('shop_clear','shop_clear',0),'fs.ui.shop_browse')
hit([768,8,180,64],action('map_fullscreen','map_fullscreen',0,haptic='click'),'fs.ui.map_active',input_block=True)
hit([0,80,1240,888],action('time_dismiss','time_picker',0),'fs.ui.time_picker',input_block=True)
# Top-right badge: same coordinates on every page that shows it. Gated on
# fs.ui.time_badge (shown whenever the badge itself renders: world ready,
# not Animals, a time scale to show) rather than fs.ui.time_available (that
# plus change actually being allowed), so the badge stays tappable while
# merely muted (e.g. a native menu open) instead of disappearing outright;
# enabled_bind on the action itself is what gives a disabled tap the
# "refused" (reject) haptic instead of either doing nothing or silently
# succeeding.
hit([1110,8,110,64],action('time_picker','time_picker',0,haptic='click',enabled_bind='fs.ui.time_available'),'fs.ui.time_badge',input_block=True)
# Map page: picker replaces the right pane's own content (908..1220 column).
# Every other page: picker floats as a top-right card (716..1220 column).
for i,factor in enumerate((1,5,15,30,60,120)):
    act=action(f'time_scale_{factor}','time_scale',factor,haptic='confirm')
    hit([908+(i%2)*164,184+(i//2)*100,148,88],act,'fs.ui.time_picker_map',input_block=True)
    hit([736+(i%2)*236,168+(i//2)*100,220,88],act,'fs.ui.time_picker_panel',input_block=True)

# FILTERS button + popup (UX-SPEC 2026-10-17 review §A, owner request:
# "beside expand, add filters that pop up ... that can toggle map markers
# like vehicle, field, etc."). Map-tab only, stays tappable even while the
# time-scale picker is open (OnViewAction's own "last tap wins" handling
# closes the picker and opens Filters instead). The popup reuses the exact
# right-pane-replace mechanism as TIME SCALE above in compact mode, and the
# same floating top-right card convention as TIME SCALE's own non-Map-page
# layout in expanded mode (farming_simulator_view.cpp's View::MapFiltersCard
# -- expanded has no right pane to replace, UX-SPEC §A.4). Adjacent to the
# existing Expand button at 768,8,180,64 rather than the spec's literal
# 648px rect (which assumed a slightly different Expand geometry than this
# build's own) -- see View::Operations' own comment for the same note.
hit([628,8,124,64],action('map_filters_open','map_filters_open',0,haptic='click'),
    'fs.ui.map_filters_active',input_block=True)
# Tap elsewhere dismisses, same pattern as time_dismiss above -- registered
# before the row/toggle hits below so those smaller rects win on overlap.
hit([0,80,1240,888],action('map_filters_dismiss','map_filters_open',0),
    'fs.ui.map_filters_pane',input_block=True)
hit([0,80,1240,1000],action('map_filters_dismiss_card','map_filters_open',0),
    'fs.ui.map_filters_card',input_block=True)
for i in range(7):
    act=action(f'map_filter_toggle_{i}','map_filter_toggle',i,haptic='light')
    # Compact pane: 908,172+i*92,312,92 (View::MapFiltersRows' own geometry).
    hit([908,172+i*92,312,92],act,'fs.ui.map_filters_pane',input_block=True)
    # Expanded card: 752,172+i*88,452,88 (View::MapFiltersCard's own geometry:
    # CardX+20, CardY+76, CardW-40, 88 px rows).
    hit([752,172+i*88,452,88],act,'fs.ui.map_filters_card',input_block=True)

# Fixed direct destination rail, matching the rendered native-art cells.
# Hidden (fs.ui.footer_visible) while the map is expanded, so the map gets
# the full y=80..1080 height instead of a dead grey band under it.
for i,p in enumerate([0,1,4,3,6,5,7,8]):
    hit([155*i,968,155,112],action(f'footer_{i}','page',p,haptic='light'),'fs.ui.footer_visible')
hit([12,968,328,112],action('static_help_back','page',0,haptic='light'),'fs.ui.static_help')
hit([60,730,400,120],action('title_help','page',8),'fs.ui.help_landing')

manifest={'format':1,'title_id':'0100D04004176000','name':'Farming Simulator Nintendo Switch Edition companion',
          # Runtime 20 (Eden Duo 1.3.0): the native time-scale key swap/release
          # write_batch runs up to 25/17 ops respectively (guards + the
          # key-storage swap); EDEN_DSMOD_WRITE_BATCH_MAX_OPS is only 32 (and
          # the module's own write_batch-size guard only matches the host it
          # actually gets) from runtime 20 on -- 16 on older hosts, where
          # those batches would simply never apply.
          # Runtime 22 (Eden Duo 1.3.0-beta.15): two-axis ScrollRegion keys
          # ("axes"/"lock_axis"/"content_w_bind"/scroll_follow) used by the
          # Prices matrix's "fs-prices" region above.
          'min_runtime':22,'canvas_w':1240,'canvas_h':1080,'background':'#FF2F2F2F','nav':False,
          'requires_module':True,'module':{'abi':1,'build_ids':[BUILD]},
          # Guest load plan (tools/farming-simulator/gen_load_plan.py): the main-loop hook that runs
          # the instant game calls' Lua on the game thread. Format 1 (runtime 18+).
          'load_plan':'guest/<build>/load-plan.json',
          'haptics':{'enabled':True,'respect_system':True,'tap':'light','select':'light','drag':'light',
                     'nav':'light','swipe':'light','refused':'reject','hold':'heavy'},
          '_about':'Generated by tools/farming-simulator/gen_manifest.py. Accepted A. Native SDF font, maps and artwork decoded by the module from the player merged RomFS; no game assets included.',
          '_scope':'Operations, Prices matrix, five-day Finances, Session/Total Statistics, Animals, local Shop/Garage browsing and static English Guide; eight direct native-art destination tabs with gesture/wheel regions. No economy transactions or synthetic live values. Live Crops/Growth/Soil layers and guarded native time-scale input are available for the supported build. Map drag/pinch is the runtime\'s own native pan_zoom gesture handling (map-compact/map-expanded widgets), not a module action. Reset (a native view_reset action) appears once that viewport\'s view has moved away from 2x following and glides it back. Expand/Restore changes the map pane and hides the footer tabs while expanded. Lists and tables follow continuous drags. Centered map-layer selector supports tap arrows and horizontal swipes. FILTERS toggles which of the 7 native map-marker categories (Vehicles/Harvesters/Trailers/Tools/Selling points/Field jobs/Other) draw on the map, default all on; the popup replaces the right pane in compact and floats as a top-right card in expanded.',
          # fs.view.map_reset_{compact,expanded}: the Reset button's own
          # need_bind -- that viewport is both on-screen (fs.ui.map_compact/
          # expanded) AND its pan_zoom view has moved away from home
          # (view_custom:map-compact/expanded, the runtime's own read of
          # whether that widget's view differs from its bound default).
          'derived':[
              # G3 critique Finding 6 (two "Reset" buttons at once): the map-view Reset (glides
              # pan/zoom back to home) and the selection card's own Reset/vehicle-Reset button
              # could both show at once, reading as duplicate/ambiguous controls. Decision: hide
              # the map-view Reset while a selection card is open rather than rename either one
              # -- the card's own Reset/Visit/Tag row already needs that exact wording to match
              # the native InGameMenu buttons (UX-SPEC B/C), so renaming it would break native
              # fidelity, while the map-view Reset is a companion-only addition (no native
              # equivalent) free to be scoped instead. Re-panning while a card is open still
              # shows Reset again once the card closes, same as any other panned-away state.
              {'name':'fs.view.map_reset_compact',
               'expr':"fs.ui.map_compact && 'view_custom:map-compact' && !fs.ui.map_selection.active"},
              {'name':'fs.view.map_reset_expanded',
               'expr':"fs.ui.map_expanded && 'view_custom:map-expanded' && !fs.ui.map_selection.active"},
              # map-arrow-{compact,expanded}'s own need_bind: that viewport is
              # on-screen AND the module has a heading to show (fs.ui.map_arrow_visible).
              # fs.ui.map_arrow_pan_tracked_{compact,expanded} (ArrowPanOffset,
              # farming_simulator_view.h) is true once the runtime answers that
              # viewport's live pan_zoom view back to the module -- then the arrow's
              # x_bind/y_bind already point it at the player's true screen position,
              # so it stays shown unconditionally, panned/zoomed away from home or not.
              # On an older runtime (no answer, pan_tracked stays 0) this falls back to
              # the prior rule: hidden while the user has panned/zoomed away, since the
              # overlay then sits at the (untracked) viewport centre, which is the
              # player only while following. Reset restores it either way.
              {'name':'fs.view.map_arrow_compact',
               'expr':"fs.ui.map_view_compact && fs.ui.map_arrow_visible && "
                      "(fs.ui.map_arrow_pan_tracked_compact || !'view_custom:map-compact')"},
              {'name':'fs.view.map_arrow_expanded',
               'expr':"fs.ui.map_view_expanded && fs.ui.map_arrow_visible && "
                      "(fs.ui.map_arrow_pan_tracked_expanded || !'view_custom:map-expanded')"},
              # Garage action row (UX-SPEC B.3): gen_manifest.py's hit()/image widgets above
              # compare fs.ui.garage.action_kind (0 none / 1 both / 2 Enter only / 3 Reset only,
              # Sample()) against each case directly, so only one of the three fires per vehicle.
              {'name':'fs.ui.garage.action_visible','expr':'fs.ui.page.4 && fs.ui.garage.action_kind!=0'},
              {'name':'fs.ui.garage.action_both','expr':'fs.ui.page.4 && fs.ui.garage.action_kind==1'},
              {'name':'fs.ui.garage.action_enter_only','expr':'fs.ui.page.4 && fs.ui.garage.action_kind==2'},
              {'name':'fs.ui.garage.action_reset_only','expr':'fs.ui.page.4 && fs.ui.garage.action_kind==3'},
              # Map selection card action row (UX-SPEC C.2/C.3, +C review's expanded-mode card):
              # same kind convention as Garage above, split by fs.ui.map_selection.place (0
              # vehicle / 1 place) so the right hit() widgets (Enter/Reset vs Visit/Tag place)
              # fire for the right marker type -- and by fs.ui.map_fullscreen so exactly one of
              # the pane's (compact) or the card's (expanded, View::Operations' own floating
              # MapSelectionCard-pattern branch) action-row widgets is ever live: map_selection
              # .active itself is set independently of which mode the map is in (a marker stays
              # selected across Expand/Restore), so without this split both widget sets would
              # answer taps at once.
          ]+[
              {'name':'fs.ui.map_sel.pane_active',
               'expr':'fs.ui.map_selection.active && !fs.ui.map_fullscreen'},
              {'name':'fs.ui.map_sel.card_active',
               'expr':'fs.ui.map_selection.active && fs.ui.map_fullscreen'},
          ]+[
              {'name':f'fs.ui.map_sel.{suffix}_{kind}',
               'expr':f"fs.ui.map_selection.active && fs.ui.map_selection.place=={place} && "
                      f"fs.ui.map_selection.kind=={n} && {mode_expr}"}
              for suffix,mode_expr in (('pane','!fs.ui.map_fullscreen'),('card','fs.ui.map_fullscreen'))
              for kind,place,n in (('both_vehicle',0,1),('enter_only',0,2),('reset_only',0,3),
                                   ('both_place',1,1),('visit_only',1,2),('tag_only',1,3))
          ]+[
              # Per-marker tap-select hit regions (UX-SPEC C, MARKER_CAP above): the module
              # publishes fs.ui.map_marker_selectable_{i} (a selectable, discovered marker)
              # without knowing whether either map viewport has panned away from its default
              # view (it cannot -- see the widgets' own comment above); ANDing in the runtime's
              # own view_custom read here is what actually gates the hit regions on "still at
              # home view OR pan-tracked" per viewport -- the exact same
              # "pan_tracked || !view_custom" rule fs.view.map_arrow_{suffix} above already
              # uses, since the per-marker offset is pan-tracked by the identical mechanism
              # (map_arrow_pan_tracked_{compact,expanded} is the module's one query-answered
              # flag per viewport, shared by the arrow and every marker in it).
              {'name':f'fs.ui.map_marker_show_compact_{i}',
               'expr':f"fs.ui.map_marker_selectable_{i} && fs.ui.map_compact && "
                      "(fs.ui.map_arrow_pan_tracked_compact || !'view_custom:map-compact')"}
              for i in range(MARKER_CAP)
          ]+[
              {'name':f'fs.ui.map_marker_show_expanded_{i}',
               'expr':f"fs.ui.map_marker_selectable_{i} && fs.ui.map_expanded && "
                      "(fs.ui.map_arrow_pan_tracked_expanded || !'view_custom:map-expanded')"}
              for i in range(MARKER_CAP)
          ],
          'actions':actions,'pages':[{'id':'companion','name':'Companion','widgets':widgets,'scrolls':scrolls}]}
OUT.write_text(json.dumps(manifest,indent=2)+'\n')
print(OUT)
