// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "yoru_assets.h"
#include "yoru_history.h"
#include "acnh_tex.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace yoru {
namespace {
using dsmod_sdk::Le16;
using dsmod_sdk::Le32;
using dsmod_sdk::Le64;
using Picture = acnh::Image;
bool Fits(size_t size,uint64_t at,uint64_t n) {return at<=size && n<=size-at;}
std::u32string Unicode(std::string_view s) {
    std::u32string out;
    for(size_t i=0;i<s.size();) {
        uint8_t a=s[i++]; uint32_t cp{};int n{};
        if(a<128)cp=a;
        else if(a>=0xc2 && a<=0xdf){cp=a&31;n=1;}
        else if(a>=0xe0 && a<=0xef){cp=a&15;n=2;}
        else if(a>=0xf0 && a<=0xf4){cp=a&7;n=3;}
        else return {};
        if(i+size_t(n)>s.size())return {};
        while(n--) {uint8_t v=s[i++];if((v&0xc0)!=0x80)return {};cp=(cp<<6)|(v&63);}
        if(cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return {};
        out.push_back(cp);
    }
    return out;
}
// Unverified alphabet slots use internal private-use tokens. This preserves
// their atlas indices during space-aware wrapping without guessing punctuation.
std::u32string PairAlphabet() {
    std::u32string a=U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$%^&*()";
    const std::array<char32_t,24> tail{0,U'-',U'=',U'~',U'–',U'+',U'‘',U'’',U'“',U'”',
        U';',U'\'',U':',U'"',U',',U'.',U'/',0,0,U'?',U' ',0,U'—',U'¥'};
    a.append(tail.begin(),tail.end());
    for(size_t i=0;i<a.size();++i)if(!a[i])a[i]=char32_t(0xe000+i);
    return a;
}
const auto Pairs=PairAlphabet();
std::u32string Encode(std::u32string s) {
    std::u32string out;
    if(s.size()%2)s.push_back(U' ');
    for(size_t i=0;i<s.size();i+=2) {
        auto a=Pairs.find(s[i]),b=Pairs.find(s[i+1]);
        if(a==std::u32string::npos || b==std::u32string::npos || !s[i] || !s[i+1])return {};
        out.push_back(char32_t(0x4f00+a*96+b));
    }
    return out;
}
std::optional<std::u32string> Decode(const std::u32string& s) {
    std::u32string out;
    for(auto c:s) {
        if(c==U'\n' || c==U'\r') {out.push_back(c);continue;}
        if(c==0x3000){out.append(U"  ");continue;}
        if(c>=0x4f00 && c<0x4f00+96*96) {
            auto a=Pairs[(c-0x4f00)/96],b=Pairs[(c-0x4f00)%96];
            if(!a || !b)return std::nullopt;
            out.push_back(a);out.push_back(b);
        } else if(c<128)out.push_back(c);
        else return std::nullopt;
    }
    return out;
}
void Blend(Image& out,const Picture& pic,int x,int y,int w,int h,int opacity) {
    if(w<=0 || h<=0 || pic.Empty() || out.rgba.size()!=size_t(out.w)*out.h*4)return;
    for(int dy=std::max(0,-y);dy<std::min(h,int(out.h)-y);++dy)
        for(int dx=std::max(0,-x);dx<std::min(w,int(out.w)-x);++dx) {
            float fx=std::clamp((dx+.5f)*pic.w/w-.5f,0.f,float(pic.w-1));
            float fy=std::clamp((dy+.5f)*pic.h/h-.5f,0.f,float(pic.h-1));
            int ix=int(fx),iy=int(fy);float ax=fx-ix,ay=fy-iy;
            unsigned sample[4]{};
            for(int c=0;c<4;++c) {
                auto at=[&](int sx,int sy){return pic.rgba[(size_t(std::min(sy,pic.h-1))*pic.w+std::min(sx,pic.w-1))*4+c];};
                sample[c]=unsigned(std::lround((at(ix,iy)*(1-ax)+at(ix+1,iy)*ax)*(1-ay)+
                    (at(ix,iy+1)*(1-ax)+at(ix+1,iy+1)*ax)*ay));
            }
            auto* d=out.rgba.data()+(size_t(y+dy)*out.w+x+dx)*4;
            unsigned alpha=sample[3]*std::clamp(opacity,0,255)/255;
            // Step 1 cleanup: shared rounded/clamped blend (yoru_view.h), same helper
            // yoru_view.cpp's Composite() now uses -- was already rounded here but clamped the
            // alpha channel without the matching +127 rounding Composite() lacked entirely.
            for(int c=0;c<3;++c)d[c]=BlendChannel(sample[c],d[c],alpha);
            d[3]=BlendAlpha(alpha,d[3]);
        }
}
}

struct Assets::Impl {
    EdenDsmodHostApi host;
    bool font_ready{},english{};
    uint64_t font_size{},pixels{};
    uint32_t width{},height{},block_height{},tile{};
    std::map<std::string,std::shared_ptr<Picture>> pictures,cells;
    std::deque<std::string> picture_order,cell_order;
    size_t picture_bytes{};
    std::map<uint64_t,std::vector<uint8_t>> chunks;
    std::deque<uint64_t> chunk_order;
    std::map<char32_t,Picture> glyphs;
    std::deque<char32_t> glyph_order;
    explicit Impl(const EdenDsmodHostApi& h):host(h) {}
    bool Read(std::string_view name,uint64_t offset,void* dst,size_t count) {
        if(!host.read_romfs)return false;
        std::string path="romfs:"+std::string(name);
        auto* out=static_cast<uint8_t*>(dst);
        while(count) {
            const size_t n=std::min(count,size_t(4u<<20));
            size_t got=host.read_romfs(host.userdata,path.c_str(),offset,out,n);
            if(!got || got>n)return false;
            offset+=got;out+=got;count-=got;
        }
        return true;
    }
    uint64_t Size(std::string_view name) {
        std::string path="romfs:"+std::string(name);
        return host.read_romfs?host.read_romfs(host.userdata,path.c_str(),0,nullptr,0):0;
    }
    bool Font() {
        if(font_ready)return true;
        font_size=Size("font/font.bntx");
        if(font_size<8192 || font_size>(64u<<20))return false;
        std::array<uint8_t,8192> b{};
        if(!Read("font/font.bntx",0,b.data(),b.size()) || std::string_view(reinterpret_cast<char*>(b.data()),4)!="BNTX")return false;
        auto arr=Le64(b.data()+0x28);
        if(!Fits(b.size(),arr,8))return false;
        auto o=Le64(b.data()+arr);
        if(!Fits(b.size(),o,0x78) || Le32(b.data()+o)!=0x49545242 || (Le32(b.data()+o+0x1c)>>8)!=0x1d)return false;
        width=Le32(b.data()+o+0x24);height=Le32(b.data()+o+0x28);
        tile=Le16(b.data()+o+0x12);block_height=1u<<(Le32(b.data()+o+0x34)&7);
        if(width!=12800 || height!=8352 || block_height>32 || tile>1)return false;
        const auto mip=Le64(b.data()+o+0x70);
        if(!Fits(b.size(),mip,8))return false;
        pixels=Le64(b.data()+mip);
        if(!Fits(font_size,pixels,8))return false;
        // Patch1.0.0 encoded first chapter heading; distinguish the effective source, not filenames.
        std::array<uint8_t,22> title{};
        constexpr std::array<uint8_t,22> EnglishTitle{0xff,0xfe,0x1d,0x66,0xac,0x71,0x8c,0x56,0x84,0x53,0x73,0x55,0x0c,0x52,0xbe,0x50,0xdc,0x71,0xdc,0x71,0xdc,0x71};
        english=Read("script/prologue_00.csv",0,title.data(),title.size()) && title==EnglishTitle;
        font_ready=true;return true;
    }
    const uint8_t* FontBytes(uint64_t offset,size_t count) {
        constexpr uint64_t Chunk=256u<<10;
        auto key=offset/Chunk*Chunk;
        if(!Fits(font_size,offset,count) || offset+count>key+Chunk)return nullptr;
        auto it=chunks.find(key);
        if(it==chunks.end()) {
            std::vector<uint8_t> bytes(size_t(std::min(Chunk,font_size-key)));
            if(!Read("font/font.bntx",key,bytes.data(),bytes.size()))return nullptr;
            if(chunks.size()>=4){chunks.erase(chunk_order.front());chunk_order.pop_front();}
            it=chunks.emplace(key,std::move(bytes)).first;chunk_order.push_back(key);
        }
        return it->second.data()+offset-key;
    }
    uint64_t BlockOffset(uint32_t bx,uint32_t by) const {
        uint64_t xb=uint64_t(bx)*8;
        if(tile==1)return (uint64_t(by)*((width+3)/4)+bx)*8;
        uint64_t bh=block_height,he=(height+3)/4;
        while(bh>1 && bh*8>=2*he)bh/=2;
        uint64_t gx=(((width+3)/4)*8+63)/64,gb=512*bh;
        return (by/(8*bh))*gb*gx+(xb/64)*gb+((by%(8*bh))/8)*512+
            ((xb%64)/32)*256+((by%8)/2)*64+((xb%32)/16)*32+(by%2)*16+xb%16;
    }
    const Picture* Glyph(char32_t cp) {
        auto cached=glyphs.find(cp);if(cached!=glyphs.end())return &cached->second;
        int64_t index=cp>=0x4e00?1280+int64_t(cp)-0x4e00:cp>=32 && cp<128?int64_t(cp)-32:-1;
        if(index<0)return nullptr;
        const int sx=int(index%256)*50,sy=int(index/256)*96+16;
        if(sx+50>int(width) || sy+64>int(height))return nullptr;
        Picture im(50,64);
        for(int by=sy/4;by<(sy+64+3)/4;++by)
            for(int bx=sx/4;bx<(sx+50+3)/4;++bx) {
                auto* block=FontBytes(pixels+BlockOffset(bx,by),8);if(!block)return nullptr;
                std::array<unsigned,8> a{};a[0]=block[0];a[1]=block[1];
                if(a[0]>a[1])for(int j=1;j<=6;++j)a[j+1]=((7-j)*a[0]+j*a[1])/7;
                else {for(int j=1;j<=4;++j)a[j+1]=((5-j)*a[0]+j*a[1])/5;a[6]=0;a[7]=255;}
                uint64_t bits=0;for(int j=0;j<6;++j)bits|=uint64_t(block[j+2])<<(j*8);
                for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx) {
                    int dx=bx*4+xx-sx,dy=by*4+yy-sy;
                    if(dx<0 || dy<0 || dx>=50 || dy>=64)continue;
                    auto* p=im.At(dx,dy);p[0]=p[1]=p[2]=255;p[3]=uint8_t(a[(bits>>(3*(yy*4+xx)))&7]);
                }
            }
        if(glyphs.size()>=512){glyphs.erase(glyph_order.front());glyph_order.pop_front();}
        auto it=glyphs.emplace(cp,std::move(im)).first;glyph_order.push_back(cp);return &it->second;
    }
    std::shared_ptr<Picture> Texture(std::string_view name) {
        std::string key{name};
        auto it=pictures.find(key);if(it!=pictures.end())return it->second;
        std::string path=(name.starts_with("still_")?"image/":"ui/")+key+".bntx";
        auto n=Size(path);if(n<64 || n>(32u<<20))return {};
        std::vector<uint8_t> bytes(size_t(n),0);
        if(!Read(path,0,bytes.data(),bytes.size()))return {};
        auto pic=std::make_shared<Picture>();
        if(!acnh::BntxDecode(bytes,name,*pic) || pic->rgba.size()>(32u<<20))return {};
        while(picture_bytes+pic->rgba.size()>(32u<<20) && !picture_order.empty()) {
            auto old=pictures.find(picture_order.front());picture_order.pop_front();
            if(old!=pictures.end()){picture_bytes-=old->second->rgba.size();pictures.erase(old);}
        }
        picture_bytes+=pic->rgba.size();pictures.emplace(key,pic);picture_order.push_back(key);return pic;
    }
    std::shared_ptr<Picture> Cell(std::string_view map,std::string_view name) {
        const auto key=std::string(map)+"/"+std::string(name);
        auto existing=cells.find(key);if(existing!=cells.end())return existing->second;
        std::string path="ui/"+std::string(map)+".ssbp";auto size=Size(path);
        if(size<32 || size>(4u<<20))return {};
        std::vector<uint8_t> b(size,0);if(!Read(path,0,b.data(),b.size()))return {};
        auto offset=Le32(b.data()+16);auto count=Le16(b.data()+28);
        if(count>4096 || !Fits(b.size(),offset,uint64_t(count)*28))return {};
        for(uint32_t i=0;i<count;++i) {
            const auto* rec=b.data()+offset+i*28;auto str=Le32(rec);
            if(!Fits(b.size(),str,1))return {};
            auto end=std::find(b.begin()+str,b.end(),0);
            if(end==b.end() || size_t(end-(b.begin()+str))>256)return {};
            if(std::string_view(reinterpret_cast<char*>(b.data()+str),end-(b.begin()+str))!=name)continue;
            auto texture=Texture(map);if(!texture)return {};
            int x=int16_t(Le16(rec+10)),y=int16_t(Le16(rec+12));
            int w=Le16(rec+14),h=Le16(rec+16);
            if(x<0 || y<0 || !w || !h || x+w>texture->w || y+h>texture->h)return {};
            auto cropped=std::make_shared<Picture>(w,h);
            for(int row=0;row<h;++row)std::copy_n(texture->At(x,y+row),size_t(w)*4,cropped->At(0,row));
            // Step 1 fix: LRU-evict like the other three caches (pictures/chunks/glyphs) instead
            // of permanently refusing to cache -- and thus permanently refusing to draw -- once
            // 48 distinct (map,name) cells have ever been requested.
            if(cells.size()>=48){cells.erase(cell_order.front());cell_order.pop_front();}
            cells.emplace(key,cropped);cell_order.push_back(key);return cropped;
        }
        return {};
    }
};
Assets::Assets(const EdenDsmodHostApi& host):impl_(std::make_unique<Impl>(host)) {}
Assets::~Assets()=default;
bool Assets::Ready(){return impl_->Font();}
bool Assets::Cell(Image& out,std::string_view map,std::string_view name,int x,int y,int w,int h,int opacity) {
    auto pic=impl_->Cell(map,name);if(!pic)return false;Blend(out,*pic,x,y,w,h,opacity);return true;
}
bool Assets::Art(Image& out,std::string_view name,int x,int y,int w,int h,int opacity) {
    auto pic=impl_->Texture(name);if(!pic)return false;Blend(out,*pic,x,y,w,h,opacity);return true;
}
int Assets::Text(Image& out,std::string_view value,int x,int y,int px,int maxw,uint32_t colour,bool label,bool center,int* out_width) {
    if(!impl_->Font() || px<12 || px>64 || maxw<1)return 0;
    auto raw=Unicode(value);if(raw.empty() && !value.empty())return 0;
    const int pitch=std::max(1,int(std::lround(50.0*px/40))),height=std::max(1,int(std::lround(64.0*px/40)));
    std::vector<std::u32string> lines;std::u32string line;
    const auto decoded=impl_->english?(label?std::optional{raw}:Decode(raw)):std::nullopt;
    if(decoded) {
        // Keep the source's own spacing (letter-spaced lines, U+3000 indents); pairs are
        // re-encoded per line, so a line of n decoded characters is (n+1)/2 cells wide.
        for(const auto& wrapped:WrapKeepingSpaces(JoinSoftBreaks(*decoded),[&](size_t n){return int((n+1)/2)*pitch<=maxw;}))
            lines.push_back(Encode(wrapped));
    } else {
        for(auto c:raw) {
            if(c==U'\r' || c==char32_t(SoftBreak))continue;
            if(c==U'\n' || int(line.size()+1)*pitch>maxw) {
                lines.push_back(line);line.clear();if(c==U'\n')continue;
            }
            line.push_back(c);
        }
        if(!line.empty())lines.push_back(line);
    }
    int top=y;
    int widest=0;
    for(const auto& row:lines) {
        // The renderer advances by exactly `pitch` per encoded codepoint regardless of its
        // glyph (space included), so a line's width is just codepoint_count*pitch -- no second
        // measurement pass is needed to centre it.
        const int line_w=int(row.size())*pitch;
        widest=std::max(widest,line_w);
        int left=center?x-line_w/2:x;
        for(auto cp:row) {
            if(cp!=U' ' && cp!=0x3000) {
                auto* source=impl_->Glyph(cp);
                if(source) {
                    Picture tinted=*source;
                    for(size_t i=0;i<tinted.rgba.size();i+=4) {
                        tinted.rgba[i]=colour>>24;tinted.rgba[i+1]=colour>>16;tinted.rgba[i+2]=colour>>8;
                        tinted.rgba[i+3]=uint8_t(unsigned(tinted.rgba[i+3])*(colour&255)/255);
                    }
                    Blend(out,tinted,left,top,pitch,height,255);
                }
            }
            left+=pitch;
        }
        top+=int(std::lround(px*1.45));
    }
    if(out_width)*out_width=widest;
    return lines.empty()?0:top-y-int(std::lround(px*1.45))+height;
}
} // namespace yoru
