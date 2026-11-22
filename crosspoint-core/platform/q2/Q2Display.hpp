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
    // frame[i] is a 32-bit ARGB framebuffer; the caller owns it.
    // bw is CrossPoint's row-major 1-bit black/white image, MSB first.
    // Bit 1 is dark ink, bit 0 light paper. Use only after the page changes.
    static bool presentBW(const uint8_t *bw,std::size_t bytes,uint32_t *frame,
                          std::size_t pixels,unsigned warmth,bool night) {
        constexpr std::size_t RowBytes=(Width+7)/8;
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
                const unsigned bit=(bw[std::size_t(y)*RowBytes+unsigned(x)/8u]>>(7-(unsigned(x)&7u)))&1u;
                frame[std::size_t(y)*Width+unsigned(x)]=bit?ink:paper;
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
                              uint32_t baseInk,uint32_t basePaper,const Zone* zones,std::size_t count) {
        constexpr std::size_t RowBytes=(Width+7)/8;
        if(!bw||!frame||bytes<RowBytes*Height||pixels<Width*Height)return false;
        const uint32_t ink=0xff000000u|baseInk,paper=0xff000000u|basePaper;
        auto bit=[bw](int x,int y){return (bw[std::size_t(y)*RowBytes+unsigned(x)/8u]>>(7-(unsigned(x)&7u)))&1u;};
        for(int y=0;y<Height;y++)
            for(int x=0;x<Width;x++)
                frame[std::size_t(y)*Width+unsigned(x)]=bit(x,y)?ink:paper;
        for(std::size_t i=0;zones&&i<count;i++){
            const Zone& z=zones[i];
            if(z.w<=0||z.h<=0)continue;
            const int y0=z.y<0?0:z.y,y1=z.y+z.h>Height?Height:z.y+z.h;
            const int x0=z.x<0?0:z.x,x1=z.x+z.w>Width?Width:z.x+z.w;
            for(int y=y0;y<y1;y++){
                uint32_t rowPaper=z.paper;
                if(z.paperRows&&z.rowCount)rowPaper=z.paperRows[std::size_t(y-z.y)*z.rowCount/std::size_t(z.h)];
                const uint32_t zi=0xff000000u|z.ink,zp=0xff000000u|rowPaper;
                for(int x=x0;x<x1;x++)
                    if(insideRounded(x,y,z))frame[std::size_t(y)*Width+unsigned(x)]=bit(x,y)?zi:zp;
            }
        }
        return true;
    }
};
}
