// SPDX-License-Identifier: MIT
// Framebuffer output adapter. CrossPoint renders the page; this adapter only
// publishes CrossPoint's monochrome raster to the LCD. No game renderer.
#pragma once
#include <cstddef>
#include <cstdint>

namespace books {
class Q2Display {
public:
    static constexpr int Width=375,Height=320;
    static constexpr std::size_t RowBytes=(Width+7)/8;

    // ---- Smooth text -------------------------------------------------------
    // CrossPoint fonts store 2-bit coverage (0 white, 1 light, 2 dark, 3
    // black). The BW pass paints levels 1-3 as solid ink, which is what makes
    // text jagged on an LCD. The engine's two grayscale passes (made for
    // e-ink) set a bit where a pixel is grey: the MSB plane for levels 1 and
    // 2, the LSB plane for level 2 only. Solid ink sets neither. So for an
    // ink pixel of the BW page:  MSB 0 -> 3,  MSB 1 LSB 1 -> 2,  MSB 1 LSB 0 -> 1.
    // Images use the same two planes for their dark and light greys.
    static unsigned bitAt(const uint8_t* plane,int x,int y) {
        return (plane[std::size_t(y)*RowBytes+unsigned(x)/8u]>>(7-(unsigned(x)&7u)))&1u;
    }
    static unsigned coverage(unsigned pageInk,unsigned lsb,unsigned msb) {
        if(!pageInk)return 0;
        if(!msb)return 3;
        return lsb?2:1;
    }
    // Fills levels (Width*Height bytes, 0..3 = share of ink) for the 1-bit
    // image ink (1 = ink). A pixel takes its grey level only while it still
    // shows the rendered page: pageInk equal to ink, or inverted when the
    // whole page was inverted (night mode), where the level flips to 3-L.
    // Anything drawn over the page since (menus, highlights, the progress
    // bar) keeps plain ink or paper.
    static void computeLevels(const uint8_t* ink,const uint8_t* pageInk,const uint8_t* lsb,
                              const uint8_t* msb,bool inverted,uint8_t* levels) {
        for(int y=0;y<Height;y++)
            for(int x=0;x<Width;x++){
                const unsigned now=bitAt(ink,x,y),page=bitAt(pageInk,x,y);
                uint8_t level=static_cast<uint8_t>(now?3:0);
                if(!inverted&&now==page)level=static_cast<uint8_t>(coverage(page,bitAt(lsb,x,y),bitAt(msb,x,y)));
                else if(inverted&&now!=page)level=static_cast<uint8_t>(3-coverage(page,bitAt(lsb,x,y),bitAt(msb,x,y)));
                levels[std::size_t(y)*Width+unsigned(x)]=level;
            }
    }
    // Share of ink (of 255) for each level. Slightly above an even split, so
    // small text keeps its weight on the Q2's LCD.
    static constexpr uint8_t kInkShare[4]={0,104,184,255};
    static uint32_t blend(uint32_t paper,uint32_t ink,unsigned level) {
        if(level==0)return 0xff000000u|paper;
        if(level>=3)return 0xff000000u|ink;
        const unsigned a=kInkShare[level];
        auto mix=[a](uint32_t p,uint32_t i,int shift){
            const unsigned pc=(p>>shift)&0xffu,ic=(i>>shift)&0xffu;
            return ((pc*(255u-a)+ic*a+127u)/255u)<<shift;
        };
        return 0xff000000u|mix(paper,ink,16)|mix(paper,ink,8)|mix(paper,ink,0);
    }
    // frame[i] is a 32-bit ARGB framebuffer; the caller owns it.
    // bw is CrossPoint's row-major 1-bit black/white image, MSB first.
    // Bit 1 is dark ink, bit 0 light paper. Use only after the page changes.
    static bool presentBW(const uint8_t *bw,std::size_t bytes,uint32_t *frame,
                          std::size_t pixels,unsigned warmth,bool night,
                          const uint8_t* levels=nullptr) {
        if(!bw||!frame||bytes<RowBytes*Height||pixels<Width*Height)return false;
        if(warmth>100)warmth=100;
        auto lerp=[warmth](unsigned cold,unsigned warm) {
            return (cold*(100-warmth)+warm*warmth)/100;
        };
        const unsigned background[3]={
            night?lerp(12,35):lerp(232,255),
            night?lerp(24,22):lerp(242,220),
            night?lerp(37,18):lerp(253,188)
        };
        const unsigned foreground[3]={
            night?lerp(181,255):lerp(22,76),
            night?lerp(207,212):lerp(38,51),
            night?lerp(230,168):lerp(58,27)
        };
        const uint32_t paper=0xff000000u|(background[0]<<16)|(background[1]<<8)|background[2];
        const uint32_t ink=0xff000000u|(foreground[0]<<16)|(foreground[1]<<8)|foreground[2];
        for(int y=0;y<Height;y++){
            for(int x=0;x<Width;x++){
                const std::size_t i=std::size_t(y)*Width+unsigned(x);
                const unsigned level=levels?levels[i]:bitAt(bw,x,y)*3u;
                frame[i]=blend(paper,ink,level);
            }
        }
        return true;
    }
    // Q2 Pod theme colours over the same 1-bit image (1 = ink): the base
    // pair everywhere, then each zone in order over its rectangle (rounded
    // corners left to what is under them).
    struct Zone {
        int16_t x,y,w,h;
        uint8_t radius;
        uint32_t ink,paper;
        const uint32_t* paperRows;
        uint8_t rowCount;
    };
    static bool insideRounded(int px,int py,const Zone& z) {
        const int r=z.radius;
        if(r<=0)return true;
        const int lx=px-z.x,ly=py-z.y;
        const int cx=lx<r?r-1:(lx>=z.w-r?z.w-r:lx);
        const int cy=ly<r?r-1:(ly>=z.h-r?z.h-r:ly);
        if(cx==lx||cy==ly)return true;
        const int dx=lx-cx,dy=ly-cy;
        return dx*dx+dy*dy<=r*r;
    }
    static bool presentThemed(const uint8_t *bw,std::size_t bytes,uint32_t *frame,std::size_t pixels,
                              uint32_t baseInk,uint32_t basePaper,const Zone* zones,std::size_t count,
                              const uint8_t* levels=nullptr) {
        if(!bw||!frame||bytes<RowBytes*Height||pixels<Width*Height)return false;
        auto level=[bw,levels](int x,int y){
            return levels?unsigned(levels[std::size_t(y)*Width+unsigned(x)]):bitAt(bw,x,y)*3u;
        };
        for(int y=0;y<Height;y++)
            for(int x=0;x<Width;x++)
                frame[std::size_t(y)*Width+unsigned(x)]=blend(basePaper,baseInk,level(x,y));
        for(std::size_t i=0;zones&&i<count;i++){
            const Zone& z=zones[i];
            if(z.w<=0||z.h<=0)continue;
            const int y0=z.y<0?0:z.y,y1=z.y+z.h>Height?Height:z.y+z.h;
            const int x0=z.x<0?0:z.x,x1=z.x+z.w>Width?Width:z.x+z.w;
            for(int y=y0;y<y1;y++){
                uint32_t rowPaper=z.paper;
                if(z.paperRows&&z.rowCount)rowPaper=z.paperRows[std::size_t(y-z.y)*z.rowCount/std::size_t(z.h)];
                for(int x=x0;x<x1;x++)
                    if(insideRounded(x,y,z))frame[std::size_t(y)*Width+unsigned(x)]=blend(rowPaper,z.ink,level(x,y));
            }
        }
        return true;
    }
};
}
