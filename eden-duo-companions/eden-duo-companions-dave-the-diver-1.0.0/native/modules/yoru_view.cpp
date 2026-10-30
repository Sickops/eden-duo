// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "yoru_view.h"
#include "yoru_assets.h"
#include "yoru_history.h"
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yoru {
namespace {
constexpr uint32_t Ivory = 0xeee9dfff, Muted = 0xb3ad9dff, Gold = 0xffca50ff;
constexpr std::array<std::string_view,53> Gallery{
#include "yoru_gallery.inc"
};
Image Blank(int w,int h) {
    Image im;im.w=w;im.h=h;im.rgba.resize(size_t(w)*h*4);return im;
}
void Composite(Image& dst,const Image& src,int x,int y) {
    for(int sy=std::max(0,-y);sy<int(src.h) && y+sy<int(dst.h);++sy)
        for(int sx=std::max(0,-x);sx<int(src.w) && x+sx<int(dst.w);++sx) {
            const auto s=(size_t(sy)*src.w+sx)*4,d=(size_t(y+sy)*dst.w+x+sx)*4;
            unsigned a=src.rgba[s+3];
            for(int c=0;c<3;++c)dst.rgba[d+c]=BlendChannel(src.rgba[s+c],dst.rgba[d+c],a);
            dst.rgba[d+3]=BlendAlpha(a,dst.rgba[d+3]);
        }
}
void Shade(Image& im,unsigned alpha) {
    for(size_t i=0;i<im.rgba.size();i+=4)
        for(int c=0;c<3;++c)im.rgba[i+c]=uint8_t(unsigned(im.rgba[i+c])*(255-alpha)/255);
}
void OpaqueBlack(Image& im) {
    for(size_t i=3;i<im.rgba.size();i+=4)im.rgba[i]=255;
}
std::string_view VisibleText(std::string_view value) {
    // Script records include layout-ending LFs; retain source in Snapshot, omit only trailing blank lines.
    while(!value.empty() && (value.back()=='\n' || value.back()=='\r'))value.remove_suffix(1);
    return value;
}
// Step 2a/owner plate fix, 2026-10-15: the vertical gap BEFORE an entry (from the previous
// entry's box bottom to this one's box top). A narrated (speaker-less) entry keeps the original
// uniform 35px rhythm. A named entry needs more: its plate overlaps its OWN box's top border (see
// DrawSpeaker()'s call site below), so the gap above the PLATE itself -- not the box -- must still
// read as clearly larger separation from the entry above (>=48px, owner's own number) than the
// plate's own overlap into its box. `first` is the newest (topmost) entry, which still wants
// overlap room even though there is no entry above it to separate from.
int LeadFor(bool first, int plate_h) {
    if (plate_h)
        return first ? std::max(16, plate_h / 2 + 12) : 48 + plate_h / 2;
    return first ? 16 : 35;
}
} // namespace

// Owner feedback 2026-10-16: PlateHeight/DrawSpeaker/BuildLogLayout are free functions (not
// View::Impl methods) so yoru_module.cpp's Module::Sample (tick thread) can call the exact same
// geometry code as the asset-worker renderer below, each against its OWN Assets instance (Module
// owns a measurement-only one; View::Impl owns the rendering one) -- never sharing one Assets
// across threads, matching the existing "View is asset-worker-only" contract.

// Measurement helper: the plate's drawn height for `name`, independent of where it will be
// placed. Used only to size the lead gap before layout; DrawSpeaker() below repeats the same ink
// measurement (it already has to, to centre the glyphs) rather than threading this value through.
int PlateHeight(Assets& assets, std::string_view name) {
    if(name.empty())return 0;
    Image glyphs=Blank(1100,96);
    assets.Text(glyphs,name,0,0,34,1100,Ivory);
    int first=96,last=-1;
    for(int y=0;y<96;++y)for(int x=0;x<1100;++x)
        if(glyphs.rgba[(size_t(y)*1100+x)*4+3]){first=std::min(first,y);last=std::max(last,y);}
    if(last<first)return 0;
    return (last-first+1+16)*160/91;
}
void DrawSpeaker(Image& body, Assets& assets, std::string_view name, int top) {
    Image glyphs=Blank(1100,96);
    assets.Text(glyphs,name,0,0,34,1100,Ivory);
    int left=1100,right=-1,first=96,last=-1;
    for(int y=0;y<96;++y)for(int x=0;x<1100;++x)
        if(glyphs.rgba[(size_t(y)*1100+x)*4+3]) {
            left=std::min(left,x);right=std::max(right,x);
            first=std::min(first,y);last=std::max(last,y);
        }
    if(right<left || last<first)return;
    const int ink_w=right-left+1,ink_h=last-first+1;
    // Native name material's visible alpha bounds are (60,34)-(587,125) in its 640x160 cell.
    // Size the visible ribbon around actual ink, not advance cells.
    const int plate_w=(ink_w+48)*640/527,plate_h=(ink_h+16)*160/91;
    const int visible_top=top+(80-(ink_h+16))/2;
    assets.Cell(body,"ui_avg","name",90-60*plate_w/640,
                visible_top-34*plate_h/160,plate_w,plate_h);
    Composite(body,glyphs,114-left,top+(80-ink_h)/2-first);
}
// Draws one past-history entry's box/plate/voice-icon/text at `local_top` (already offset for
// whichever strip is being rendered -- RenderLogStrip subtracts the strip's own pixel origin
// before calling this, so an entry straddling two strips draws identically, clipped, in each).
void DrawLogEntry(Image& canvas, Assets& assets, const Row& row, bool is_newest, int px,
                  int local_top, int height) {
    assets.Cell(canvas,"ui_avg","text_win",-5,local_top,1230,height);
    // Owner fix 2026-10-15 (kept): the box is drawn first, the name plate second (on top), its
    // ribbon overlapping THIS box's top border.
    if(!row.speaker.empty())DrawSpeaker(canvas,assets,row.speaker,local_top-40);
    if(is_newest && row.voice>=0)assets.Cell(canvas,"ui_avg","voice",35,local_top+37,58,39);
    assets.Text(canvas,CollapseSoftBreaks(VisibleText(row.text)),114,local_top+29,px,1025,
                is_newest?Ivory:Muted);
}
// Lays out every retained past row (newest at the top, offset0, down to the oldest still
// retained) into one virtual content column: the full Log history, not just a 4-row window.
// Shared by the asset-worker's strip renderer and the tick-thread's strip_count/layout_rev
// publisher, so both always agree on exactly the same geometry (see yoru_view.h's comment).
LogLayoutResult BuildLogLayout(Assets& assets, const Snapshot& snapshot, int font_px) {
    LogLayoutResult out;
    if(!snapshot.ready)return out;
    const auto past=PastCount(snapshot);
    if(!past)return out;
    const auto& rows=snapshot.rows;
    const int px=std::clamp(font_px,28,52);
    int y=0;
    for(size_t offset=0;offset<past;++offset) {
        const auto found=PastIndex(snapshot,int(offset));
        if(!found)break;
        const int index=int(*found);
        // The returned wrap height depends only on px/maxw, not on the canvas it draws into --
        // measure into a 1x1 probe instead of a full-size image per row (Step 1 perf fix, kept).
        Image measure;measure.w=measure.h=1;measure.rgba.assign(4,0);
        const int th=assets.Text(measure,CollapseSoftBreaks(VisibleText(rows[index].text)),0,0,
                                 px,1025,Ivory);
        const int plate_h=rows[index].speaker.empty()?0:PlateHeight(assets,rows[index].speaker);
        const int h=std::max(210,th+76);
        y+=LeadFor(out.entries.empty(),plate_h);
        out.entries.push_back({int32_t(index),int32_t(y),int32_t(h)});
        y+=h;
    }
    out.total_height=LogTotalHeight(out.entries,kLogBottomPad);
    return out;
}

struct View::Impl {
    explicit Impl(const EdenDsmodHostApi& host):assets(host) {}
    Assets assets;
    bool Backdrop(Image& im,bool classroom=false) {
        if(!assets.Art(im,classroom?"ui_extra_bg":"ui_title_bg",-340,0,1920,1080))return false;
        Shade(im,classroom?155:105);return true;
    }
    void Label(Image& im,std::string_view value,int x,int y,int px=36,int maxw=1100,
               uint32_t colour=Ivory,bool center=false) {
        assets.Text(im,value,x,y,px,maxw,colour,true,center);
    }
    void Heading(Image& im,std::string_view label) {
        Image raw=Blank(448,112);assets.Cell(raw,"ui_avg","label_log",0,0,448,112);
        // Only the text-free ruled left material is reused; never paint Japanese beneath English.
        Image lines=Blank(336,84);
        for(int y=0;y<84;++y)for(int x=0;x<336;++x) {
            auto s=(size_t(y*112/84)*448+x*100/336)*4,d=(size_t(y)*336+x)*4;
            std::copy_n(raw.rgba.data()+s,4,lines.rgba.data()+d);
        }
        // Native rule alpha begins at source row44: y31 places it below the glyph ink.
        Composite(im,lines,8,31);
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)
            assets.Text(im,label,84+dx,dy,34,400,0x000000ff,true);
        Label(im,label,84,0,34,400);
    }
    void Footer(Image& im,int selected) {
        assets.Cell(im,"ui_common","help_win",0,960,1240,120);
        constexpr std::array<std::string_view,3> names{"Log","Option","CG"};
        constexpr std::array<int,3> x{158,545,1011};
        for(int i=0;i<3;++i) {
            if(i==selected)assets.Cell(im,"ui_avg","topmenue_focus",i*413+25,982,363,74,140);
            Label(im,names[i],x[i],995,40,350,i==selected?Gold:Ivory);
        }
    }
    // One option row: a label on the left, up to 3 selectable plates on the right (`choices`),
    // the same topmenue_focus plate style as the old reading controls row, now living on the
    // Option page (owner item 4: "Make options like scroll speed etc on the Manual page's top
    // side"). The module draws every choice identically (plain plate, Ivory text): it has no
    // reliable way to know which one is currently selected -- a manifest "flag" (mod_actions.cpp's
    // ActionKind::Flag) is only published into the snapshot by PublishFlags AFTER RunGameModule
    // for the tick that changed it (mod_runtime.cpp:667-669: the module samples BEFORE flags are
    // published, every tick, forever), so Module::Sample's get_i64("@flag:...") read-back this
    // build briefly tried always saw the fallback default, not the real value (confirmed live:
    // the Option page kept showing "Normal"/"On" after tapping "Fast"). The selected choice is
    // instead highlighted by a NATIVE widget on top (gen_manifest.py's opt.sel.* derived points,
    // evaluated well after PublishFlags -- see its own comment), which is actually reactive.
    void OptionRow(Image& im,std::string_view label,int y,
                   const std::vector<std::pair<std::string_view,int>>& choices) {
        Label(im,label,84,y+10,30,330,Ivory);
        int x=430;
        for(const auto& [text,w]:choices) {
            assets.Cell(im,"ui_avg","topmenue_focus",x-12,y-4,w+12,57,64);
            Label(im,text,x+w/2,y+6,26,w,Ivory,/*center*/true);
            x+=w+18;
        }
    }
    void Manual(Image& im,const ViewState& state,int top,int h) {
        const int page=std::clamp(state.manual_page,0,1);
        const int art_w=int(h*1184.0/672.0);
        const int art_x=(1240-art_w)/2;
        assets.Cell(im,"ui_manual",page?"02_procon":"01_joycon",art_x,top,art_w,h);
        const int tab_y=top+h+18;
        for(int i=0;i<2;++i) {
            int x=190+i*520;
            if(i==page)assets.Cell(im,"ui_avg","topmenue_focus",x-24,tab_y,440,72,115);
            Label(im,i?"Pro Controller":"Joy-Con",x,tab_y+15,32,470,i==page?Gold:Ivory);
        }
    }
    void RuleOrnament(Image& im,int cx,int y,int w,int h=20) {
        Image raw=Blank(448,112);
        if(!assets.Cell(raw,"ui_avg","label_log",0,0,448,112))return;
        Image strip=Blank(w,h);
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx) {
            const auto sy=size_t(std::clamp(36+yy*28/std::max(1,h),0,111));
            const auto sx=size_t(std::min(99,xx*100/w));
            std::copy_n(raw.rgba.data()+(sy*448+sx)*4,4,strip.rgba.data()+(size_t(yy)*w+xx)*4);
        }
        Composite(im,strip,cx-w/2,y);
    }
    void CentredMessage(Image& im,std::string_view title,int title_px,int title_y,
                        std::string_view subline,int sub_px,int gap,bool rule,
                        uint32_t title_colour=Ivory) {
        constexpr int cx=620,maxw=900;
        int y=title_y+assets.Text(im,title,cx,title_y,title_px,maxw,title_colour,true,true);
        if(rule) {
            y+=26;
            RuleOrnament(im,cx,y,260,20);
            y+=20+26;
        } else if(!subline.empty()) {
            y+=gap;
        }
        if(!subline.empty())
            assets.Text(im,subline,cx,y,sub_px,maxw,Muted,true,true);
    }
    // Owner item 4 ("In Log top right, add text size with - and + buttons"): per the owner's
    // 2026-10-16 follow-up ("Text size removed from Option, Log header only"), this Log-header
    // row is now the ONLY text-size control; it calls the module's existing
    // log_smaller/log_larger/log_font_reset actions (see yoru_module.cpp's Action() comment on
    // why those accept page 0 as well as page 1), laid out compactly in the top-right header band
    // (y 20-85 per the owner's own numbers) -- left of/above "Latest", which stays at the
    // scrollbar's own top (y98+).
    // Ink bounds of `value` rendered at px: text is placed by its visible glyph ink, not by font
    // cells, so labels, plate glyphs and numbers centre exactly (like Speaker()).
    struct InkBox {int l=0,t=0,r=-1,b=-1;bool Empty() const {return r<l || b<t;}};
    InkBox InkOf(const Image& g) {
        InkBox box{int(g.w),int(g.h),-1,-1};
        for(uint32_t y=0;y<g.h;++y)for(uint32_t x=0;x<g.w;++x)
            if(g.rgba[(size_t(y)*g.w+x)*4+3]) {
                box.l=std::min(box.l,int(x));box.r=std::max(box.r,int(x));
                box.t=std::min(box.t,int(y));box.b=std::max(box.b,int(y));
            }
        return box;
    }
    // Draws `value` with its ink vertically centred on cy; horizontally centred on x, or with the
    // ink's left edge at x when `left` is set.
    void InkText(Image& im,std::string_view value,int x,int cy,int px,uint32_t colour,bool left=false) {
        Image g=Blank(px*14,px*3);
        assets.Text(g,value,0,px/2,px,px*14,colour,true);
        const auto ink=InkOf(g);if(ink.Empty())return;
        const int dx=left?x-ink.l:x-(ink.l+ink.r+1)/2;
        Composite(im,g,dx,cy-(ink.t+ink.b+1)/2);
    }
    // Vertical centre line of the "Log" heading's glyph ink (Heading draws it at y 0, 34 px).
    int HeadingInkCentre() {
        Image g=Blank(400,120);assets.Text(g,"Log",0,0,34,400,Ivory,true);
        const auto ink=InkOf(g);return ink.Empty()?28:(ink.t+ink.b+1)/2;
    }
    void LogTextSizeRow(Image& im,int size) {
        // Owner 2026-10-16: the whole row sits on the Log title's line; "-"/"+" centred on their
        // plates and the number centred between them.
        constexpr int plate_w=76,plate_h=64,minus_x=958,plus_x=1138;
        const int cy=HeadingInkCentre();
        InkText(im,"Text size",770,cy,26,Ivory,true);
        for(const int px:{minus_x,plus_x})
            assets.Cell(im,"ui_avg","topmenue_focus",px,cy-plate_h/2,plate_w,plate_h,96);
        InkText(im,"-",minus_x+plate_w/2,cy,28,Ivory);
        InkText(im,"+",plus_x+plate_w/2,cy,28,Ivory);
        InkText(im,std::to_string(size),(minus_x+plate_w+plus_x)/2,cy,26,Muted);
    }
    // Log page chrome: just the heading plus the start/loading/empty-history messages. The real
    // history is drawn by RenderLogStrip's native-scrolled strips, composited by the manifest on
    // top of this same backdrop -- nothing else is drawn here while there IS history to show.
    void LogChrome(Image& im,const ViewState& state) {
        Heading(im,"Log");
        LogTextSizeRow(im,std::clamp(state.log_font_size,28,52));
        if(!state.snapshot.ready) {
            if(state.screen==Screen::Loading) {
                CentredMessage(im,"Loading...",44,490,{},0,0,false,Muted);
            } else {
                CentredMessage(im,"Yoru, Tomosu",62,400,
                    "Start or continue a story \xE2\x80\x94 past dialogue will appear here.",
                    30,0,true);
            }
            return;
        }
        if(!PastCount(state.snapshot)) {
            CentredMessage(im,"No past dialogue yet",40,460,
                "The Log starts fresh when a save is loaded.",24,20,false,Muted);
            return;
        }
        // "Latest" above the scroll viewport (y 144..960). "Oldest" floats over the bottom of the
        // history as its own overlay image (BarArt::OldestLabel), drawn after the strips.
        assets.Cell(im,"ui_avg","topmenue_focus",1004,98,226,42,96);
        assets.Text(im,"Latest",1117,100,30,220,Gold,true,true);
    }
    void GridChrome(Image& im,const ViewState& state) {
        Heading(im,"CG");
        if(!state.snapshot.cg_ready) {
            CentredMessage(im,"Gallery opens once a story is loaded",32,490,{},0,0,false,Muted);
        }
    }
    // Owner item 4: text size (moved from the Log), scroll speed and follow-newest (new), then
    // the controller manual below, scaled to fit. Persistence: scroll speed and follow-newest
    // are manifest "flags" (persist_flags survives a session, mod_persist.h -- see
    // gen_manifest.py); text size stays module (ViewState) state and so is session-only, same as
    // every build before this one (it must stay in the module: the strips it affects are
    // module-rendered pixels, and the module has no durable storage of its own). Which choice is
    // selected is shown by a native widget, not drawn here -- see OptionRow's own comment.
    void OptionPage(Image& im,const ViewState& state) {
        Heading(im,"Option");
        // Text size lives in the Log header only (owner 2026-10-16).
        OptionRow(im,"Scroll speed",150,{{"Slow",120},{"Normal",150},{"Fast",120}});
        OptionRow(im,"Follow newest",250,{{"On",120},{"Off",120}});
        Manual(im,state,378,460);
    }
};
View::View(const EdenDsmodHostApi& host):impl_(std::make_unique<Impl>(host)) {}
View::~View()=default;
bool View::Render(const ViewState& state,Image& im) {
    if(!state.snapshot.compatible || !impl_->assets.Ready())return false;
    im=Blank(1240,1080);OpaqueBlack(im);
    if(state.page==2 && state.full_cg) {
        int selected=state.cg_selected;
        if(!state.snapshot.cg_ready || selected<0 || selected>=int(Gallery.size()) || !state.snapshot.cg_flags[selected])return false;
        // Verified native gallery illustrations are 1920x1080; preserve original brightness/aspect.
        return impl_->assets.Art(im,Gallery[selected],0,191,1240,698);
    }
    if(!impl_->Backdrop(im,state.page==2))return false;
    if(state.page==0)impl_->LogChrome(im,state);
    else if(state.page==1)impl_->OptionPage(im,state);
    else if(state.page==2)impl_->GridChrome(im,state);
    else return false;
    impl_->Footer(im,state.page);return true;
}
bool View::RenderLogStrip(const ViewState& state,int strip_index,Image& im) {
    im=Blank(kLogStripW,kLogStripH); // transparent: the page's own backdrop shows through.
    if(!state.snapshot.compatible || !impl_->assets.Ready() || strip_index<0)return false;
    const auto layout=BuildLogLayout(impl_->assets,state.snapshot,state.log_font_size);
    if(layout.entries.empty())return false;
    const int strip_top=strip_index*kLogStripH;
    for(const size_t i:EntriesInStrip(layout.entries,strip_index,kLogStripH)) {
        const auto& e=layout.entries[i];
        DrawLogEntry(im,impl_->assets,state.snapshot.rows[size_t(e.index)],i==0,
                    std::clamp(state.log_font_size,28,52),e.top-strip_top,e.height);
    }
    return true;
}
bool View::RenderCgTile(int index,bool unlocked,Image& im) {
    im=Blank(kCgTileW,kCgTileH);
    if(!impl_->assets.Ready() || index<0 || index>=int(Gallery.size()))return false;
    if(unlocked) {
        impl_->assets.Cell(im,"ui_extra","cg_nonactive",0,0,kCgTileW,kCgTileH);
        impl_->assets.Art(im,Gallery[size_t(index)],52,48,416,234);
    } else {
        impl_->assets.Cell(im,"ui_extra","cg_close",0,0,kCgTileW,kCgTileH);
    }
    return true;
}
bool View::RenderBarArt(BarArt which,Image& im) {
    // Fixed small source crops, stretched by the host to the actual track/thumb rect each frame
    // (mod_ui.cpp's draw_scroll_bars) -- the track is pre-rendered close to its real on-screen
    // height so that stretch is a near no-op; the thumb pill is pre-rendered taller than its
    // native 40x40 cell so a tall thumb (a short list) stretches less visibly.
    switch(which) {
    case BarArt::LogTrack: im=Blank(34,860);
        if(!impl_->assets.Ready())return false;
        return impl_->assets.Cell(im,"ui_avg","scrollbar",2,0,30,860);
    case BarArt::LogThumb: im=Blank(34,80);
        if(!impl_->assets.Ready())return false;
        return impl_->assets.Cell(im,"ui_avg","scroll",-3,20,40,40);
    case BarArt::CgTrack: im=Blank(34,835);
        if(!impl_->assets.Ready())return false;
        return impl_->assets.Cell(im,"ui_extra","scrollbar",2,0,30,835);
    case BarArt::OldestLabel: {
        // Dark backing so the label stays legible over scrolled text, then the native plate.
        im=Blank(226,46);
        if(!impl_->assets.Ready())return false;
        // Feathered: fades in over the left 70 px and softens 8 px at the top and bottom.
        for(uint32_t y=0;y<im.h;++y)for(uint32_t x=0;x<im.w;++x) {
            const float fx=std::min(1.0f,float(x)/70.0f);
            const float fy=std::min({1.0f,float(y)/8.0f,float(im.h-1-y)/8.0f});
            im.rgba[(size_t(y)*im.w+x)*4+3]=uint8_t(200.0f*fx*fy);
        }
        impl_->assets.Cell(im,"ui_avg","topmenue_focus",0,2,226,42,110);
        impl_->InkText(im,"Oldest",113,23,30,Gold);
        return true;
    }
    case BarArt::CgThumb: im=Blank(34,80);
        if(!impl_->assets.Ready())return false;
        return impl_->assets.Cell(im,"ui_extra","scroll",-3,20,40,40);
    }
    return false;
}
} // namespace yoru
